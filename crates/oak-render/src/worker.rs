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

//! The job-dispatch seam (M15 S2): what a render ticket posts through.
//!
//! The in-process thread pool was **removed** in M15 S2 (user mandate:
//! "delete the internal render thread pool"); the only video backend is
//! the process-isolated [`crate::procpool::ProcessDispatcher`]
//! (oak-worker children over NDJSON + shared memory). This module keeps
//! the ticket-facing surface:
//!
//! - [`Job`] — one unit of render work plus its scheduler hints.
//! - [`JobDispatch`] — the backend seam the arena posts through, with
//!   default no-ops for the process-backend extras (poll / release /
//!   preview-window cancellation).
//! - [`InlineDispatcher`] — a thread-free dispatcher that executes jobs
//!   on the calling thread. Used as the **audio** backend (audio stays on
//!   main-process inline execution until S3 — design §3.7) and by the
//!   manager's test-only `Threads` backend and by unit tests.
//! - [`GraphSnapshotStore`] — the graph-snapshot file refcounting cache
//!   shared with worker processes.

use std::collections::{HashMap, VecDeque};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};

use oak_core::Rational;

use crate::error::{Error, Result};
use crate::procpool::ShmFrameRef;
use crate::scheduler::FramePriority;
use crate::ticket::{AudioTicketParams, Completion, Producer, VideoTicketParams};

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}

/// A unit of render work (produced by the ticket arena).
pub struct Job {
	/// The graph position this job evaluates.
	pub node_identity: u64,
	/// Frame time.
	pub time: Rational,
	/// Ticket parameters (size/format overrides).
	pub params: Arc<VideoTicketParams>,
	/// Audio ticket parameters when this is an audio range pull (M15 S3):
	/// the process dispatcher renders it through the worker's
	/// `render_audio_batch` path into a shm slot; the in-process inline
	/// fallback executes the producer instead. `None` for video jobs.
	pub audio: Option<Arc<AudioTicketParams>>,
	/// Frame producer (arena-installed; the process backend never invokes
	/// it — workers render from the wire spec).
	pub produce: Producer,
	/// Completion delivery.
	pub done: Completion,
	/// Scheduler hints (M15 S2). Defaults to a Seek single-frame request.
	pub schedule: JobSchedule,
}

/// Scheduler hints a posted job carries (M15 S2). The process dispatcher
/// maps these onto [`crate::scheduler::FrameKey`] / priority; the inline
/// dispatcher ignores them.
#[derive(Clone, Debug, Default)]
pub struct JobSchedule {
	/// Priority class. Default [`FramePriority::Seek`] (single-frame).
	pub priority: FramePriority,
	/// Scheduler key frame number. `None` = the ticket id (the Seek
	/// single-frame convention).
	pub frame: Option<i64>,
	/// Playhead distance (orders the Playback class).
	pub distance: i64,
	/// Parameter version (graph/proxy/resolution/color); bumping it
	/// invalidates stale requests for the same sequence+frame.
	pub version: u64,
}

impl JobSchedule {
	/// A Seek single-frame request (the default for every ticket).
	pub fn seek() -> Self {
		Self::default()
	}

	/// A Background request (exports / precache): rendered whenever the
	/// workers have no Seek/Playback work.
	pub fn background() -> Self {
		Self {
			priority: FramePriority::Background,
			..Default::default()
		}
	}

	/// A Playback-window request at `frame`, ordered by `distance` from
	/// the playhead and keyed under `version`.
	pub fn playback(frame: i64, distance: i64, version: u64) -> Self {
		Self {
			priority: FramePriority::Playback,
			frame: Some(frame),
			distance,
			version,
		}
	}
}

/// The job-dispatch seam (M15 S1): the ticket arena posts [`Job`]s
/// through this interface without knowing the backend. Implemented by the
/// process-isolated [`crate::procpool::ProcessDispatcher`] (oak-worker
/// children) and the thread-free [`InlineDispatcher`] (audio / tests).
pub trait JobDispatch: Send + Sync {
	/// Enqueue a job; false when the backend is gone (the arena then
	/// delivers the completion itself with `Error::State`).
	fn post(&self, job: Job) -> bool;

	/// Stop accepting work, deliver the queued completions (cancelled)
	/// and release the backend. Idempotent.
	fn shutdown(&self);

	/// Pump backend completions (the process dispatcher's poll loop).
	/// Default no-op: backends that deliver inline have nothing to pump.
	/// The UI tick and blocking ticket waits call this so the process
	/// backend's completions are delivered without a dedicated thread.
	fn poll(&self) {}

