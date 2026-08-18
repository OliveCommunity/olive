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

//! The worker layer (C++ RenderWorkerPool + RenderThread +
//! workerprocess/workerjson): thread pool AND process-isolated pool
//! behind one dispatch seam.
//!
//! This pass ships the in-process [`WorkerPool`] fully. The
//! process-isolated backend landed in M15 S1 as
//! [`crate::procpool::ProcessDispatcher`] (spawn/handshake/crash-restart
//! of oak-worker binaries over NDJSON + shared memory); both backends
//! implement the [`JobDispatch`] seam the ticket arena posts through.
//! [`ProcessPool`] below is the frozen pre-M15 facade stub kept for C
//! ABI parity.

use std::collections::{HashMap, VecDeque};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard};

use oakcore_rs::Rational;

use crate::error::{Error, Result};
use crate::ticket::{Completion, Producer, VideoTicketParams};

/// A unit of render work (produced by the ticket arena).
pub struct Job {
	/// The graph position this job evaluates.
	pub node_identity: u64,
	/// Frame time.
	pub time: Rational,
	/// Ticket parameters (size/format overrides).
	pub params: Arc<VideoTicketParams>,
	/// Frame producer (arena-installed).
	pub produce: Producer,
	/// Completion delivery.
	pub done: Completion,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}

/// The job-dispatch seam (M15 S1): the ticket arena posts [`Job`]s
/// through this interface without knowing the backend. Implemented by
/// the in-process [`WorkerPool`] (threads) and the process-isolated
/// [`crate::procpool::ProcessDispatcher`] (oak-worker children); S2
/// removes the thread pool and this seam becomes process-only.
pub trait JobDispatch: Send + Sync {
	/// Enqueue a job; false when the backend is gone (the arena then
	/// delivers the completion itself with `Error::State`).
	fn post(&self, job: Job) -> bool;

	/// Stop accepting work, deliver the queued completions (cancelled)
	/// and release the backend. Idempotent.
	fn shutdown(&self);
}

/// Thread-pool backend (C++ RenderThread model). Cheap to clone (all
/// state is behind an `Arc`); the manager and the ticket arena share one
/// pool.
#[derive(Clone)]
pub struct WorkerPool {
	inner: Arc<PoolInner>,
}

struct PoolInner {
	workers: usize,
	queue: Mutex<VecDeque<Job>>,
	cv: Condvar,
	stopping: AtomicBool,
	threads: Mutex<Vec<std::thread::JoinHandle<()>>>,
}

impl WorkerPool {
	/// Pool with `workers` threads (0 = hardware concurrency).
	pub fn new(workers: usize) -> Self {
		let workers = if workers == 0 {
			std::thread::available_parallelism()
				.map(|n| n.get())
				.unwrap_or(1)
		} else {
			workers
		};
		Self {
			inner: Arc::new(PoolInner {
				workers,
				queue: Mutex::new(VecDeque::new()),
				cv: Condvar::new(),
				stopping: AtomicBool::new(false),
				threads: Mutex::new(Vec::new()),
			}),
		}
	}

	/// The number of worker threads.
	pub fn worker_count(&self) -> usize {
		self.inner.workers
	}

	/// Start threads (idempotent).
	pub fn start(&mut self) {
		let mut threads = lock(&self.inner.threads);
		if !threads.is_empty() {
			return;
		}
		for _ in 0..self.inner.workers {
			let inner = self.inner.clone();
			let handle = std::thread::spawn(move || worker_loop(inner));
			threads.push(handle);
		}
	}

	/// True when threads are running.
	pub fn is_running(&self) -> bool {
		!lock(&self.inner.threads).is_empty()
	}

	/// Enqueue a job. Returns false when the pool is shut down.
	pub fn post(&self, job: Job) -> bool {
		// The stopping check and the push share one queue lock: a shutdown
		// racing the check would otherwise leave the job queued after every
		// worker exited (and after the defensive drain), so its completion
		// could never fire.
		let mut queue = lock(&self.inner.queue);
		if self.inner.stopping.load(Ordering::Acquire) {
			return false;
		}
		queue.push_back(job);
		self.inner.cv.notify_one();
		true
	}

