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

//! Shared helpers for the integration tests.

use std::sync::{Mutex, MutexGuard};

/// Serializes tests that initialize the process-wide RenderManager
/// singleton (it is process-global; parallel tests must not race it).
static MANAGER_LOCK: Mutex<()> = Mutex::new(());

/// A held manager lock: initializes the manager on construction and
/// shuts it down on drop. Every test that touches the manager singleton
/// must hold this guard for its whole body.
pub struct ManagerGuard {
	_guard: MutexGuard<'static, ()>,
}

impl ManagerGuard {
	/// Initialize the manager and hold the serialization lock.
	pub fn init() -> Self {
		let guard = MANAGER_LOCK.lock().unwrap_or_else(|e| e.into_inner());
		oakrender::manager::RenderManager::shutdown();
		oakrender::manager::RenderManager::init().expect("manager init");
		Self { _guard: guard }
	}
}

impl Drop for ManagerGuard {
	fn drop(&mut self) {
		oakrender::manager::RenderManager::shutdown();
	}
}

/// A non-null fake handle (ctx only — the ABI functions that accept
/// borrowed handles only check `ctx` in this pass).
pub fn fake_handle(seed: usize) -> oakrender::handle::CHandle {
	oakrender::handle::CHandle {
		ctx: seed as *mut std::ffi::c_void,
		addref: None,
		release: None,
		abi_version: oakrender::handle::OAKRENDER_ABI_VERSION,
	}
}

/// Pins `OAK_CONFIG_DIR` to a fresh temp directory for the duration of
/// the guard, keeping cache state writes out of the real media cache.
/// Serializes against other env-mutating tests via the crate's env lock.
pub struct CacheDirGuard {
	old: Option<std::ffi::OsString>,
	dir: std::path::PathBuf,
	_env: MutexGuard<'static, ()>,
}

impl CacheDirGuard {
	/// Set up a temp cache dir and return the guard (plus the dir).
	pub fn new() -> Self {
		let env = oakrender::bridge::common::ENV_TEST_LOCK
			.lock()
			.unwrap_or_else(|e| e.into_inner());
		let old = std::env::var_os("OAK_CONFIG_DIR");
		let dir = std::env::temp_dir().join(format!("oakrender-it-{}", std::process::id()));
		let _ = std::fs::remove_dir_all(&dir);
		std::fs::create_dir_all(&dir).unwrap();
		std::env::set_var("OAK_CONFIG_DIR", &dir);
		Self { old, dir, _env: env }
	}

	/// The temp directory.
	pub fn dir(&self) -> &std::path::Path {
		&self.dir
	}
}

impl Drop for CacheDirGuard {
	fn drop(&mut self) {
		std::env::remove_var("OAK_CONFIG_DIR");
		if let Some(old) = self.old.take() {
			std::env::set_var("OAK_CONFIG_DIR", old);
		}
		let _ = std::fs::remove_dir_all(&self.dir);
	}
}

/// C string buffer readback helper for two-stage getters.
pub fn read_two_stage(getter: impl Fn(*mut std::ffi::c_char, i32) -> i32) -> (i32, Option<String>) {
	use std::ffi::c_char;
	unsafe {
		let size = getter(std::ptr::null_mut(), 0);
		if size <= 0 {
			return (size, None);
		}
		let mut buf = vec![0u8; size as usize];
		let got = getter(buf.as_mut_ptr() as *mut c_char, size);
		if got <= 0 {
			return (got, None);
		}
		let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
		(
			got,
			Some(String::from_utf8_lossy(&buf[..end]).into_owned()),
		)
	}
}