	/// Release a consumed shm frame's slot back to its worker (slot
	/// release = cache eviction, design §3.1). Default no-op: only the
	/// process backend holds slots.
	fn release_frame(&self, _frame: &ShmFrameRef) {}

	/// Release a consumed shm audio frame's slot (M15 S3). Default no-op:
	/// only the process backend holds slots.
	fn release_audio_frame(&self, _frame: &crate::procpool::ShmAudioRef) {}

	/// Cancel every pending AND claimed request of `sequence` (M15 S2
	/// preview-window invalidation); their completions fire with
	/// `Error::State`. Default no-op: only the process backend schedules.
	fn cancel_preview_sequence(&self, _sequence: u64) {}

	/// Slot headroom available to a best-effort pre-render window (total
	/// pool slots minus one per worker), so interactive and audio tickets
	/// always keep credit. Default `None`: backends without slots impose
	/// no constraint.
	fn preview_window_capacity(&self) -> Option<usize> {
		None
	}

	/// Cancel one pre-render window frame by scheduler key (pending or in
	/// flight; the completion fires `Error::State`). Default no-op: only
	/// the process backend schedules.
	fn cancel_preview_frame(&self, _sequence: u64, _frame: i64, _version: u64) {}
}

/// Thread-free job dispatcher (M15 S2). Executes jobs on the calling
/// thread — there are deliberately **no worker threads**:
///
///   - **Sync mode** ([`InlineDispatcher::sync`]): every `post` runs its
///     job immediately on the caller's thread. This is the production
///     **audio** backend (audio stays on main-process inline execution
///     until S3 — design §3.7: the crash risk is dominated by video
///     plugins, which already live in oak-worker) and the manager's
///     test-only `Threads` backend.
///   - **Queued mode** ([`InlineDispatcher::queued`]): `post` queues the
///     job; the test drains it with [`InlineDispatcher::run`]. This keeps
///     the arena's cancel-race and shutdown semantics deterministic
///     without any threads.
pub struct InlineDispatcher {
	inner: Arc<InlineInner>,
}

struct InlineInner {
	queue: Mutex<VecDeque<Job>>,
	sync: bool,
	stopping: AtomicBool,
}

impl InlineDispatcher {
	/// A sync-mode dispatcher: jobs run immediately on the posting thread.
	pub fn sync() -> Arc<Self> {
		Arc::new(Self {
			inner: Arc::new(InlineInner {
				queue: Mutex::new(VecDeque::new()),
				sync: true,
				stopping: AtomicBool::new(false),
			}),
		})
	}

	/// A queued-mode dispatcher: jobs wait for [`InlineDispatcher::run`].
	pub fn queued() -> Arc<Self> {
		Arc::new(Self {
			inner: Arc::new(InlineInner {
				queue: Mutex::new(VecDeque::new()),
				sync: false,
				stopping: AtomicBool::new(false),
			}),
		})
	}

	/// Run every queued job synchronously on the calling thread (queued
	/// mode). Jobs posted after `shutdown` are refused by `post`.
	pub fn run(&self) {
		if self.inner.stopping.load(Ordering::Acquire) {
			return;
		}
		loop {
			let job = lock(&self.inner.queue).pop_front();
			let Some(job) = job else { break };
			execute_job(job);
		}
	}

	/// The number of queued (not yet run) jobs.
	pub fn queued_count(&self) -> usize {
		lock(&self.inner.queue).len()
	}
}

fn execute_job(job: Job) {
	let result = catch_unwind(AssertUnwindSafe(|| (job.produce)(job.time, &job.params)))
		.unwrap_or_else(|_| Err(Error::Failed("frame producer panicked".into())));
	(job.done)(result);
}

impl JobDispatch for InlineDispatcher {
	fn post(&self, job: Job) -> bool {
		if self.inner.sync {
			// Sync mode: run now (refusing only when shutting down).
			if self.inner.stopping.load(Ordering::Acquire) {
				return false;
			}
			execute_job(job);
			return true;
		}
		let mut queue = lock(&self.inner.queue);
		if self.inner.stopping.load(Ordering::Acquire) {
			return false;
		}
		queue.push_back(job);
		true
	}

	fn shutdown(&self) {
		// Stop accepting and drain the queue with cancellation (queued
		// jobs never run after shutdown).
		let jobs: Vec<Job> = {
			let mut queue = lock(&self.inner.queue);
			self.inner.stopping.store(true, Ordering::Release);
			queue.drain(..).collect()
		};
		for job in jobs {
			(job.done)(Err(Error::State));
		}
	}
}

