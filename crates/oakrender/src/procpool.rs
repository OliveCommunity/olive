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

//! The process-isolated render backend (M15 S1): the main-process side
//! of the oak-worker pool — spawn, handshake, NDJSON control, shared
//! memory creation, crash detection and restart, and the ticket-facing
//! [`JobDispatch`] implementation (design doc §3.1–§3.4).
//!
//! ```text
//! TicketArena --Job--> ProcessDispatcher
//!                          |  scheduler.claim_batch (interleaved shards)
//!                          v
//!                WorkerHandle x N  ---- stdio NDJSON (control plane)
//!                  shm segment          render_batch { tickets, slots }
//!                  FrameSlotPool  <---> oak-worker process
//!                          |
//!                frame_ready(ticket, slot)
//!                          v
//!                Completion(Ok(TicketPayload::ShmFrame(ShmFrameRef)))
//! ```
//!
//! Model:
//!   - **Single-threaded control plane.** All dispatcher state lives in
//!     one mutex-guarded [`Inner`] pumped by [`ProcessDispatcher::poll`]
//!     (non-blocking try_recv + try_wait). The mutex guards control
//!     structures only — frame bytes never pass through it: workers
//!     write pixels straight into the shm slots and consumers read them
//!     from the mapping via [`ShmFrameRef`] (zero copy; the only
//!     counted copy path is [`ShmRegionView::slot_to_vec`]).
//!   - **Slot addressing.** The dispatcher assigns destination slots
//!     (main-side addressing, design §3.1); the worker renders into the
//!     given slot and publishes it through the ready ring. Free-slot
//!     bookkeeping mirrors the free SPSC ring in FIFO order, so the
//!     worker's `acquire` always pops exactly the assigned slot.
//!   - **Crash isolation.** Stdout EOF or a non-zero exit marks the
//!     worker dead: its claimed frames are re-queued to the scheduler
//!     (any healthy worker may claim them), the child is reaped, the
//!     segment recreated and the process respawned (bounded restarts).
//!   - **S2 model.** The in-process [`crate::worker::WorkerPool`] is
//!     gone (M15 S2 mandate); [`crate::manager::RenderManager`] defaults
//!     to this backend. The ticket arena also routes **playback-window**
//!     frames here via [`JobSchedule::playback`], and the app pumps the
//!     control plane from the UI tick ([`ProcessDispatcher::poll`]) and
//!     from blocking ticket waits.

use std::collections::{HashMap, HashSet, VecDeque};
use std::io::Write as _;
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc;
use std::sync::{Arc, Mutex, MutexGuard, OnceLock, Weak};
use std::time::{Duration, Instant};

use serde_json::{json, Value};

use crate::error::{Error, Result};
use crate::ipc::{
	write_message, AudioTicketSpec, BatchAcceptedMsg, BatchTicketSpec, FrameFailedMsg, FrameReadyMsg,
	FrameSlotMeta, FrameSlotPool, HandshakeMsg, HelloCapsMsg, PluginProgressMsg, RenderAudioBatchMsg,
	RenderBatchMsg, SharedMemoryRegion, ShmMode, WireMontageClip, SLOT_FORMAT_BGRA8,
	TYPE_BATCH_ACCEPTED, TYPE_ERROR, TYPE_FRAME_FAILED, TYPE_FRAME_READY, TYPE_HANDSHAKE,
	TYPE_HELLO_CAPS, TYPE_PLUGIN_CANCEL, TYPE_PLUGIN_PROGRESS, TYPE_RENDER_AUDIO_BATCH,
	plugin_cancel_json,
};
use crate::scheduler::{FrameKey, FrameRequest, PreviewScheduler, SubmitOutcome};
use crate::ticket::{
	AudioSamples, AudioTicketParams, Completion, TicketPayload, TicketResult, VideoTicketParams,
};
use crate::worker::{Job, JobDispatch};

/// Protocol version spoken by the dispatcher (v1 base; v2 messages are
/// additive — the oak-worker handshake check stays `== 1`).
pub const DISPATCH_PROTOCOL_VERSION: i32 = 1;

/// Restart attempts per worker before its tickets fail permanently.
const MAX_RESTARTS: u32 = 5;

/// Maximum audio bytes a process-backend audio ticket may occupy in a shm
/// slot (M15 S3). Larger ranges (long exports) are refused by `post` so
/// the arena falls back to main-process inline rendering — a several-
/// minute export audio buffer does not need (and should not force) a
/// giant shared-memory segment. ~64 MB ≈ 2.9 min of 48 kHz stereo.
const MAX_AUDIO_SLOT_BYTES: usize = 64 * 1024 * 1024;

/// Legacy fixed default slots per worker (design §3.1 "8 slots starting").
/// The M15 S3 adaptive [`default_slots_per_worker`] policy supersedes it
/// for auto-configured dispatchers; kept as the documented starting point
/// and the cap for small frames.
pub const DEFAULT_SLOTS_PER_WORKER: u32 = 8;

/// Frame bytes copied into main-process heap buffers. The playback path
/// is zero-copy by construction (completions carry [`ShmFrameRef`]s,
/// never pixel `Vec`s); only [`ShmRegionView::slot_to_vec`] bumps this.
/// Tests assert it stays 0 on the preview path.
static MAIN_FRAME_COPIES: AtomicU64 = AtomicU64::new(0);

/// The main-process frame-copy counter (zero-copy assertion; design
/// §3.5).
pub fn main_heap_frame_copies() -> u64 {
	MAIN_FRAME_COPIES.load(Ordering::Relaxed)
}

/// Reset the copy counter (tests).
pub fn reset_main_heap_frame_copies() {
	MAIN_FRAME_COPIES.store(0, Ordering::Relaxed);
}

// ---------------------------------------------------------------------------
// Plugin-progress forwarding (worker -> main) and cancel broadcast
// ---------------------------------------------------------------------------

/// The app-facing plugin-progress callback: invoked by the dispatcher (on
/// the UI tick's poll) for every worker-forwarded `plugin_progress` line
/// (label, message, fraction). The app's [`crate::oakui::ofx`] wiring
/// forwards these into its `PluginProgressEvent` channel. `Arc` so the
/// registry can hand out cheap clones (the callback is `Fn`, not `Clone`).
pub type PluginProgressCb = Arc<dyn Fn(String, String, f64) + Send + Sync>;

static PLUGIN_PROGRESS_CB: OnceLock<Mutex<Option<PluginProgressCb>>> = OnceLock::new();

/// Register (or clear) the plugin-progress forwarding callback.
pub fn set_plugin_progress_cb(cb: Option<PluginProgressCb>) {
	*PLUGIN_PROGRESS_CB
		.get_or_init(|| Mutex::new(None))
		.lock()
		.unwrap_or_else(|e| e.into_inner()) = cb;
}

fn plugin_progress_cb() -> Option<PluginProgressCb> {
	PLUGIN_PROGRESS_CB
		.get_or_init(|| Mutex::new(None))
		.lock()
		.unwrap_or_else(|e| e.into_inner())
		.clone()
}

/// Weak handle to the live dispatcher, registered by
/// [`ProcessDispatcher::new`] so the cancel broadcast can reach the
/// workers without threading a handle through the app.
static DISPATCHER: OnceLock<Mutex<Weak<ProcessDispatcher>>> = OnceLock::new();

fn dispatcher_slot() -> &'static Mutex<Weak<ProcessDispatcher>> {
	DISPATCHER.get_or_init(|| Mutex::new(Weak::new()))
}

/// Broadcast a `plugin_cancel` message to every alive worker: the user
/// cancelled the plugin render; the workers set their sticky cancel flag
/// and their live progress reporters answer false from then on (the
/// plugin aborts at its next progressUpdate). Falls back to a no-op when
/// no dispatcher is live (inline/test backends).
pub fn request_plugin_cancel_all() {
	let dispatcher = dispatcher_slot()
		.lock()
		.unwrap_or_else(|e| e.into_inner())
		.upgrade();
	if let Some(dispatcher) = dispatcher {
		dispatcher.broadcast_plugin_cancel();
	}
}

// ---------------------------------------------------------------------------
// ShmRegionView — one worker segment as seen from the main process
// ---------------------------------------------------------------------------

/// Owned view of the FrameSlotMeta currently in a slot (the shm POD
/// copied out, colorspace as a string).
#[derive(Clone, Debug, PartialEq)]
pub struct ShmFrameMeta {
	/// Caller tag (ticket id).
	pub id: i64,
	/// Frame timestamp numerator.
	pub time_num: i64,
	/// Frame timestamp denominator.
	pub time_den: i64,
	/// Frame width.
	pub width: i32,
	/// Frame height.
	pub height: i32,
	/// Slot wire format (`PixelFormat` int or [`SLOT_FORMAT_BGRA8`]).
	pub format: i32,
	/// Channel count.
	pub channel_count: i32,
	/// Bytes per scanline.
	pub linesize: i32,
	/// Valid bytes in the slot.
	pub data_size: i32,
	/// Input colorspace name.
	pub colorspace: String,
}

