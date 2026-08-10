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

//! Smoke tests for the task family (`engine/include/oakengine/task.h`).
//!
//! NOTE: the oaktask crate is currently NOT a facade dev-dependency (its
//! Cargo.toml is being restructured in a parallel session), so this file
//! cannot LINK until the dev-dependency is re-added. It is written against
//! the real surface and should run unmodified once `Cargo.toml` is
//! restored.
//!
//! Two process-wide states serialize the tests, mirroring tests/undo.rs:
//! the facade's global task manager (initialized lazily) and its global
//! undo stack (`oakengine_project_new` clears it), so the manager-mutating
//! and project-mutating tests are each a single test function; the
//! handle/accessor tests touch neither and run in parallel.

#[path = "common/mod.rs"]
mod common;


use std::ffi::{c_char, c_int, c_void};

use oakengine::node::{
	oakengine_node_free, oakengine_project_create, oakengine_project_free, oakengine_project_new,
	oakengine_project_root, oakengine_project_set_filename,
};
use oakengine::task::{
	oakengine_cli_task_dialog_run, oakengine_task_cancel, oakengine_task_create_export,
	oakengine_task_create_project_import, oakengine_task_create_project_load,
	oakengine_task_create_project_load_otio, oakengine_task_create_project_save,
	oakengine_task_create_project_save_otio, oakengine_task_create_proxy, oakengine_task_error,
	oakengine_task_free, oakengine_task_import_file_count, oakengine_task_import_footage_at,
	oakengine_task_import_footage_count, oakengine_task_import_get_command,
	oakengine_task_import_invalid_file_at, oakengine_task_import_invalid_files_count,
	oakengine_task_is_cancelled, oakengine_task_manager_add, oakengine_task_manager_cancel,
	oakengine_task_manager_count, oakengine_task_manager_first, oakengine_task_manager_handle,
	oakengine_task_save_get_project, oakengine_task_start_sync, oakengine_task_start_time,
	oakengine_task_title,
};

/// Read a two-stage string buffer (NUL-terminated) as a Rust `String`.
fn read_buf(buf: &mut [c_char]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
		.into_owned()
}

// ---------------------------------------------------------------------------
// NULL / invalid-handle rejection (no shared state; parallel-safe)
// ---------------------------------------------------------------------------

