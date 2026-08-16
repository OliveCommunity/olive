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

//! Live write-through to the oakstorage project library (plan M13 §2/§3).
//!
//! The facade binds every opened project to a database session — a
//! `(library uri, project uuid)` pair — and re-saves it after every
//! successful undo-path operation ([`note_command`], hooked from
//! [`crate::undo`]), so each command's diff lands in the journal
//! transactionally (the oakstorage database backend does the diff itself).
//!
//! ## Binding model
//!
//! The map is keyed by the project handle's `ctx` pointer (the module
//! `RefBox` identity — one per in-memory project instance), so several
//! projects can be bound at once (multi-project, plan §3) without
//! confusing their library rows. Every undo-path operation re-saves ALL
//! bound projects ([`note_command`]): the oakstorage backend diffs each
//! project against its own library head, so untouched projects are
//! no-op touches and only the project the command actually changed
//! advances its journal. (The plan's "current project" phrasing maps to
//! this — the undo stack is cleared on every project switch, so at most
//! one project's graph changes per command; a bound-but-untouched
//! project can gain its import row this way, which reflects its true
//! state.) Closing a project ([`unbind_project`], hooked from
//! `project_free`) flushes its pending writes and drops the binding.
//!
//! The library is selected from the `Storage` config group (all defaults
//! are config-driven, plan §5):
//!
//! - `Storage/Backend` — `"sqlite"` (the documented default value),
//!   `"database"` or `"pg"` enable the write-through; any other value
//!   (e.g. `"off"`) disables it. When the key is absent, no library is
//!   configured: projects stay unbound and the undo path runs without
//!   touching a database (graceful degradation — this is what keeps
//!   headless consumers and the test suite from writing to the user's
//!   default library).
//! - `Storage/SqlitePath` — the SQLite library file; default (used when
//!   storage is enabled) `<system data directory>/library.db` (the same
//!   location `FileFunctions::get_configuration_location` derives,
//!   honoring `OAK_CONFIG_DIR` and portable mode).
//! - `Storage/PgUrl` — the PostgreSQL connection string (plan D3), used
//!   when `Storage/Backend` is `"pg"`: `user:pass@host:5432/dbname`
//!   (libpq URL form; an optional `postgres://`/`postgresql://` scheme is
//!   accepted and stripped). The resolved library URI is
//!   `oakdb+pg://<PgUrl>`. When `Backend = "pg"` but `PgUrl` is absent
//!   or empty, no library is configured (same graceful degradation).
//!
//! ## Snapshot thread and exit flush
//!
//! A background thread re-snapshots every *dirty* binding every
//! `Storage/SnapshotIntervalSec` seconds (default 600; ≤ 0 acts every
//! wake). Snapshots are latest-wins: the backend writes the full payload
//! at the current head seq and prunes to the newest three. The thread is
//! notified on bind and stops on the exit flush. [`flush_all`] (exported
//! as `oakengine_storage_flush`) is the facade's exit path: it stops the
//! thread, then writes through and snapshots every still-bound project
//! (save + snapshot drain). A write-through failure never propagates to
//! the caller — it is recorded in the binding's `last_error`
//! ([`oakengine_storage_last_error`]) and the project keeps working
//! (graceful degradation, plan §5).

use std::collections::HashMap;
use std::ffi::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::{Condvar, Mutex, OnceLock};
use std::time::Duration;

use oakstorage::backend::StorageBackend;
use oakstorage::backends::database::DatabaseBackend;
use oakstorage::uri::StorageUri;

use crate::handle::{guard_int, CHandle, OakEngineProject};

/// One bound project: its session (library uri + row uuid) plus the
/// facade-side write state.
struct Binding {
	/// The project handle (addref'd at bind, released at unbind; keeps
	/// the project alive past the caller's own handle).
	project: CHandle,
	/// Library uri (`oakdb+sqlite:///…`).
	uri: String,
	/// Library row uuid.
	uuid: String,
	/// Whether a write-through happened since the last successful
	/// snapshot (the snapshot thread and the flush drain on this).
	dirty: bool,
	/// Monotonic counter bumped on every write-through; a snapshot only
	/// clears `dirty` when the generation still matches, so a write that
	/// lands while the snapshot runs is not lost.
	write_gen: u64,
	/// Last write-through / snapshot error (graceful degradation).
	last_error: Option<String>,
}

/// The process-wide oakstorage backend (shared by the UI thread's
/// write-throughs, the snapshot thread and the library manager exports in
/// [`crate::library`]; the backend serializes its own operations).
pub(crate) fn backend() -> &'static DatabaseBackend {
	static BACKEND: OnceLock<DatabaseBackend> = OnceLock::new();
	BACKEND.get_or_init(DatabaseBackend::new)
}

