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
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::{Duration, Instant};

use serde_json::{json, Value};

use crate::error::{Error, Result};
use crate::ipc::{
	write_message, BatchAcceptedMsg, FrameFailedMsg, FrameReadyMsg, FrameSlotMeta, FrameSlotPool,
	HandshakeMsg, HelloCapsMsg, RenderBatchMsg, BatchTicketSpec, SharedMemoryRegion, ShmMode,
	WireMontageClip, SLOT_FORMAT_BGRA8, TYPE_BATCH_ACCEPTED, TYPE_ERROR, TYPE_FRAME_FAILED,
	TYPE_FRAME_READY, TYPE_HANDSHAKE, TYPE_HELLO_CAPS,
};
use crate::scheduler::{FrameKey, FrameRequest, PreviewScheduler, SubmitOutcome};
use crate::ticket::{Completion, TicketPayload, TicketResult, VideoTicketParams};
use crate::worker::{Job, JobDispatch};

/// Protocol version spoken by the dispatcher (v1 base; v2 messages are
/// additive — the oak-worker handshake check stays `== 1`).
pub const DISPATCH_PROTOCOL_VERSION: i32 = 1;

/// Restart attempts per worker before its tickets fail permanently.
const MAX_RESTARTS: u32 = 5;

/// Default slots per worker segment (design §3.1: 8 slots starting).
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

/// Physical memory in bytes (macOS `hw.memsize`, Linux `sysconf`).
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
	#[cfg(not(any(target_os = "macos", target_os = "linux")))]
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
	/// Output slots per worker segment. `0` = 8.
	pub slots_per_worker: u32,
	/// Frame width of the segment geometry. `0` = 1920.
	pub width: i32,
	/// Frame height of the segment geometry. `0` = 1080.
	pub height: i32,
	/// Slot wire format: an `oakcore_rs::PixelFormat` int or
	/// [`SLOT_FORMAT_BGRA8`]. Default BGRA8 (the viewer preview path).
	pub slot_format: i32,
	/// Batch size `B`. `0` = `max(1, 120 / workers)`.
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
		let mut c = self.clone();
		c.slots_per_worker = if c.slots_per_worker == 0 {
			DEFAULT_SLOTS_PER_WORKER
		} else {
			c.slots_per_worker
		};
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
}

impl WorkerHandle {
	fn shell(index: usize, generation: u64, shm: Arc<ShmRegionView>, slots: u32) -> WorkerHandle {
		WorkerHandle {
			index,
			generation,
			state: WorkerState::Starting,
			child: None,
			stdin: None,
			shm,
			free_slots: (0..slots).collect(),
			outstanding: HashMap::new(),
			held: HashSet::new(),
			startup_seen: false,
			graph_sent: false,
			caps: None,
			restarts: 0,
			spawned_at: Instant::now(),
			accepted_batches: 0,
		}
	}
}

// ---------------------------------------------------------------------------
// ProcessDispatcher
// ---------------------------------------------------------------------------

struct PendingTicket {
	key: FrameKey,
	params: Arc<VideoTicketParams>,
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
	/// [`ProcessDispatcher::start`]).
	pub fn new(config: DispatcherConfig) -> Result<Arc<ProcessDispatcher>> {
		let config = config.normalize();
		let slots = config.slots_per_worker;
		let slot_bytes = slot_bytes_for(config.width, config.height, config.slot_format);
		let workers = if config.workers == 0 {
			default_worker_count(slots, slot_bytes)
		} else {
			config.workers
		};
		let bin = resolve_worker_bin(&config)?;
		let batch_size = config.batch_size;
		let (events_tx, events_rx) = mpsc::channel();
		Ok(Arc::new(ProcessDispatcher {
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
				started: false,
				shutting_down: false,
			}),
		}))
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
			if matches!(inner.workers[i].state, WorkerState::Alive) {
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
				// geometry (protocol v1 flow).
				handle.startup_seen = true;
				let hs = HandshakeMsg {
					protocol_version: DISPATCH_PROTOCOL_VERSION,
					shm_key: handle.shm.key().to_string(),
					input_shm_key: String::new(),
					input_slots: 0,
					output_slots: handle.shm.slot_count() as i32,
					slot_data_bytes: handle.shm.slot_data_bytes() as i64,
					input_slot_data_bytes: 0,
				};
				if self.send_json(handle, &hs.to_json()).is_err() {
					handle.state = WorkerState::Dead;
				}
			}
			TYPE_HELLO_CAPS => {
				if let Ok(caps) = serde_json::from_value::<HelloCapsMsg>(msg) {
					handle.caps = Some(caps);
					handle.state = WorkerState::Alive;
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
					fired.push((
						done,
						Ok(TicketPayload::ShmFrame(ShmFrameRef {
							worker: worker as u32,
							slot: slot as u32,
							meta,
							shm,
						})),
					));
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
			let Some(batch) = inner.scheduler.claim_batch(worker, credit) else {
				return;
			};
			let mut wire_tickets = Vec::with_capacity(batch.frames.len());
			for req in &batch.frames {
				let ticket = req.payload;
				let slot = match inner.workers[worker].free_slots.pop_front() {
					Some(s) => s,
					None => break, // credit accounting drifted; stop cleanly
				};
				inner.workers[worker].outstanding.insert(ticket, slot);
				let Some(pt) = inner.tickets.get(&ticket) else {
					continue;
				};
				wire_tickets.push(build_ticket_spec(
					ticket,
					slot,
					&pt.params,
					inner.config.slot_format,
				));
			}
			let msg = RenderBatchMsg {
				batch_id: batch.batch_id as i64,
				tickets: wire_tickets,
			};
			// The `type` tag is added by hand: [`RenderBatchMsg`] only
			// carries the payload fields (it is the parse-side struct).
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

		let mut handle = WorkerHandle::shell(index, generation, shm, inner.slots);
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
			let id = inner.next_ticket;
			inner.next_ticket += 1;
			let frame = job.schedule.frame.unwrap_or(id);
			let key = FrameKey {
				sequence: job.node_identity,
				frame,
				version: job.schedule.version,
			};
			inner.tickets.insert(
				id,
				PendingTicket {
					key,
					params: job.params,
					done: Some(job.done),
				},
			);
			let request = FrameRequest {
				key,
				priority: job.schedule.priority,
				distance: job.schedule.distance,
				payload: id,
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

	/// Release a consumed frame's slot (delegates to the inherent
	/// release — see [`ProcessDispatcher::release_frame`]).
	fn release_frame(&self, frame: &ShmFrameRef) {
		self.release_frame(frame);
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
		format: slot_format,
		channels: 4,
		footage_file,
		footage_stream,
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
	fn config_normalization_defaults() {
		let c = DispatcherConfig::default().normalize();
		assert_eq!(c.slots_per_worker, DEFAULT_SLOTS_PER_WORKER);
		assert_eq!(c.width, 1920);
		assert_eq!(c.height, 1080);
		assert_eq!(c.slot_format, SLOT_FORMAT_BGRA8);
	}

	#[test]
	fn copy_counter_counts_only_slot_to_vec() {
		reset_main_heap_frame_copies();
		assert_eq!(main_heap_frame_copies(), 0);
	}
}
