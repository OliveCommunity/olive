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

//! The render manager: process-wide singleton owning the process
//! dispatcher, the ticket arena, the auto-cacher, and backend selection
//! (C++ `RenderManager`).
//!
//! The singleton lives behind a `Mutex<Option<Arc<…>>>` so `init` /
//! `shutdown` round-trips (C++ `create_instance` / `destroy_instance`),
//! and consumers share the `Arc`. `global()` returns the `Arc` (the
//! skeleton's `&'static` reference was replaced because a resettable
//! singleton cannot hand out stable references safely).

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};

use crate::autocacher::PreviewAutoCacher;
use crate::backend::BackendKind;
use crate::error::{Error, Result};
use crate::eval;
use crate::procpool::{DispatcherConfig, ProcessDispatcher, ShmAudioRef, ShmFrameRef};
use crate::ticket::{TicketArena, TicketId};
use crate::worker::{InlineDispatcher, JobDispatch};

static MANAGER: Mutex<Option<Arc<RenderManager>>> = Mutex::new(None);

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}

/// The render backend the manager initializes (M15 S2).
pub enum RenderBackendChoice {
	/// In-process thread-free dispatch (**test-only** after M15 S2: the
	/// internal render thread pool was deleted by mandate; this backend
	/// runs jobs synchronously on the calling thread). Kept so manager /
	/// integration tests do not spawn oak-worker children.
	Threads,
	/// Process-isolated oak-worker pool (crash isolation + shm frames).
	/// The M15 S2 default.
	Processes(DispatcherConfig),
}

/// The manager. Created by `oakrender_manager_init` (C ABI), accessed
/// internally through [`RenderManager::global`].
pub struct RenderManager {
	/// Video job dispatch (the process dispatcher, M15).
	pub dispatch: Arc<dyn JobDispatch>,
	/// Audio job dispatch (M15 S3: the process dispatcher, like video —
	/// audio renders in oak-worker so plugin crashes are isolated; the
	/// inline fallback lives in the arena, design §3.7).
	pub audio_dispatch: Arc<dyn JobDispatch>,
	/// Ticket arena.
	pub tickets: Arc<TicketArena>,
	/// Active GPU backend.
	pub backend: BackendKind,
	/// The backend the user requested (C++ `requested_backend`).
	pub requested_backend: BackendKind,
	/// Auto-cacher (None until first access; created lazily by
	/// [`RenderManager::get_cacher`]).
	pub autocacher: Mutex<Option<PreviewAutoCacher>>,
	/// Aggressive decoder GC toggle.
	aggressive_gc: AtomicBool,
}

impl RenderManager {
	/// Initialize the process-wide manager with the default backend — the
	/// process-isolated oak-worker pool (M15 S2 mandate; idempotent; C++
	/// instance() semantics — only the main GUI process does this).
	pub fn init() -> Result<()> {
		Self::init_with_backend(RenderBackendChoice::Processes(DispatcherConfig::default()))
	}

	/// Initialize the process-wide manager with an explicit backend.
	/// `Threads` is the test-only inline backend (no worker threads, no
	/// child processes); `Processes` spawns the oak-worker pool.
	pub fn init_with_backend(choice: RenderBackendChoice) -> Result<()> {
		let mut guard = lock(&MANAGER);
		if guard.is_some() {
			return Err(Error::State);
		}
		let backend = BackendKind::from_user_config();
		let producer: crate::ticket::Producer = Arc::new(|time, params| {
			eval::render_produced_frame(time, params)
				.map(crate::ticket::TicketPayload::Video)
		});
		let (dispatch, audio_dispatch, audio_fallback): (
			Arc<dyn JobDispatch>,
			Arc<dyn JobDispatch>,
			Option<Arc<dyn JobDispatch>>,
		) = match choice {
			RenderBackendChoice::Threads => {
				// Test-only inline backend: synchronous execution on the
				// calling thread, shared by video and audio.
				let inline = InlineDispatcher::sync();
				(inline.clone(), inline, None)
			}
			RenderBackendChoice::Processes(config) => {
				let dispatcher = ProcessDispatcher::new(config)?;
				dispatcher.start()?;
				// M15 S3: audio rendering runs in the worker pool too (a
				// plugin crash during an audio render must not take down
				// the main process — design §3.7). The inline dispatcher
				// stays as the fallback when the process dispatcher is
				// unavailable (teardown).
				let fallback = InlineDispatcher::sync();
				(dispatcher.clone(), dispatcher, Some(fallback))
			}
		};
		let tickets = Arc::new(TicketArena::new_with_audio_fallback(
			dispatch.clone(),
			audio_dispatch.clone(),
			audio_fallback,
			producer,
		));
		*guard = Some(Arc::new(RenderManager {
			dispatch,
			audio_dispatch,
			tickets,
			backend,
			requested_backend: backend,
			autocacher: Mutex::new(None),
			aggressive_gc: AtomicBool::new(false),
		}));
		Ok(())
	}