impl ShmFrameMeta {
	fn from_pod(pod: &FrameSlotMeta) -> ShmFrameMeta {
		let colorspace = {
			// SAFETY: the POD char array is NUL-padded by the worker.
			let cstr = unsafe { std::ffi::CStr::from_ptr(pod.colorspace.as_ptr()) };
			cstr.to_string_lossy().into_owned()
		};
		ShmFrameMeta {
			id: pod.id,
			time_num: pod.time_num,
			time_den: pod.time_den,
			width: pod.width,
			height: pod.height,
			format: pod.format,
			channel_count: pod.channel_count,
			linesize: pod.linesize,
			data_size: pod.data_size,
			colorspace,
		}
	}
}

/// A worker's shared-memory segment + frame-slot pool, owned by the
/// main process (creator side). Shared through an `Arc` so delivered
/// [`ShmFrameRef`]s keep the mapping alive across worker restarts.
pub struct ShmRegionView {
	region: SharedMemoryRegion,
	pool: FrameSlotPool,
}

// The segment mapping is usable from any local thread; cross-process
// synchronization lives in the rings' atomics.
unsafe impl Send for ShmRegionView {}
unsafe impl Sync for ShmRegionView {}

impl ShmRegionView {
	/// Create (and initialize) a segment of `slots` x `slot_bytes` under
	/// `key`. A stale segment under the same name (left by a crashed
	/// previous owner) is unlinked and the create retried once.
	fn create(key: &str, slots: u32, slot_bytes: usize) -> Result<Arc<ShmRegionView>> {
		let mut region = SharedMemoryRegion::new();
		let bytes = FrameSlotPool::bytes_needed(slots, slot_bytes);
		if !region.open(key, bytes, ShmMode::Create) {
			SharedMemoryRegion::unlink_key(key);
			if !region.open(key, bytes, ShmMode::Create) {
				return Err(Error::Failed(format!(
					"create shm segment {key}: {}",
					region.error()
				)));
			}
		}
		// SAFETY: `region` is a live mapping of exactly `bytes` bytes.
		let pool = unsafe { FrameSlotPool::create(region.data(), slots, slot_bytes) };
		Ok(Arc::new(ShmRegionView { region, pool }))
	}

	/// The segment key.
	pub fn key(&self) -> &str {
		self.region.key()
	}

	/// Slot count.
	pub fn slot_count(&self) -> u32 {
		self.pool.slot_count()
	}

	/// Per-slot data capacity.
	pub fn slot_data_bytes(&self) -> usize {
		self.pool.slot_data_bytes()
	}

	/// Zero-copy read of a slot's pixel block (borrowed from the live
	/// mapping; valid until this view drops).
	pub fn slot_bytes(&self, slot: u32) -> &[u8] {
		let len = self.pool.slot_data_bytes();
		// SAFETY: `slot` is in range for the pool's lifetime and the
		// mapping outlives &self.
		unsafe { std::slice::from_raw_parts(self.pool.slot_data_const(slot), len) }
	}

	/// Copy a slot's pixel block into a heap buffer (the one counted
	/// copy path — long-term caches that must outlive the slot).
	pub fn slot_to_vec(&self, slot: u32) -> Vec<u8> {
		MAIN_FRAME_COPIES.fetch_add(1, Ordering::Relaxed);
		self.slot_bytes(slot).to_vec()
	}

	/// The slot's metadata, copied out of shm.
	pub fn meta_copy(&self, slot: u32) -> ShmFrameMeta {
		// SAFETY: `slot` is in range; the meta POD is fully initialized
		// by the pool create/attach.
		let pod = unsafe { &*self.pool.meta_const(slot) };
		ShmFrameMeta::from_pod(pod)
	}

	/// The pool view (dispatcher ring operations).
	pub(crate) fn pool(&self) -> &FrameSlotPool {
		&self.pool
	}
}

/// Zero-copy handle to a rendered frame in a worker segment: what a
/// video ticket completion carries on the process backend. No frame
/// bytes travel inside — the consumer reads them from the mapping with
/// [`ShmRegionView::slot_bytes`] and releases the slot through
/// [`ProcessDispatcher::release_frame`] when done (slot release =
/// cache eviction, design §3.1).
#[derive(Clone)]
pub struct ShmFrameRef {
	/// Worker index owning the segment.
	pub worker: u32,
	/// Slot index in that segment.
	pub slot: u32,
	/// Frame metadata (copied at delivery).
	pub meta: ShmFrameMeta,
	/// The segment view (keeps the mapping alive).
	pub shm: Arc<ShmRegionView>,
}

impl std::fmt::Debug for ShmFrameRef {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		f.debug_struct("ShmFrameRef")
			.field("worker", &self.worker)
			.field("slot", &self.slot)
			.field("meta", &self.meta)
			.finish()
	}
}

/// Zero-copy handle to rendered audio in a worker segment (M15 S3): what
/// an audio ticket completion carries on the process backend. The samples
/// live in the shm slot as little-endian interleaved f32 (wire format
/// [`crate::ipc::SLOT_FORMAT_AUDIO_F32`]); the consumer reads them with
/// [`ShmAudioRef::samples`] and releases the slot through
/// [`ProcessDispatcher::release_audio_frame`] when done. `sample_rate` /
/// `channel_layout` are carried from the ticket params (they are not part
/// of the slot meta POD).
#[derive(Clone)]
pub struct ShmAudioRef {
	/// Worker index owning the segment.
	pub worker: u32,
	/// Slot index in that segment.
	pub slot: u32,
	/// Slot metadata (format = `SLOT_FORMAT_AUDIO_F32`).
	pub meta: ShmFrameMeta,
	/// The segment view (keeps the mapping alive).
	pub shm: Arc<ShmRegionView>,
	/// Output sample rate (Hz; from the ticket params).
	pub sample_rate: i32,
	/// Output channel layout mask (from the ticket params).
	pub channel_layout: u64,
	/// Channel count (also in the slot meta).
	pub channel_count: i32,
}

impl ShmAudioRef {
	/// View the same slot as a generic [`ShmFrameRef`] (slot release paths
	/// that are shared with video frames).
	pub fn frame_ref(&self) -> ShmFrameRef {
		ShmFrameRef {
			worker: self.worker,
			slot: self.slot,
			meta: self.meta.clone(),
			shm: self.shm.clone(),
		}
	}

	/// Copy the interleaved f32 samples out of the slot (the counted
	/// copy path — audio bytes must outlive the slot to reach the output
	/// device / encoder).
	pub fn samples(&self) -> Vec<f32> {
		let bytes = self.shm.slot_bytes(self.slot);
		let valid = bytes
			.get(..self.meta.data_size.max(0) as usize)
			.unwrap_or(&[]);
		valid
			.chunks_exact(4)
			.map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]]))
			.collect()
	}

	/// The decoded [`AudioSamples`] (sample rate / layout from the ticket
	/// params, not the slot).
	pub fn to_audio_samples(&self) -> AudioSamples {
		AudioSamples {
			samples: self.samples(),
			sample_rate: self.sample_rate,
			channel_layout: self.channel_layout,
			channel_count: self.channel_count,
		}
	}
}

impl std::fmt::Debug for ShmAudioRef {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		f.debug_struct("ShmAudioRef")
			.field("worker", &self.worker)
			.field("slot", &self.slot)
			.field("sample_rate", &self.sample_rate)
			.field("channel_count", &self.channel_count)
			.finish()
	}
}

// ---------------------------------------------------------------------------
// Slot pixel conversions (M15 S2)
// ---------------------------------------------------------------------------
//
// The process backend writes BGRA8 into slots (the viewer preview format).
// Long-lived consumers that need the bytes in a different order/format
// convert once after copying out of the slot.

/// Convert a BGRA8 block into an RGBA8 block (swapping R and B). Used by
/// PNG writers (footage thumbnails) and PPM/CLI output, which require
/// RGB-order buffers. `src.len()` must be a multiple of 4.
pub fn bgra8_to_rgba8(src: &[u8]) -> Vec<u8> {
	let mut out = Vec::with_capacity(src.len());
	for px in src.chunks_exact(4) {
		out.push(px[2]); // R
		out.push(px[1]); // G
		out.push(px[0]); // B
		out.push(px[3]); // A
	}
	out
}

