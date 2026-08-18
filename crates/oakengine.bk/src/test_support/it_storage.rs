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

//! D2 integration tests: the facade's live write-through (plan M13 §2/§3).
//!
//! End-to-end against real SQLite library files in temp directories,
//! driving the facade exactly like the app: `oakengine_project_new` binds
//! the project, undoable edits (`oakengine_project_add_node`) write
//! through on push, and the journal is verified directly with raw sea-orm
//! reads (the same pattern as the oakstorage database tests) plus fresh
//! oakstorage sessions for cross-process recovery.
//!
//! Coverage: write-through lands journal rows without any flush; a
//! kill-9-style session (no cleanup, a fresh session reading the same
//! file) recovers the last command; undo history persists across sessions
//! (`load_at`); the background snapshot thread writes and prunes
//! snapshots and the exit flush drains; multiple bound projects keep
//! their rows apart; and the graceful-degradation paths (backend off /
//! unwritable library) record `last_error` without breaking the undo
//! stack.
//!
//! Every test holds the shared undo-stack lock (the facade's stack is
//! process-wide, same as the it_undo family) and the storage-config lock
//! (the config store is process-global too), so the suite never races on
//! either singleton.

use std::path::{Path, PathBuf};
use std::time::Duration;

use sea_orm::entity::prelude::*;
use sea_orm::QueryOrder;
use oakstorage::backend::StorageBackend;
use oakstorage::backends::database::entities::{journal, project, snapshot};
use oakstorage::backends::database::DatabaseBackend;
use oakstorage::error::OAKSTORAGE_OK;
use oakstorage::handle::CHandle;
use oakstorage::nodeutil::project_arc;
use oakstorage::uri::StorageUri;

use super::common;
use super::it_undo::GLOBAL_STACK_LOCK;

use crate::handle::OakEngineProject;

/// The node type the tests add (a real graph node with an input the
/// serializer round-trips).
const MATH: &str = "org.olivevideoeditor.Olive.math";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Serialize a storage test: hold the process-global undo-stack lock AND
/// the storage-config lock for the whole body, then point the write-through
/// backend at a temp library.
fn with_storage<R>(db: &Path, interval: i32, f: impl FnOnce() -> R) -> R {
	let _stack = GLOBAL_STACK_LOCK.lock();
	let _config = common::STORAGE_CONFIG_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let store = oakcommon::configstore::ConfigStore::instance();
	store.set(Some("Storage"), "Backend", "sqlite");
	store.set(Some("Storage"), "SqlitePath", &db.to_string_lossy());
	store.set_int(Some("Storage"), "SnapshotIntervalSec", interval);
	f()
}

/// Serialize a storage test with the backend disabled (`Storage/Backend =
/// "off"`): projects bind to nothing and the undo stack stays untouched by
/// write-throughs.
fn with_storage_off<R>(f: impl FnOnce() -> R) -> R {
	let _stack = GLOBAL_STACK_LOCK.lock();
	let _config = common::STORAGE_CONFIG_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	oakcommon::configstore::ConfigStore::instance().set(Some("Storage"), "Backend", "off");
	f()
}

/// A fresh, unique temp directory for one test.
fn temp_dir(tag: &str) -> PathBuf {
	let dir =
		std::env::temp_dir().join(format!("oakengine_storage_{}_{}", std::process::id(), tag));
	let _ = std::fs::remove_dir_all(&dir);
	std::fs::create_dir_all(&dir).unwrap();
	dir
}

/// `oakdb+sqlite:///…` uri for a database file.
fn db_uri(path: &Path) -> String {
	format!("oakdb+sqlite://{}", path.display())
}

/// `…?project=<uuid>` uri selecting one library row.
fn project_uri(db: &str, uuid: &str) -> String {
	format!("{db}?project={uuid}")
}

/// Release an owned handle (refcount 1).
fn release(h: CHandle) {
	if let Some(release) = h.release {
		unsafe { release(h.ctx) };
	}
}

