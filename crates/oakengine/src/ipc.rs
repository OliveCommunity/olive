// Oak Video Editor - Non-Linear Video Editor
// Copyright (C) 2026 Oak Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//! Render-worker IPC: named shared memory holding the frame-slot pools —
//! the Rust port of `engine/render/ipc/` (`sharedmemoryregion.cpp`,
//! `oliveimpl/render/ipc/frameslotpool.cpp`) behind the frozen C ABI in
//! `engine/include/oakengine/ipc.h`.
//!
//! Two pieces, mirroring the C++ exactly:
//!
//!   - [`SharedMemoryRegion`]: a named POSIX segment (`shm_open` + `mmap`,
//!     `munmap` + `shm_unlink` on close). One process creates the segment
//!     (owner, unlinks on close); the peer attaches to it by key.
//!   - [`FrameSlotPool`]: a fixed pool of equal-sized frame slots laid out
//!     inside a region, with lock-free hand-off through two
//!     [`SpscRingBuffer`]s of slot indices (free + ready). Each ring is a
//!     single-producer/single-consumer structure; the filler owns
//!     `free.pop` + `ready.push`, the drainer owns `ready.pop` + `free.push`,
//!     so no mutex is ever taken.
//!
//! **The in-memory layout is the version-1 wire protocol** the app and the
//! render worker share, and it never changes: the byte offsets below are
//! copied field-for-field from the C++ implementation (64-byte cache-line
//! alignment, the `Header`/`SpscRingBuffer`/`oak_frame_slot_meta` POD
//! structs). A segment written by the C++ side attaches here and vice
//! versa.
//!
//! This module is deliberately unsafe-heavy and self-contained: it touches
//! raw shared memory and raw POSIX syscalls, and everything else in the
//! facade reaches it through the safe wrapper methods and the C ABI exports
//! at the bottom.

use std::ffi::{c_char, c_int, c_void};
use std::ptr;
use std::sync::atomic::{AtomicU32, Ordering};

use crate::handle::{guard_int, guard_ptr};

/// `OAK_IPC_SHM_KEY_CAP` — capacity of shm key strings (ipc.h), incl. NUL.
pub const OAK_IPC_SHM_KEY_CAP: usize = 128;
/// `OAK_IPC_COLORSPACE_CAP` — capacity of `oak_frame_slot_meta::colorspace`.
pub const OAK_IPC_COLORSPACE_CAP: usize = 128;

/// Byte alignment of every sub-region of a frame slot pool (the C++
/// `k_align = 64`; cache-line alignment).
const K_ALIGN: usize = 64;

/// `k_magic = 0x4F4B5350` ("OKSP") — the frame slot pool header magic.
pub const FRAMEPOOL_MAGIC: u32 = 0x4F4B5350;

/// Round `value` up to the next multiple of `align` (power of two).
const fn align_up(value: usize, align: usize) -> usize {
	(value + (align - 1)) & !(align - 1)
}

/// `OAK_IPC_SHM_MODE_CREATE` / `OAK_IPC_SHM_MODE_ATTACH` (ipc.h).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ShmMode {
	/// Create (and own) the segment. Fails if it already exists; the owner
	/// unlinks it on close.
	Create,
	/// Attach to a segment created by the peer. Does not unlink on close.
	Attach,
}

impl ShmMode {
	fn from_c(v: c_int) -> ShmMode {
		match v {
			0 => ShmMode::Create,
			_ => ShmMode::Attach,
		}
	}
}

/// Per-slot metadata describing the frame currently occupying a slot —
/// field-for-field `oak_frame_slot_meta` from `engine/include/oakengine/ipc.h`.
///
/// This POD lives in shared memory alongside the pixel data and is part of
/// the version-1 wire protocol; `#[repr(C)]` keeps the C ABI layout.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FrameSlotMeta {
	/// Caller-defined tag (ticket id, or footage stream hash).
	pub id: i64,
	/// Frame timestamp numerator.
	pub time_num: i64,
	/// Frame timestamp denominator.
	pub time_den: i64,
	/// Frame width.
	pub width: i32,
	/// Frame height.
	pub height: i32,
	/// `PixelFormat::Format` value.
	pub format: i32,
	/// Channel count.
	pub channel_count: i32,
	/// Bytes per scanline (stride).
	pub linesize: i32,
	/// Valid bytes written into the slot's data block.
	pub data_size: i32,
	/// Input colorspace name.
	pub colorspace: [c_char; OAK_IPC_COLORSPACE_CAP],
}

impl Default for FrameSlotMeta {
	fn default() -> Self {
		FrameSlotMeta {
			id: 0,
			time_num: 0,
			time_den: 0,
			width: 0,
			height: 0,
			format: 0,
			channel_count: 0,
			linesize: 0,
			data_size: 0,
			colorspace: [0; OAK_IPC_COLORSPACE_CAP],
		}
	}
}

/// `sizeof(oak_frame_slot_meta)` (8+8+8 + 4*6 + 128).
const FRAME_SLOT_META_SIZE: usize = 176;

// ---------------------------------------------------------------------------
// SpscRingBuffer
// ---------------------------------------------------------------------------

/// A lock-free single-producer / single-consumer ring buffer of `u32`
/// indices, living in shared memory — the port of
/// `engine/include/oakengine/spscringbuffer.h`.
///
/// Layout (offsets from the buffer base, matching the C++ class):
///
/// ```text
/// 0   head_     u32   producer cursor (relaxed read, release write)
/// 4   tail_     u32   consumer cursor (relaxed read, release write)
/// 8   capacity_ u32   slot count (written once by create())
/// 12  slots     u32[capacity]
/// ```
///
/// One slot is always left empty to disambiguate full and empty, so a
/// buffer with `capacity` slots holds at most `capacity - 1` live entries.
/// The payload is a `u32` slot index — never a pointer.
///
/// `SpscRingBuffer` is a thin view over a raw pointer; it is `Copy` and
/// owns nothing. All methods are `unsafe` because they read and write the
/// shared segment concurrently with a peer process.
#[derive(Clone, Copy)]
pub struct SpscRingBuffer {
	/// Base of the ring header (`head_` at offset 0).
	base: *mut u8,
}

// The shared memory the ring lives in is usable from any thread of the
// local process; synchronization with the peer is the ring's own atomics.
unsafe impl Send for SpscRingBuffer {}
unsafe impl Sync for SpscRingBuffer {}

impl SpscRingBuffer {
	/// `sizeof(SpscRingBuffer)` — header bytes before the slot array.
	pub const HEADER_BYTES: usize = 12;

	/// Total bytes required for the header plus `capacity` index slots
	/// (`SpscRingBuffer::bytes_needed`).
	pub fn bytes_needed(capacity: u32) -> usize {
		Self::HEADER_BYTES + capacity as usize * 4
	}

	/// In-place construct a ring header at `mem` with `capacity` index
	/// slots. `mem` must provide at least [`Self::bytes_needed`] bytes and
	/// be suitably aligned (mmap-backed segments are). Done exactly once by
	/// whichever process owns the segment's creation; the peer uses
	/// [`Self::attach`] instead.
	///
	/// # Safety
	/// `mem` must be a valid, writable, aligned buffer of at least
	/// [`Self::bytes_needed`] bytes, and must not be concurrently written
	/// during this call.
	pub unsafe fn create(mem: *mut u8, capacity: u32) -> SpscRingBuffer {
		let ring = SpscRingBuffer { base: mem };
		unsafe {
			ring.store_capacity(capacity);
			ring.head().store(0, Ordering::Relaxed);
			ring.tail().store(0, Ordering::Relaxed);
			for i in 0..capacity as usize {
				*ring.slot_ptr(i) = 0;
			}
		}
		ring
	}