/// Convert a BGRA8 block into tightly-packed F32 RGBA samples (`0..=1`).
/// Used by the export/encoder path, which declares F32 input: the worker
/// converts its F32 pipeline output to BGRA8 for the slot (design §3.1),
/// and the export converts back — a necessary conversion at the encoder
/// boundary with 8-bit quantization (S2; per-ticket slot formats are S3
/// work).
pub fn bgra8_to_f32_rgba(src: &[u8]) -> Vec<f32> {
	let mut out = Vec::with_capacity(src.len() / 4 * 4);
	for px in src.chunks_exact(4) {
		out.push(f32::from(px[2]) / 255.0); // R
		out.push(f32::from(px[1]) / 255.0); // G
		out.push(f32::from(px[0]) / 255.0); // B
		out.push(f32::from(px[3]) / 255.0); // A
	}
	out
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

/// Bytes one slot needs for `width` x `height` at a wire format
/// ([`oakcore_rs::PixelFormat`] int or [`SLOT_FORMAT_BGRA8`]).
pub fn slot_bytes_for(width: i32, height: i32, format: i32) -> usize {
	let pixels = (width.max(0) as usize).saturating_mul(height.max(0) as usize);
	let bytes_per_pixel = if format == SLOT_FORMAT_BGRA8 {
		4
	} else {
		let fmt = match format {
			0 => oakcore_rs::PixelFormat::U8,
			1 => oakcore_rs::PixelFormat::U10,
			2 => oakcore_rs::PixelFormat::U16,
			3 => oakcore_rs::PixelFormat::F16,
			4 => oakcore_rs::PixelFormat::F32,
			_ => oakcore_rs::PixelFormat::F32,
		};
		fmt.bytes_per_channel() * 4
	};
	pixels.saturating_mul(bytes_per_pixel)
}

/// The worker-count policy (design doc S1 item 3):
/// `max(1, min(logical_cores - 2, memory_budget / per_worker_slots))`,
/// with a memory budget of one quarter of physical RAM.
pub fn default_worker_count(slots_per_worker: u32, slot_bytes: usize) -> usize {
	let cores = std::thread::available_parallelism()
		.map(|n| n.get())
		.unwrap_or(4);
	let by_cores = cores.saturating_sub(2).max(1);
	let mem = physical_memory_bytes().unwrap_or(8u64 << 30);
	let budget = mem / 4;
	let per_worker = (slots_per_worker as usize).saturating_mul(slot_bytes).max(1);
	let by_mem = (budget as usize / per_worker).max(1);
	by_cores.min(by_mem).max(1)
}

/// Slot-count policy when a segment grows (M15 S3 grow-on-demand): cap
/// the per-worker segment memory at `GROWN_SEGMENT_BUDGET`, never drop
/// below 2 slots (enough to keep a worker flowing), never exceed the
/// current count.
pub fn default_slots_for_bytes(slot_bytes: usize, current_slots: u32) -> u32 {
	/// Per-worker segment budget for a grown segment (256 MiB).
	const GROWN_SEGMENT_BUDGET: usize = 256 * 1024 * 1024;
	let by_mem = (GROWN_SEGMENT_BUDGET / slot_bytes.max(1)).max(2) as u32;
	by_mem.min(current_slots).max(2)
}

/// Default slots per worker segment (M15 S3 adaptive policy): the
/// segment is sized so per-worker shared memory stays within
/// `DEFAULT_SEGMENT_BUDGET` (128 MiB), bounded to `[2, 8]`. Small preview
/// frames (BGRA8 1080p ≈ 8.3 MB) get the full 8 slots (~66 MB); F32 1080p
/// (≈ 33 MB) drops to 3; F32 4K (≈ 132 MB) to 2. The worker-count policy
/// then bounds the whole pool against RAM/4.
pub fn default_slots_per_worker(slot_bytes: usize) -> u32 {
	const DEFAULT_SEGMENT_BUDGET: usize = 128 * 1024 * 1024;
	const MIN_SLOTS: u32 = 2;
	const MAX_SLOTS: u32 = 8;
	((DEFAULT_SEGMENT_BUDGET / slot_bytes.max(1)).max(MIN_SLOTS as usize) as u32)
		.clamp(MIN_SLOTS, MAX_SLOTS)
}

/// Default batch size B (M15 S3 adaptive policy): the design figure
/// `120 / workers` (a full playback pre-render window split across the
/// pool), capped at the per-worker slot count — credit caps a batch at
/// the free slots anyway, so a B larger than the slots just wastes a
/// claim round trip.
pub fn default_batch_size(workers: usize, slots: u32) -> usize {
	let design = (120 / workers.max(1)).max(1);
	design.min(slots.max(1) as usize)
}

/// Physical memory in bytes (macOS `hw.memsize`, Linux `sysconf`,
/// Windows `GlobalMemoryStatusEx`).
fn physical_memory_bytes() -> Option<u64> {
	#[cfg(target_os = "macos")]
	{
		let mut size: u64 = 0;
		let mut len = std::mem::size_of::<u64>();
		let name = b"hw.memsize\0";
		let rc = unsafe {
			libc::sysctlbyname(
				name.as_ptr() as *const libc::c_char,
				&mut size as *mut u64 as *mut libc::c_void,
				&mut len,
				std::ptr::null_mut(),
				0,
			)
		};
		if rc == 0 {
			Some(size)
		} else {
			None
		}
	}
	#[cfg(target_os = "linux")]
	{
		unsafe {
			let pages = libc::sysconf(libc::_SC_PHYS_PAGES);
			let page = libc::sysconf(libc::_SC_PAGESIZE);
			if pages > 0 && page > 0 {
				Some(pages as u64 * page as u64)
			} else {
				None
			}
		}
	}
	#[cfg(target_os = "windows")]
	{
		// GlobalMemoryStatusEx (kernel32): ullTotalPhys.
		#[repr(C)]
		struct MemoryStatusEx {
			length: u32,
			memory_load: u32,
			total_phys: u64,
			avail_phys: u64,
			total_page_file: u64,
			avail_page_file: u64,
			total_virtual: u64,
			avail_virtual: u64,
			avail_extended_virtual: u64,
		}
		#[link(name = "kernel32")]
		unsafe extern "system" {
			fn GlobalMemoryStatusEx(status: *mut MemoryStatusEx) -> i32;
		}
		let mut status = MemoryStatusEx {
			length: std::mem::size_of::<MemoryStatusEx>() as u32,
			memory_load: 0,
			total_phys: 0,
			avail_phys: 0,
			total_page_file: 0,
			avail_page_file: 0,
			total_virtual: 0,
			avail_virtual: 0,
			avail_extended_virtual: 0,
		};
		let ok = unsafe { GlobalMemoryStatusEx(&mut status) };
		if ok != 0 && status.total_phys > 0 {
			Some(status.total_phys)
		} else {
			None
		}
	}
	#[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
	{
		None
	}
}

/// Dispatcher configuration.
#[derive(Clone, Debug)]
pub struct DispatcherConfig {
	/// Path to the oak-worker binary. `None` = `$OAK_WORKER_BIN`, else
	/// `oak-worker` next to the current executable.
	pub worker_bin: Option<PathBuf>,
	/// Worker process count. `0` = the [`default_worker_count`] policy.
	pub workers: usize,
	/// Output slots per worker segment. `0` = the
	/// [`default_slots_per_worker`] policy (adaptive to the frame size).
	pub slots_per_worker: u32,
	/// Frame width of the segment geometry. `0` = 1920.
	pub width: i32,
	/// Frame height of the segment geometry. `0` = 1080.
	pub height: i32,
	/// Slot wire format: an `oakcore_rs::PixelFormat` int or
	/// [`SLOT_FORMAT_BGRA8`]. Default BGRA8 (the viewer preview path).
	pub slot_format: i32,
	/// Batch size `B`. `0` = the [`default_batch_size`] policy (adaptive
	/// to workers and slots).
	pub batch_size: usize,
	/// Graph snapshot path sent to every worker via `load_graph` after
	/// the handshake (`None` = no graph).
	pub graph_snapshot: Option<String>,
	/// Handshake timeout per (re)spawn.
	pub handshake_timeout_ms: u64,
}

impl Default for DispatcherConfig {
	fn default() -> Self {
		Self {
			worker_bin: None,
			workers: 0,
			slots_per_worker: 0,
			width: 0,
			height: 0,
			slot_format: SLOT_FORMAT_BGRA8,
			batch_size: 0,
			graph_snapshot: None,
			handshake_timeout_ms: 10_000,
		}
	}
}

impl DispatcherConfig {
	fn normalize(&self) -> DispatcherConfig {
		// Only the geometry defaults are resolved here; the adaptive
		// policies (workers / slots / batch size) are resolved in
		// `ProcessDispatcher::new` where slot_bytes is known.
		let mut c = self.clone();
		c.width = if c.width == 0 { 1920 } else { c.width };
		c.height = if c.height == 0 { 1080 } else { c.height };
		c
	}
}

// ---------------------------------------------------------------------------
// WorkerHandle
// ---------------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum WorkerState {
	/// Spawned, handshake in flight.
	Starting,
	/// Handshaken, accepting batches.
	Alive,
	/// Exited / EOF detected; a restart is pending.
	Dead,
	/// Restart budget exhausted; tickets fail permanently.
	PermanentlyDead,
}

enum WorkerEvent {
	Line {
		worker: usize,
		generation: u64,
		line: String,
	},
	Eof {
		worker: usize,
		generation: u64,
	},
}

struct WorkerHandle {
	index: usize,
	/// Spawn generation (increments on every restart): reader-thread
	/// events carry the generation of the child they read from, so a
	/// late EOF from a dead child cannot kill its replacement.
	generation: u64,
	state: WorkerState,
	child: Option<Child>,
	stdin: Option<std::process::ChildStdin>,
	shm: Arc<ShmRegionView>,
	/// Current per-slot data capacity (grows on demand, M15 S3: a ticket
	/// requesting F32 or a larger frame rebuilds the segment first).
	slot_bytes: usize,
	/// FIFO mirror of the shm free ring's contents (the credit).
	free_slots: VecDeque<u32>,
	/// Dispatched ticket -> assigned slot (awaiting frame_ready).
	outstanding: HashMap<i64, u32>,
	/// Slots delivered to consumers, awaiting release_frame.
	held: HashSet<u32>,
	startup_seen: bool,
	graph_sent: bool,
	caps: Option<HelloCapsMsg>,
	restarts: u32,
	spawned_at: Instant,
	accepted_batches: u64,
	/// True between a segment grow (M15 S3) and the worker's hello_caps
	/// re-attach: the dispatcher must not send new batches while the worker
	/// is still attached to the old pool.
	reconfiguring: bool,
}