/// Create a project through the facade and initialize it (bind + active).
fn new_project() -> *mut OakEngineProject {
	let project = unsafe { crate::node::oakengine_project_create() };
	assert!(!project.is_null());
	assert_eq!(unsafe { crate::node::oakengine_project_new(project) }, 0);
	project
}

/// The project's uuid (from its in-memory payload).
fn project_uuid(project: *mut OakEngineProject) -> String {
	let h = unsafe { crate::handle::unbox(project) }.expect("project handle");
	let arc = unsafe { crate::handle::domain::project_of(&h) }.expect("project payload");
	let guard = arc.lock().unwrap_or_else(|e| e.into_inner());
	guard.uuid.clone()
}

/// Add a math node (an undoable command; pushes and write-throughs).
fn add_math_node(project: *mut OakEngineProject) -> *mut crate::handle::OakEngineNode {
	unsafe {
		crate::node::oakengine_project_add_node(
			project,
			c"org.olivevideoeditor.Olive.math".as_ptr(),
		)
	}
}

/// Load the head state through a *fresh* database backend (a new
/// connection pool = a new session, as after a process restart).
fn load_head(uri: &str) -> std::sync::Arc<std::sync::Mutex<oaknode::project::Project>> {
	let parsed = StorageUri::parse(uri).unwrap();
	let result = DatabaseBackend::new().load(&parsed).unwrap();
	assert_eq!(result.version_info, OAKSTORAGE_OK);
	let handle = result.project;
	let loaded = unsafe { project_arc(&handle) }.unwrap();
	release(handle);
	loaded
}

/// Load the state at `seq` through a fresh database backend.
fn load_at(uri: &str, uuid: &str, seq: i64) -> std::sync::Arc<std::sync::Mutex<oaknode::project::Project>> {
	let parsed = StorageUri::parse(uri).unwrap();
	let handle = DatabaseBackend::new()
		.load_at(&parsed, uuid, seq)
		.unwrap();
	let loaded = unsafe { project_arc(&handle) }.unwrap();
	release(handle);
	loaded
}

/// Count the math nodes of a loaded project (the root folder is slot 0).
fn math_count(p: &oaknode::project::Project) -> usize {
	p.graph
		.node_ids()
		.into_iter()
		.filter(|id| {
			p.graph
				.get(*id)
				.map(|e| e.behavior.type_id() == MATH)
				.unwrap_or(false)
		})
		.count()
}

/// Open a raw sea-orm connection to the library file and drive one future
/// against it on a private current-thread runtime (inspection behind the
/// backend's back — same pattern as the oakstorage database tests).
fn inspect_db<R, Fut>(path: &Path, f: impl FnOnce(sea_orm::DatabaseConnection) -> Fut) -> R
where
	Fut: std::future::Future<Output = R>,
{
	let rt = tokio::runtime::Builder::new_current_thread()
		.enable_all()
		.build()
		.unwrap();
	rt.block_on(async {
		let options = sea_orm::sqlx::sqlite::SqliteConnectOptions::new()
			.filename(path)
			.create_if_missing(true)
			.journal_mode(sea_orm::sqlx::sqlite::SqliteJournalMode::Wal)
			.busy_timeout(Duration::from_secs(5))
			.foreign_keys(true);
		let pool = sea_orm::sqlx::sqlite::SqlitePoolOptions::new()
			.max_connections(1)
			.connect_with(options)
			.await
			.unwrap();
		f(sea_orm::DatabaseConnection::from(pool)).await
	})
}

/// The `(command_seq, journal rows)` of a library row.
fn journal_rows(
	path: &Path,
	uuid: &str,
) -> (i64, Vec<oakstorage::backends::database::entities::journal::Model>) {
	let uuid = uuid.to_string();
	inspect_db(path, move |conn| async move {
		let proj = project::Entity::find()
			.filter(project::Column::Uuid.eq(&uuid))
			.one(&conn)
			.await
			.unwrap()
			.expect("library row exists");
		let rows = journal::Entity::find()
			.filter(journal::Column::ProjectId.eq(proj.id))
			.order_by_asc(journal::Column::Seq)
			.order_by_asc(journal::Column::NodeIdentity)
			.all(&conn)
			.await
			.unwrap();
		(proj.command_seq, rows)
	})
}

