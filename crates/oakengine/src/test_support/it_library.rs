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

//! D4 integration tests: the project-library manager C ABI
//! (`src/library.rs`, plan M13 §4).
//!
//! End-to-end against real SQLite library files in temp directories,
//! driving the facade exactly like the app's project manager: create lands
//! a row immediately, list reports it with the derived stats, open
//! (`oakengine_project_load_library`) binds the loaded project to the
//! library session (the next undoable edit write-throughs onto the row's
//! journal), rename / duplicate / delete / import / export round-trip, and
//! the disabled-backend configuration degrades to an empty list + error
//! codes.
//!
//! Every test holds the shared undo-stack lock (the facade's stack is
//! process-wide, same as the it_undo / it_storage families) and the
//! storage-config lock, so the suite never races on either singleton.

use std::ffi::CString;
use std::path::{Path, PathBuf};

use super::common;
use super::it_undo::GLOBAL_STACK_LOCK;

use crate::error::{OAKENGINE_OK, OAKENGINE_E_INVALID, OAKENGINE_E_NOT_FOUND, OAKENGINE_E_STATE};
use crate::handle::OakEngineProject;

/// The math node type id (a factory type the tests add as an undoable
/// edit; footage is not factory-creatable, so the write-through is
/// verified on the journal rows directly).
const MATH: &str = "org.olivevideoeditor.Olive.math";

/// The journal row count of a library row (direct sea-orm read, the same
/// pattern as the it_storage tests).
fn journal_rows(db: &Path, uuid: &str) -> usize {
	use sea_orm::entity::prelude::*;
	use oakstorage::backends::database::entities::{journal, project};

	let runtime = tokio::runtime::Builder::new_current_thread()
		.enable_all()
		.build()
		.unwrap();
	runtime.block_on(async {
		let conn = sea_orm::Database::connect(format!("sqlite://{}?mode=ro", db.display()))
			.await
			.unwrap();
		let model = project::Entity::find()
			.filter(project::Column::Uuid.eq(uuid))
			.one(&conn)
			.await
			.unwrap()
			.expect("the row exists");
		journal::Entity::find()
			.filter(journal::Column::ProjectId.eq(model.id))
			.all(&conn)
			.await
			.unwrap()
			.len()
	})
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Serialize a library test: hold the process-global undo-stack lock AND
/// the storage-config lock for the whole body, then point the library at a
/// temp SQLite file.
fn with_library<R>(db: &Path, f: impl FnOnce() -> R) -> R {
	let _stack = GLOBAL_STACK_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let _config = common::STORAGE_CONFIG_LOCK
		.lock()
		.unwrap_or_else(|e| e.into_inner());
	let store = oakcommon::configstore::ConfigStore::instance();
	store.set(Some("Storage"), "Backend", "sqlite");
	store.set(Some("Storage"), "SqlitePath", &db.to_string_lossy());
	f()
}

/// A fresh, unique temp directory for one test.
fn temp_dir(tag: &str) -> PathBuf {
	let dir =
		std::env::temp_dir().join(format!("oakengine_library_{}_{}", std::process::id(), tag));
	let _ = std::fs::remove_dir_all(&dir);
	std::fs::create_dir_all(&dir).unwrap();
	dir
}

/// Two-stage string read over a facade `(buf, size)` getter.
fn read_string(f: impl Fn(*mut std::ffi::c_char, i32) -> i32) -> String {
	let needed = f(std::ptr::null_mut(), 0);
	if needed <= 0 {
		return String::new();
	}
	let mut buf = vec![0 as std::ffi::c_char; needed as usize + 1];
	f(buf.as_mut_ptr(), needed + 1);
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
		.into_owned()
}

/// The library list JSON.
fn list_json() -> String {
	read_string(|buf, size| unsafe { crate::library::oakengine_library_list(buf, size) })
}

/// Create a library row; returns its uuid. The create export has a side
/// effect, so it is called ONCE with a stack buffer (never two-stage).
fn create(name: &str) -> String {
	let name = CString::new(name).unwrap();
	let mut buf = [0 as std::ffi::c_char; 256];
	let rc = unsafe { crate::library::oakengine_library_create(name.as_ptr(), buf.as_mut_ptr(), 256) };
	assert!(rc > 0, "create {name:?} rc={rc}");
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
		.into_owned()
}

/// Duplicate a library row (single call with a stack buffer, see
/// [`create`]).
fn duplicate(uuid: &str) -> String {
	let uuid = CString::new(uuid).unwrap();
	let mut buf = [0 as std::ffi::c_char; 256];
	let rc = unsafe {
		crate::library::oakengine_library_duplicate(
			uuid.as_ptr(),
			std::ptr::null(),
			buf.as_mut_ptr(),
			256,
		)
	};
	assert!(rc > 0, "duplicate rc={rc}");
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
		.into_owned()
}

/// The uuids in the library list JSON (order preserved).
fn list_uuids(json: &str) -> Vec<String> {
	serde_json::from_str::<serde_json::Value>(json)
		.expect("list is JSON")
		.as_array()
		.expect("list is an array")
		.iter()
		.map(|row| {
			row.get("uuid")
				.and_then(|v| v.as_str())
				.expect("row uuid")
				.to_string()
		})
		.collect()
}

/// One row of the library list JSON by uuid.
fn list_row<'a>(json: &'a str, uuid: &str) -> Option<serde_json::Value> {
	serde_json::from_str::<serde_json::Value>(json)
		.expect("list is JSON")
		.as_array()
		.expect("list is an array")
		.iter()
		.find(|row| row.get("uuid").and_then(|v| v.as_str()) == Some(uuid))
		.cloned()
}