impl WorkerHandle {
	fn shell(
		index: usize,
		generation: u64,
		shm: Arc<ShmRegionView>,
		slots: u32,
		slot_bytes: usize,
	) -> WorkerHandle {
		WorkerHandle {
			index,
			generation,
			state: WorkerState::Starting,
			child: None,
			stdin: None,
			shm,
			slot_bytes,
			free_slots: (0..slots).collect(),
			outstanding: HashMap::new(),
			held: HashSet::new(),
			startup_seen: false,
			graph_sent: false,
			caps: None,
			restarts: 0,
			spawned_at: Instant::now(),
			accepted_batches: 0,
			reconfiguring: false,
		}
	}
}

// ---------------------------------------------------------------------------
// ProcessDispatcher
// ---------------------------------------------------------------------------

struct PendingTicket {
	key: FrameKey,
	params: Arc<VideoTicketParams>,
	/// Audio ticket params when this ticket is an audio range pull (M15
	/// S3); `None` for video tickets.
	audio: Option<Arc<AudioTicketParams>>,
	done: Option<Completion>,
}

struct Inner {
	config: DispatcherConfig,
	bin: PathBuf,
	slots: u32,
	slot_bytes: usize,
	workers: Vec<WorkerHandle>,
	scheduler: PreviewScheduler<i64>,
	tickets: HashMap<i64, PendingTicket>,
	next_ticket: i64,
	events_rx: mpsc::Receiver<WorkerEvent>,
	events_tx: mpsc::Sender<WorkerEvent>,
	/// Segment rebuild generation (M15 S3 grow-on-demand geometry): bumped
	/// on every per-worker segment resize so re-created segments never
	/// reuse the name of a live mapping.
	seg_generation: u64,
	started: bool,
	shutting_down: bool,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}

/// The process-isolated dispatcher (design doc §2 ProcessDispatcher).
/// Cloning shares one dispatcher; the control plane is a single mutex
/// pumped by [`ProcessDispatcher::poll`] — frame bytes never touch it.
pub struct ProcessDispatcher {
	inner: Mutex<Inner>,
}

impl ProcessDispatcher {
	/// Build a dispatcher from `config` (does not spawn; call
	/// [`ProcessDispatcher::start`]). The adaptive policies resolve here
	/// (M15 S3): slots scale with the frame's slot bytes, workers with
	/// cores/RAM, batch size with workers and slots.
	pub fn new(config: DispatcherConfig) -> Result<Arc<ProcessDispatcher>> {
		let config = config.normalize();
		let slot_bytes = slot_bytes_for(config.width, config.height, config.slot_format);
		let slots = if config.slots_per_worker == 0 {
			default_slots_per_worker(slot_bytes)
		} else {
			config.slots_per_worker
		};
		let workers = if config.workers == 0 {
			default_worker_count(slots, slot_bytes)
		} else {
			config.workers
		};
		let batch_size = if config.batch_size == 0 {
			default_batch_size(workers, slots)
		} else {
			config.batch_size
		};
		let bin = resolve_worker_bin(&config)?;
		let (events_tx, events_rx) = mpsc::channel();
		let dispatcher = Arc::new(ProcessDispatcher {
			inner: Mutex::new(Inner {
				config,
				bin,
				slots,
				slot_bytes,
				workers: Vec::new(),
				scheduler: PreviewScheduler::new(workers, batch_size),
				tickets: HashMap::new(),
				next_ticket: 1,
				events_rx,
				events_tx,
				seg_generation: 0,
				started: false,
				shutting_down: false,
			}),
		});
		// Register the weak handle for the plugin-cancel broadcast.
		*dispatcher_slot()
			.lock()
			.unwrap_or_else(|e| e.into_inner()) = Arc::downgrade(&dispatcher);
		Ok(dispatcher)
	}

	/// Spawn all workers and wait for the handshakes (bounded by
	/// `handshake_timeout_ms`).
	pub fn start(&self) -> Result<()> {
		let timeout = {
			let mut inner = lock(&self.inner);
			if inner.started {
				return Err(Error::State);
			}
			inner.started = true;
			let count = inner.scheduler.workers();
			for i in 0..count {
				self.spawn_worker(&mut inner, i)?;
			}
			Duration::from_millis(inner.config.handshake_timeout_ms)
		};
		let deadline = Instant::now() + timeout;
		loop {
			self.poll();
			{
				let inner = lock(&self.inner);
				if inner
					.workers
					.iter()
					.all(|w| matches!(w.state, WorkerState::Alive))
				{
					return Ok(());
				}
				if inner
					.workers
					.iter()
					.any(|w| matches!(w.state, WorkerState::PermanentlyDead))
				{
					return Err(Error::Failed("worker failed to start permanently".into()));
				}
			}
			if Instant::now() > deadline {
				return Err(Error::Failed(
					"render workers did not finish the startup handshake in time".into(),
				));
			}
			std::thread::sleep(Duration::from_millis(2));
		}
	}

	/// Slot headroom for best-effort pre-render windows: the pool's total
	/// slots minus one per worker, so interactive (seek / synchronous
	/// display) and audio tickets always keep credit to dispatch. A
	/// pre-render window larger than the pool exhausted every slot, which
	/// deadlocked the UI's synchronous frame wait (the playback freeze).
	pub fn preview_window_capacity(&self) -> usize {
		let inner = lock(&self.inner);
		let workers = inner.scheduler.workers();
		workers
			.saturating_mul(inner.slots as usize)
			.saturating_sub(workers)
			.max(1)
	}

	/// The configured worker count.
	pub fn worker_count(&self) -> usize {
		lock(&self.inner).scheduler.workers()
	}

	/// True when worker `i` is alive (handshaken).
	pub fn is_alive(&self, worker: usize) -> bool {
		lock(&self.inner)
			.workers
			.get(worker)
			.map(|w| matches!(w.state, WorkerState::Alive))
			.unwrap_or(false)
	}

	/// Restart count of worker `i` (crash-isolation metric).
	pub fn restarts_of(&self, worker: usize) -> u32 {
		lock(&self.inner)
			.workers
			.get(worker)
			.map(|w| w.restarts)
			.unwrap_or(0)
	}

	/// Batches accepted by worker `i` (claim-confirmation metric).
	pub fn accepted_batches_of(&self, worker: usize) -> u64 {
		lock(&self.inner)
			.workers
			.get(worker)
			.map(|w| w.accepted_batches)
			.unwrap_or(0)
	}

	/// The segment view of worker `i` (tests / S2 cache integration).
	pub fn shm_of(&self, worker: usize) -> Option<Arc<ShmRegionView>> {
		lock(&self.inner).workers.get(worker).map(|w| w.shm.clone())
	}

	/// Pump the control plane: drain worker events, restart the dead,
	/// claim + dispatch batches. Non-blocking; call from the UI tick (or
	/// after any submit/release). Completions fire after the lock drops.
	pub fn poll(&self) {
		let mut fired: Vec<(Completion, TicketResult)> = Vec::new();
		{
			let mut inner = lock(&self.inner);
			self.pump(&mut inner, &mut fired);
		}
		for (done, result) in fired {
			done(result);
		}
	}

	/// Release a consumed frame back to its worker's free pool (slot
	/// release = cache eviction). Stale refs (worker restarted since)
	/// are ignored — their segment is already gone.
	pub fn release_frame(&self, frame: &ShmFrameRef) {
		let mut inner = lock(&self.inner);
		let Some(handle) = inner.workers.get_mut(frame.worker as usize) else {
			return;
		};
		if !Arc::ptr_eq(&handle.shm, &frame.shm) {
			return; // stale ref: the segment was recreated
		}
		if !handle.held.remove(&frame.slot) {
			return; // double release
		}
		handle.free_slots.push_back(frame.slot);
		// SAFETY: the pool is a live view of the worker's segment; the
		// dispatcher is the drainer, so pushing to the free ring is its
		// SPSC role.
		unsafe { handle.shm.pool().release(frame.slot) };
	}

	/// Release a consumed audio frame's slot (M15 S3) — the audio
	/// counterpart of [`ProcessDispatcher::release_frame`].
	pub fn release_audio_frame(&self, frame: &ShmAudioRef) {
		self.release_frame(&frame.frame_ref());
	}

	/// Cancel one frame request (pending or in flight). The completion
	/// fires with `Error::State` exactly once; a late frame_ready for an
	/// in-flight cancel recycles the slot silently.
	pub fn cancel_frame(&self, key: &FrameKey) {
		let mut fired: Vec<(Completion, TicketResult)> = Vec::new();
		{
			let mut inner = lock(&self.inner);
			if !inner.scheduler.cancel_key(key) {
				return;
			}
			// Find the ticket behind the key and deliver the cancellation.
			let ticket = inner
				.tickets
				.iter()
				.find(|(_, pt)| &pt.key == key)
				.map(|(id, _)| *id);
			if let Some(id) = ticket {
				if let Some(pt) = inner.tickets.get_mut(&id) {
					if let Some(done) = pt.done.take() {
						fired.push((done, Err(Error::State)));
					}
				}
			}
		}
		for (done, result) in fired {
			done(result);
		}
	}

	/// Broadcast the plugin-cancel signal to every alive worker (the user
	/// cancelled the plugin render from the progress dialog). The message
	/// is a fire-and-forget control line; the worker sets its sticky cancel
	/// flag and the next reporter update answers false. A send failure
	/// recycles that worker (it will restart on the next pump).
	pub fn broadcast_plugin_cancel(&self) {
		let mut inner = lock(&self.inner);
		for handle in inner.workers.iter_mut() {
			if matches!(handle.state, WorkerState::Alive | WorkerState::Starting) {
				if self.send_json(handle, &plugin_cancel_json()).is_err() {
					handle.state = WorkerState::Dead;
				}
			}
		}
	}