/// The snapshot seqs of a library row (newest first).
fn snapshot_seqs(path: &Path, uuid: &str) -> Vec<i64> {
	let uuid = uuid.to_string();
	inspect_db(path, move |conn| async move {
		let proj = project::Entity::find()
			.filter(project::Column::Uuid.eq(&uuid))
			.one(&conn)
			.await
			.unwrap()
			.expect("library row exists");
		snapshot::Entity::find()
			.filter(snapshot::Column::ProjectId.eq(proj.id))
			.order_by_desc(snapshot::Column::CommandSeq)
			.all(&conn)
			.await
			.unwrap()
			.into_iter()
			.map(|s| s.command_seq)
			.collect()
	})
}

// ---------------------------------------------------------------------------
// Write-through
// ---------------------------------------------------------------------------

/// Every undoable edit lands in the journal synchronously (no flush): the
/// first edit is the import command, later edits are diffs.
#[test]
fn write_through_persists_commands() {
	common::force_link();
	let dir = temp_dir("wt");
	let db = dir.join("lib.db");
	with_storage(&db, 600, || {
		let project = new_project();
		let node1 = add_math_node(project);
		assert!(!node1.is_null());
		let node2 = add_math_node(project);
		assert!(!node2.is_null());
		let uuid = project_uuid(project);

		let (head, rows) = journal_rows(&db, &uuid);
		assert_eq!(head, 2, "two commands written through");
		// Seq 1 (import): the root folder + the first math node + the
		// settings pseudo-node, all with only after-images.
		let seq1: Vec<_> = rows.iter().filter(|r| r.seq == 1).collect();
		assert_eq!(seq1.len(), 3, "root + first math node + settings row");
		assert!(seq1.iter().all(|r| r.kind == "import"));
		assert!(seq1.iter().all(|r| r.old_xml.is_none() && r.new_xml.is_some()));
		// Seq 2 (redo diff): exactly the second math node.
		let seq2: Vec<_> = rows.iter().filter(|r| r.seq == 2).collect();
		assert_eq!(seq2.len(), 1, "one changed node in the diff");
		assert_eq!(seq2[0].kind, "redo");
		assert!(seq2[0].new_xml.as_deref().unwrap().contains(MATH));

		// The head state reads back through a fresh session (3 nodes: root
		// + two math nodes), and the import point has exactly one math node.
		let uri = db_uri(&db);
		let head_loaded = load_head(&project_uri(&uri, &uuid));
		let guard = head_loaded.lock().unwrap();
		assert_eq!(guard.graph.node_count(), 3);
		assert_eq!(math_count(&guard), 2);
		drop(guard);
		let at1 = load_at(&uri, &uuid, 1);
		let guard = at1.lock().unwrap();
		assert_eq!(guard.graph.node_count(), 2);
		assert_eq!(math_count(&guard), 1);

		unsafe { crate::node::oakengine_project_free(project) };
	});
	let _ = std::fs::remove_dir_all(&dir);
}

/// kill -9 recovery: the write-through is synchronous, so a fresh session
/// reading the same file (no flush, no cleanup) sees the state after the
/// LAST command.
#[test]
fn kill_nine_recovers_last_command() {
	common::force_link();
	let dir = temp_dir("k9");
	let db = dir.join("lib.db");
	with_storage(&db, 600, || {
		let project = new_project();
		for _ in 0..3 {
			let node = add_math_node(project);
			assert!(!node.is_null());
		}
		let uuid = project_uuid(project);

		// NOTE: the project is intentionally NOT freed — the process "dies"
		// here. The journal already holds every command.
		let uri = db_uri(&db);
		let (head, rows) = journal_rows(&db, &uuid);
		assert_eq!(head, 3);
		assert_eq!(rows.len(), 5, "3 import rows + 2 diff rows");

		let loaded = load_head(&project_uri(&uri, &uuid));
		let guard = loaded.lock().unwrap();
		assert_eq!(guard.graph.node_count(), 4, "root + three math nodes");
		assert_eq!(math_count(&guard), 3);

		// Cleanup without flush would leave the file behind; free the
		// project so the temp dir can be removed.
		unsafe { crate::node::oakengine_project_free(project) };
	});
	let _ = std::fs::remove_dir_all(&dir);
}