	/// Global access; `None` before init.
	pub fn global() -> Option<Arc<RenderManager>> {
		lock(&MANAGER).clone()
	}

	/// The auto-cacher, creating it on first access (C++ `get_cacher`).
	pub fn get_cacher(&self) -> MutexGuard<'_, Option<PreviewAutoCacher>> {
		let mut guard = lock(&self.autocacher);
		if guard.is_none() {
			*guard = Some(PreviewAutoCacher::new(self.tickets.clone()));
		}
		guard
	}

	/// Shut down: cancel tickets, drain both dispatch backends.
	pub fn shutdown() {
		let manager = lock(&MANAGER).take();
		if let Some(manager) = manager {
			manager.tickets.cancel_all();
			// Drain after the cancels so queued completions fire. Both
			// dispatches are idempotent.
			manager.dispatch.shutdown();
			manager.audio_dispatch.shutdown();
			drop(manager);
		}
	}

	/// Pump the video backend's control plane (M15 S2): the process
	/// dispatcher delivers ticket completions from its poll loop, so the
	/// UI tick and any blocking wait must call this regularly. No-op on
	/// backends that deliver inline.
	pub fn poll(&self) {
		self.dispatch.poll();
	}

	/// Release a consumed shm frame's slot back to its worker (M15 S2
	/// zero-copy onscreen path: slot release = cache eviction). No-op on
	/// backends that hold no slots.
	pub fn release_frame(&self, frame: &ShmFrameRef) {
		self.dispatch.release_frame(frame);
	}

	/// Release a consumed shm audio frame's slot (M15 S3; see
	/// [`RenderManager::release_frame`]).
	pub fn release_audio_frame(&self, frame: &ShmAudioRef) {
		self.dispatch.release_audio_frame(frame);
	}

	/// Cancel every pending AND claimed frame of `sequence` (M15 S2
	/// preview-window invalidation); their completions fire with
	/// `Error::State`. No-op on backends that schedule no window.
	pub fn cancel_preview_sequence(&self, sequence: u64) {
		self.dispatch.cancel_preview_sequence(sequence);
	}

	/// Aggressive-GC toggle (C++ `SetAggressiveGarbageCollection`).
	pub fn set_aggressive_gc(&self, on: bool) {
		self.aggressive_gc.store(on, Ordering::Release);
	}

	/// The aggressive-GC toggle.
	pub fn aggressive_gc(&self) -> bool {
		self.aggressive_gc.load(Ordering::Acquire)
	}

	/// Submit a video ticket through the manager's arena (used by the
	/// auto-cacher and the FFI request path).
	pub fn submit_video(
		&self,
		params: crate::ticket::VideoTicketParams,
		done: crate::ticket::Completion,
	) -> TicketId {
		self.tickets.submit_video(params, done)
	}
}

/// The default disk cache directory (C++ `DiskManager::
/// get_default_disk_cache_path`); shared with oaknode via oakcommon
/// (single-lib unification).
pub fn disk_cache_path() -> String {
	oak_common::filefunctions::default_disk_cache_path()
}

/// Bytes consumed by the default disk cache folder (direct filesystem
/// scan; the C++ DiskManager index is replaced by the folder walk).
pub fn disk_cache_size() -> Result<i64> {
	let path = disk_cache_path();
	let root = std::path::Path::new(&path);
	if !root.exists() {
		return Ok(0);
	}
	let mut total: i64 = 0;
	for entry in walk(root) {
		total = total.saturating_add(entry.metadata().map(|m| m.len() as i64).unwrap_or(0));
	}
	Ok(total)
}