/// Graph snapshot files shared with worker processes (C++
/// write_graph_snapshot + path refcounting): a snapshot is written once
/// and reference-counted; the file is unlinked at zero.
pub struct GraphSnapshotStore {
	entries: Mutex<HashMap<String, SnapshotEntry>>,
	dir: std::path::PathBuf,
}

struct SnapshotEntry {
	refs: u64,
	cached: bool,
}

impl GraphSnapshotStore {
	/// Empty store rooted in the process temp directory.
	pub fn new() -> Self {
		let dir = std::env::temp_dir().join(format!("oakrender-snapshots-{}", std::process::id()));
		let _ = std::fs::create_dir_all(&dir);
		Self {
			entries: Mutex::new(HashMap::new()),
			dir,
		}
	}

	/// The store's root directory (tests).
	pub fn root(&self) -> &std::path::Path {
		&self.dir
	}

	/// Write (or reuse) the snapshot for a project copy; returns the path
	/// token with the reference count incremented.
	pub fn acquire(&mut self, project_copy: u64) -> Result<String> {
		let path = self.dir.join(format!("{project_copy}.json"));
		let path_str = path.to_string_lossy().into_owned();
		let mut entries = lock(&self.entries);
		if let Some(entry) = entries.get_mut(&path_str) {
			entry.refs += 1;
			return Ok(path_str);
		}
		// Minimal snapshot payload: the copied-project identity. The real
		// graph serialization is owned by oaknode.
		let payload = format!("{{\"project_copy\":{project_copy}}}\n");
		std::fs::write(&path, payload)
			.map_err(|e| Error::Failed(format!("write snapshot: {e}")))?;
		entries.insert(
			path_str.clone(),
			SnapshotEntry {
				refs: 1,
				cached: false,
			},
		);
		Ok(path_str)
	}

	/// Drop one reference; unlinks the file at zero.
	pub fn release(&mut self, path: &str) {
		let mut entries = lock(&self.entries);
		let remove = if let Some(entry) = entries.get_mut(path) {
			entry.refs = entry.refs.saturating_sub(1);
			entry.refs == 0
		} else {
			false
		};
		if remove {
			entries.remove(path);
			let _ = std::fs::remove_file(path);
		}
	}

	/// Mark a snapshot as already uploaded to all live children
	/// (C++ set_graph_path_cached).
	pub fn mark_cached(&mut self, path: &str, cached: bool) {
		if let Some(entry) = lock(&self.entries).get_mut(path) {
			entry.cached = cached;
		}
	}

	/// Whether the snapshot is marked cached (tests).
	pub fn is_cached(&self, path: &str) -> bool {
		lock(&self.entries)
			.get(path)
			.map(|e| e.cached)
			.unwrap_or(false)
	}

	/// Current reference count for a path (tests).
	pub fn refs(&self, path: &str) -> u64 {
		lock(&self.entries).get(path).map(|e| e.refs).unwrap_or(0)
	}
}