// ---------------------------------------------------------------------------
// Undo across sessions
// ---------------------------------------------------------------------------

/// The journal is the persistent undo history: three commands, one undo,
/// then a NEW session's `load_at(2)` reproduces the post-command-2 state
/// while `load_at(3)` still yields the pre-undo (post-command-3) state.
///
/// The undoable operations are label renames: their undo closure captures
/// `(project, id)` and reliably reverts, unlike the add-node command
/// (whose factory handle is released at push — a facade limitation).
#[test]
fn undo_history_crosses_sessions() {
	common::force_link();
	let dir = temp_dir("ux");
	let db = dir.join("lib.db");
	with_storage(&db, 600, || {
		let project = new_project();
		// Command 1: add a math node (import).
		let node = add_math_node(project);
		assert!(!node.is_null());
		// Command 2: rename to "Alpha".
		assert_eq!(
			unsafe { crate::node::oakengine_node_set_label(node, c"Alpha".as_ptr()) },
			0
		);
		// Command 3: rename to "Beta".
		assert_eq!(
			unsafe { crate::node::oakengine_node_set_label(node, c"Beta".as_ptr()) },
			0
		);
		let uuid = project_uuid(project);

		// Undo one command through the facade (jump + write-through).
		assert_eq!(unsafe { crate::node::oakengine_project_undo(project) }, 0);

		let uri = db_uri(&db);
		let (head, _) = journal_rows(&db, &uuid);
		assert_eq!(head, 4, "the undo itself is a written command");

		// A new session at seq 2 = the state right after command 2.
		let at2 = load_at(&uri, &uuid, 2);
		assert_eq!(math_label(&at2), "Alpha");
		// At seq 3 the pre-undo state (after command 3) is intact.
		let at3 = load_at(&uri, &uuid, 3);
		assert_eq!(math_label(&at3), "Beta");
		// The head (default load) matches the undone state.
		let head_loaded = load_head(&project_uri(&uri, &uuid));
		assert_eq!(math_label(&head_loaded), "Alpha");

		unsafe { crate::node::oakengine_project_free(project) };
	});
	let _ = std::fs::remove_dir_all(&dir);
}

/// The label of the first math node of a loaded project (its renames are
/// the undoable operations of [`undo_history_crosses_sessions`]).
fn math_label(arc: &std::sync::Arc<std::sync::Mutex<oaknode::project::Project>>) -> String {
	let guard = arc.lock().unwrap();
	let id = guard
		.graph
		.node_ids()
		.into_iter()
		.find(|id| {
			guard
				.graph
				.get(*id)
				.map(|e| e.behavior.type_id() == MATH)
				.unwrap_or(false)
		})
		.expect("a math node is present");
	guard.graph.get(id).unwrap().core.label.clone()
}

// ---------------------------------------------------------------------------
// Snapshot thread
// ---------------------------------------------------------------------------