	// ---- internals ------------------------------------------------------

	fn pump(&self, inner: &mut Inner, fired: &mut Vec<(Completion, TicketResult)>) {
		// 1. Drain worker events (non-blocking). Events from a previous
		//    spawn generation (a dead child's reader) are dropped so a
		//    late EOF cannot kill the replacement worker.
		while let Ok(ev) = inner.events_rx.try_recv() {
			match ev {
				WorkerEvent::Line {
					worker,
					generation,
					line,
				} => {
					let current = inner
						.workers
						.get(worker)
						.map(|w| w.generation)
						.unwrap_or(u64::MAX);
					if current != generation {
						continue;
					}
					self.on_line(inner, worker, &line, fired);
				}
				WorkerEvent::Eof { worker, generation } => {
					if let Some(handle) = inner.workers.get_mut(worker) {
						if handle.generation != generation {
							continue;
						}
						if !matches!(handle.state, WorkerState::PermanentlyDead) {
							handle.state = WorkerState::Dead;
						}
					}
				}
			}
		}

		// 2. Restart dead workers / handshake timeouts.
		let timeout = Duration::from_millis(inner.config.handshake_timeout_ms);
		for i in 0..inner.workers.len() {
			let action = {
				let handle = &inner.workers[i];
				match handle.state {
					WorkerState::Dead => true,
					WorkerState::Starting => handle.spawned_at.elapsed() > timeout,
					_ => false,
				}
			};
			if action {
				self.restart_worker(inner, i, fired);
			}
		}

		// 3. Interleaved batch claims + dispatch (free slots = credit).
		for i in 0..inner.workers.len() {
			if matches!(inner.workers[i].state, WorkerState::Alive)
				&& !inner.workers[i].reconfiguring
			{
				self.dispatch_to(inner, i);
			}
		}
	}

	fn on_line(
		&self,
		inner: &mut Inner,
		worker: usize,
		line: &str,
		fired: &mut Vec<(Completion, TicketResult)>,
	) {
		let msg: Value = match serde_json::from_str::<Value>(line) {
			Ok(v) if v.is_object() => v,
			_ => return,
		};
		let typ = msg.get("type").and_then(Value::as_str).unwrap_or("");
		let handle = match inner.workers.get_mut(worker) {
			Some(h) => h,
			None => return,
		};
		match typ {
			TYPE_HANDSHAKE => {
				// The worker's startup handshake: answer with the shm
				// geometry (protocol v1 flow). A mid-session handshake is a
				// segment grow (M15 S3): the worker re-attaches the new pool.
				handle.startup_seen = true;
				if self.send_json(handle, &handshake_for(handle)).is_err() {
					handle.state = WorkerState::Dead;
				}
			}
			TYPE_HELLO_CAPS => {
				if let Ok(caps) = serde_json::from_value::<HelloCapsMsg>(msg) {
					handle.caps = Some(caps);
					handle.state = WorkerState::Alive;
					// A re-attach after a segment grow is complete: the
					// dispatcher may send batches again.
					handle.reconfiguring = false;
					// One load_graph right after the first handshake.
					if !handle.graph_sent {
						if let Some(path) = inner.config.graph_snapshot.clone() {
							handle.graph_sent = true;
							if self
								.send_json(handle, &json!({ "type": "load_graph", "path": path }))
								.is_err()
							{
								handle.state = WorkerState::Dead;
							}
						}
					}
				}
			}
			TYPE_BATCH_ACCEPTED => {
				if let Ok(accepted) = serde_json::from_value::<BatchAcceptedMsg>(msg) {
					let _ = accepted;
					handle.accepted_batches += 1;
				}
			}
			TYPE_FRAME_READY => {
				if let Ok(ready) = serde_json::from_value::<FrameReadyMsg>(msg) {
					self.on_frame_ready(inner, worker, ready.ticket, ready.slot, fired);
				}
			}
			TYPE_FRAME_FAILED => {
				if let Ok(failed) = serde_json::from_value::<FrameFailedMsg>(msg) {
					self.on_frame_failed(inner, worker, failed.ticket, &failed.error, fired);
				}
			}
			TYPE_ERROR => {
				let ticket = msg.get("ticket").and_then(Value::as_i64);
				let message = msg
					.get("message")
					.and_then(Value::as_str)
					.unwrap_or("(no message)")
					.to_string();
				match ticket {
					Some(t) => self.on_frame_failed(inner, worker, t, &message, fired),
					None => {
						// A session-level error (e.g. load_graph or shm
						// attach failed): recycle the worker.
						eprintln!("procpool: worker {worker} error: {message}");
						if matches!(handle.state, WorkerState::Starting) {
							handle.state = WorkerState::Dead;
						}
					}
				}
			}
			TYPE_PLUGIN_PROGRESS => {
				// A worker forwarded an OFX plugin progress event; hand it
				// to the app's registered callback (which drives the
				// plugin-progress dialog).
				if let Ok(progress) = serde_json::from_value::<PluginProgressMsg>(msg) {
					if let Some(cb) = plugin_progress_cb() {
						cb(progress.label, progress.message, progress.fraction);
					}
				}
			}
			_ => {}
		}
	}

	fn on_frame_ready(
		&self,
		inner: &mut Inner,
		worker: usize,
		ticket: i64,
		slot: i32,
		fired: &mut Vec<(Completion, TicketResult)>,
	) {
		let handle = match inner.workers.get_mut(worker) {
			Some(h) => h,
			None => return,
		};
		if handle.outstanding.remove(&ticket).is_none() {
			return; // late / duplicate / post-restart frame
		}
		// Drain the ready ring in lockstep (the SPSC hand-off contract);
		// frame_ready is authoritative about the slot.
		let mut ring_slot = 0;
		// SAFETY: live pool view; the dispatcher is the ready-ring
		// consumer.
		let popped = unsafe { handle.shm.pool().consume(&mut ring_slot) };
		if !popped || ring_slot != slot as u32 {
			eprintln!(
				"procpool: worker {worker} ready-ring out of sync (popped {popped}, ring {ring_slot}, msg {slot})"
			);
		}
		let meta = handle.shm.meta_copy(slot as u32);
		let shm = handle.shm.clone();
		handle.held.insert(slot as u32);

		let pt = inner.tickets.get_mut(&ticket);
		match pt {
			Some(pt) => {
				let key = pt.key;
				inner.scheduler.frame_done(&key);
				if let Some(done) = pt.done.take() {
					// M15 S3: audio tickets complete with the shm audio
					// payload (the consumer reads the slot and releases it);
					// video tickets keep the ShmFrame payload.
					if let Some(audio) = &pt.audio {
						let params = audio.clone();
						fired.push((
							done,
							Ok(TicketPayload::ShmAudio(ShmAudioRef {
								worker: worker as u32,
								slot: slot as u32,
								meta,
								shm,
								sample_rate: params.sample_rate,
								channel_layout: params.channel_layout,
								channel_count: params.channel_layout.count_ones().max(1) as i32,
							})),
						));
					} else {
						fired.push((
							done,
							Ok(TicketPayload::ShmFrame(ShmFrameRef {
								worker: worker as u32,
								slot: slot as u32,
								meta,
								shm,
							})),
						));
					}
				} else {
					// Cancelled while in flight: recycle the slot now.
					self.recycle_slot(inner, worker, slot as u32);
				}
			}
			None => {
				self.recycle_slot(inner, worker, slot as u32);
			}
		}
	}

	fn on_frame_failed(
		&self,
		inner: &mut Inner,
		worker: usize,
		ticket: i64,
		error: &str,
		fired: &mut Vec<(Completion, TicketResult)>,
	) {
		let slot = {
			let handle = match inner.workers.get_mut(worker) {
				Some(h) => h,
				None => return,
			};
			handle.outstanding.remove(&ticket)
		};
		let Some(slot) = slot else { return };
		// The worker acquired the slot but never published it: the
		// dispatcher (drainer) hands it back to the free pool.
		self.recycle_slot(inner, worker, slot);
		if let Some(pt) = inner.tickets.get_mut(&ticket) {
			inner.scheduler.frame_failed(&pt.key);
			if let Some(done) = pt.done.take() {
				fired.push((done, Err(Error::Failed(format!("render failed: {error}")))));
			}
		}
	}

	/// Return a slot to the worker's free pool (queue + ring).
	fn recycle_slot(&self, inner: &mut Inner, worker: usize, slot: u32) {
		let Some(handle) = inner.workers.get_mut(worker) else {
			return;
		};
		handle.held.remove(&slot);
		handle.free_slots.push_back(slot);
		// SAFETY: live pool view; drainer-side free-ring push.
		unsafe { handle.shm.pool().release(slot) };
	}