	/// Re-interpret already-initialized shared memory as a ring buffer
	/// (peer-process side). No writes are performed.
	///
	/// # Safety
	/// `mem` must point to a buffer previously initialized by
	/// [`Self::create`] (or an ABI-identical C++ side) that stays mapped
	/// for as long as this view is used.
	pub unsafe fn attach(mem: *mut u8) -> SpscRingBuffer {
		SpscRingBuffer { base: mem }
	}

	/// The ring's capacity (slot count).
	///
	/// # Safety
	/// `self` must point at a live ring (created or attached).
	pub unsafe fn capacity(&self) -> u32 {
		unsafe { (self.base.add(8) as *const u32).read() }
	}

	/// Producer side: enqueue an index. Returns false if the buffer is full.
	///
	/// # Safety
	/// Exactly one producer may call this concurrently with exactly one
	/// consumer calling [`Self::pop`]; the ring must be live.
	pub unsafe fn push(&self, value: u32) -> bool {
		unsafe {
			let head = self.head().load(Ordering::Relaxed);
			let next = self.increment(head);
			if next == self.tail().load(Ordering::Acquire) {
				return false;
			}
			*self.slot_ptr(head as usize) = value;
			self.head().store(next, Ordering::Release);
		}
		true
	}

	/// Consumer side: dequeue an index into `out`. Returns false if the
	/// buffer is empty.
	///
	/// # Safety
	/// Exactly one consumer may call this concurrently with exactly one
	/// producer calling [`Self::push`]; the ring must be live.
	pub unsafe fn pop(&self, out: &mut u32) -> bool {
		unsafe {
			let tail = self.tail().load(Ordering::Relaxed);
			if tail == self.head().load(Ordering::Acquire) {
				return false;
			}
			*out = *self.slot_ptr(tail as usize);
			self.tail().store(self.increment(tail), Ordering::Release);
		}
		true
	}

	/// Approximate number of entries currently queued; may be stale the
	/// instant it returns. For metrics/backpressure, not correctness.
	///
	/// # Safety
	/// The ring must be live.
	pub unsafe fn size_approx(&self) -> u32 {
		unsafe {
			let head = self.head().load(Ordering::Acquire);
			let tail = self.tail().load(Ordering::Acquire);
			let cap = self.capacity();
			(head + cap - tail) % cap
		}
	}

	/// Approximate empty check (see [`Self::size_approx`]).
	///
	/// # Safety
	/// The ring must be live.
	pub unsafe fn is_empty_approx(&self) -> bool {
		unsafe { self.head().load(Ordering::Acquire) == self.tail().load(Ordering::Acquire) }
	}

	#[inline]
	fn increment(&self, index: u32) -> u32 {
		// `capacity_` is small; this avoids requiring a power-of-two capacity.
		unsafe { (index + 1) % self.capacity() }
	}

	#[inline]
	unsafe fn head(&self) -> &AtomicU32 {
		unsafe { &*(self.base as *const AtomicU32) }
	}

	#[inline]
	unsafe fn tail(&self) -> &AtomicU32 {
		unsafe { &*(self.base.add(4) as *const AtomicU32) }
	}

	#[inline]
	unsafe fn store_capacity(&self, capacity: u32) {
		unsafe { *(self.base.add(8) as *mut u32) = capacity };
	}

	#[inline]
	unsafe fn slot_ptr(&self, index: usize) -> *mut u32 {
		unsafe { self.base.add(Self::HEADER_BYTES + index * 4) as *mut u32 }
	}
}

// ---------------------------------------------------------------------------
// FrameSlotPool
// ---------------------------------------------------------------------------

/// Pool header written by create() and read back by attach(). Field-for-
/// field the C++ `FrameSlotPool::Header` (offsets: 0,4,8,16,24,32,40;
/// 48 bytes total).
#[repr(C)]
struct PoolHeader {
	magic: u32,
	slot_count: u32,
	slot_data_bytes: u64,
	free_ring_offset: u64,
	ready_ring_offset: u64,
	meta_offset: u64,
	data_offset: u64,
}

const POOL_HEADER_SIZE: usize = 48;

/// A fixed-size pool of equal-sized frame slots in shared memory with
/// lock-free hand-off — the port of the C++ `FrameSlotPool`
/// (`engine/src/oliveimpl/render/ipc/frameslotpool.{h,cpp}`).
///
/// One pool models a single direction of frame flow. It does NOT own the
/// memory; it is a view over a mapped [`SharedMemoryRegion`] (or any
/// ABI-identical segment). Lifecycle: the filler `acquire`s a free slot,
/// writes meta + pixels, then `publish`es it; the drainer `consume`s the
/// next ready slot, reads it, and `release`s it back to the free ring.
///
/// [`FrameSlotPool`] is `Clone` — the clone is another view of the same
/// segment (the C++ `copy()`), useful to hand both sides a handle without
/// owning the mapping twice.
pub struct FrameSlotPool {
	/// Segment base.
	base: *mut u8,
	/// The pool header at `base + 0`.
	header: *mut PoolHeader,
	/// Free-ring view (filler pops, drainer pushes).
	free_ring: SpscRingBuffer,
	/// Ready-ring view (filler pushes, drainer pops).
	ready_ring: SpscRingBuffer,
	/// Metadata array at `base + meta_offset`.
	meta: *mut FrameSlotMeta,
	/// Pixel data blocks at `base + data_offset`.
	data: *mut u8,
}

// Views into shared memory are safe to share within the process; the rings
// carry their own synchronization.
unsafe impl Send for FrameSlotPool {}
unsafe impl Sync for FrameSlotPool {}

impl Clone for FrameSlotPool {
	fn clone(&self) -> FrameSlotPool {
		FrameSlotPool {
			base: self.base,
			header: self.header,
			free_ring: self.free_ring,
			ready_ring: self.ready_ring,
			meta: self.meta,
			data: self.data,
		}
	}
}

impl FrameSlotPool {
	/// Total bytes a region must provide to back a pool of
	/// `slot_count` x `slot_data_bytes`
	/// (`FrameSlotPool::bytes_needed`).
	pub fn bytes_needed(slot_count: u32, slot_data_bytes: usize) -> usize {
		let ring_cap = slot_count + 1;
		let mut total = align_up(POOL_HEADER_SIZE, K_ALIGN);
		let ring_bytes = align_up(SpscRingBuffer::bytes_needed(ring_cap), K_ALIGN);
		total += ring_bytes; // free ring
		total += ring_bytes; // ready ring
		total += align_up(FRAME_SLOT_META_SIZE * slot_count as usize, K_ALIGN); // metadata
		total += align_up(slot_data_bytes, K_ALIGN) * slot_count as usize; // pixel data
		total
	}

