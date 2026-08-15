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

//! The render manager: process-wide singleton owning the worker pool,
//! the ticket arena, the auto-cacher, and backend selection
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
use crate::ticket::{TicketArena, TicketId};
use crate::worker::WorkerPool;

static MANAGER: Mutex<Option<Arc<RenderManager>>> = Mutex::new(None);

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}

/// The manager. Created by `oakrender_manager_init` (C ABI), accessed
/// internally through [`RenderManager::global`].
pub struct RenderManager {
	/// Worker pool.
	pub pool: WorkerPool,
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
	/// Initialize the process-wide manager (idempotent; C++ instance()
	/// semantics — only the main GUI process does this).
	pub fn init() -> Result<()> {
		let mut guard = lock(&MANAGER);
		if guard.is_some() {
			return Err(Error::State);
		}
		let backend = BackendKind::from_user_config();
		let mut pool = WorkerPool::new(0);
		pool.start();
		let producer: crate::ticket::Producer = Arc::new(|time, params| {
			eval::render_produced_frame(time, params)
				.map(crate::ticket::TicketPayload::Video)
		});
		let tickets = Arc::new(TicketArena::new(pool.clone(), producer));
		*guard = Some(Arc::new(RenderManager {
			pool,
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

	/// Shut down: cancel tickets, drain pool, release backend.
	pub fn shutdown() {
		let manager = lock(&MANAGER).take();
		if let Some(manager) = manager {
			manager.tickets.cancel_all();
			// Drop the manager (releases the pool clone) after the pool is
			// drained; the drain delivers queued completions.
			let mut pool = manager.pool.clone();
			pool.shutdown();
			drop(manager);
		}
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
	oakcommon::filefunctions::default_disk_cache_path()
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
		// Ensure a clean slate.
		RenderManager::shutdown();
		RenderManager::init().unwrap();
		assert!(RenderManager::global().is_some());
		// Idempotence: second init is a state error.
		assert_eq!(
			RenderManager::init().unwrap_err().code(),
			Error::State.code()
		);
		RenderManager::shutdown();
		assert!(RenderManager::global().is_none());
		// Re-init works after shutdown (C++ destroy_instance semantics).
		RenderManager::init().unwrap();
		RenderManager::shutdown();
	}

	#[test]
	fn aggressive_gc_toggle() {
		let _lock = manager_lock();
		RenderManager::shutdown();
		RenderManager::init().unwrap();
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
		RenderManager::init().unwrap();
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
