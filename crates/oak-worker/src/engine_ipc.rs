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

//! The `liboakengine` C-ABI surface, as consumed by the worker binary.
//!
//! The engine facade (crates/oakengine) is the cdylib and the ONLY
//! implementation owner of the worker/IPC runtime; this crate is a pure
//! C-ABI consumer. Everything here goes through the `extern "C"`
//! declarations of the `oakengine_*` symbols below — never a direct Rust
//! call into an oak* module crate.
//!
//! The module provides three layers:
//!
//!   - **The externs.** `#[link(name = "oakengine", kind = "dylib")]`
//!     declarations of the frozen `oakengine_worker_*` and
//!     `oakengine_ipc_*` exports (`engine/include/oakengine/worker.h` and
//!     `ipc.h`), plus the opaque `Oak*` handle mirrors and the POD
//!     [`FrameSlotMeta`] / [`ShmMode`] mirrors the signatures reference.
//!     build.rs points the linker at the built dylib and embeds its rpath.
//!   - **In-process wrapper types.** [`SharedMemoryRegion`] and
//!     [`FrameSlotPool`] wrap the opaque handles with `Drop` and a safe
//!     surface, so [`crate::transport`]/[`crate::session`] (and their
//!     tests) can attach real shared-memory pools through the engine.
//!   - **The wire protocol.** [`HandshakeMsg`]/[`RenderFrameMsg`]/
//!     [`LoadGraphMsg`] + [`error_message`] + the `TYPE_*` constants —
//!     serde-only structs matching the engine's NDJSON control plane; the
//!     worker-side session mirror validates and builds these lines
//!     locally.

#![allow(dead_code)]

use std::ffi::{c_char, c_int, c_void};

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

// ---------------------------------------------------------------------------
// Opaque handle mirrors (engine/include/oakengine/{worker,ipc}.h)
// ---------------------------------------------------------------------------

/// Opaque `OakWorkerSession` handle (worker.h). The engine owns the box;
/// this crate only ever sees the pointer.
#[repr(C)]
pub struct OakWorkerSession {
	_opaque: [u8; 0],
}

/// Opaque `OakSharedMemoryRegion` handle (ipc.h).
#[repr(C)]
pub struct OakSharedMemoryRegion {
	_opaque: [u8; 0],
}

/// Opaque `OakFrameSlotPool` handle (ipc.h).
#[repr(C)]
pub struct OakFrameSlotPool {
	_opaque: [u8; 0],
}

/// `OAK_IPC_SHM_KEY_CAP` — capacity of shm key strings (ipc.h), incl. NUL.
pub const OAK_IPC_SHM_KEY_CAP: usize = 128;
/// `OAK_IPC_COLORSPACE_CAP` — capacity of `oak_frame_slot_meta::colorspace`.
pub const OAK_IPC_COLORSPACE_CAP: usize = 128;

/// Per-slot metadata describing the frame currently occupying a slot —
/// field-for-field `oak_frame_slot_meta` from `engine/include/oakengine/ipc.h`
/// (the POD lives in shared memory; the layout is the version-1 wire
/// protocol).
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

/// `OAK_IPC_SHM_MODE_CREATE` / `OAK_IPC_SHM_MODE_ATTACH` (ipc.h).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ShmMode {
	/// Create (and own) the segment. Fails if it already exists; the owner
	/// unlinks it on close.
	Create,
	/// Attach to a segment created by the peer. Does not unlink on close.
	Attach,
}

// ---------------------------------------------------------------------------
// C ABI exports (engine/include/oakengine/{worker,ipc}.h)
// ---------------------------------------------------------------------------