	/// Stop accepting, drain, join all workers. In-flight job completions
	/// fire with cancellation (queued jobs are delivered `Error::State`
	/// without running); running jobs are joined so no completion fires
	/// after shutdown returns.
	pub fn shutdown(&mut self) {
		self.shutdown_ref();
	}

	/// [`Self::shutdown`] on a shared reference (the [`JobDispatch`]
	/// seam; all state is interior-mutable). Idempotent.
	pub fn shutdown_ref(&self) {
		// Set the flag and wake the workers while holding the queue lock.
		// Workers decide whether to block in `cv.wait` while holding that
		// lock, so a flag set outside it could land between a worker's
		// predicate check and its wait: the wakeup is lost, the worker
		// sleeps forever, and the join below hangs. Serializing store +
		// notify with the waiters' lock closes that window.
		{
			let _guard = lock(&self.inner.queue);
			self.inner.stopping.store(true, Ordering::Release);
			self.inner.cv.notify_all();
		}

		let threads = std::mem::take(&mut *lock(&self.inner.threads));
		for handle in threads {
			let _ = handle.join();
		}

		// Defensive drain: any job that landed between `stopping` and the
		// workers' exit (post() refuses them, so this is normally empty).
		let mut queue = lock(&self.inner.queue);
		while let Some(job) = queue.pop_front() {
			deliver_cancelled(job);
		}
	}
}

impl JobDispatch for WorkerPool {
	fn post(&self, job: Job) -> bool {
		WorkerPool::post(self, job)
	}

	fn shutdown(&self) {
		self.shutdown_ref();
	}
}

fn worker_loop(inner: Arc<PoolInner>) {
	loop {
		let job = {
			let mut queue = lock(&inner.queue);
			while !inner.stopping.load(Ordering::Acquire) && queue.is_empty() {
				queue = inner.cv.wait(queue).unwrap_or_else(|e| e.into_inner());
			}
			queue.pop_front()
		};
		let Some(job) = job else {
			return; // stopping and queue drained
		};
		if inner.stopping.load(Ordering::Acquire) {
			// Shutdown raced this pop: deliver cancellation.
			deliver_cancelled(job);
			continue;
		}
		let result = catch_unwind(AssertUnwindSafe(|| (job.produce)(job.time, &job.params)))
			.unwrap_or_else(|_| Err(Error::Failed("frame producer panicked".into())));
		(job.done)(result);
	}
}

fn deliver_cancelled(job: Job) {
	(job.done)(Err(Error::State));
}

/// Process-isolated worker backend (C++ RenderWorkerPool +
/// PooledWorker). Child processes talk the oakengine_ipc C ABI; this side
/// is only a client (spawn, dispatch, reap). Not wired in this pass.
pub struct ProcessPool {
	workers: usize,
}

impl ProcessPool {
	/// Pool of `workers` child processes.
	pub fn new(workers: usize) -> Self {
		Self { workers }
	}

	/// The configured child count.
	pub fn worker_count(&self) -> usize {
		self.workers
	}

	/// Spawn children and handshake.
	pub fn start(&mut self) -> Result<()> {
		Err(Error::Failed(
			"oakengine_ipc worker-process bridge not implemented in this pass".into(),
		))
	}

	/// Dispatch a job to a free child.
	pub fn post(&self, _job: Job) -> Result<()> {
		Err(Error::Failed(
			"oakengine_ipc worker-process bridge not implemented in this pass".into(),
		))
	}

	/// Cancel the job running in a child (C++ cancel_active_process).
	pub fn cancel_active(&self, _process_slot: usize) {}

	/// Terminate and reap all children; pending jobs complete with
	/// cancellation.
	pub fn shutdown(&mut self) {}
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

/// The pool the manager runs (config-selected, C++ parity).
pub enum WorkerBackend {
	/// In-process threads.
	Threads(WorkerPool),
	/// Child processes (crash isolation).
	Processes(ProcessPool),
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::atomic::AtomicUsize;
	use std::sync::mpsc;
	use std::time::Duration;

	use crate::texture::Texture;