/// Open a library row into a fresh facade project shell.
fn open(uuid: &str) -> *mut OakEngineProject {
	let project = unsafe { crate::node::oakengine_project_create() };
	assert!(!project.is_null());
	let uuid_c = CString::new(uuid).unwrap();
	let mut err = [0 as std::ffi::c_char; 4096];
	let rc = unsafe {
		crate::library::oakengine_project_load_library(
			project,
			uuid_c.as_ptr(),
			err.as_mut_ptr(),
			err.len() as i32,
		)
	};
	assert_eq!(rc, OAKENGINE_OK, "open {uuid}");
	project
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/// Create lands a row immediately; list reports it with the metadata and
/// the (zero) stats.
#[test]
fn create_then_list_shows_the_row() {
	let dir = temp_dir("create");
	with_library(&dir.join("lib.db"), || {
		let uuid = create("Demo Reel");
		assert!(!uuid.is_empty(), "create reports the new uuid");

		let json = list_json();
		let row = list_row(&json, &uuid).expect("the created row is listed");
		assert_eq!(row.get("name").and_then(|v| v.as_str()), Some("Demo Reel"));
		assert!(row.get("modified_at").and_then(|v| v.as_i64()).unwrap() > 0);
		assert_eq!(row.get("track_count").and_then(|v| v.as_i64()), Some(0));
		assert_eq!(row.get("footage_count").and_then(|v| v.as_i64()), Some(0));

		// A second create adds a second row.
		let other = create("Second");
		assert_ne!(uuid, other);
		assert_eq!(list_uuids(&list_json()).len(), 2);
	});
	let _ = std::fs::remove_dir_all(&dir);
}

/// Opening a library row binds the project to the library session: the
/// next undoable edit write-throughs onto the row's journal.
#[test]
fn open_binds_and_write_through_advances_the_row() {
	let dir = temp_dir("open");
	let db = dir.join("lib.db");
	with_library(&db, || {
		let uuid = create("Editable");
		let before = journal_rows(&db, &uuid);
		let project = open(&uuid);
		assert_eq!(
			unsafe { crate::storage::oakengine_storage_is_bound(project) },
			1,
			"the library-opened project is bound"
		);

		// An undoable edit write-throughs.
		let node = unsafe {
			crate::node::oakengine_project_add_node(project, CString::new(MATH).unwrap().as_ptr())
		};
		assert!(!node.is_null());
		unsafe { crate::node::oakengine_node_free(node) };

		let after = journal_rows(&db, &uuid);
		assert!(
			after > before,
			"the edit journaled new rows ({before} -> {after})"
		);

		// The row name comes from the projectname setting (the facade's
		// project name is filename-derived, so a library project displays
		// "(untitled)"; the app overrides it with the row name).
		let row = list_row(&list_json(), &uuid).expect("row after the edit");
		assert_eq!(row.get("name").and_then(|v| v.as_str()), Some("Editable"));

		unsafe { crate::node::oakengine_project_free(project) };
	});
	let _ = std::fs::remove_dir_all(&dir);
}

/// Rename and duplicate keep the list coherent; delete removes the row and
/// opening it afterwards fails.
#[test]
fn rename_duplicate_delete() {
	let dir = temp_dir("rdd");
	with_library(&dir.join("lib.db"), || {
		let uuid = create("Original");

		// Rename.
		let rc = unsafe {
			crate::library::oakengine_library_rename(
				CString::new(uuid.clone()).unwrap().as_ptr(),
				CString::new("Renamed").unwrap().as_ptr(),
			)
		};
		assert_eq!(rc, OAKENGINE_OK);
		let row = list_row(&list_json(), &uuid).expect("renamed row");
		assert_eq!(row.get("name").and_then(|v| v.as_str()), Some("Renamed"));

		// Duplicate (default "<name> (copy)" name).
		let copy = duplicate(&uuid);
		assert_ne!(copy, uuid);
		let row = list_row(&list_json(), &copy).expect("the copy is listed");
		assert_eq!(
			row.get("name").and_then(|v| v.as_str()),
			Some("Renamed (copy)")
		);

		// The copy opens (its journal history came along).
		let project = open(&copy);
		unsafe { crate::node::oakengine_project_free(project) };

		// Delete the copy; opening it afterwards fails.
		let rc = unsafe {
			crate::library::oakengine_library_delete(CString::new(copy.clone()).unwrap().as_ptr())
		};
		assert_eq!(rc, OAKENGINE_OK);
		assert!(!list_uuids(&list_json()).contains(&copy));
		let shell = unsafe { crate::node::oakengine_project_create() };
		let rc = unsafe {
			crate::library::oakengine_project_load_library(
				shell,
				CString::new(copy).unwrap().as_ptr(),
				std::ptr::null_mut(),
				0,
			)
		};
		assert_eq!(rc, OAKENGINE_E_NOT_FOUND, "a deleted row does not open");
		unsafe { crate::node::oakengine_project_free(shell) };

		// Unknown uuids are E_NOT_FOUND; empty arguments E_INVALID.
		let rc = unsafe {
			crate::library::oakengine_library_delete(CString::new("{no-such}").unwrap().as_ptr())
		};
		assert_eq!(rc, OAKENGINE_E_NOT_FOUND);
		let rc = unsafe { crate::library::oakengine_library_delete(c"".as_ptr()) };
		assert_eq!(rc, OAKENGINE_E_INVALID);
	});
	let _ = std::fs::remove_dir_all(&dir);
}

/// Export writes the row's head state to a file (dispatched by extension),
/// and import brings a file back as a new library row that opens.
#[test]
fn export_then_import_round_trip() {
	let dir = temp_dir("xport");
	with_library(&dir.join("lib.db"), || {
		// A row with one node, so the exported file has content.
		let uuid = create("Exchange");
		let project = open(&uuid);
		let node = unsafe {
			crate::node::oakengine_project_add_node(project, CString::new(MATH).unwrap().as_ptr())
		};
		assert!(!node.is_null());
		unsafe { crate::node::oakengine_node_free(node) };
		unsafe { crate::node::oakengine_project_free(project) };

		// Export as .ove and as .otio.
		let ove = dir.join("out.ove");
		let otio = dir.join("out.otio");
		for path in [&ove, &otio] {
			let rc = unsafe {
				crate::library::oakengine_library_export(
					CString::new(uuid.clone()).unwrap().as_ptr(),
					CString::new(path.to_string_lossy().into_owned()).unwrap().as_ptr(),
				)
			};
			assert_eq!(rc, OAKENGINE_OK, "export {}", path.display());
			assert!(path.exists(), "{} exists", path.display());
		}
		let xml = std::fs::read_to_string(&ove).unwrap();
		assert!(xml.contains("<project"), "ove payload:\n{xml}");

		// Import the .ove back as a new row and open it (single call with a
		// stack buffer — the import export has a side effect).
		let path_c = CString::new(ove.to_string_lossy().into_owned()).unwrap();
		let mut buf = [0 as std::ffi::c_char; 256];
		let rc = unsafe {
			crate::library::oakengine_library_import(path_c.as_ptr(), buf.as_mut_ptr(), 256)
		};
		assert!(rc > 0, "import rc={rc}");
		let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
		let imported =
			String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
				.into_owned();
		assert!(!imported.is_empty(), "import reports the new uuid");
		assert_ne!(imported, uuid, "import assigns a fresh uuid");
		let project = open(&imported);
		// The projectname setting round-trips into the imported row's name.
		let row = list_row(&list_json(), &imported).expect("the imported row is listed");
		assert_eq!(row.get("name").and_then(|v| v.as_str()), Some("Exchange"));
		unsafe { crate::node::oakengine_project_free(project) };

		// Importing a missing file fails.
		let rc = unsafe {
			crate::library::oakengine_library_import(
				CString::new(dir.join("nope.ove").to_string_lossy().into_owned())
					.unwrap()
					.as_ptr(),
				std::ptr::null_mut(),
				0,
			)
		};
		assert!(rc < 0, "a missing file does not import");
	});
	let _ = std::fs::remove_dir_all(&dir);
}

/// With the backend disabled the list is empty (not an error) and every
/// mutating call fails with E_STATE.
#[test]
fn disabled_backend_degrades_gracefully() {
	let _stack = GLOBAL_STACK_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let _off = common::storage_off_guard();

	assert_eq!(list_json(), "[]", "no library configured reads as empty");

	let rc = unsafe {
		crate::library::oakengine_library_create(
			c"Nope".as_ptr(),
			std::ptr::null_mut(),
			0,
		)
	};
	assert_eq!(rc, OAKENGINE_E_STATE);
	let rc = unsafe { crate::library::oakengine_library_delete(c"{x}".as_ptr()) };
	assert_eq!(rc, OAKENGINE_E_STATE);
	let rc = unsafe { crate::library::oakengine_library_export(c"{x}".as_ptr(), c"/tmp/x.ove".as_ptr()) };
	assert_eq!(rc, OAKENGINE_E_STATE);

	let shell = unsafe { crate::node::oakengine_project_create() };
	let rc = unsafe {
		crate::library::oakengine_project_load_library(
			shell,
			c"{x}".as_ptr(),
			std::ptr::null_mut(),
			0,
		)
	};
	assert_eq!(rc, OAKENGINE_E_STATE);
	unsafe { crate::node::oakengine_project_free(shell) };
}