// The built `liboakengine` dylib (crates/oakengine, cdylib). build.rs
// emits the link-search path and rpath for the target profile dir.
#[link(name = "oakengine", kind = "dylib")]
extern "C" {
	// ---- engine/include/oakengine/worker.h ----
	/// Full render-worker main (argv-based `--backend` scanning, startup
	/// handshake, NDJSON control loop). Returns the process exit code.
	pub fn oakengine_worker_main(argc: c_int, argv: *mut *mut c_char) -> c_int;
	/// Create a session for the given render backend (NULL = none).
	pub fn oakengine_worker_session_create(backend: *const c_char) -> *mut OakWorkerSession;
	/// Free a session. NULL no-op.
	pub fn oakengine_worker_session_free(self_: *mut OakWorkerSession);
	/// 1 when the session holds an initialized render backend.
	pub fn oakengine_worker_session_has_renderer(self_: *const OakWorkerSession) -> c_int;
	/// Load the runtime services; 1 on success, 0 for a NULL session.
	pub fn oakengine_worker_session_initialize_runtime(self_: *mut OakWorkerSession) -> c_int;
	/// Build the startup handshake (buf/size convention; returns the
	/// required size, -1 on failure).
	pub fn oakengine_worker_session_startup_handshake(
		self_: *const OakWorkerSession,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// Handle one NDJSON control line; 0 for "no response", the response
	/// length for a response, -1 on a fatal handler failure.
	pub fn oakengine_worker_session_handle_json(
		self_: *mut OakWorkerSession,
		line: *const c_char,
		response_buf: *mut c_char,
		response_buf_size: c_int,
	) -> c_int;
	/// 1 once a shutdown control message has been received.
	pub fn oakengine_worker_session_shutdown_requested(self_: *const OakWorkerSession) -> c_int;

	// ---- engine/include/oakengine/ipc.h ----
	/// Allocate an empty (invalid) region object.
	pub fn oakengine_ipc_shm_create() -> *mut OakSharedMemoryRegion;
	/// Free a region object. NULL no-op.
	pub fn oakengine_ipc_shm_free(self_: *mut OakSharedMemoryRegion);
	/// Open the segment; 1 on success, 0 on failure (shm_error carries the
	/// reason). Mode: 0 = create, 1 = attach.
	pub fn oakengine_ipc_shm_open(
		self_: *mut OakSharedMemoryRegion,
		key: *const c_char,
		size: usize,
		mode: c_int,
	) -> c_int;
	/// Unmap and (if owner) unlink.
	pub fn oakengine_ipc_shm_close(self_: *mut OakSharedMemoryRegion);
	/// 1 when the region holds a live mapping.
	pub fn oakengine_ipc_shm_is_valid(self_: *const OakSharedMemoryRegion) -> c_int;
	/// The mapped data pointer (NULL when invalid).
	pub fn oakengine_ipc_shm_data(self_: *mut OakSharedMemoryRegion) -> *mut c_void;
	/// Mapping size in bytes.
	pub fn oakengine_ipc_shm_size(self_: *const OakSharedMemoryRegion) -> usize;
	/// The key the region was opened with (buf/size convention).
	pub fn oakengine_ipc_shm_key(
		self_: *const OakSharedMemoryRegion,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// Reason of the last failed open (buf/size convention).
	pub fn oakengine_ipc_shm_error(
		self_: *const OakSharedMemoryRegion,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// Build a unique segment key ("olive-rw-<pid>-<index>", buf/size
	/// convention).
	pub fn oakengine_ipc_shm_make_key(
		owner_pid: i64,
		worker_index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// Total bytes a region must provide to back a pool of
	/// `slot_count` x `slot_data_bytes`.
	pub fn oakengine_ipc_framepool_bytes_needed(slot_count: u32, slot_data_bytes: usize) -> usize;
	/// Lay out and initialize a brand-new pool over `mem` (owner side,
	/// once). The handle does not own `mem`.
	pub fn oakengine_ipc_framepool_create(
		mem: *mut c_void,
		slot_count: u32,
		slot_data_bytes: usize,
	) -> *mut OakFrameSlotPool;
	/// Map an existing pool (peer side). NULL when the segment holds no
	/// pool.
	pub fn oakengine_ipc_framepool_attach(mem: *mut c_void) -> *mut OakFrameSlotPool;
	/// Copy the view (same shared memory, independent handle).
	pub fn oakengine_ipc_framepool_copy(self_: *const OakFrameSlotPool) -> *mut OakFrameSlotPool;
	/// Free a pool view. NULL no-op.
	pub fn oakengine_ipc_framepool_free(self_: *mut OakFrameSlotPool);
	/// 1 when the pool was attached to a valid pool header.
	pub fn oakengine_ipc_framepool_is_valid(self_: *const OakFrameSlotPool) -> c_int;
	/// Number of slots in the pool.
	pub fn oakengine_ipc_framepool_slot_count(self_: *const OakFrameSlotPool) -> u32;
	/// Bytes available in every slot's pixel-data block.
	pub fn oakengine_ipc_framepool_slot_data_bytes(self_: *const OakFrameSlotPool) -> usize;
	/// Take a free slot; 1 on success (`*index` set).
	pub fn oakengine_ipc_framepool_acquire(self_: *mut OakFrameSlotPool, index: *mut u32) -> c_int;
	/// Pointer to a slot's pixel data block.
	pub fn oakengine_ipc_framepool_slot_data(
		self_: *mut OakFrameSlotPool,
		index: u32,
	) -> *mut c_void;
	/// Immutable pixel data for a slot.
	pub fn oakengine_ipc_framepool_slot_data_const(
		self_: *const OakFrameSlotPool,
		index: u32,
	) -> *const c_void;
	/// Mutable per-slot metadata (borrowed).
	pub fn oakengine_ipc_framepool_meta(
		self_: *mut OakFrameSlotPool,
		index: u32,
	) -> *mut FrameSlotMeta;
	/// Immutable per-slot metadata.
	pub fn oakengine_ipc_framepool_meta_const(
		self_: *const OakFrameSlotPool,
		index: u32,
	) -> *const FrameSlotMeta;
	/// Publish a filled slot; 1 on success.
	pub fn oakengine_ipc_framepool_publish(self_: *mut OakFrameSlotPool, index: u32) -> c_int;
	/// Take the next published slot; 1 on success (`*index` set).
	pub fn oakengine_ipc_framepool_consume(self_: *mut OakFrameSlotPool, index: *mut u32) -> c_int;
	/// Return a consumed slot to the free pool; 1 on success.
	pub fn oakengine_ipc_framepool_release(self_: *mut OakFrameSlotPool, index: u32) -> c_int;
}

// ---------------------------------------------------------------------------
// Wire protocol (the engine's NDJSON control plane, mirrored locally)
// ---------------------------------------------------------------------------

/// `"handshake"`.
pub const TYPE_HANDSHAKE: &str = "handshake";
/// `"load_graph"`.
pub const TYPE_LOAD_GRAPH: &str = "load_graph";
/// `"render_frame"`.
pub const TYPE_RENDER_FRAME: &str = "render_frame";
/// `"frame_ready"`.
pub const TYPE_FRAME_READY: &str = "frame_ready";
/// `"cancel"`.
pub const TYPE_CANCEL: &str = "cancel";
/// `"graph_update"`.
pub const TYPE_GRAPH_UPDATE: &str = "graph_update";
/// `"shutdown"`.
pub const TYPE_SHUTDOWN: &str = "shutdown";
/// `"error"`.
pub const TYPE_ERROR: &str = "error";

/// `handshake` — field-for-field equivalent of `oak_ipc_handshake`
/// (ipc.h). Wire field names match the C++ serializer.
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct HandshakeMsg {
	/// Protocol version.
	pub protocol_version: i32,
	/// Worker->main output shared-memory segment key.
	pub shm_key: String,
	/// Main->worker input shared-memory segment key (optional).
	pub input_shm_key: String,
	/// Number of main->worker input frame slots.
	pub input_slots: i32,
	/// Number of worker->main output frame slots.
	pub output_slots: i32,
	/// Per-output-slot pixel block size.
	pub slot_data_bytes: i64,
	/// Per-input-slot pixel block size.
	pub input_slot_data_bytes: i64,
}

impl HandshakeMsg {
	/// The worker's startup handshake (`worker.cpp startup_handshake()`).
	pub fn to_json(&self) -> Value {
		json!({
			"type": TYPE_HANDSHAKE,
			"protocol_version": self.protocol_version,
			"shm_key": self.shm_key,
			"input_shm_key": self.input_shm_key,
			"input_slots": self.input_slots,
			"output_slots": self.output_slots,
			"slot_data_bytes": self.slot_data_bytes,
			"input_slot_data_bytes": self.input_slot_data_bytes,
		})
	}
}

/// `render_frame` — request a frame render. Wire names per ipcmessage.cpp:
/// `ticket`, `node`, `channels` (not the ipc.h POD names).
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct RenderFrameMsg {
	/// Correlates with the eventual frame_ready.
	pub ticket: i64,
	/// Viewer node stable uuid in the loaded graph.
	pub node: String,
	pub time_num: i64,
	pub time_den: i64,
	/// Forced output size (0 = graph default).
	pub width: i32,
	pub height: i32,
	/// Forced PixelFormat (-1 = default).
	pub format: i32,
	/// Channel count (0 = default).
	pub channels: i32,
	/// RenderMode.
	pub mode: i32,
	/// Optional decoded input slot (-1 = none).
	pub input_slot: i32,
	/// Ordered decoded input slots.
	pub input_slots: Vec<i32>,
	/// Output color transform present?
	pub has_color_transform: bool,
	pub color_is_display: bool,
	pub color_output: String,
	pub color_view: String,
	pub color_look: String,
}

/// `load_graph` — path to a temporary file holding the serialized graph.
#[derive(Serialize, Deserialize, Default, Debug, Clone)]
#[serde(default)]
pub struct LoadGraphMsg {
	pub path: String,
}

/// Build a worker-side error report, mirroring `error_message()` in
/// worker.cpp: `{"type":"error","message":...}` plus `"ticket"` when
/// non-zero.
pub fn error_message(message: &str, ticket: Option<i64>) -> Value {
	match ticket.filter(|t| *t != 0) {
		Some(t) => json!({ "type": TYPE_ERROR, "message": message, "ticket": t }),
		None => json!({ "type": TYPE_ERROR, "message": message }),
	}
}

// ---------------------------------------------------------------------------
// In-process wrapper types over the opaque C-ABI handles
// ---------------------------------------------------------------------------

/// Two-stage read of a buf/size-convention string getter (the engine's
/// `write_string` reports the length excluding the NUL for a NULL buffer).
unsafe fn region_string(
	region: *const OakSharedMemoryRegion,
	getter: unsafe extern "C" fn(*const OakSharedMemoryRegion, *mut c_char, c_int) -> c_int,
) -> String {
	// SAFETY: the engine's buf/size convention: a NULL/0 buffer only
	// queries the required length.
	let len = unsafe { getter(region, std::ptr::null_mut(), 0) };
	if len <= 0 {
		return String::new();
	}
	let mut buf = vec![0 as c_char; len as usize + 1];
	// SAFETY: `buf` provides len + 1 writable bytes (length + NUL).
	unsafe { getter(region, buf.as_mut_ptr(), buf.len() as c_int) };
	// SAFETY: the engine NUL-terminates what it writes into `buf`.
	unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
		.to_string_lossy()
		.into_owned()
}

/// A named, fixed-size POSIX shared-memory segment mapped inside the
/// engine process (C-ABI wrapper around `OakSharedMemoryRegion`). One side
/// opens the segment in [`ShmMode::Create`] (owner, unlinks on close); the
/// peer attaches by key. The wrapper owns the engine-side handle and
/// closes/frees it on drop.
pub struct SharedMemoryRegion {
	handle: *mut OakSharedMemoryRegion,
}

impl SharedMemoryRegion {
	/// An empty (invalid) region.
	pub fn new() -> SharedMemoryRegion {
		// SAFETY: shm_create is the factory for owned handles (never
		// observes caller memory).
		SharedMemoryRegion {
			handle: unsafe { oakengine_ipc_shm_create() },
		}
	}

	/// Build a unique segment key for a worker, e.g.
	/// "olive-rw-<pid>-<index>" (the engine's `SharedMemoryRegion::make_key`
	/// behind the C ABI). Centralized so the owner and the spawned worker
	/// agree on the same name.
	pub fn make_key(owner_pid: i64, worker_index: i32) -> String {
		let mut buf = [0 as c_char; OAK_IPC_SHM_KEY_CAP];
		// SAFETY: `buf` is writable and sized by the engine's key capacity.
		let n = unsafe {
			oakengine_ipc_shm_make_key(owner_pid, worker_index, buf.as_mut_ptr(), buf.len() as c_int)
		};
		if n <= 0 {
			return String::new();
		}
		// SAFETY: the engine NUL-terminates the key within `buf`.
		unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_string_lossy()
			.into_owned()
	}

	/// Open the segment identified by `key` with the given `size` in bytes
	/// (through the engine). Returns true on success; on failure
	/// [`Self::error`] carries a human-readable reason.
	pub fn open(&mut self, key: &str, size: usize, mode: ShmMode) -> bool {
		let Ok(key_c) = std::ffi::CString::new(key) else {
			return false;
		};
		// SAFETY: `key_c` is a valid C string; `self.handle` is the live
		// handle this wrapper owns.
		unsafe { oakengine_ipc_shm_open(self.handle, key_c.as_ptr(), size, mode as c_int) == 1 }
	}

	/// Unmap and (if owner) unlink the segment.
	pub fn close(&mut self) {
		// SAFETY: the engine's shm_close is a NULL no-op.
		unsafe { oakengine_ipc_shm_close(self.handle) };
	}

	/// True when the region holds a live mapping.
	pub fn is_valid(&self) -> bool {
		// SAFETY: the engine's shm_is_valid returns 0 for NULL handles.
		unsafe { oakengine_ipc_shm_is_valid(self.handle) == 1 }
	}

	/// The mapped data pointer (null when invalid).
	pub fn data(&self) -> *mut u8 {
		// SAFETY: the engine returns NULL for invalid regions.
		unsafe { oakengine_ipc_shm_data(self.handle) as *mut u8 }
	}

	/// The mapping size in bytes.
	pub fn size(&self) -> usize {
		// SAFETY: the engine returns 0 for NULL handles.
		unsafe { oakengine_ipc_shm_size(self.handle) }
	}

	/// The key the region was opened with.
	pub fn key(&self) -> String {
		// SAFETY: region_string follows the engine's buf/size convention.
		unsafe { region_string(self.handle, oakengine_ipc_shm_key) }
	}

	/// Human-readable reason of the last failed open.
	pub fn error(&self) -> String {
		// SAFETY: region_string follows the engine's buf/size convention.
		unsafe { region_string(self.handle, oakengine_ipc_shm_error) }
	}
}

impl Drop for SharedMemoryRegion {
	fn drop(&mut self) {
		// Close first so the owner unlinks the segment, then free the
		// engine-side box. Both are NULL no-ops in the engine.
		// SAFETY: `self.handle` is the handle this wrapper owns and is not
		// used after this.
		unsafe {
			oakengine_ipc_shm_close(self.handle);
			oakengine_ipc_shm_free(self.handle);
		}
	}
}

/// A fixed-size pool of equal-sized frame slots in shared memory with
/// lock-free hand-off (C-ABI wrapper around `OakFrameSlotPool`). The pool
/// does NOT own the memory — it is a view the engine attaches over a
/// mapped [`SharedMemoryRegion`]. Lifecycle: the filler `acquire`s a free
/// slot, writes meta + pixels, then `publish`es it; the drainer `consume`s
/// the next ready slot, reads it, and `release`s it back to the free ring.
///
/// `Clone` mirrors the engine's pool-view copy (same shared memory,
/// independent handle).
pub struct FrameSlotPool {
	handle: *mut OakFrameSlotPool,
}

impl FrameSlotPool {
	/// Total bytes a region must provide to back a pool of
	/// `slot_count` x `slot_data_bytes` (the engine's
	/// `FrameSlotPool::bytes_needed` behind the C ABI).
	pub fn bytes_needed(slot_count: u32, slot_data_bytes: usize) -> usize {
		// SAFETY: pure computation, no handles involved.
		unsafe { oakengine_ipc_framepool_bytes_needed(slot_count, slot_data_bytes) }
	}

	/// Lay out and initialize a brand-new pool over `mem` (owner side,
	/// once) through the engine.
	///
	/// # Safety
	/// `mem` must be a valid, writable, aligned buffer of at least
	/// [`Self::bytes_needed`] bytes, not concurrently written during this
	/// call.
	pub unsafe fn create(mem: *mut u8, slot_count: u32, slot_data_bytes: usize) -> FrameSlotPool {
		// SAFETY: forwarded to the engine's create contract.
		FrameSlotPool {
			handle: unsafe {
				oakengine_ipc_framepool_create(mem as *mut c_void, slot_count, slot_data_bytes)
			},
		}
	}

	/// Map an existing, already-initialized pool (peer side) through the
	/// engine. The returned pool reports `is_valid() == false` when the
	/// segment does not contain a pool.
	///
	/// # Safety
	/// `mem` must point to a mapped segment that either contains a pool or
	/// is an arbitrary buffer whose first bytes are readable.
	pub unsafe fn attach(mem: *mut u8) -> FrameSlotPool {
		// SAFETY: forwarded to the engine's attach contract.
		FrameSlotPool {
			handle: unsafe { oakengine_ipc_framepool_attach(mem as *mut c_void) },
		}
	}

	/// True when the pool was attached to a segment containing a valid pool
	/// header.
	pub fn is_valid(&self) -> bool {
		// SAFETY: the engine returns 0 for NULL handles.
		unsafe { oakengine_ipc_framepool_is_valid(self.handle) == 1 }
	}

	/// Number of slots in the pool (0 for an invalid pool).
	pub fn slot_count(&self) -> u32 {
		// SAFETY: the engine returns 0 for NULL handles.
		unsafe { oakengine_ipc_framepool_slot_count(self.handle) }
	}

	/// Bytes available in every slot's pixel-data block (0 for invalid).
	pub fn slot_data_bytes(&self) -> usize {
		// SAFETY: the engine returns 0 for NULL handles.
		unsafe { oakengine_ipc_framepool_slot_data_bytes(self.handle) }
	}

	/// Take ownership of a free slot. Returns false (leaving `index`
	/// untouched) if none is free.
	///
	/// # Safety
	/// The pool must be a valid view of a live segment.
	pub unsafe fn acquire(&self, index: &mut u32) -> bool {
		// SAFETY: forwarded to the engine's acquire contract.
		unsafe { oakengine_ipc_framepool_acquire(self.handle, index) == 1 }
	}

	/// Pointer to a slot's pixel data block (`slot_data_bytes` available).
	///
	/// # Safety
	/// `index` must be in `0..slot_count`; the pool must be a valid view of
	/// a live segment.
	pub unsafe fn slot_data(&self, index: u32) -> *mut u8 {
		// SAFETY: forwarded to the engine's slot_data contract.
		unsafe { oakengine_ipc_framepool_slot_data(self.handle, index) as *mut u8 }
	}

	/// Immutable pixel data for a slot.
	///
	/// # Safety
	/// `index` must be in `0..slot_count`; the pool must be a valid view of
	/// a live segment.
	pub unsafe fn slot_data_const(&self, index: u32) -> *const u8 {
		// SAFETY: forwarded to the engine's slot_data_const contract.
		unsafe { oakengine_ipc_framepool_slot_data_const(self.handle, index) as *const u8 }
	}

	/// Mutable metadata for a slot. The filler writes this before
	/// [`Self::publish`]. The returned pointer addresses shared memory; it
	/// is borrowed, not owned.
	///
	/// # Safety
	/// `index` must be in `0..slot_count`; the pool must be a valid view of
	/// a live segment.
	pub unsafe fn meta(&self, index: u32) -> *mut FrameSlotMeta {
		// SAFETY: forwarded to the engine's meta contract.
		unsafe { oakengine_ipc_framepool_meta(self.handle, index) }
	}

	/// Immutable metadata for a slot (drainer side).
	///
	/// # Safety
	/// `index` must be in `0..slot_count`; the pool must be a valid view of
	/// a live segment.
	pub unsafe fn meta_const(&self, index: u32) -> *const FrameSlotMeta {
		// SAFETY: forwarded to the engine's meta_const contract.
		unsafe { oakengine_ipc_framepool_meta_const(self.handle, index) }
	}

	/// Publish a filled slot to the drainer. Must follow a successful
	/// [`Self::acquire`] of `index`. Returns false if the ready ring is
	/// full.
	///
	/// # Safety
	/// `index` must be a slot previously acquired and not yet released.
	pub unsafe fn publish(&self, index: u32) -> bool {
		// SAFETY: forwarded to the engine's publish contract.
		unsafe { oakengine_ipc_framepool_publish(self.handle, index) == 1 }
	}

	/// Take the next published slot. Returns false if nothing is ready.
	///
	/// # Safety
	/// The pool must be a valid view of a live segment.
	pub unsafe fn consume(&self, index: &mut u32) -> bool {
		// SAFETY: forwarded to the engine's consume contract.
		unsafe { oakengine_ipc_framepool_consume(self.handle, index) == 1 }
	}

	/// Return a consumed slot to the free pool for reuse. Must follow a
	/// successful [`Self::consume`] of `index`. Returns false if the free
	/// ring is full.
	///
	/// # Safety
	/// `index` must be a slot previously consumed and not yet re-acquired.
	pub unsafe fn release(&self, index: u32) -> bool {
		// SAFETY: forwarded to the engine's release contract.
		unsafe { oakengine_ipc_framepool_release(self.handle, index) == 1 }
	}
}

impl Clone for FrameSlotPool {
	fn clone(&self) -> FrameSlotPool {
		// SAFETY: the engine's framepool_copy yields NULL for a NULL
		// handle.
		FrameSlotPool {
			handle: unsafe { oakengine_ipc_framepool_copy(self.handle) },
		}
	}
}

impl Drop for FrameSlotPool {
	fn drop(&mut self) {
		// SAFETY: the engine's framepool_free is a NULL no-op; the handle
		// is not used after this.
		unsafe { oakengine_ipc_framepool_free(self.handle) };
	}
}