/// project identity (handle `ctx` pointer) -> binding.
fn bindings() -> &'static Mutex<HashMap<usize, Binding>> {
	static BINDINGS: OnceLock<Mutex<HashMap<usize, Binding>>> = OnceLock::new();
	BINDINGS.get_or_init(|| Mutex::new(HashMap::new()))
}

/// Snapshot-thread runtime state.
struct SnapshotRuntime {
	/// The background thread (None after the exit flush joined it).
	handle: Option<std::thread::JoinHandle<()>>,
	/// Exit request (set by [`flush_all`]).
	stop: bool,
}

static SNAPSHOT: Mutex<SnapshotRuntime> = Mutex::new(SnapshotRuntime {
	handle: None,
	stop: false,
});

/// Wakes the snapshot thread on bind/flush/stop.
static SNAPSHOT_CV: Condvar = Condvar::new();

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

/// Bind `project` to the configured default library. No-op when the
/// backend is disabled by config, the project cannot be addressed (no
/// uuid), or it is already bound. No database write happens here — the
/// first undo-path operation creates the library row.
pub fn bind_project(project: CHandle) {
	let _ = catch_unwind(AssertUnwindSafe(|| {
		if project.is_null() {
			return;
		}
		let key = project.ctx as usize;
		{
			let g = bindings().lock().unwrap_or_else(|e| e.into_inner());
			if g.contains_key(&key) {
				return; // Already bound (re-bind): nothing to refresh.
			}
		}
		if !storage_enabled() {
			return;
		}
		let Some(uri) = library_uri() else {
			return;
		};
		let uuid = {
			let arc = unsafe { crate::handle::domain::project_of(&project) };
			match arc {
				Some(a) => a.lock().unwrap_or_else(|e| e.into_inner()).uuid.clone(),
				None => return,
			}
		};
		if uuid.is_empty() {
			return;
		}
		// Addref the handle so the binding owns a reference independent of
		// the caller's.
		let mut owned = project;
		if let Some(addref) = owned.addref {
			unsafe { addref(owned.ctx) };
		}
		bindings()
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.insert(
				key,
				Binding {
					project: owned,
					uri,
					uuid,
					dirty: false,
					write_gen: 0,
					last_error: None,
				},
			);
		ensure_thread();
	}));
}

/// Flush `project`'s pending writes and drop its binding (closing the
/// project). No-op when not bound.
pub fn unbind_project(project: CHandle) {
	let _ = catch_unwind(AssertUnwindSafe(|| {
		let key = project.ctx as usize;
		flush_one(key);
		let mut g = bindings().lock().unwrap_or_else(|e| e.into_inner());
		if let Some(b) = g.remove(&key) {
			if let Some(release) = b.project.release {
				unsafe { release(b.project.ctx) };
			}
		}
	}));
}

/// Whether `project` currently has a binding (status-bar / D5 surface).
pub fn is_bound(project: CHandle) -> bool {
	if project.is_null() {
		return false;
	}
	bindings()
		.lock()
		.unwrap_or_else(|e| e.into_inner())
		.contains_key(&(project.ctx as usize))
}

/// The last write-through / snapshot error of `project` (empty when none
/// or not bound).
pub fn last_error(project: CHandle) -> Option<String> {
	if project.is_null() {
		return None;
	}
	bindings()
		.lock()
		.unwrap_or_else(|e| e.into_inner())
		.get(&(project.ctx as usize))
		.and_then(|b| b.last_error.clone())
}

// ---------------------------------------------------------------------------
// Write-through
// ---------------------------------------------------------------------------

/// Persist every bound project after a successful undo-path operation
/// (push / group_end / jump). Hooked from [`crate::undo`]; the oakstorage
/// backend diffs each project against its own head, so unchanged projects
/// are no-op touches. No-op when nothing is bound.
pub fn note_command() {
	let _ = catch_unwind(AssertUnwindSafe(|| {
		let keys: Vec<usize> = {
			let g = bindings().lock().unwrap_or_else(|e| e.into_inner());
			g.keys().copied().collect()
		};
		for key in keys {
			write_through(key);
		}
	}));
}

/// Save the project of `key` through the database backend (the backend
/// diffs and journals internally). Failures are recorded, never returned.
fn write_through(key: usize) {
	let (project, uri, _uuid) = {
		let mut g = bindings().lock().unwrap_or_else(|e| e.into_inner());
		match g.get_mut(&key) {
			Some(b) => (b.project, b.uri.clone(), b.uuid.clone()),
			None => return,
		}
	};
	let parsed = match StorageUri::parse(&uri) {
		Ok(u) => u,
		Err(_) => {
			record_error(key, "invalid storage uri");
			return;
		}
	};
	match backend().save(project, &parsed, 0) {
		Ok(()) => {
			let mut g = bindings().lock().unwrap_or_else(|e| e.into_inner());
			if let Some(b) = g.get_mut(&key) {
				b.dirty = true;
				b.write_gen = b.write_gen.wrapping_add(1);
				b.last_error = None;
			}
		}
		Err(e) => record_error(key, &format!("{e}")),
	}
}