/// Every accessor rejects a NULL task with OAKENGINE_E_INVALID (-1);
/// creators return NULL; the CLI dialog returns 0 for NULL per the capi.
#[test]
fn task_null_handles_are_rejected() {
	common::force_link();

	let mut buf = [0 as c_char; 256];

	assert_eq!(unsafe { oakengine_task_title(std::ptr::null_mut(), buf.as_mut_ptr(), 256) }, -1);
	assert_eq!(unsafe { oakengine_task_error(std::ptr::null_mut(), buf.as_mut_ptr(), 256) }, -1);
	assert_eq!(unsafe { oakengine_task_start_time(std::ptr::null_mut()) }, -1);
	assert_eq!(unsafe { oakengine_task_is_cancelled(std::ptr::null_mut()) }, -1);
	assert_eq!(unsafe { oakengine_task_cancel(std::ptr::null_mut()) }, -1);
	assert_eq!(unsafe { oakengine_task_start_sync(std::ptr::null_mut()) }, -1);
	assert_eq!(unsafe { oakengine_task_free(std::ptr::null_mut()) }, -1);

	// Import/save result accessors on NULL → E_INVALID / NULL.
	assert_eq!(unsafe { oakengine_task_import_file_count(std::ptr::null_mut()) }, -1);
	assert_eq!(unsafe { oakengine_task_import_footage_count(std::ptr::null_mut()) }, -1);
	assert_eq!(
		unsafe { oakengine_task_import_invalid_files_count(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_task_import_invalid_file_at(std::ptr::null_mut(), 0, buf.as_mut_ptr(), 256) },
		-1
	);
	assert!(unsafe { oakengine_task_import_get_command(std::ptr::null_mut()) }.is_null());
	assert!(unsafe { oakengine_task_import_footage_at(std::ptr::null_mut(), 0) }.is_null());
	assert!(unsafe { oakengine_task_save_get_project(std::ptr::null_mut()) }.is_null());

	// Creators with NULL input → NULL.
	assert!(unsafe { oakengine_task_create_project_load(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_task_create_project_load_otio(std::ptr::null()) }.is_null());
	assert!(unsafe {
		oakengine_task_create_project_save(
			std::ptr::null_mut(),
			0,
			std::ptr::null(),
			std::ptr::null(),
		)
	}
	.is_null());
	assert!(unsafe { oakengine_task_create_project_save_otio(std::ptr::null_mut()) }.is_null());
	assert!(unsafe { oakengine_task_create_project_import(std::ptr::null_mut(), std::ptr::null(), 0) }
		.is_null());
	assert!(unsafe { oakengine_task_create_proxy(std::ptr::null_mut()) }.is_null());
	assert!(unsafe { oakengine_task_create_export(std::ptr::null_mut(), std::ptr::null_mut()) }
		.is_null());

	// The CLI dialog returns 0 (not E_INVALID) for NULL, mirroring the capi.
	assert_eq!(unsafe { oakengine_cli_task_dialog_run(std::ptr::null_mut(), std::ptr::null_mut()) }, 0);
}

// ---------------------------------------------------------------------------
// Accessor / lifecycle tests (no shared state)
// ---------------------------------------------------------------------------

/// A project-load task with a bad filename: created (non-NULL), has a
/// title, fails synchronously (start_sync → 0) with a non-empty error, and
/// reports the facade-side start stamp once started.
#[test]
fn load_task_with_bad_filename_fails_sync() {
	common::force_link();

	let task = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert!(!task.is_null());

	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_task_title(task, buf.as_mut_ptr(), 256) };
	assert!(len > 0);
	assert!(read_buf(&mut buf).contains("Loading"));

	// The synchronous run fails (file does not exist).
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 0);

	// The error string is non-empty after the failed run.
	let len = unsafe { oakengine_task_error(task, buf.as_mut_ptr(), 256) };
	assert!(len > 0);
	assert!(!read_buf(&mut buf).is_empty());

	// The facade-side start stamp is reported once the task started.
	assert_ne!(unsafe { oakengine_task_start_time(task) }, 0);
	assert_eq!(unsafe { oakengine_task_is_cancelled(task) }, 0);

	// Cancel round-trip through the facade flag.
	assert_eq!(unsafe { oakengine_task_cancel(task) }, 0);
	assert_eq!(unsafe { oakengine_task_is_cancelled(task) }, 1);

	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}

/// The CLI dialog runs the task synchronously: 0 for a failing task, and
/// the dialog is a stub around that sync-run core.
#[test]
fn cli_dialog_runs_task_sync() {
	common::force_link();

	let task = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert!(!task.is_null());
	assert_eq!(unsafe { oakengine_cli_task_dialog_run(task, std::ptr::null_mut()) }, 0);
	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}

// ---------------------------------------------------------------------------
// Project-backed tasks (serialized: `oakengine_project_new` clears the
// process-wide undo stack)
// ---------------------------------------------------------------------------