	fn dispatch_to(&self, inner: &mut Inner, worker: usize) {
		loop {
			let credit = inner.workers[worker].free_slots.len();
			if credit == 0 {
				return;
			}
			// Grow-on-demand (M15 S3): if a pending request for this worker
			// needs a bigger slot than the segment provides, and the worker
			// has no in-flight frames, rebuild its segment first (the worker
			// re-attaches on a fresh handshake). While the worker is busy the
			// oversized request simply stays pending — claim_batch filters it
			// by max_bytes, so it is served after the drain.
			let grow = {
				let handle = &inner.workers[worker];
				if handle.outstanding.is_empty() {
					inner
						.scheduler
						.max_pending_bytes_for_worker(worker, handle.slot_bytes)
				} else {
					None
				}
			};
			if let Some(need) = grow {
				if let Err(e) = self.rebuild_segment(inner, worker, need) {
					eprintln!("procpool: worker {worker} segment grow to {need} B failed: {e}");
				}
				// Stop here: the worker is re-attaching to the new segment
				// (hello_caps pending). Dispatch resumes on the next pump
				// once `reconfiguring` clears — sending a batch now would
				// race the pool swap.
				return;
			}
			let max_bytes = inner.workers[worker].slot_bytes;
			let Some(batch) = inner.scheduler.claim_batch(worker, credit, max_bytes) else {
				return;
			};
			// Slot assignment order MUST match the worker's acquisition
			// order: the batch is delivered as the video message first and
			// the audio message second, and the worker pops one slot per
			// ticket in that message order, checking each pop against the
			// assignment. Assigning in the scheduler's interleaved frame
			// order scrambles the free ring (every audio ticket in a mixed
			// batch mismatched, and each mismatch leaked a slot — the
			// "slot assignment mismatch" flood). Two passes: video first.
			let (video_reqs, audio_reqs): (Vec<_>, Vec<_>) =
				batch.frames.iter().partition(|r| {
					!inner
						.tickets
						.get(&r.payload)
						.is_some_and(|pt| pt.audio.is_some())
				});
			let mut video_tickets = Vec::with_capacity(video_reqs.len());
			let mut audio_tickets: Vec<AudioTicketSpec> = Vec::with_capacity(audio_reqs.len());
			for req in video_reqs.into_iter().chain(audio_reqs) {
				let ticket = req.payload;
				let Some(slot) = inner.workers[worker].free_slots.pop_front() else {
					break; // credit accounting drifted; stop cleanly
				};
				inner.workers[worker].outstanding.insert(ticket, slot);
				let Some(pt) = inner.tickets.get(&ticket) else {
					continue;
				};
				if let Some(audio) = &pt.audio {
					audio_tickets.push(build_audio_ticket_spec(ticket, slot, audio));
				} else {
					video_tickets.push(build_ticket_spec(
						ticket,
						slot,
						&pt.params,
						inner.config.slot_format,
					));
				}
			}
			// A single claim may mix audio and video (different scheduler
			// keys in one batch); they are delivered as two messages under
			// the same batch id, claimed by the worker in order.
			if !video_tickets.is_empty() {
				let msg = RenderBatchMsg {
					batch_id: batch.batch_id as i64,
					tickets: video_tickets,
				};
				// The `type` tag is added by hand: the parse-side structs only
				// carry the payload fields.
				let mut value = match serde_json::to_value(&msg) {
					Ok(v) => v,
					Err(_) => return,
				};
				if let Some(obj) = value.as_object_mut() {
					obj.insert(
						"type".to_string(),
						Value::String(crate::ipc::TYPE_RENDER_BATCH.to_string()),
					);
				}
				if self.send_json(&mut inner.workers[worker], &value).is_err() {
					inner.workers[worker].state = WorkerState::Dead;
					return;
				}
			}
			if !audio_tickets.is_empty() {
				let msg = RenderAudioBatchMsg {
					batch_id: batch.batch_id as i64,
					tickets: audio_tickets,
				};
				let mut value = match serde_json::to_value(&msg) {
					Ok(v) => v,
					Err(_) => return,
				};
				if let Some(obj) = value.as_object_mut() {
					obj.insert(
						"type".to_string(),
						Value::String(TYPE_RENDER_AUDIO_BATCH.to_string()),
					);
				}
				if self.send_json(&mut inner.workers[worker], &value).is_err() {
					inner.workers[worker].state = WorkerState::Dead;
					return;
				}
			}
		}
	}

	fn send_json(&self, handle: &mut WorkerHandle, msg: &Value) -> Result<()> {
		let stdin = handle.stdin.as_mut().ok_or(Error::State)?;
		write_message(stdin, msg).map_err(|e| Error::Failed(format!("worker stdin: {e}")))?;
		stdin
			.flush()
			.map_err(|e| Error::Failed(format!("worker stdin flush: {e}")))
	}

	fn spawn_worker(&self, inner: &mut Inner, index: usize) -> Result<()> {
		// One segment generation per (re)spawn: the key carries the restart
		// count so a restart never reuses the previous name — dropping the
		// old handle unlinks the OLD segment by name and must not remove
		// the freshly created one (SharedMemoryRegion::close unlinks by
		// name for Create-mode regions).
		let generation = inner
			.workers
			.get(index)
			.map(|w| w.restarts as u64)
			.unwrap_or(0);
		let key = format!(
			"{}-g{generation}",
			SharedMemoryRegion::make_key(std::process::id() as i64, index as i32)
		);
		let shm = ShmRegionView::create(&key, inner.slots, inner.slot_bytes)?;

		let mut child = Command::new(&inner.bin)
			.args(["--backend", "cpu"])
			.stdin(Stdio::piped())
			.stdout(Stdio::piped())
			.stderr(Stdio::inherit())
			.spawn()
			.map_err(|e| Error::Failed(format!("spawn oak-worker: {e}")))?;
		let stdin = child.stdin.take();
		let stdout = child
			.stdout
			.take()
			.ok_or_else(|| Error::Failed("oak-worker stdout not piped".into()))?;

		// Reader thread: stdout lines -> event channel (control plane).
		// Events carry the spawn generation so stale events from a dead
		// child are dropped after a restart.
		let tx = inner.events_tx.clone();
		std::thread::Builder::new()
			.name(format!("oak-worker-{index}-reader"))
			.spawn(move || {
				use std::io::BufRead;
				let mut reader = std::io::BufReader::new(stdout);
				let mut line = String::new();
				loop {
					line.clear();
					match reader.read_line(&mut line) {
						Ok(0) => {
							let _ = tx.send(WorkerEvent::Eof {
								worker: index,
								generation,
							});
							return;
						}
						Ok(_) => {
							let _ = tx.send(WorkerEvent::Line {
								worker: index,
								generation,
								line: line.trim_end().to_string(),
							});
						}
						Err(_) => {
							let _ = tx.send(WorkerEvent::Eof {
								worker: index,
								generation,
							});
							return;
						}
					}
				}
			})
			.map_err(|e| Error::Failed(format!("spawn reader thread: {e}")))?;

		let mut handle = WorkerHandle::shell(index, generation, shm, inner.slots, inner.slot_bytes);
		handle.child = Some(child);
		handle.stdin = stdin;
		handle.spawned_at = Instant::now();
		if index < inner.workers.len() {
			// Restart path: keep the restart counter.
			handle.restarts = inner.workers[index].restarts;
			inner.workers[index] = handle;
		} else {
			inner.workers.push(handle);
		}
		Ok(())
	}

	fn restart_worker(
		&self,
		inner: &mut Inner,
		worker: usize,
		fired: &mut Vec<(Completion, TicketResult)>,
	) {
		// Reap the child and drop the pipes.
		let restarts = {
			let handle = &mut inner.workers[worker];
			if let Some(mut child) = handle.child.take() {
				let _ = child.kill();
				let _ = child.wait();
			}
			handle.stdin = None;
			handle.startup_seen = false;
			handle.graph_sent = false;
			handle.caps = None;
			handle.held.clear();
			handle.outstanding.clear();
			handle.restarts += 1;
			handle.restarts
		};

		// Crash recovery (design §3.2): every claimed frame of this
		// worker — un-started batches and un-finished frames alike — is
		// re-queued; any healthy worker may claim it.
		let reclaimed = inner.scheduler.worker_crashed(worker);

		if restarts > MAX_RESTARTS {
			// Restart budget exhausted: the worker stays down and its
			// frames fail permanently (main paints the fallback).
			inner.workers[worker].state = WorkerState::PermanentlyDead;
			for req in reclaimed {
				inner.scheduler.cancel_key(&req.key);
				if let Some(pt) = inner.tickets.get_mut(&req.payload) {
					if let Some(done) = pt.done.take() {
						fired.push((
							done,
							Err(Error::Failed(
								"render worker crashed repeatedly; frame dropped".into(),
							)),
						));
					}
				}
			}
			return;
		}

		// Fresh segment + respawn (a Create unlinks any stale segment).
		if let Err(e) = self.spawn_worker(inner, worker) {
			eprintln!("procpool: worker {worker} respawn failed: {e}");
			inner.workers[worker].state = WorkerState::Dead;
		}
	}