/// The background snapshot thread writes periodic snapshots of dirty
/// projects (short interval), the backend prunes to the newest three, and
/// the exit flush drains and leaves a snapshot behind.
#[test]
fn snapshot_thread_and_exit_flush() {
	common::force_link();
	let dir = temp_dir("snap");
	let db = dir.join("lib.db");
	with_storage(&db, 1, || {
		let project = new_project();
		let uuid = project_uuid(project);

		// Four command batches ~1.1 s apart (the 1 s interval): each batch
		// is a rapid pair of writes so the save-time snapshot policy stays
		// quiet and the THREAD is the one capturing the new head.
		for _ in 0..4 {
			let a = add_math_node(project);
			assert!(!a.is_null());
			let b = add_math_node(project);
			assert!(!b.is_null());
			std::thread::sleep(Duration::from_millis(1100));
		}
		// One more tick window so the final batch is snapshotted too.
		std::thread::sleep(Duration::from_millis(1500));

		let seqs = snapshot_seqs(&db, &uuid);
		assert!(!seqs.is_empty(), "the thread produced snapshots");
		assert!(
			seqs.len() <= 3,
			"pruning keeps at most three (got {:?})",
			seqs
		);
		let (head, _) = journal_rows(&db, &uuid);
		assert_eq!(head, 8, "eight commands written through");

		// Exit flush: drains (save + snapshot) and leaves the snapshot at
		// the head seq regardless of thread timing.
		assert_eq!(crate::storage::oakengine_storage_flush(), 0);
		let seqs = snapshot_seqs(&db, &uuid);
		assert!(!seqs.is_empty(), "flush leaves a snapshot behind");
		assert_eq!(seqs[0], 8, "the newest snapshot covers the head seq");

		unsafe { crate::node::oakengine_project_free(project) };
	});
	let _ = std::fs::remove_dir_all(&dir);
}

/// A dirty project flushed on close (project_free) gets its head snapshot
/// even with a long (default) interval — the flush, not the thread, is the
/// guaranteed drain.
#[test]
fn close_flushes_snapshot() {
	common::force_link();
	let dir = temp_dir("flush");
	let db = dir.join("lib.db");
	with_storage(&db, 600, || {
		let project = new_project();
		let uuid = project_uuid(project);
		let node = add_math_node(project);
		assert!(!node.is_null());
		let node2 = add_math_node(project);
		assert!(!node2.is_null());

		// With the 600 s interval only the FIRST save snapshotted (the
		// backend's policy fires when no snapshot exists yet), so the head
		// seq is not covered.
		assert_eq!(snapshot_seqs(&db, &uuid), vec![1]);

		// Closing the project flushes: write-through + snapshot at the head.
		unsafe { crate::node::oakengine_project_free(project) };

		let seqs = snapshot_seqs(&db, &uuid);
		assert_eq!(seqs[0], 2, "close flushed a snapshot at the head seq");
	});
	let _ = std::fs::remove_dir_all(&dir);
}

// ---------------------------------------------------------------------------
// Multi-project bindings
// ---------------------------------------------------------------------------

/// Two projects bound to one library keep their rows apart: each project's
/// writes advance only its own journal.
#[test]
fn multi_project_bindings_do_not_cross() {
	common::force_link();
	let dir = temp_dir("multi");
	let db = dir.join("lib.db");
	with_storage(&db, 600, || {
		// Project A: two commands.
		let a = new_project();
		for _ in 0..2 {
			let node = add_math_node(a);
			assert!(!node.is_null());
		}
		let uuid_a = project_uuid(a);

		// Project B: binding it does not disturb A's row; its write goes
		// only to B's row (A's saves during B's commands are no-ops).
		let b = new_project();
		let node = add_math_node(b);
		assert!(!node.is_null());
		let uuid_b = project_uuid(b);
		assert_ne!(uuid_a, uuid_b);

		let (head_a, rows_a) = journal_rows(&db, &uuid_a);
		assert_eq!(head_a, 2, "A's journal stops at its own second command");
		assert!(rows_a.iter().all(|r| r.seq <= 2));
		let (head_b, rows_b) = journal_rows(&db, &uuid_b);
		assert_eq!(head_b, 1, "B has exactly its one command");
		assert!(rows_b.iter().all(|r| r.seq == 1));

		// Both projects load correctly through fresh sessions.
		let uri = db_uri(&db);
		let loaded_a = load_head(&project_uri(&uri, &uuid_a));
		let guard = loaded_a.lock().unwrap();
		assert_eq!(math_count(&guard), 2);
		drop(guard);
		let loaded_b = load_head(&project_uri(&uri, &uuid_b));
		let guard = loaded_b.lock().unwrap();
		assert_eq!(math_count(&guard), 1);

		unsafe { crate::node::oakengine_project_free(a) };
		unsafe { crate::node::oakengine_project_free(b) };
	});
	let _ = std::fs::remove_dir_all(&dir);
}