	fn job(tag: u64, tx: mpsc::Sender<u64>, gate: Option<Arc<AtomicUsize>>) -> Job {
		let produce: Producer = Arc::new(move |_, _| {
			if let Some(g) = &gate {
				g.fetch_add(1, Ordering::SeqCst);
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
			produce,
			done: Box::new(move |r| {
				assert!(r.is_ok(), "producer must succeed here");
				let _ = tx.send(tag);
			}),
		}
	}

	#[test]
	fn pool_saturation_all_jobs_complete() {
		let mut pool = WorkerPool::new(4);
		pool.start();
		let (tx, rx) = mpsc::channel();
		for i in 0..64u64 {
			assert!(pool.post(job(i, tx.clone(), None)));
		}
		drop(tx);
		let mut seen = Vec::new();
		while let Ok(tag) = rx.recv_timeout(Duration::from_secs(10)) {
			seen.push(tag);
		}
		assert_eq!(seen.len(), 64);
		seen.sort_unstable();
		for (i, tag) in seen.iter().enumerate() {
			assert_eq!(*tag, i as u64, "every job runs exactly once");
		}
		pool.shutdown();
	}

	#[test]
	fn shutdown_delivers_cancellation_to_queued_jobs() {
		// 1 worker + a gate that blocks: jobs 2..N stay queued and must be
		// delivered Err(State) at shutdown.
		let gate = Arc::new(AtomicUsize::new(0));
		let mut pool = WorkerPool::new(1);
		pool.start();
		let (tx, rx) = mpsc::channel();
		for i in 0..8u64 {
			let tx = tx.clone();
			let gate = gate.clone();
			let produce: Producer = Arc::new(move |_, _| {
				if i == 0 {
					// First job blocks until shutdown begins.
					let start = std::time::Instant::now();
					while gate.load(Ordering::Acquire) == 0
						&& start.elapsed() < Duration::from_secs(5)
					{
						std::thread::sleep(Duration::from_millis(1));
					}
				}
				Ok(crate::ticket::TicketPayload::Video(Texture::dummy()))
			});
			let job = Job {
				node_identity: i,
				time: Rational::new(i as i64, 1),
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
				produce,
				done: Box::new(move |r| {
					let _ = tx.send(r.is_err());
				}),
			};
			pool.post(job);
		}
		drop(tx);
		gate.store(1, Ordering::Release);
		pool.shutdown();

		let mut delivered = Vec::new();
		while let Ok(is_err) = rx.recv_timeout(Duration::from_secs(5)) {
			delivered.push(is_err);
		}
		assert_eq!(delivered.len(), 8, "all 8 completions fire");
		assert!(
			delivered.iter().filter(|&&e| e).count() >= 7,
			"queued jobs complete with cancellation"
		);
	}

	#[test]
	fn post_after_shutdown_is_refused() {
		let mut pool = WorkerPool::new(1);
		pool.start();
		pool.shutdown();
		let (tx, _rx) = mpsc::channel();
		assert!(!pool.post(job(1, tx, None)));
	}

	#[test]
	fn producer_panic_does_not_kill_worker() {
		let mut pool = WorkerPool::new(1);
		pool.start();
		let (tx, rx) = mpsc::channel();
		let tx1 = tx.clone();
		let tx2 = tx.clone();
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
		pool.post(Job {
			node_identity: 0,
			time: Rational::new(0, 1),
			params: params.clone(),
			produce: boom,
			done: Box::new(move |r| {
				assert!(r.is_err());
				let _ = tx1.send(1u64);
			}),
		});
		pool.post(Job {
			node_identity: 1,
			time: Rational::new(1, 1),
			params,
			produce: ok,
			done: Box::new(move |r| {
				assert!(r.is_ok());
				let _ = tx2.send(2u64);
			}),
		});
		let mut got = Vec::new();
		while let Ok(v) = rx.recv_timeout(Duration::from_secs(5)) {
			got.push(v);
		}
		assert_eq!(got.len(), 2, "worker survives a panicking producer");
		pool.shutdown();
	}

	#[test]
	fn process_pool_is_documented_stub() {
		let mut pp = ProcessPool::new(2);
		assert_eq!(pp.worker_count(), 2);
		assert!(pp.start().is_err(), "oakengine_ipc bridge pending");
		let (tx, _rx) = mpsc::channel();
		assert!(pp.post(job(1, tx, None)).is_err());
		pp.cancel_active(0); // no-op
		pp.shutdown(); // no-op
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