/// Clear the default disk cache folder (C++ `DiskManager::
/// clear_disk_cache`).
pub fn disk_cache_clear() -> Result<()> {
	let path = disk_cache_path();
	let root = std::path::Path::new(&path);
	if root.exists() {
		std::fs::remove_dir_all(root)
			.map_err(|e| Error::Failed(format!("clear disk cache: {e}")))?;
	}
	std::fs::create_dir_all(root)
		.map_err(|e| Error::Failed(format!("recreate disk cache: {e}")))?;
	Ok(())
}

/// Recursively walk a directory (files only).
fn walk(dir: &std::path::Path) -> Vec<std::path::PathBuf> {
	let mut out = Vec::new();
	if let Ok(rd) = std::fs::read_dir(dir) {
		for entry in rd.flatten() {
			let path = entry.path();
			if path.is_dir() {
				out.extend(walk(&path));
			} else {
				out.push(path);
			}
		}
	}
	out
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::{Mutex, MutexGuard};

	/// Serializes the manager-singleton tests (the singleton is global).
	static MANAGER_TEST_LOCK: Mutex<()> = Mutex::new(());

	fn manager_lock() -> MutexGuard<'static, ()> {
		MANAGER_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner())
	}

	#[test]
	fn init_shutdown_roundtrip() {
		let _lock = manager_lock();
		// Ensure a clean slate. The manager tests use the test-only inline
		// backend (the process backend spawns real oak-worker children).
		RenderManager::shutdown();
		RenderManager::init_with_backend(RenderBackendChoice::Threads).unwrap();
		assert!(RenderManager::global().is_some());
		// Idempotence: second init is a state error.
		assert_eq!(
			RenderManager::init_with_backend(RenderBackendChoice::Threads)
				.unwrap_err()
				.code(),
			Error::State.code()
		);
		RenderManager::shutdown();
		assert!(RenderManager::global().is_none());
		// Re-init works after shutdown (C++ destroy_instance semantics).
		RenderManager::init_with_backend(RenderBackendChoice::Threads).unwrap();
		RenderManager::shutdown();
	}

	#[test]
	fn aggressive_gc_toggle() {
		let _lock = manager_lock();
		RenderManager::shutdown();
		RenderManager::init_with_backend(RenderBackendChoice::Threads).unwrap();
		let m = RenderManager::global().unwrap();
		assert!(!m.aggressive_gc());
		m.set_aggressive_gc(true);
		assert!(m.aggressive_gc());
		RenderManager::shutdown();
	}

	#[test]
	fn cacher_is_lazily_created() {
		let _lock = manager_lock();
		RenderManager::shutdown();
		RenderManager::init_with_backend(RenderBackendChoice::Threads).unwrap();
		let m = RenderManager::global().unwrap();
		{
			let g = m.get_cacher();
			assert!(g.is_some());
		}
		RenderManager::shutdown();
	}

	#[test]
	fn disk_cache_size_and_clear() {
		let _guard = crate::commonutil::ENV_TEST_LOCK
			.lock()
			.unwrap_or_else(|e| e.into_inner());
		let dir = std::env::temp_dir().join("oakrender-diskcache-test");
		std::env::set_var("OAK_CONFIG_DIR", &dir);
		std::fs::create_dir_all(dir.join("mediacache").join("sub")).unwrap();
		std::fs::write(dir.join("mediacache").join("sub").join("a.bin"), [1u8; 100]).unwrap();
		assert_eq!(disk_cache_size().unwrap(), 100);
		disk_cache_clear().unwrap();
		assert_eq!(disk_cache_size().unwrap(), 0);
		assert!(dir.join("mediacache").exists());
		std::fs::remove_dir_all(&dir).ok();
		std::env::remove_var("OAK_CONFIG_DIR");
	}

	#[test]
	fn disk_cache_size_missing_dir_is_zero() {
		let _guard = crate::commonutil::ENV_TEST_LOCK
			.lock()
			.unwrap_or_else(|e| e.into_inner());
		let dir = std::env::temp_dir().join("oakrender-diskcache-missing");
		std::env::set_var("OAK_CONFIG_DIR", &dir);
		let _ = std::fs::remove_dir_all(&dir);
		assert_eq!(disk_cache_size().unwrap(), 0);
		std::env::remove_var("OAK_CONFIG_DIR");
	}
}