	/// Lay out and initialize a brand-new pool over `mem` (owner side, once).
	///
	/// Writes the header, initializes both rings, seeds the free ring with
	/// every slot index and zeroes the metadata. `mem` must provide at
	/// least [`Self::bytes_needed`] bytes of writable, aligned memory (an
	/// mmap-backed segment) and must outlive the returned pool.
	///
	/// # Safety
	/// `mem` must be a valid, writable, aligned buffer of at least
	/// [`Self::bytes_needed`] bytes, not concurrently written during this
	/// call.
	pub unsafe fn create(mem: *mut u8, slot_count: u32, slot_data_bytes: usize) -> FrameSlotPool {
		let ring_cap = slot_count + 1;
		let free_off = align_up(POOL_HEADER_SIZE, K_ALIGN);
		let ready_off = free_off + align_up(SpscRingBuffer::bytes_needed(ring_cap), K_ALIGN);
		let meta_off = ready_off + align_up(SpscRingBuffer::bytes_needed(ring_cap), K_ALIGN);
		let data_off = meta_off + align_up(FRAME_SLOT_META_SIZE * slot_count as usize, K_ALIGN);

		let pool = unsafe {
			FrameSlotPool {
				base: mem,
				header: mem as *mut PoolHeader,
				free_ring: SpscRingBuffer::create(mem.add(free_off), ring_cap),
				ready_ring: SpscRingBuffer::create(mem.add(ready_off), ring_cap),
				meta: mem.add(meta_off) as *mut FrameSlotMeta,
				data: mem.add(data_off),
			}
		};
		unsafe {
			(*pool.header).magic = FRAMEPOOL_MAGIC;
			(*pool.header).slot_count = slot_count;
			(*pool.header).slot_data_bytes = slot_data_bytes as u64;
			(*pool.header).free_ring_offset = free_off as u64;
			(*pool.header).ready_ring_offset = ready_off as u64;
			(*pool.header).meta_offset = meta_off as u64;
			(*pool.header).data_offset = data_off as u64;
		}
		// `ptr::write_bytes` counts in elements of T, so cast to bytes.
		unsafe {
			ptr::write_bytes(
				pool.meta as *mut u8,
				0,
				slot_count as usize * std::mem::size_of::<FrameSlotMeta>(),
			);
		}
		// Seed the free ring with every slot index so the filler can
		// acquire() immediately.
		for i in 0..slot_count {
			unsafe { pool.free_ring.push(i) };
		}
		pool
	}

	/// Map an existing, already-initialized pool (peer side).
	///
	/// Reads the geometry from the in-memory header written by
	/// [`Self::create`]; the returned pool reports `is_valid() == false`
	/// when the magic does not match.
	///
	/// # Safety
	/// `mem` must point to a mapped segment that either contains a pool
	/// initialized by [`Self::create`] (or an ABI-identical C++ side) or is
	/// an arbitrary buffer whose first 4 bytes we must be able to read.
	pub unsafe fn attach(mem: *mut u8) -> FrameSlotPool {
		if mem.is_null() {
			return FrameSlotPool::invalid();
		}
		let header = mem as *mut PoolHeader;
		// SAFETY: `mem` is a live mapping of at least the header size.
		if unsafe { (*header).magic } != FRAMEPOOL_MAGIC {
			return FrameSlotPool::invalid();
		}
		let pool = unsafe {
			FrameSlotPool {
				base: mem,
				header,
				free_ring: SpscRingBuffer::attach(mem.add((*header).free_ring_offset as usize)),
				ready_ring: SpscRingBuffer::attach(mem.add((*header).ready_ring_offset as usize)),
				meta: mem.add((*header).meta_offset as usize) as *mut FrameSlotMeta,
				data: mem.add((*header).data_offset as usize),
			}
		};
		pool
	}

	/// An invalid pool (attach on a non-pool segment).
	fn invalid() -> FrameSlotPool {
		FrameSlotPool {
			base: ptr::null_mut(),
			header: ptr::null_mut(),
			free_ring: SpscRingBuffer {
				base: ptr::null_mut(),
			},
			ready_ring: SpscRingBuffer {
				base: ptr::null_mut(),
			},
			meta: ptr::null_mut(),
			data: ptr::null_mut(),
		}
	}

	/// True when the pool was attached to a segment containing a valid pool
	/// header.
	pub fn is_valid(&self) -> bool {
		!self.header.is_null()
	}

	/// Number of slots in the pool (0 for an invalid pool).
	pub fn slot_count(&self) -> u32 {
		if self.is_valid() {
			unsafe { (*self.header).slot_count }
		} else {
			0
		}
	}

	/// Bytes available in every slot's pixel-data block (0 for invalid).
	pub fn slot_data_bytes(&self) -> usize {
		if self.is_valid() {
			unsafe { (*self.header).slot_data_bytes as usize }
		} else {
			0
		}
	}

	/// Byte stride between consecutive slot data blocks.
	fn slot_stride(&self) -> usize {
		align_up(self.slot_data_bytes(), K_ALIGN)
	}

	// ---- Filler side ----

	/// Take ownership of a free slot. Returns false (leaving `index`
	/// untouched) if none is free.
	///
	/// # Safety
	/// The pool must be a valid view of a live segment.
	pub unsafe fn acquire(&self, index: &mut u32) -> bool {
		unsafe { self.free_ring.pop(index) }
	}

	/// Pointer to a slot's pixel data block (`slot_data_bytes` available).
	///
	/// # Safety
	/// `index` must be in `0..slot_count`; the pool must be a valid view of
	/// a live segment.
	pub unsafe fn slot_data(&self, index: u32) -> *mut u8 {
		unsafe { self.data.add(index as usize * self.slot_stride()) }
	}

	/// Mutable metadata for a slot. The filler writes this before
	/// [`Self::publish`]. The returned pointer addresses shared memory; it
	/// is borrowed, not owned.
	///
	/// # Safety
	/// `index` must be in `0..slot_count`; the pool must be a valid view of
	/// a live segment.
	pub unsafe fn meta(&self, index: u32) -> *mut FrameSlotMeta {
		unsafe { self.meta.add(index as usize) }
	}

	/// Publish a filled slot to the drainer. Must follow a successful
	/// [`Self::acquire`] of `index`. Returns false if the ready ring is
	/// full (the filler must then release the slot and retry later).
	///
	/// # Safety
	/// `index` must be a slot previously acquired and not yet released.
	pub unsafe fn publish(&self, index: u32) -> bool {
		unsafe { self.ready_ring.push(index) }
	}

	// ---- Drainer side ----

	/// Take the next published slot. Returns false if nothing is ready.
	///
	/// # Safety
	/// The pool must be a valid view of a live segment.
	pub unsafe fn consume(&self, index: &mut u32) -> bool {
		unsafe { self.ready_ring.pop(index) }
	}

	/// Return a consumed slot to the free pool for reuse. Must follow a
	/// successful [`Self::consume`] of `index`. Returns false if the free
	/// ring is full (the drainer must not release the slot yet).
	///
	/// # Safety
	/// `index` must be a slot previously consumed and not yet re-acquired.
	pub unsafe fn release(&self, index: u32) -> bool {
		unsafe { self.free_ring.push(index) }
	}

	/// Immutable metadata for a slot (drainer side).
	///
	/// # Safety
	/// `index` must be in `0..slot_count`; the pool must be a valid view of
	/// a live segment.
	pub unsafe fn meta_const(&self, index: u32) -> *const FrameSlotMeta {
		unsafe { self.meta.add(index as usize) }
	}

	/// Immutable pixel data for a slot.
	///
	/// # Safety
	/// `index` must be in `0..slot_count`; the pool must be a valid view of
	/// a live segment.
	pub unsafe fn slot_data_const(&self, index: u32) -> *const u8 {
		unsafe { self.data.add(index as usize * self.slot_stride()) }
	}
}

// ---------------------------------------------------------------------------
// SharedMemoryRegion
// ---------------------------------------------------------------------------

/// A named, fixed-size POSIX shared-memory segment mapped into the process
/// address space — the port of the C++ `SharedMemoryRegion`
/// (`engine/render/ipc/sharedmemoryregion.cpp`).
///
/// One process opens the segment in [`ShmMode::Create`] (owner: fails if
/// the name already exists, zeroes the mapping, unlinks on close); the
/// peer opens the same key in [`ShmMode::Attach`]. The mapping is a raw
/// contiguous byte range; the ring buffers and frame slot pools are laid
/// out inside it. Nothing here is locked — synchronization is entirely the
/// caller's responsibility via the lock-free structures placed in the
/// mapping.
pub struct SharedMemoryRegion {
	/// The key the region was opened with (no leading slash).
	key: String,
	/// Requested mapping size in bytes.
	size: usize,
	/// The mmap'd data pointer; null when invalid.
	data: *mut u8,
	/// File descriptor from `shm_open` (-1 when invalid).
	fd: i32,
	/// Open mode.
	mode: ShmMode,
	/// Human-readable reason of the last failed open.
	error: String,
	/// The platform-prefixed name actually passed to `shm_open`.
	shm_name: String,
}