impl Default for GraphSnapshotStore {
	fn default() -> Self {
		Self::new()
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::mpsc;
	use std::time::Duration;

	use crate::texture::Texture;

	fn job(tag: u64, tx: mpsc::Sender<u64>, gate: Option<Arc<AtomicBool>>) -> Job {
		let produce: Producer = Arc::new(move |_, _| {
			if let Some(g) = &gate {
				if g.load(Ordering::Acquire) {
					return Err(Error::Failed("gated producer".into()));
				}
			}
			Ok(crate::ticket::TicketPayload::Video(Texture::dummy()))
		});
		Job {
			node_identity: tag,
			time: Rational::new(tag as i64, 1),
			params: Arc::new(VideoTicketParams {
				viewer: 0,
				time: Rational::new(0, 1),
				force_size: None,
				force_format: None,
				cache: None,
				cache_dir: None,
				cache_id: None,
				cache_timebase: None,
				footage: None,
				montage: Vec::new(),
			}),
			audio: None,
			produce,
			done: Box::new(move |r| {
				assert!(r.is_ok(), "producer must succeed here");
				let _ = tx.send(tag);
			}),
			schedule: JobSchedule::seek(),
		}
	}

	#[test]
	fn sync_dispatcher_runs_every_job_immediately() {
		let d = InlineDispatcher::sync();
		let (tx, rx) = mpsc::channel();
		for i in 0..8u64 {
			assert!(d.post(job(i, tx.clone(), None)));
		}
		drop(tx);
		let mut seen = Vec::new();
		while let Ok(tag) = rx.recv_timeout(Duration::from_secs(5)) {
			seen.push(tag);
		}
		assert_eq!(seen.len(), 8, "every job ran on the posting thread");
		d.shutdown();
		// Post after shutdown is refused.
		let (tx2, _rx2) = mpsc::channel();
		assert!(!d.post(job(9, tx2, None)), "post after shutdown is refused");
	}

	#[test]
	fn queued_dispatcher_runs_on_demand() {
		let d = InlineDispatcher::queued();
		let (tx, rx) = mpsc::channel();
		for i in 0..8u64 {
			assert!(d.post(job(i, tx.clone(), None)));
		}
		assert_eq!(d.queued_count(), 8, "nothing ran yet");
		d.run();
		assert_eq!(d.queued_count(), 0);
		drop(tx);
		let mut seen = Vec::new();
		while let Ok(tag) = rx.recv_timeout(Duration::from_secs(5)) {
			seen.push(tag);
		}
		assert_eq!(seen.len(), 8);
		d.shutdown();
	}

	#[test]
	fn queued_dispatcher_shutdown_delivers_cancellation() {
		let d = InlineDispatcher::queued();
		let (tx, rx) = mpsc::channel();
		for _ in 0..4 {
			let tx = tx.clone();
			let p: Producer = Arc::new(|_, _| Ok(crate::ticket::TicketPayload::Video(Texture::dummy())));
			d.post(Job {
				node_identity: 1,
				time: Rational::new(0, 1),
				params: Arc::new(VideoTicketParams {
					viewer: 0,
					time: Rational::new(0, 1),
					force_size: None,
					force_format: None,
					cache: None,
					cache_dir: None,
					cache_id: None,
					cache_timebase: None,
					footage: None,
					montage: Vec::new(),
				}),
				audio: None,
				produce: p,
				done: Box::new(move |r| {
					let _ = tx.send(r.is_err());
				}),
				schedule: JobSchedule::seek(),
			});
		}
		drop(tx);
		d.shutdown();
		let mut delivered = Vec::new();
		while let Ok(err) = rx.recv_timeout(Duration::from_secs(5)) {
			delivered.push(err);
		}
		assert_eq!(delivered.len(), 4, "all queued completions fire");
		assert!(delivered.iter().all(|&e| e), "queued jobs cancel at shutdown");
	}

	#[test]
	fn producer_panic_does_not_kill_the_dispatcher() {
		let d = InlineDispatcher::sync();
		let (tx, rx) = mpsc::channel();
		let tx1 = tx.clone();
		let boom: Producer = Arc::new(|_, _| panic!("boom"));
		let ok: Producer = Arc::new(|_, _| Ok(crate::ticket::TicketPayload::Video(Texture::dummy())));
		let params = Arc::new(VideoTicketParams {
			viewer: 0,
			time: Rational::new(0, 1),
			force_size: None,
			force_format: None,
			cache: None,
			cache_dir: None,
			cache_id: None,
			cache_timebase: None,
			footage: None,
			montage: Vec::new(),
		});
		d.post(Job {
			node_identity: 0,
			time: Rational::new(0, 1),
			params: params.clone(),
			audio: None,
			produce: boom,
			done: Box::new(move |r| {
				assert!(r.is_err());
				let _ = tx1.send(1u64);
			}),
			schedule: JobSchedule::seek(),
		});
		d.post(Job {
			node_identity: 1,
			time: Rational::new(1, 1),
			params,
			audio: None,
			produce: ok,
			done: Box::new(move |r| {
				assert!(r.is_ok());
				let _ = tx.send(2u64);
			}),
			schedule: JobSchedule::seek(),
		});
		let mut got = Vec::new();
		while let Ok(v) = rx.recv_timeout(Duration::from_secs(5)) {
			got.push(v);
		}
		assert_eq!(got.len(), 2, "the dispatcher survives a panicking producer");
		d.shutdown();
	}

	#[test]
	fn snapshot_store_refcount_and_unlink() {
		let mut store = GraphSnapshotStore::new();
		let p1 = store.acquire(42).unwrap();
		let p2 = store.acquire(42).unwrap();
		assert_eq!(p1, p2, "second acquire reuses the file");
		assert!(std::path::Path::new(&p1).exists());
		store.mark_cached(&p1, true);
		assert!(store.is_cached(&p1));
		assert_eq!(store.refs(&p1), 2);

		store.release(&p1);
		assert!(
			std::path::Path::new(&p1).exists(),
			"refcount 1: still alive"
		);
		store.release(&p1);
		assert!(!std::path::Path::new(&p1).exists(), "refcount 0: unlinked");
		assert_eq!(store.refs(&p1), 0);
	}
}