/// Save, save-otio and import task creation against a real project — one
/// test because `oakengine_project_new` touches the global undo stack.
#[test]
fn project_task_lifecycle() {
	common::force_link();

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);

	let root = unsafe { oakengine_project_root(project) };
	assert!(!root.is_null());

	// ---- save task on a real project → sync run writes the file ----------
	let save_path = std::env::temp_dir().join(format!(
		"oakengine_task_save_{}.ovexml",
		std::process::id()
	));
	let save_c = std::ffi::CString::new(save_path.to_str().unwrap()).unwrap();
	let save_task = unsafe {
		oakengine_task_create_project_save(project, 0, save_c.as_ptr(), std::ptr::null())
	};
	assert!(!save_task.is_null());
	assert_eq!(unsafe { oakengine_task_start_sync(save_task) }, 1);
	assert!(save_path.exists());

	// save_get_project returns a borrowed project handle (freed by the
	// caller) — NULL on other tasks.
	let saved = unsafe { oakengine_task_save_get_project(save_task) };
	assert!(!saved.is_null());
	unsafe { oakengine_project_free(saved) };
	assert!(unsafe { oakengine_task_save_get_project(std::ptr::null_mut()) }.is_null());

	// A NULL project yields a NULL save task.
	assert!(unsafe {
		oakengine_task_create_project_save(std::ptr::null_mut(), 0, save_c.as_ptr(), std::ptr::null())
	}
	.is_null());

	unsafe { oakengine_task_free(save_task) };

	// ---- save-otio: the facade derives the output filename from the
	// project's own filename; NULL without one, a real task with one. ------
	assert!(unsafe { oakengine_task_create_project_save_otio(project) }.is_null());
	assert_eq!(
		unsafe { oakengine_project_set_filename(project, c"/tmp/oakengine_task_otio.otio".as_ptr()) },
		0
	);
	let otio_task = unsafe { oakengine_task_create_project_save_otio(project) };
	assert!(!otio_task.is_null());
	unsafe { oakengine_task_free(otio_task) };

	// ---- import with 0 urls: task created, file count 0 -------------------
	let import_task = unsafe {
		oakengine_task_create_project_import(root, std::ptr::null(), 0)
	};
	assert!(!import_task.is_null());
	assert_eq!(unsafe { oakengine_task_import_file_count(import_task) }, 0);
	assert_eq!(unsafe { oakengine_task_import_footage_count(import_task) }, 0);
	assert_eq!(unsafe { oakengine_task_import_invalid_files_count(import_task) }, 0);
	// Nothing ran, so no command / footage / invalid entries. An
	// out-of-range invalid-file index reports the module's
	// OAKTASK_E_NOT_FOUND (-80004) pass-through.
	assert!(unsafe { oakengine_task_import_get_command(import_task) }.is_null());
	assert!(unsafe { oakengine_task_import_footage_at(import_task, 0) }.is_null());
	let mut buf = [0 as c_char; 256];
	assert_eq!(
		unsafe { oakengine_task_import_invalid_file_at(import_task, 0, buf.as_mut_ptr(), 256) },
		-80004
	);
	unsafe { oakengine_task_free(import_task) };

	// A negative url count is rejected (NULL task).
	assert!(unsafe { oakengine_task_create_project_import(root, std::ptr::null(), -1) }.is_null());

	unsafe { oakengine_node_free(root) };
	unsafe { oakengine_project_free(project) };
	let _ = std::fs::remove_file(&save_path);
}

// ---------------------------------------------------------------------------
// Global task manager (serialized: the manager is process-wide)
// ---------------------------------------------------------------------------

/// The global manager is created lazily: handle non-NULL, count 0, then a
/// task handed over with `manager_add` is visible to `manager_count` /
/// `manager_first` and can be cancelled.
#[test]
fn task_manager_lifecycle() {
	common::force_link();

	assert!(!unsafe { oakengine_task_manager_handle() }.is_null());
	assert_eq!(unsafe { oakengine_task_manager_count() }, 0);

	let task = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert!(!task.is_null());

	// Handing the task to the manager transfers ownership.
	assert_eq!(unsafe { oakengine_task_manager_add(task) }, 0);
	assert!(unsafe { oakengine_task_manager_count() } >= 1);

	// The queue is non-empty, so the first task is borrowed.
	let first = unsafe { oakengine_task_manager_first() };
	assert!(!first.is_null());
	assert_eq!(unsafe { oakengine_task_free(first) }, 0);

	// Cancelling through the manager succeeds (the task may already have
	// failed fast on the missing file; cancel on a finished task is safe).
	assert_eq!(unsafe { oakengine_task_manager_cancel(task) }, 0);

	// Releasing the (now borrowed) handle is safe: the manager owns the
	// task and will delete it when it is cleaned up.
	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}