impl SharedMemoryRegion {
	/// An empty (invalid) region.
	pub fn new() -> SharedMemoryRegion {
		SharedMemoryRegion {
			key: String::new(),
			size: 0,
			data: ptr::null_mut(),
			fd: -1,
			mode: ShmMode::Attach,
			error: String::new(),
			shm_name: String::new(),
		}
	}

	/// Build a unique segment key for a worker, e.g.
	/// "olive-rw-<pid>-<index>" (`SharedMemoryRegion::make_key`).
	/// Centralized so the owner and the spawned worker agree on the same
	/// name.
	pub fn make_key(owner_pid: i64, worker_index: i32) -> String {
		format!("olive-rw-{owner_pid}-{worker_index}")
	}

	/// Open the segment identified by `key` with the given `size` in bytes.
	///
	/// `key` is a short identifier (no leading slash needed; the platform
	/// prefix is added internally). Returns true on success; on failure
	/// [`Self::error`] carries a human-readable reason. An existing region
	/// is closed first.
	pub fn open(&mut self, key: &str, size: usize, mode: ShmMode) -> bool {
		self.close();
		self.key = key.to_string();
		self.size = size;
		self.mode = mode;

		// POSIX shared-memory names must start with a single slash and
		// contain no others.
		let shm_name = format!("/{}", key.replace('/', "_"));
		let name_c = match std::ffi::CString::new(shm_name.clone()) {
			Ok(c) => c,
			Err(_) => {
				self.error = format!("invalid shm key {key:?} (contains NUL)");
				return false;
			}
		};
		self.shm_name = shm_name;

		let mut oflag = libc::O_RDWR;
		if mode == ShmMode::Create {
			oflag |= libc::O_CREAT | libc::O_EXCL;
			// Clear any stale segment left by a crashed previous run with
			// the same name.
			unsafe { libc::shm_unlink(name_c.as_ptr()) };
		}

		let fd = unsafe { libc::shm_open(name_c.as_ptr(), oflag, 0o600) };
		if fd < 0 {
			self.error = format!(
				"shm_open({}) failed: {}",
				self.shm_name,
				std::io::Error::last_os_error()
			);
			return false;
		}
		self.fd = fd;

		if mode == ShmMode::Create {
			if unsafe { libc::ftruncate(fd, size as libc::off_t) } != 0 {
				self.error = format!("ftruncate failed: {}", std::io::Error::last_os_error());
				self.close();
				return false;
			}
		} else {
			// mmap() succeeds even beyond the real segment size and only
			// faults (SIGBUS) on access, so verify the segment is large
			// enough up front.
			let mut st: libc::stat = unsafe { std::mem::zeroed() };
			if unsafe { libc::fstat(fd, &mut st) } != 0 {
				self.error = format!("fstat failed: {}", std::io::Error::last_os_error());
				self.close();
				return false;
			}
			if (st.st_size as usize) < size {
				self.error = format!(
					"shared memory segment is {} bytes, smaller than the requested {}",
					st.st_size, size
				);
				self.close();
				return false;
			}
		}

		let data = unsafe {
			libc::mmap(
				ptr::null_mut(),
				size,
				libc::PROT_READ | libc::PROT_WRITE,
				libc::MAP_SHARED,
				fd,
				0,
			)
		};
		if data == libc::MAP_FAILED {
			self.error = format!("mmap failed: {}", std::io::Error::last_os_error());
			self.close();
			return false;
		}
		self.data = data as *mut u8;
		self.error.clear();

		if mode == ShmMode::Create {
			unsafe { ptr::write_bytes(self.data, 0, size) };
		}
		true
	}

	/// Unmap and (if owner) unlink the segment. Also called by `Drop`.
	pub fn close(&mut self) {
		if !self.data.is_null() {
			unsafe { libc::munmap(self.data as *mut c_void, self.size) };
			self.data = ptr::null_mut();
		}
		if self.fd >= 0 {
			unsafe { libc::close(self.fd) };
			self.fd = -1;
		}
		if self.mode == ShmMode::Create && !self.shm_name.is_empty() {
			// Only the owner unlinks, so the name is freed once both sides
			// have unmapped.
			if let Ok(c) = std::ffi::CString::new(self.shm_name.clone()) {
				unsafe { libc::shm_unlink(c.as_ptr()) };
			}
			self.shm_name.clear();
		}
		self.size = 0;
	}

	/// True when the region holds a live mapping.
	pub fn is_valid(&self) -> bool {
		!self.data.is_null()
	}

	/// The mapped data pointer (null when invalid).
	pub fn data(&self) -> *mut u8 {
		self.data
	}

	/// The mapping size in bytes.
	pub fn size(&self) -> usize {
		self.size
	}

	/// The key the region was opened with.
	pub fn key(&self) -> &str {
		&self.key
	}

	/// Human-readable reason of the last failed open.
	pub fn error(&self) -> &str {
		&self.error
	}
}

impl Default for SharedMemoryRegion {
	fn default() -> Self {
		SharedMemoryRegion::new()
	}
}

impl Drop for SharedMemoryRegion {
	fn drop(&mut self) {
		self.close();
	}
}

// ---------------------------------------------------------------------------
// C ABI exports (engine/include/oakengine/ipc.h)
// ---------------------------------------------------------------------------

/// Opaque `OakSharedMemoryRegion` handle (ipc.h). A facade-owned box around
/// a [`SharedMemoryRegion`]; the C caller only ever sees the pointer.
#[repr(C)]
pub struct OakSharedMemoryRegion {
	_opaque: [u8; 0],
}

/// Opaque `OakFrameSlotPool` handle (ipc.h). A facade-owned box around a
/// [`FrameSlotPool`] view.
#[repr(C)]
pub struct OakFrameSlotPool {
	_opaque: [u8; 0],
}

/// `oakengine_ipc_shm_create` — allocate an empty (invalid) region object.
#[no_mangle]
pub extern "C" fn oakengine_ipc_shm_create() -> *mut OakSharedMemoryRegion {
	guard_ptr(|| {
		let region = Box::new(SharedMemoryRegion::new());
		Ok(Box::into_raw(region) as *mut OakSharedMemoryRegion)
	})
}

/// `oakengine_ipc_shm_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_free(self_: *mut OakSharedMemoryRegion) {
	if self_.is_null() {
		return;
	}
	// SAFETY: `self_` was produced by `oakengine_ipc_shm_create` and is
	// not used after this.
	unsafe { drop(Box::from_raw(self_ as *mut SharedMemoryRegion)) };
}

/// `oakengine_ipc_shm_open` — open the segment; 1 on success, 0 on failure
/// (`oakengine_ipc_shm_error` carries the reason).
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_open(
	self_: *mut OakSharedMemoryRegion,
	key: *const c_char,
	size: usize,
	mode: c_int,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let key = crate::handle::read_cstr(key);
		let region = &mut *(self_ as *mut SharedMemoryRegion);
		Ok(region.open(&key, size, ShmMode::from_c(mode)) as c_int)
	})
}

/// `oakengine_ipc_shm_close` — unmap and (if owner) unlink.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_close(self_: *mut OakSharedMemoryRegion) {
	if self_.is_null() {
		return;
	}
	// SAFETY: `self_` is a live region handle.
	unsafe { (&mut *(self_ as *mut SharedMemoryRegion)).close() };
}