	/// Grow a worker's segment to `need_bytes` per slot (M15 S3 grow-on-
	/// demand geometry, design §3.1 "段按需扩容或重建"): create a fresh
	/// segment under a new key (the old mapping stays alive for consumers
	/// still holding [`ShmFrameRef`]s into it — they release as stale
	/// refs), re-point the handle, reseed the free slots and have the
	/// worker re-attach through a fresh handshake. The caller guarantees
	/// `outstanding` is empty (no frame is mid-render in the old pool).
	/// The worker's hello_caps clears `reconfiguring`, unblocking dispatch.
	fn rebuild_segment(&self, inner: &mut Inner, worker: usize, need_bytes: usize) -> Result<()> {
		let current_slots = inner.workers[worker].shm.slot_count();
		let slots = default_slots_for_bytes(need_bytes, current_slots);
		let generation = inner.seg_generation;
		inner.seg_generation += 1;
		let base = SharedMemoryRegion::make_key(std::process::id() as i64, worker as i32);
		let key = format!(
			"{base}-g{}-s{generation}",
			inner.workers[worker].generation
		);
		let shm = ShmRegionView::create(&key, slots, need_bytes)?;
		{
			let handle = &mut inner.workers[worker];
			handle.shm = shm;
			handle.slot_bytes = need_bytes;
			handle.free_slots = (0..slots).collect();
			handle.held.clear();
			// No new batches until the worker re-attaches the new pool.
			handle.reconfiguring = true;
		}
		let hs = {
			let handle = &inner.workers[worker];
			handshake_for(handle)
		};
		if self.send_json(&mut inner.workers[worker], &hs).is_err() {
			inner.workers[worker].state = WorkerState::Dead;
		}
		Ok(())
	}
}

/// The handshake reply the dispatcher sends a worker (startup and M15 S3
/// segment-grow re-attach): the worker's current shm geometry.
fn handshake_for(handle: &WorkerHandle) -> Value {
	HandshakeMsg {
		protocol_version: DISPATCH_PROTOCOL_VERSION,
		shm_key: handle.shm.key().to_string(),
		input_shm_key: String::new(),
		input_slots: 0,
		output_slots: handle.shm.slot_count() as i32,
		slot_data_bytes: handle.shm.slot_data_bytes() as i64,
		input_slot_data_bytes: 0,
	}
	.to_json()
}

impl JobDispatch for ProcessDispatcher {
	/// Submit one frame job (the ticket-arena seam). The job joins the
	/// scheduler under its [`JobSchedule`] (Seek single-frame by default,
	/// Playback for the pre-render window, Background for exports) and is
	/// dispatched on the next pump; the completion fires with
	/// `TicketPayload::ShmFrame(ShmFrameRef)` — never a pixel buffer.
	/// Re-submitting a key that is still pending replaces the old request
	/// and cancels its ticket; a key already in flight is left running
	/// (its result is still valid for the same params).
	fn post(&self, job: Job) -> bool {
		let mut fired: Vec<(Completion, TicketResult)> = Vec::new();
		{
			let mut inner = lock(&self.inner);
			if inner.shutting_down {
				return false;
			}
			// M15 S3: audio ranges larger than a practical shm slot (export
			// of many minutes of audio) are refused here so the arena falls
			// back to main-process inline rendering (design §3.7) — the
			// process backend stays for the real-time chunks and short
			// ranges that fit a segment.
			if let Some(audio) = &job.audio {
				let too_large = match crate::eval::audio_samples_byte_len(audio) {
					Ok(bytes) => bytes > MAX_AUDIO_SLOT_BYTES,
					Err(_) => true, // invalid range: let the inline path report it
				};
				if too_large {
					return false;
				}
			}
			let id = inner.next_ticket;
			inner.next_ticket += 1;
			let frame = job.schedule.frame.unwrap_or(id);
			let key = FrameKey {
				sequence: job.node_identity,
				frame,
				version: job.schedule.version,
			};
			// M15 S3: per-request slot geometry. Audio tickets need the
			// sample bytes of their range; video tickets need the frame
			// size x the ticket's wire format (force_format honored).
			let slot_bytes = match &job.audio {
				Some(audio) => crate::eval::audio_samples_byte_len(audio).unwrap_or(0),
				None => {
					let (w, h) = job.params.render_size();
					slot_bytes_for(w, h, ticket_wire_format(&job.params, inner.config.slot_format))
				}
			};
			inner.tickets.insert(
				id,
				PendingTicket {
					key,
					params: job.params,
					audio: job.audio,
					done: Some(job.done),
				},
			);
			let request = FrameRequest {
				key,
				priority: job.schedule.priority,
				distance: job.schedule.distance,
				payload: id,
				slot_bytes,
			};
			match inner.scheduler.submit(request) {
				SubmitOutcome::Accepted => {}
				SubmitOutcome::Replaced(old) => {
					// A newer request for the same key superseded the old
					// pending one: cancel the old ticket's completion.
					if let Some(pt) = inner.tickets.get_mut(&old.payload) {
						if let Some(done) = pt.done.take() {
							fired.push((done, Err(Error::State)));
						}
					}
				}
				SubmitOutcome::InFlight => {
					// Already claimed by a worker; the rendered frame is
					// still valid for the same params (playback window
					// slides re-request frames that are in flight).
				}
			}
		}
		for (done, result) in fired {
			done(result);
		}
		// Pump once so a live worker picks the frame up immediately.
		self.poll();
		true
	}

	/// Cancel every pending AND claimed request of `sequence` (M15 S2
	/// preview-window invalidation — graph/proxy/resolution/color bump or
	/// a sequence switch). Dropped completions fire `Error::State`;
	/// frames already dispatched recycle their slots when the late
	/// `frame_ready` arrives.
	fn cancel_preview_sequence(&self, sequence: u64) {
		let mut fired: Vec<(Completion, TicketResult)> = Vec::new();
		{
			let mut inner = lock(&self.inner);
			let dropped = inner.scheduler.cancel_sequence(sequence);
			for request in dropped {
				if let Some(pt) = inner.tickets.get_mut(&request.payload) {
					if let Some(done) = pt.done.take() {
						fired.push((done, Err(Error::State)));
					}
				}
			}
		}
		for (done, result) in fired {
			done(result);
		}
	}

	/// Pump the control plane (delegates to the inherent poll — the UI
	/// tick and blocking ticket waits call this through the trait seam).
	fn poll(&self) {
		self.poll();
	}

	/// The pre-render window's slot headroom (see the inherent
	/// [`ProcessDispatcher::preview_window_capacity`]).
	fn preview_window_capacity(&self) -> Option<usize> {
		Some(self.preview_window_capacity())
	}

	/// Cancel one pre-render window frame (delegates to the inherent
	/// [`ProcessDispatcher::cancel_frame`]).
	fn cancel_preview_frame(&self, sequence: u64, frame: i64, version: u64) {
		self.cancel_frame(&FrameKey {
			sequence,
			frame,
			version,
		});
	}

	/// Release a consumed frame's slot (delegates to the inherent
	/// release — see [`ProcessDispatcher::release_frame`]).
	fn release_frame(&self, frame: &ShmFrameRef) {
		self.release_frame(frame);
	}

	/// Release a consumed audio frame's slot (M15 S3; delegates to the
	/// inherent release).
	fn release_audio_frame(&self, frame: &ShmAudioRef) {
		self.release_audio_frame(frame);
	}

	/// Graceful shutdown: `shutdown` messages, a short drain pumping
	/// completions, then kill stragglers; every ticket still open
	/// completes with `Error::State`.
	fn shutdown(&self) {
		let mut fired: Vec<(Completion, TicketResult)> = Vec::new();
		{
			let mut inner = lock(&self.inner);
			if inner.shutting_down {
				return;
			}
			inner.shutting_down = true;
			for i in 0..inner.workers.len() {
				let handle = &mut inner.workers[i];
				if matches!(handle.state, WorkerState::Alive | WorkerState::Starting) {
					let _ = self.send_json(handle, &json!({ "type": "shutdown" }));
				}
			}
		}
		// Drain window: let workers finish in-flight frames and deliver
		// the completions.
		let deadline = Instant::now() + Duration::from_secs(3);
		loop {
			{
				let mut inner = lock(&self.inner);
				self.pump(&mut inner, &mut fired);
				let any_running = inner.workers.iter_mut().any(|w| {
					w.child
						.as_mut()
						.map(|c| c.try_wait().ok().flatten().is_none())
						.unwrap_or(false)
				});
				if !any_running {
					break;
				}
			}
			if Instant::now() > deadline {
				break;
			}
			std::thread::sleep(Duration::from_millis(2));
			// Deliver what pumped so far before the next round.
			for (done, result) in fired.drain(..) {
				done(result);
			}
		}
		{
			let mut inner = lock(&self.inner);
			// Kill stragglers and reap.
			for w in inner.workers.iter_mut() {
				if let Some(mut child) = w.child.take() {
					let _ = child.kill();
					let _ = child.wait();
				}
				w.stdin = None;
			}
			// Every ticket still open completes with cancellation.
			for (_, pt) in inner.tickets.iter_mut() {
				if let Some(done) = pt.done.take() {
					fired.push((done, Err(Error::State)));
				}
			}
		}
		for (done, result) in fired {
			done(result);
		}
	}
}

/// Resolve the oak-worker binary path.
fn resolve_worker_bin(config: &DispatcherConfig) -> Result<PathBuf> {
	if let Some(p) = &config.worker_bin {
		return Ok(p.clone());
	}
	if let Ok(p) = std::env::var("OAK_WORKER_BIN") {
		return Ok(PathBuf::from(p));
	}
	let exe = std::env::current_exe()
		.map_err(|e| Error::Failed(format!("resolve oak-worker: current exe: {e}")))?;
	let candidate = exe
		.parent()
		.ok_or_else(|| Error::Failed("resolve oak-worker: no exe parent".into()))?
		.join(format!("oak-worker{}", std::env::consts::EXE_SUFFIX));
	if candidate.exists() {
		return Ok(candidate);
	}
	Err(Error::Failed(format!(
		"oak-worker binary not found at {}; set DispatcherConfig::worker_bin or OAK_WORKER_BIN",
		candidate.display()
	)))
}

