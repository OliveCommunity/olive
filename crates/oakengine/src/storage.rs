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

//! Live write-through to the oakstorage project library — a thin
//! forward to the module's session manager (plan M13 §2/§3; M14 R1:
//! the binding map, snapshot thread and exit flush moved into
//! [`oakstorage::writethrough`]).
//!
//! The write-through itself is subscribed directly to the oakundo
//! process-wide stack's command-success observers (see
//! [`oakstorage::writethrough`]); this module only keeps the frozen
//! `oakengine_storage_*` C ABI exports and the box/buf-size glue, and
//! re-exports the manager entry points the rest of the facade (the
//! library manager, the project family) calls.
//!
//! See [`oakstorage::writethrough`] for the binding model, the config
//! keys (`Storage/Backend`, `Storage/SqlitePath`, `Storage/PgUrl`,
//! `Storage/SnapshotIntervalSec`) and the graceful-degradation rules.

use std::ffi::{c_char, c_int};

use oakstorage::backends::database::DatabaseBackend;

use crate::handle::{guard_int, CHandle, OakEngineProject};

/// Bind `project` to the configured default library (no-op when the
/// backend is disabled, the project has no uuid, or it is already bound).
pub fn bind_project(project: CHandle) {
	oakstorage::writethrough::bind_project(project);
}

/// Flush `project`'s pending writes and drop its binding (closing the
/// project).
pub fn unbind_project(project: CHandle) {
	oakstorage::writethrough::unbind_project(project);
}

/// Whether `project` currently has a binding (status-bar / D5 surface).
pub fn is_bound(project: CHandle) -> bool {
	oakstorage::writethrough::is_bound(project)
}

/// The last write-through / snapshot error of `project` (empty when none
/// or not bound).
pub fn last_error(project: CHandle) -> Option<String> {
	oakstorage::writethrough::last_error(project)
}

/// Persist every bound project after a successful undo-path operation.
/// No-op when nothing is bound (also called by the module's command
/// observer; kept here for the facade's own call sites).
pub fn note_command() {
	oakstorage::writethrough::note_command();
}

/// Whether the write-through backend is enabled (config-driven).
pub(crate) fn storage_enabled() -> bool {
	oakstorage::writethrough::storage_enabled()
}

/// The `oakdb+…` uri of the configured library (None when it cannot be
/// resolved). Shared with the library manager exports.
pub(crate) fn library_uri() -> Option<String> {
	oakstorage::writethrough::library_uri()
}

/// The default library file path (config-driven data directory).
pub(crate) fn default_library_path() -> String {
	oakstorage::writethrough::default_library_path()
}

/// The process-wide oakstorage backend (shared with the library manager
/// exports in [`crate::library`]).
pub(crate) fn backend() -> &'static DatabaseBackend {
	oakstorage::writethrough::backend()
}

/// The exit path: stop the snapshot thread and drain every still-bound
/// project (save + snapshot).
pub fn flush_all() {
	oakstorage::writethrough::flush_all();
}

// ---------------------------------------------------------------------------
// Facade exports
// ---------------------------------------------------------------------------

/// `oakengine_storage_flush` — flush every bound project (write-through +
/// snapshot) and stop the snapshot thread. The app calls this on exit
/// (the facade's shutdown path; the write-through is already per-command,
/// so this only drains the periodic snapshot backlog).
#[no_mangle]
pub extern "C" fn oakengine_storage_flush() -> c_int {
	flush_all();
	crate::error::OAKENGINE_OK
}

/// `oakengine_storage_is_bound` — 1 when `project` is bound to a library
/// session, 0 otherwise (NULL project -> 0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_storage_is_bound(
	project: *mut OakEngineProject,
) -> c_int {
	guard_int(|| unsafe {
		let h = crate::handle::unbox(project)?;
		Ok(is_bound(h) as c_int)
	})
}

/// `oakengine_storage_last_error` — the last write-through / snapshot
/// error of `project` (buf/size convention; empty when none or not
/// bound).
#[no_mangle]
pub unsafe extern "C" fn oakengine_storage_last_error(
	project: *mut OakEngineProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let h = crate::handle::unbox(project)?;
		let msg = last_error(h).unwrap_or_default();
		Ok(crate::handle::write_string(&msg, buf, buf_size))
	})
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn library_uri_resolves_configured_path() {
		use oakcommon::configstore::ConfigStore;
		let store = ConfigStore::instance();
		let _g = crate::tests::common::STORAGE_CONFIG_LOCK
			.lock()
			.unwrap_or_else(|e| e.into_inner());

		// A configured path wins and yields an absolute oakdb+sqlite uri.
		let dir =
			std::env::temp_dir().join(format!("oakengine_storage_uri_{}", std::process::id()));
		let _ = std::fs::create_dir_all(&dir);
		let lib = dir.join("lib.db");
		store.set(Some("Storage"), "Backend", "sqlite");
		store.set(Some("Storage"), "SqlitePath", &lib.to_string_lossy());
		let uri = library_uri().expect("configured path resolves");
		assert!(uri.starts_with("oakdb+sqlite://"), "{uri}");
		assert!(uri.ends_with("lib.db"), "{uri}");
		assert!(storage_enabled());

		// Leave the store in a safe state: backend off (the config store
		// has no remove API, so later unguarded tests see "off" and never
		// bind a project to a library).
		store.set(Some("Storage"), "Backend", "off");
		assert!(!storage_enabled());
		let _ = std::fs::remove_dir_all(&dir);
	}

	#[test]
	fn library_uri_resolves_pg_config() {
		use oakcommon::configstore::ConfigStore;
		let store = ConfigStore::instance();
		let _g = crate::tests::common::STORAGE_CONFIG_LOCK
			.lock()
			.unwrap_or_else(|e| e.into_inner());

		// Backend = "pg" yields an oakdb+pg uri from Storage/PgUrl; a
		// postgres:// scheme on the config value is stripped.
		store.set(Some("Storage"), "Backend", "pg");
		store.set(
			Some("Storage"),
			"PgUrl",
			"postgres://user:pass@host:5432/oak",
		);
		assert!(storage_enabled());
		assert_eq!(
			library_uri().as_deref(),
			Some("oakdb+pg://user:pass@host:5432/oak")
		);

		// postgresql:// is accepted too.
		store.set(
			Some("Storage"),
			"PgUrl",
			"postgresql://u@h/db?sslmode=disable",
		);
		assert_eq!(
			library_uri().as_deref(),
			Some("oakdb+pg://u@h/db?sslmode=disable")
		);

		// Backend = "pg" with no PgUrl = no library (graceful
		// degradation, same as an absent sqlite path).
		store.set(Some("Storage"), "PgUrl", "");
		assert_eq!(library_uri(), None);

		// Leave the store in a safe state.
		store.set(Some("Storage"), "Backend", "off");
		assert!(!storage_enabled());
	}
}