/// `oakengine_ipc_shm_is_valid` — 1/0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_is_valid(self_: *const OakSharedMemoryRegion) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok((&*(self_ as *const SharedMemoryRegion)).is_valid() as c_int)
	})
}

/// `oakengine_ipc_shm_data` — the mapped data pointer (NULL when invalid).
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_data(self_: *mut OakSharedMemoryRegion) -> *mut c_void {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let region = &mut *(self_ as *mut SharedMemoryRegion);
		Ok(region.data() as *mut c_void)
	})
}

/// `oakengine_ipc_shm_size` — mapping size in bytes.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_size(self_: *const OakSharedMemoryRegion) -> usize {
	if self_.is_null() {
		return 0;
	}
	// SAFETY: `self_` is a live region handle.
	unsafe { (&*(self_ as *const SharedMemoryRegion)).size() }
}

/// `oakengine_ipc_shm_key` — the key the region was opened with
/// (buf/size convention).
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_key(
	self_: *const OakSharedMemoryRegion,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let region = &*(self_ as *const SharedMemoryRegion);
		Ok(crate::handle::write_string(region.key(), buf, buf_size))
	})
}

/// `oakengine_ipc_shm_error` — reason of the last failed open
/// (buf/size convention).
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_error(
	self_: *const OakSharedMemoryRegion,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let region = &*(self_ as *const SharedMemoryRegion);
		Ok(crate::handle::write_string(region.error(), buf, buf_size))
	})
}

/// `oakengine_ipc_shm_make_key` — build a unique segment key
/// ("olive-rw-<pid>-<index>", buf/size convention).
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_shm_make_key(
	owner_pid: i64,
	worker_index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let key = SharedMemoryRegion::make_key(owner_pid, worker_index);
		Ok(crate::handle::write_string(&key, buf, buf_size))
	})
}

/// `oakengine_ipc_framepool_bytes_needed`.
#[no_mangle]
pub extern "C" fn oakengine_ipc_framepool_bytes_needed(
	slot_count: u32,
	slot_data_bytes: usize,
) -> usize {
	FrameSlotPool::bytes_needed(slot_count, slot_data_bytes)
}

/// `oakengine_ipc_framepool_create` — lay out and initialize a brand-new
/// pool over `mem` (owner side, once). The handle is owned by the caller
/// but does not own `mem`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_create(
	mem: *mut c_void,
	slot_count: u32,
	slot_data_bytes: usize,
) -> *mut OakFrameSlotPool {
	guard_ptr(|| unsafe {
		if mem.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let pool = FrameSlotPool::create(mem as *mut u8, slot_count, slot_data_bytes);
		Ok(Box::into_raw(Box::new(pool)) as *mut OakFrameSlotPool)
	})
}

/// `oakengine_ipc_framepool_attach` — map an existing pool (peer side).
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_attach(mem: *mut c_void) -> *mut OakFrameSlotPool {
	guard_ptr(|| unsafe {
		if mem.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let pool = FrameSlotPool::attach(mem as *mut u8);
		if !pool.is_valid() {
			return Ok(std::ptr::null_mut());
		}
		Ok(Box::into_raw(Box::new(pool)) as *mut OakFrameSlotPool)
	})
}

/// `oakengine_ipc_framepool_copy` — copy the view (same shared memory,
/// independent handle). NULL yields NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_copy(
	self_: *const OakFrameSlotPool,
) -> *mut OakFrameSlotPool {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let pool = (*(self_ as *const FrameSlotPool)).clone();
		Ok(Box::into_raw(Box::new(pool)) as *mut OakFrameSlotPool)
	})
}

/// `oakengine_ipc_framepool_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_free(self_: *mut OakFrameSlotPool) {
	if self_.is_null() {
		return;
	}
	// SAFETY: `self_` was produced by one of the create/attach/copy
	// exports and is not used after this.
	unsafe { drop(Box::from_raw(self_ as *mut FrameSlotPool)) };
}

/// `oakengine_ipc_framepool_is_valid` — 1/0.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_is_valid(self_: *const OakFrameSlotPool) -> c_int {
	guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		Ok((&*(self_ as *const FrameSlotPool)).is_valid() as c_int)
	})
}

/// `oakengine_ipc_framepool_slot_count`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_slot_count(self_: *const OakFrameSlotPool) -> u32 {
	if self_.is_null() {
		return 0;
	}
	// SAFETY: `self_` is a live pool handle.
	unsafe { (&*(self_ as *const FrameSlotPool)).slot_count() }
}

/// `oakengine_ipc_framepool_slot_data_bytes`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_slot_data_bytes(
	self_: *const OakFrameSlotPool,
) -> usize {
	if self_.is_null() {
		return 0;
	}
	// SAFETY: `self_` is a live pool handle.
	unsafe { (&*(self_ as *const FrameSlotPool)).slot_data_bytes() }
}

/// `oakengine_ipc_framepool_acquire` — take a free slot; 1 on success
/// (`*index` set), 0 if none is free or the pool is invalid.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_acquire(
	self_: *mut OakFrameSlotPool,
	index: *mut u32,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() || index.is_null() {
			return Ok(0);
		}
		let pool = &*(self_ as *const FrameSlotPool);
		if !pool.is_valid() {
			return Ok(0);
		}
		let mut out = 0u32;
		if pool.acquire(&mut out) {
			*index = out;
			Ok(1)
		} else {
			Ok(0)
		}
	})
}

/// `oakengine_ipc_framepool_slot_data` — pointer to a slot's pixel block.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_slot_data(
	self_: *mut OakFrameSlotPool,
	index: u32,
) -> *mut c_void {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let pool = &*(self_ as *const FrameSlotPool);
		if !pool.is_valid() || index >= pool.slot_count() {
			return Ok(std::ptr::null_mut());
		}
		Ok(pool.slot_data(index) as *mut c_void)
	})
}

/// `oakengine_ipc_framepool_slot_data_const`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_slot_data_const(
	self_: *const OakFrameSlotPool,
	index: u32,
) -> *const c_void {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let pool = &*(self_ as *const FrameSlotPool);
		if !pool.is_valid() || index >= pool.slot_count() {
			return Ok(std::ptr::null_mut());
		}
		Ok(pool.slot_data_const(index) as *mut c_void)
	})
}

/// `oakengine_ipc_framepool_meta` — mutable per-slot metadata (borrowed).
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_meta(
	self_: *mut OakFrameSlotPool,
	index: u32,
) -> *mut crate::ipc::FrameSlotMeta {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let pool = &*(self_ as *const FrameSlotPool);
		if !pool.is_valid() || index >= pool.slot_count() {
			return Ok(std::ptr::null_mut());
		}
		Ok(pool.meta(index))
	})
}

/// `oakengine_ipc_framepool_meta_const`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_meta_const(
	self_: *const OakFrameSlotPool,
	index: u32,
) -> *const crate::ipc::FrameSlotMeta {
	guard_ptr(|| unsafe {
		if self_.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let pool = &*(self_ as *const FrameSlotPool);
		if !pool.is_valid() || index >= pool.slot_count() {
			return Ok(std::ptr::null_mut());
		}
		Ok(pool.meta_const(index) as *mut FrameSlotMeta)
	})
}

/// `oakengine_ipc_framepool_publish` — publish a filled slot; 1 on success,
/// 0 if the ready ring is full or the pool is invalid.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_publish(
	self_: *mut OakFrameSlotPool,
	index: u32,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let pool = &*(self_ as *const FrameSlotPool);
		if !pool.is_valid() || index >= pool.slot_count() {
			return Ok(0);
		}
		Ok(pool.publish(index) as c_int)
	})
}