/// The wire slot format a video ticket requests: the ticket's forced
/// PixelFormat when set (F32 for exports / full-resolution / scopes, M15
/// S3 — the worker then writes F32 straight into the slot and the export
/// reads it back with no BGRA8 round trip), else the dispatcher's default
/// slot format (BGRA8 for the viewer preview path).
fn ticket_wire_format(params: &VideoTicketParams, config_format: i32) -> i32 {
	params.force_format.map(|f| f as i32).unwrap_or(config_format)
}

/// Map ticket params to the wire ticket spec (main assigns `slot`).
fn build_ticket_spec(
	ticket: i64,
	slot: u32,
	params: &VideoTicketParams,
	slot_format: i32,
) -> BatchTicketSpec {
	let (width, height) = params.render_size();
	let (footage_file, footage_stream) = match &params.footage {
		Some((f, s)) => (f.clone(), *s),
		None => (String::new(), 0),
	};
	let montage = params
		.montage
		.iter()
		.map(|c| WireMontageClip {
			filename: c.filename.clone(),
			stream_index: c.stream_index,
			in_num: c.in_time.numerator(),
			in_den: c.in_time.denominator(),
			out_num: c.out_time.numerator(),
			out_den: c.out_time.denominator(),
			media_in_num: c.media_in.numerator(),
			media_in_den: c.media_in.denominator(),
			gain: c.gain,
		})
		.collect();
	BatchTicketSpec {
		ticket,
		slot: slot as i32,
		time_num: params.time.numerator(),
		time_den: params.time.denominator(),
		width,
		height,
		format: ticket_wire_format(params, slot_format),
		channels: 4,
		footage_file,
		footage_stream,
		montage,
	}
}

/// Map audio ticket params to the wire audio ticket spec (M15 S3; main
/// assigns `slot`).
fn build_audio_ticket_spec(ticket: i64, slot: u32, params: &AudioTicketParams) -> AudioTicketSpec {
	let duration = params.range.out() - params.range.in_();
	let montage = params
		.montage
		.iter()
		.map(|c| WireMontageClip {
			filename: c.filename.clone(),
			stream_index: c.stream_index,
			in_num: c.in_time.numerator(),
			in_den: c.in_time.denominator(),
			out_num: c.out_time.numerator(),
			out_den: c.out_time.denominator(),
			media_in_num: c.media_in.numerator(),
			media_in_den: c.media_in.denominator(),
			gain: c.gain,
		})
		.collect();
	AudioTicketSpec {
		ticket,
		slot: slot as i32,
		time_num: params.range.in_().numerator(),
		time_den: params.range.in_().denominator(),
		duration_num: duration.numerator(),
		duration_den: duration.denominator(),
		sample_rate: params.sample_rate,
		channel_layout: params.channel_layout,
		channels: params.channel_layout.count_ones().max(1) as i32,
		montage,
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn slot_bytes_for_formats() {
		// F32 RGBA: 16 bytes per pixel.
		assert_eq!(slot_bytes_for(1920, 1080, 4), 1920 * 1080 * 16);
		// BGRA8: 4 bytes per pixel (the 8.3 MB design figure).
		assert_eq!(slot_bytes_for(1920, 1080, SLOT_FORMAT_BGRA8), 1920 * 1080 * 4);
		// U8 RGBA.
		assert_eq!(slot_bytes_for(64, 64, 0), 64 * 64 * 4);
	}

	#[test]
	fn worker_count_policy_is_clamped() {
		// With absurd slot sizes the memory budget clamps to 1.
		let n = default_worker_count(64, 1 << 30); // 64 GiB per worker
		assert_eq!(n, 1);
		// With tiny slots the core policy dominates (>= 1).
		let n = default_worker_count(1, 64);
		assert!(n >= 1);
	}

	#[test]
	fn preview_window_capacity_reserves_one_slot_per_worker() {
		// workers=3 × slots=4 → the window may hold 12-3=9 slots; the
		// reserve keeps interactive/audio tickets dispatchable (the
		// playback-freeze regression guard).
		let config = DispatcherConfig {
			worker_bin: Some(std::path::PathBuf::from("/bin/true")),
			workers: 3,
			slots_per_worker: 4,
			width: 64,
			height: 64,
			batch_size: 2,
			..Default::default()
		};
		let dispatcher = ProcessDispatcher::new(config).expect("dispatcher");
		assert_eq!(dispatcher.preview_window_capacity(), 9);
	}

	#[test]
	fn config_normalization_defaults() {
		let c = DispatcherConfig::default().normalize();
		// Geometry defaults resolve here; the adaptive counts stay 0 (auto)
		// and resolve in `ProcessDispatcher::new` where slot_bytes is known.
		assert_eq!(c.width, 1920);
		assert_eq!(c.height, 1080);
		assert_eq!(c.slot_format, SLOT_FORMAT_BGRA8);
		assert_eq!(c.slots_per_worker, 0);
		assert_eq!(c.workers, 0);
		assert_eq!(c.batch_size, 0);
	}

	#[test]
	fn slots_policy_adapts_to_slot_size() {
		// BGRA8 1080p: the full 8 slots (~66 MB per worker segment).
		let bgra8_1080p = slot_bytes_for(1920, 1080, SLOT_FORMAT_BGRA8);
		assert_eq!(default_slots_per_worker(bgra8_1080p), 8);
		// F32 1080p: drops to 4 (~133 MB per worker segment).
		let f32_1080p = slot_bytes_for(1920, 1080, 4);
		assert_eq!(default_slots_per_worker(f32_1080p), 4);
		// F32 4K: 2 slots (the floor).
		let f32_4k = slot_bytes_for(3840, 2160, 4);
		assert_eq!(default_slots_per_worker(f32_4k), 2);
		// Tiny slots: the cap at 8.
		assert_eq!(default_slots_per_worker(16), 8);
	}

	#[test]
	fn batch_size_policy_scales_with_workers_and_slots() {
		// 4 workers x 8 slots: the design 120/4 = 30 caps at the 8 slots.
		assert_eq!(default_batch_size(4, 8), 8);
		// 1 worker x 8 slots: 120/1 = 120 caps at 8.
		assert_eq!(default_batch_size(1, 8), 8);
		// 2 workers x 4 slots: 120/2 = 60 caps at 4.
		assert_eq!(default_batch_size(2, 4), 4);
		// 8 workers x 8 slots: 120/8 = 15 caps at 8.
		assert_eq!(default_batch_size(8, 8), 8);
	}

	#[test]
	fn grown_segment_slots_stay_bounded() {
		// Growing a segment keeps a sane slot count: 8.3 MB slots keep 8;
		// 33 MB slots keep 8 (still within the 256 MiB grown budget);
		// absurd sizes clamp at 2.
		assert_eq!(default_slots_for_bytes(8_300_000, 8), 8);
		assert_eq!(default_slots_for_bytes(33_000_000, 8), 8);
		assert_eq!(default_slots_for_bytes(1 << 30, 8), 2);
	}

	#[test]
	fn copy_counter_counts_only_slot_to_vec() {
		reset_main_heap_frame_copies();
		assert_eq!(main_heap_frame_copies(), 0);
	}

	/// A worker's `plugin_progress` NDJSON line is forwarded to the
	/// registered app callback as (label, message, fraction) — the seam
	/// that drives the main-process plugin-progress dialog.
	#[test]
	fn plugin_progress_line_forwards_to_callback() {
		let config = DispatcherConfig {
			worker_bin: Some(std::path::PathBuf::from("/bin/true")),
			workers: 1,
			slots_per_worker: 2,
			width: 16,
			height: 16,
			batch_size: 1,
			..Default::default()
		};
		let dispatcher = ProcessDispatcher::new(config).expect("dispatcher");
		// `new` registered the weak handle (the cancel broadcast seam).
		assert!(dispatcher_slot().lock().unwrap().upgrade().is_some());

		let received: Arc<Mutex<Vec<(String, String, f64)>>> = Arc::new(Mutex::new(Vec::new()));
		set_plugin_progress_cb(Some(Arc::new({
			let received = received.clone();
			move |label, message, fraction| {
				received.lock().unwrap().push((label, message, fraction));
			}
		})));

		// A fake worker handle so on_line has a target (no spawn needed).
		{
			let mut inner = dispatcher.inner.lock().unwrap_or_else(|e| e.into_inner());
			let key = SharedMemoryRegion::make_key(std::process::id() as i64, 999);
			let shm = ShmRegionView::create(&key, 2, 256).expect("shm");
			inner.workers.push(WorkerHandle::shell(0, 0, shm, 2, 256));
		}

		let mut fired = Vec::new();
		{
			let mut inner = dispatcher.inner.lock().unwrap_or_else(|e| e.into_inner());
			dispatcher.on_line(
				&mut inner,
				0,
				r#"{"type":"plugin_progress","label":"render","message":"pass 1","fraction":0.5}"#,
				&mut fired,
			);
		}
		set_plugin_progress_cb(None);

		let events = received.lock().unwrap().clone();
		assert_eq!(events.len(), 1);
		assert_eq!(events[0], ("render".to_string(), "pass 1".to_string(), 0.5));
	}
}