fn record_error(key: usize, message: &str) {
	let mut g = bindings().lock().unwrap_or_else(|e| e.into_inner());
	if let Some(b) = g.get_mut(&key) {
		b.last_error = Some(message.to_string());
	}
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

/// Whether the write-through backend is enabled (config-driven, plan §5):
/// `Storage/Backend` set to `"sqlite"`, `"database"` or `"pg"` enables
/// it; any other explicit value (e.g. `"off"`) disables it. When NO
/// `Storage` configuration is present the backend is NOT enabled — "no
/// library configured" degrades gracefully to plain unbound projects,
/// which keeps headless consumers (the CLI) and the test suite from ever
/// writing to the user's default library. The documented default *values*
/// are `Backend = "sqlite"` and `SqlitePath = <system data dir>/library.db`
/// (used once storage is enabled without an explicit path).
pub(crate) fn storage_enabled() -> bool {
	let store = oakcommon::configstore::ConfigStore::instance();
	match store.get(Some("Storage"), "Backend") {
		Ok(b) => b == "sqlite" || b == "database" || b == "pg",
		Err(_) => false,
	}
}

/// The configured SQLite library path (empty value = the default).
fn configured_sqlite_path() -> Option<String> {
	let store = oakcommon::configstore::ConfigStore::instance();
	match store.get(Some("Storage"), "SqlitePath") {
		Ok(p) if !p.trim().is_empty() => Some(p.trim().to_string()),
		_ => None,
	}
}

/// The configured PostgreSQL connection string (`Storage/PgUrl`; empty
/// value = not configured).
fn configured_pg_url() -> Option<String> {
	let store = oakcommon::configstore::ConfigStore::instance();
	match store.get(Some("Storage"), "PgUrl") {
		Ok(u) if !u.trim().is_empty() => Some(u.trim().to_string()),
		_ => None,
	}
}

/// The `oakdb+pg://…` uri of the configured PostgreSQL library (None
/// when `Storage/PgUrl` is absent). A `postgres://`/`postgresql://`
/// scheme on the config value is stripped — the oakdb uri body is the
/// bare connection string (`user:pass@host:5432/dbname`).
fn pg_library_uri() -> Option<String> {
	let url = configured_pg_url()?;
	let body = url
		.strip_prefix("postgres://")
		.or_else(|| url.strip_prefix("postgresql://"))
		.unwrap_or(&url);
	Some(format!("oakdb+pg://{body}"))
}

/// The default library file: `<system data directory>/library.db`, where
/// the data directory is the standard per-user location
/// (`FileFunctions::get_configuration_location`: macOS Application
/// Support / XDG config, honoring `OAK_CONFIG_DIR` and portable mode).
pub(crate) fn default_library_path() -> String {
	let dir = oakcommon::filefunctions::FileFunctions::new()
		.get_configuration_location()
		.unwrap_or_else(|_| std::env::temp_dir().to_string_lossy().into_owned());
	format!("{}/library.db", dir)
}

/// The `oakdb+…` uri of the configured library (None when the path
/// cannot be made absolute, or the PG url is missing). Shared with the
/// library manager exports in [`crate::library`].
pub(crate) fn library_uri() -> Option<String> {
	let store = oakcommon::configstore::ConfigStore::instance();
	if store.get(Some("Storage"), "Backend").ok().as_deref() == Some("pg") {
		return pg_library_uri();
	}
	let path = match configured_sqlite_path() {
		Some(p) => p,
		None => default_library_path(),
	};
	let abs = std::path::absolute(&path).ok()?;
	Some(format!("oakdb+sqlite://{}", abs.display()))
}

// ---------------------------------------------------------------------------
// Snapshot thread
// ---------------------------------------------------------------------------

/// Spawn the snapshot thread (a no-op when one is already running). The
/// thread is (re)startable after an exit flush.
fn ensure_thread() {
	let mut rt = SNAPSHOT.lock().unwrap_or_else(|e| e.into_inner());
	let alive = rt
		.handle
		.as_ref()
		.map(|h| !h.is_finished())
		.unwrap_or(false);
	if !alive {
		rt.stop = false;
		rt.handle = Some(
			std::thread::Builder::new()
				.name("oak-storage-snapshot".into())
				.spawn(snapshot_loop)
				.expect("snapshot thread spawn"),
		);
	}
}

/// The thread loop: sleep a tick, then snapshot every dirty binding.
/// The condvar makes the sleep interruptible (bind notifications and the
/// exit flush's stop signal wake it immediately).
fn snapshot_loop() {
	loop {
		let interval = snapshot_tick();
		let mut rt = SNAPSHOT.lock().unwrap_or_else(|e| e.into_inner());
		let (guard, _) = SNAPSHOT_CV
			.wait_timeout(rt, interval)
			.unwrap_or_else(|e| e.into_inner());
		rt = guard;
		if rt.stop {
			break;
		}
		drop(rt);
		snapshot_dirty();
	}
}

/// The sleep between snapshot passes: `Storage/SnapshotIntervalSec`
/// seconds (default 600), with ≤ 0 treated as "every wake" and a 100 ms
/// floor so the thread stays responsive to bind/stop signals.
fn snapshot_tick() -> Duration {
	let secs = oakcommon::configstore::ConfigStore::instance()
		.get_int(Some("Storage"), "SnapshotIntervalSec", 600);
	if secs <= 0 {
		Duration::from_millis(100)
	} else {
		Duration::from_secs(secs as u64)
	}
}

/// Snapshot every dirty binding at its head seq (latest-wins: a project
/// edited since the last pass snapshots at the newer head; an unchanged
/// head is a backend no-op).
fn snapshot_dirty() {
	let jobs: Vec<(usize, String, String, u64)> = {
		let g = bindings().lock().unwrap_or_else(|e| e.into_inner());
		g.iter()
			.filter(|(_, b)| b.dirty)
			.map(|(k, b)| (*k, b.uri.clone(), b.uuid.clone(), b.write_gen))
			.collect()
	};
	for (key, uri, uuid, gen) in jobs {
		let res = match StorageUri::parse(&uri) {
			Ok(u) => backend().snapshot(&u, &uuid),
			Err(_) => {
				record_error(key, "invalid storage uri");
				continue;
			}
		};
		match res {
			Ok(()) => {
				let mut g = bindings().lock().unwrap_or_else(|e| e.into_inner());
				if let Some(b) = g.get_mut(&key) {
					// Only clear when no write landed since the snapshot
					// started (the generation counter detects it).
					if b.write_gen == gen {
						b.dirty = false;
					}
				}
			}
			Err(e) => record_error(key, &format!("{e}")),
		}
	}
}

// ---------------------------------------------------------------------------
// Exit flush
// ---------------------------------------------------------------------------

/// The facade exit path: stop the snapshot thread, then save + snapshot
/// every still-bound project (plan §2 "退出前 flush"). Idempotent; safe
/// to call again after new projects are bound (the thread restarts on the
/// next bind).
pub fn flush_all() {
	let _ = catch_unwind(AssertUnwindSafe(|| {
		// 1. Stop the thread and wait for it to finish its pass.
		{
			let mut rt = SNAPSHOT.lock().unwrap_or_else(|e| e.into_inner());
			rt.stop = true;
		}
		SNAPSHOT_CV.notify_all();
		{
			let mut rt = SNAPSHOT.lock().unwrap_or_else(|e| e.into_inner());
			if let Some(h) = rt.handle.take() {
				drop(rt);
				let _ = h.join();
			}
		}
		// 2. Drain every binding (write-through + snapshot).
		let keys: Vec<usize> = {
			let g = bindings().lock().unwrap_or_else(|e| e.into_inner());
			g.keys().copied().collect()
		};
		for key in keys {
			flush_one(key);
		}
	}));
}

/// Save + snapshot one binding when it has pending writes (nothing to do
/// for a clean project — flush must not invent library rows).
fn flush_one(key: usize) {
	let dirty = {
		let g = bindings().lock().unwrap_or_else(|e| e.into_inner());
		g.get(&key).map(|b| b.dirty).unwrap_or(false)
	};
	if !dirty {
		return;
	}
	write_through(key);
	// Snapshot the head the write-through just reached (capture the
	// generation AFTER the write so the drain clears the dirty flag).
	let job = {
		let g = bindings().lock().unwrap_or_else(|e| e.into_inner());
		match g.get(&key) {
			Some(b) => (b.uri.clone(), b.uuid.clone(), b.write_gen),
			None => return,
		}
	};
	let res = match StorageUri::parse(&job.0) {
		Ok(u) => backend().snapshot(&u, &job.1),
		Err(_) => {
			record_error(key, "invalid storage uri");
			return;
		}
	};
	match res {
		Ok(()) => {
			let mut g = bindings().lock().unwrap_or_else(|e| e.into_inner());
			if let Some(b) = g.get_mut(&key) {
				if b.write_gen == job.2 {
					b.dirty = false;
				}
			}
		}
		Err(e) => record_error(key, &format!("{e}")),
	}
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