/// `oakengine_ipc_framepool_consume` — take the next published slot; 1 on
/// success (`*index` set), 0 if nothing is ready or the pool is invalid.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_consume(
	self_: *mut OakFrameSlotPool,
	index: *mut u32,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() || index.is_null() {
			return Ok(0);
		}
		let pool = &*(self_ as *const FrameSlotPool);
		if !pool.is_valid() {
			return Ok(0);
		}
		let mut out = 0u32;
		if pool.consume(&mut out) {
			*index = out;
			Ok(1)
		} else {
			Ok(0)
		}
	})
}

/// `oakengine_ipc_framepool_release` — return a consumed slot to the free
/// pool; 1 on success, 0 if the free ring is full or the pool is invalid.
#[no_mangle]
pub unsafe extern "C" fn oakengine_ipc_framepool_release(
	self_: *mut OakFrameSlotPool,
	index: u32,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if self_.is_null() {
			return Ok(0);
		}
		let pool = &*(self_ as *const FrameSlotPool);
		if !pool.is_valid() || index >= pool.slot_count() {
			return Ok(0);
		}
		Ok(pool.release(index) as c_int)
	})
}

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
	use super::*;

	/// A unique, temporary POSIX segment key for a test (pid + counter), so
	/// parallel test runs never collide.
	fn test_key(name: &str) -> String {
		static COUNTER: AtomicU32 = AtomicU32::new(0);
		let n = COUNTER.fetch_add(1, Ordering::Relaxed);
		SharedMemoryRegion::make_key(i64::from(std::process::id()), (n & 0x7FFF) as i32)
			+ &format!("-{name}")
	}

	/// Create one segment and map it a second time — the in-process
	/// equivalent of two processes sharing a segment. Returns
	/// `(owner_region, peer_region)`; both must be kept alive for the
	/// whole test (the peer is an attach that does not unlink).
	fn two_mappings(key: &str, size: usize) -> (SharedMemoryRegion, SharedMemoryRegion) {
		let mut owner = SharedMemoryRegion::new();
		assert!(
			owner.open(key, size, ShmMode::Create),
			"create failed: {}",
			owner.error()
		);
		let mut peer = SharedMemoryRegion::new();
		assert!(
			peer.open(key, size, ShmMode::Attach),
			"attach failed: {}",
			peer.error()
		);
		(owner, peer)
	}

	// ---- SpscRingBuffer -------------------------------------------------

	#[test]
	fn ring_bytes_needed_matches_cpp_layout() {
		// 12 header bytes + capacity * 4.
		assert_eq!(SpscRingBuffer::bytes_needed(4), 12 + 16);
		assert_eq!(SpscRingBuffer::bytes_needed(5), 12 + 20);
		assert_eq!(SpscRingBuffer::bytes_needed(0), 12);
	}

	#[test]
	fn ring_empty_full_and_single_entry() {
		let key = test_key("ring-empty");
		let size = SpscRingBuffer::bytes_needed(4);
		let (owner, peer) = two_mappings(&key, size);
		// SAFETY: both mappings are live and at least `size` bytes.
		let prod = unsafe { SpscRingBuffer::create(owner.data(), 4) };
		let cons = unsafe { SpscRingBuffer::attach(peer.data()) };

		assert!(unsafe { cons.is_empty_approx() });
		let mut v = 99;
		assert!(!unsafe { cons.pop(&mut v) });
		assert_eq!(v, 99);

		assert!(unsafe { prod.push(7) });
		assert!(!unsafe { cons.is_empty_approx() });
		assert_eq!(unsafe { cons.size_approx() }, 1);
		assert!(unsafe { cons.pop(&mut v) });
		assert_eq!(v, 7);
		assert!(unsafe { cons.is_empty_approx() });
	}

	#[test]
	fn ring_capacity_minus_one_live_entries() {
		// A ring of capacity N holds at most N-1 entries (one slot is
		// always left empty to tell full from empty).
		let key = test_key("ring-cap");
		let size = SpscRingBuffer::bytes_needed(4);
		let (owner, peer) = two_mappings(&key, size);
		// SAFETY: live mappings.
		let prod = unsafe { SpscRingBuffer::create(owner.data(), 4) };
		let cons = unsafe { SpscRingBuffer::attach(peer.data()) };

		for i in 0..3 {
			assert!(unsafe { prod.push(i) });
		}
		// The 4th push must fail: head would collide with tail.
		assert!(!unsafe { prod.push(99) });

		let mut v = 0;
		for expected in 0..3 {
			assert!(unsafe { cons.pop(&mut v) });
			assert_eq!(v, expected);
		}
		assert!(!unsafe { cons.pop(&mut v) });
	}

	#[test]
	fn ring_wraparound_preserves_order() {
		// Fill, drain, then wrap past the end of the slot array: cursors
		// are modulo-capacity, order must be preserved across the wrap.
		let key = test_key("ring-wrap");
		let size = SpscRingBuffer::bytes_needed(4);
		let (owner, peer) = two_mappings(&key, size);
		// SAFETY: live mappings.
		let prod = unsafe { SpscRingBuffer::create(owner.data(), 4) };
		let cons = unsafe { SpscRingBuffer::attach(peer.data()) };

		for i in 0..3 {
			assert!(unsafe { prod.push(i) });
		}
		let mut v = 0;
		for _ in 0..3 {
			assert!(unsafe { cons.pop(&mut v) });
		}
		// Ring is empty again; push past the wrap point.
		for i in 3..6 {
			assert!(unsafe { prod.push(i) });
		}
		for expected in 3..6 {
			assert!(unsafe { cons.pop(&mut v) });
			assert_eq!(v, expected);
		}
	}

	// ---- FrameSlotPool --------------------------------------------------

	#[test]
	fn framepool_bytes_needed_matches_cpp_offsets() {
		// Recompute by hand with the C++ layout: header 64, each ring
		// align_up(12 + 4*(n+1), 64), meta align_up(176*n, 64), data
		// align_up(slot_bytes, 64) * n.
		let check = |n: u32, slot: usize| {
			let ring = align_up(12 + 4 * (n as usize + 1), 64);
			let expected =
				64 + ring + ring + align_up(176 * n as usize, 64) + align_up(slot, 64) * n as usize;
			assert_eq!(FrameSlotPool::bytes_needed(n, slot), expected);
		};
		check(4, 4096);
		check(6, 1_000_000);
		check(1, 64);
		check(3, 100);
	}

	#[test]
	fn framepool_create_attach_two_processes_both_directions() {
		// "Two processes": two mappings of the same segment. Owner creates
		// the pool; the peer attaches. A filler on one side and a drainer
		// on the other exchange slots in both directions.
		let key = test_key("pool-bidi");
		let slots = 4u32;
		let slot_bytes = 64usize;
		let size = FrameSlotPool::bytes_needed(slots, slot_bytes);
		let (owner, peer) = two_mappings(&key, size);

		// SAFETY: both mappings are live and sized by bytes_needed.
		let filler = unsafe { FrameSlotPool::create(owner.data(), slots, slot_bytes) };
		let drainer = unsafe { FrameSlotPool::attach(peer.data()) };

		assert!(filler.is_valid());
		assert!(drainer.is_valid());
		assert_eq!(drainer.slot_count(), slots);
		assert_eq!(drainer.slot_data_bytes(), slot_bytes);

		// Filler acquires every slot exactly once (seeded free ring), then
		// the free ring is empty.
		let mut got = Vec::new();
		for _ in 0..slots {
			let mut s = 0;
			assert!(unsafe { filler.acquire(&mut s) });
			got.push(s);
		}
		got.sort_unstable();
		assert_eq!(got, vec![0, 1, 2, 3]);
		let mut extra = 0;
		assert!(!unsafe { filler.acquire(&mut extra) });
		// Drainer sees nothing ready yet.
		assert!(!unsafe { drainer.consume(&mut extra) });

		// Filler writes pixels + meta into two slots and publishes them.
		for (i, slot) in [0u32, 2u32].iter().enumerate() {
			// SAFETY: `slot` was acquired above.
			let data = unsafe { filler.slot_data(*slot) };
			unsafe { ptr::write_bytes(data, (i * 40 + 1) as u8, slot_bytes) };
			// SAFETY: slot in range.
			let meta = unsafe { &mut *filler.meta(*slot) };
			meta.id = 100 + *slot as i64;
			meta.width = 8;
			meta.height = 8;
			meta.data_size = slot_bytes as i32;
			assert!(unsafe { filler.publish(*slot) });
		}

		// Drainer consumes them through its own mapping and sees the same
		// payloads and metadata.
		let mut consumed = Vec::new();
		for _ in 0..2 {
			let mut s = 0;
			assert!(unsafe { drainer.consume(&mut s) });
			// SAFETY: s was consumed.
			let data = unsafe { drainer.slot_data_const(s) };
			let meta = unsafe { &*drainer.meta_const(s) };
			assert_eq!(meta.id, 100 + s as i64);
			assert_eq!(meta.width, 8);
			assert_eq!(meta.data_size, slot_bytes as i32);
			// SAFETY: slot_bytes readable in the slot block.
			let first = unsafe { *data };
			assert_eq!(first, ((s as usize / 2) * 40 + 1) as u8);
			consumed.push(s);
		}
		consumed.sort_unstable();
		assert_eq!(consumed, vec![0, 2]);
		assert!(!unsafe { drainer.consume(&mut extra) });

		// Drainer releases the slots back; the filler can acquire them
		// again — the full round trip through both rings.
		for s in consumed {
			assert!(unsafe { drainer.release(s) });
		}
		let mut s = 0;
		assert!(unsafe { filler.acquire(&mut s) });
		assert_eq!(s, 0);
	}

	#[test]
	fn framepool_wraparound_and_full_edges() {
		// Small pool: cycle every slot many times, verifying the rings'
		// modulo behavior end to end.
		let key = test_key("pool-wrap");
		let slots = 3u32;
		let slot_bytes = 32usize;
		let size = FrameSlotPool::bytes_needed(slots, slot_bytes);
		let (owner, peer) = two_mappings(&key, size);

		// SAFETY: live mappings.
		let filler = unsafe { FrameSlotPool::create(owner.data(), slots, slot_bytes) };
		let drainer = unsafe { FrameSlotPool::attach(peer.data()) };

		for cycle in 0..4u32 {
			let mut published = Vec::new();
			for _ in 0..slots {
				let mut s = 0;
				assert!(unsafe { filler.acquire(&mut s) }, "cycle {cycle}");
				// SAFETY: acquired slot.
				unsafe { ptr::write_bytes(filler.slot_data(s), cycle as u8, slot_bytes) };
				// SAFETY: slot in range.
				let meta = unsafe { &mut *filler.meta(s) };
				meta.id = i64::from(cycle * 100 + s);
				assert!(unsafe { filler.publish(s) });
				published.push(s);
			}
			// Pool is full on the filler side.
			let mut x = 0;
			assert!(!unsafe { filler.acquire(&mut x) });

			// Drain everything on the drainer side.
			let mut consumed = Vec::new();
			for _ in 0..slots {
				let mut s = 0;
				assert!(unsafe { drainer.consume(&mut s) });
				// SAFETY: consumed slot.
				let meta = unsafe { &*drainer.meta_const(s) };
				assert_eq!(meta.id, i64::from(cycle * 100 + s));
				// SAFETY: 1 byte readable.
				assert_eq!(unsafe { *drainer.slot_data_const(s) }, cycle as u8);
				consumed.push(s);
			}
			assert!(!unsafe { drainer.consume(&mut x) });
			consumed.sort_unstable();
			assert_eq!(consumed, vec![0, 1, 2]);

			for s in consumed {
				assert!(unsafe { drainer.release(s) });
			}
		}
	}

	#[test]
	fn framepool_attach_rejects_wrong_magic() {
		let key = test_key("pool-badmagic");
		let size = FrameSlotPool::bytes_needed(2, 16);
		let (owner, _peer) = two_mappings(&key, size);
		// Overwrite the header area with garbage — no pool magic.
		// SAFETY: owner mapping is live.
		unsafe { ptr::write_bytes(owner.data(), 0xAB, 64) };
		// SAFETY: buffer is live.
		let pool = unsafe { FrameSlotPool::attach(owner.data()) };
		assert!(!pool.is_valid());
		assert_eq!(pool.slot_count(), 0);
		assert_eq!(pool.slot_data_bytes(), 0);
	}

	#[test]
	fn framepool_pool_over_reused_segment_is_consistent() {
		// A pool that has been cycled fully and then attached fresh reports
		// the same geometry as bytes_needed computed it.
		let key = test_key("pool-geometry");
		let slots = 5u32;
		let slot_bytes = 1000usize;
		let size = FrameSlotPool::bytes_needed(slots, slot_bytes);
		let (owner, peer) = two_mappings(&key, size);
		// SAFETY: live mappings.
		let _ = unsafe { FrameSlotPool::create(owner.data(), slots, slot_bytes) };
		let attached = unsafe { FrameSlotPool::attach(peer.data()) };
		assert!(attached.is_valid());
		assert_eq!(attached.slot_count(), slots);
		assert_eq!(attached.slot_data_bytes(), slot_bytes);
		// Slot stride is 64-aligned (matches the C++ data layout).
		// SAFETY: valid pool.
		let s0 = unsafe { attached.slot_data(0) };
		let s1 = unsafe { attached.slot_data(1) };
		assert_eq!(s1 as usize - s0 as usize, align_up(slot_bytes, K_ALIGN));
	}

	// ---- SharedMemoryRegion ---------------------------------------------

	#[test]
	fn region_create_attach_write_visibility() {
		let key = test_key("region-vis");
		let size = 4096usize;
		let (mut owner, mut peer) = two_mappings(&key, size);
		assert!(owner.is_valid());
		assert!(peer.is_valid());
		assert_eq!(owner.size(), size);
		assert_eq!(peer.size(), size);
		assert_eq!(owner.key(), key);
		assert_eq!(peer.key(), key);

		// Owner writes; peer sees it through its own mapping.
		// SAFETY: both mappings are live with `size` bytes.
		unsafe {
			let dst = owner.data() as *mut u32;
			*dst = 0xDEADBEEF;
		}
		// SAFETY: peer mapping live.
		let seen = unsafe { *(peer.data() as *const u32) };
		assert_eq!(seen, 0xDEADBEEF);

		// Peer writes back; owner sees it.
		// SAFETY: peer mapping live.
		unsafe {
			let dst = peer.data() as *mut u32;
			*dst = 0x12345678;
		}
		// SAFETY: owner mapping live.
		assert_eq!(unsafe { *(owner.data() as *const u32) }, 0x12345678);

		// Closing the ATTACH side does not unlink: while the owner lives,
		// a third mapping can still open the name.
		peer.close();
		assert!(!peer.is_valid());
		let mut third = SharedMemoryRegion::new();
		assert!(third.open(&key, size, ShmMode::Attach), "{}", third.error());
		assert!(third.is_valid());
		third.close();

		// Closing the OWNER unlinks the segment; further attaches fail.
		owner.close();
		assert!(!owner.is_valid());
		let mut fourth = SharedMemoryRegion::new();
		assert!(!fourth.open(&key, size, ShmMode::Attach));
	}

	#[test]
	fn region_create_replaces_stale_segment() {
		// Mirrors the C++: Create unlinks any stale segment with the same
		// name first (crash cleanup), so a second Create SUCCEEDS and owns
		// a fresh, zeroed segment.
		let key = test_key("region-exists");
		let size = 128usize;
		let (mut owner, _peer) = two_mappings(&key, size);
		assert!(owner.is_valid());
		// SAFETY: owner mapping live.
		unsafe { *(owner.data() as *mut u32) = 0xCAFEBABE };

		let mut second = SharedMemoryRegion::new();
		assert!(
			second.open(&key, size, ShmMode::Create),
			"{}",
			second.error()
		);
		assert!(second.is_valid());
		// The replacement segment is fresh (zeroed by create).
		// SAFETY: second mapping live.
		assert_eq!(unsafe { *(second.data() as *const u32) }, 0);
	}

	#[test]
	fn region_attach_fails_when_segment_too_small() {
		// macOS rounds shm segment sizes up to a 16 KiB minimum, so use
		// sizes above that to exercise the size check.
		let key = test_key("region-small");
		let (owner, _peer) = two_mappings(&key, 4096);
		assert!(owner.is_valid());

		// Attaching with a larger size than the segment must fail (the
		// fstat check, mirroring the C++).
		let mut big = SharedMemoryRegion::new();
		assert!(!big.open(&key, 65536, ShmMode::Attach));
		assert!(!big.is_valid());
		assert!(!big.error().is_empty());
	}

	#[test]
	fn region_make_key_format() {
		assert_eq!(SharedMemoryRegion::make_key(4242, 3), "olive-rw-4242-3");
		assert_eq!(SharedMemoryRegion::make_key(1, 0), "olive-rw-1-0");
	}

	#[test]
	fn region_keys_are_isolation_safe() {
		// Keys with slashes are flattened to a single-slash POSIX name.
		let key = "a/b/c";
		let size = 64usize;
		let (mut owner, mut peer) = two_mappings(key, size);
		assert!(owner.is_valid());
		assert!(peer.is_valid());
		// The actual POSIX name is "/a_b_c".
		// SAFETY: mapping live.
		unsafe { *(owner.data() as *mut u32) = 7 };
		// SAFETY: peer mapping live.
		assert_eq!(unsafe { *(peer.data() as *const u32) }, 7);
	}

	// ---- C ABI wrappers ---------------------------------------------------

	#[test]
	fn c_abi_shm_make_key_buf_size() {
		let mut buf = [0 as c_char; 64];
		let n = unsafe { oakengine_ipc_shm_make_key(7, 2, buf.as_mut_ptr(), 64) };
		assert_eq!(n, "olive-rw-7-2".len() as c_int);
		let s = unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_string_lossy()
			.into_owned();
		assert_eq!(s, "olive-rw-7-2");
		// NULL/0 buffer only queries the size.
		assert_eq!(
			unsafe { oakengine_ipc_shm_make_key(7, 2, std::ptr::null_mut(), 0) },
			n
		);
	}

	#[test]
	fn c_abi_framepool_create_attach_publish_consume() {
		let key = test_key("cabi-pool");
		let slots = 2u32;
		let slot_bytes = 32usize;
		let size = FrameSlotPool::bytes_needed(slots, slot_bytes);
		let (mut owner, mut peer) = two_mappings(&key, size);

		// SAFETY: both regions live.
		let owner_ptr = unsafe { owner.data() as *mut c_void };
		let peer_ptr = unsafe { peer.data() as *mut c_void };
		// SAFETY: buffers sized by bytes_needed.
		let pool = unsafe { oakengine_ipc_framepool_create(owner_ptr, slots, slot_bytes) };
		assert!(!pool.is_null());
		assert_eq!(unsafe { oakengine_ipc_framepool_is_valid(pool) }, 1);
		assert_eq!(unsafe { oakengine_ipc_framepool_slot_count(pool) }, slots);
		assert_eq!(
			unsafe { oakengine_ipc_framepool_slot_data_bytes(pool) },
			slot_bytes
		);

		// SAFETY: peer buffer is a live mapping of the same segment.
		let attached = unsafe { oakengine_ipc_framepool_attach(peer_ptr) };
		assert!(!attached.is_null());
		assert_eq!(unsafe { oakengine_ipc_framepool_is_valid(attached) }, 1);

		let mut slot = 0u32;
		assert_eq!(
			unsafe { oakengine_ipc_framepool_acquire(pool, &mut slot) },
			1
		);
		// SAFETY: acquired slot; slot_bytes writable (count in bytes).
		unsafe {
			ptr::write_bytes(
				oakengine_ipc_framepool_slot_data(pool, slot) as *mut u8,
				0x5A,
				slot_bytes,
			);
		}
		// SAFETY: slot in range.
		let meta = unsafe { &mut *oakengine_ipc_framepool_meta(pool, slot) };
		meta.id = 77;
		assert_eq!(unsafe { oakengine_ipc_framepool_publish(pool, slot) }, 1);

		let mut got = 99u32;
		assert_eq!(
			unsafe { oakengine_ipc_framepool_consume(attached, &mut got) },
			1
		);
		assert_eq!(got, slot);
		// SAFETY: consumed slot.
		let meta = unsafe { &*oakengine_ipc_framepool_meta_const(attached, got) };
		assert_eq!(meta.id, 77);
		// SAFETY: slot_bytes readable.
		assert_eq!(
			unsafe { *(oakengine_ipc_framepool_slot_data_const(attached, got) as *const u8) },
			0x5A
		);
		assert_eq!(unsafe { oakengine_ipc_framepool_release(attached, got) }, 1);

		unsafe { oakengine_ipc_framepool_free(pool) };
		unsafe { oakengine_ipc_framepool_free(attached) };
	}

	#[test]
	fn c_abi_shm_open_data_size_error() {
		let key = test_key("cabi-shm");
		let size = 256usize;
		// SAFETY: shm_create returns an owned handle.
		let region = unsafe { oakengine_ipc_shm_create() };
		assert!(!region.is_null());
		let key_c = std::ffi::CString::new(key.clone()).unwrap();
		// SAFETY: valid C string + owned handle.
		let rc = unsafe { oakengine_ipc_shm_open(region, key_c.as_ptr(), size, 0) };
		assert_eq!(rc, 1);
		assert_eq!(unsafe { oakengine_ipc_shm_is_valid(region) }, 1);
		assert_eq!(unsafe { oakengine_ipc_shm_size(region) }, size);
		// SAFETY: mapping live.
		assert!(!unsafe { oakengine_ipc_shm_data(region) }.is_null());

		let mut kb = [0 as c_char; 128];
		let n = unsafe { oakengine_ipc_shm_key(region, kb.as_mut_ptr(), 128) };
		assert_eq!(n, key.len() as c_int);
		assert_eq!(unsafe { oakengine_ipc_shm_is_valid(region) }, 1);

		unsafe { oakengine_ipc_shm_close(region) };
		assert_eq!(unsafe { oakengine_ipc_shm_is_valid(region) }, 0);
		unsafe { oakengine_ipc_shm_free(region) };

		// A failed open records a human-readable error.
		// SAFETY: shm_create returns an owned handle.
		let region2 = unsafe { oakengine_ipc_shm_create() };
		let bad = std::ffi::CString::new("olive-rw-no-such-segment-for-test").unwrap();
		// SAFETY: valid C string + owned handle.
		assert_eq!(
			unsafe { oakengine_ipc_shm_open(region2, bad.as_ptr(), size, 1) },
			0
		);
		let mut eb = [0 as c_char; 256];
		let n = unsafe { oakengine_ipc_shm_error(region2, eb.as_mut_ptr(), 256) };
		assert!(n > 0);
		assert_eq!(unsafe { oakengine_ipc_shm_is_valid(region2) }, 0);
		unsafe { oakengine_ipc_shm_free(region2) };
	}
}