// ---------------------------------------------------------------------------
// Graceful degradation
// ---------------------------------------------------------------------------

/// With `Storage/Backend = "off"` projects bind to nothing: the undo
/// stack works, no library file is created, and `is_bound` reports 0.
#[test]
fn backend_off_keeps_projects_unbound() {
	common::force_link();
	let dir = temp_dir("off");
	let db = dir.join("lib.db");
	with_storage_off(|| {
		let project = new_project();
		// Not bound: no write-through happens at all.
		assert_eq!(unsafe { crate::storage::oakengine_storage_is_bound(project) }, 0);

		let node = add_math_node(project);
		assert!(!node.is_null());
		let node2 = add_math_node(project);
		assert!(!node2.is_null());

		// The undo stack still works.
		assert_eq!(unsafe { crate::node::oakengine_project_undo(project) }, 0);
		assert_eq!(unsafe { crate::node::oakengine_project_redo(project) }, 0);

		// Nothing was ever written to the configured library path.
		assert!(!db.exists(), "no library file with the backend off");

		unsafe { crate::node::oakengine_project_free(project) };
	});
	let _ = std::fs::remove_dir_all(&dir);
}

/// An unwritable library degrades gracefully: the write-through records a
/// `last_error` instead of failing the command or crashing, and the undo
/// stack keeps working.
#[test]
fn unwritable_library_records_last_error() {
	common::force_link();
	let dir = temp_dir("ro");
	let db = dir.join("no/such/dir/lib.db"); // parent dir does not exist
	with_storage(&db, 600, || {
		let project = new_project();
		// Bound, but the first write-through cannot open the library.
		assert_eq!(unsafe { crate::storage::oakengine_storage_is_bound(project) }, 1);

		let node = add_math_node(project);
		assert!(!node.is_null());

		// The command succeeded; the write failure is recorded, not raised.
		let mut buf = [0 as std::ffi::c_char; 512];
		let len = unsafe { crate::storage::oakengine_storage_last_error(project, buf.as_mut_ptr(), 512) };
		assert!(len > 0, "the failed write-through is recorded");
		let msg = unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_string_lossy()
			.into_owned();
		assert!(!msg.is_empty(), "error message non-empty");

		// A second command degrades the same way.
		let node2 = add_math_node(project);
		assert!(!node2.is_null());
		let len =
			unsafe { crate::storage::oakengine_storage_last_error(project, buf.as_mut_ptr(), 512) };
		assert!(len > 0);

		// The undo stack is unaffected.
		assert_eq!(unsafe { crate::node::oakengine_project_undo(project) }, 0);

		unsafe { crate::node::oakengine_project_free(project) };
	});
	let _ = std::fs::remove_dir_all(&dir);
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

/// The default library path resolves to `<system data dir>/library.db`
/// (absolute), and the backend is strictly config-driven: `Backend =
/// "sqlite"` enables it, `"off"` disables it (and an absent `Storage`
/// group means "no library configured" — projects stay unbound).
#[test]
fn default_library_path_and_backend() {
	common::force_link();
	let _stack = GLOBAL_STACK_LOCK.lock();
	let _config = common::STORAGE_CONFIG_LOCK.lock().unwrap_or_else(|e| e.into_inner());

	// The default path is a plain absolute `…/library.db`.
	let p = crate::storage::default_library_path();
	let path = std::path::Path::new(&p);
	assert!(path.is_absolute(), "{p}");
	assert!(p.ends_with("library.db"), "{p}");

	// Explicit values drive the enabled state.
	let store = oakcommon::configstore::ConfigStore::instance();
	store.set(Some("Storage"), "Backend", "sqlite");
	assert!(crate::storage::storage_enabled());
	store.set(Some("Storage"), "Backend", "off");
	assert!(!crate::storage::storage_enabled());
}
