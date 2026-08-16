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

//! Integration tests for the **task family** (`src/task.rs`, the
//! `oakengine_task_*` C ABI; module contract `include/task/*.h`).
//!
//! Coverage rules (see the family test charter):
//!   1. no mocks — every call goes through the real facade into the real
//!      oaktask/oaknode/oakundo/oakcodec module crates (the
//!      `oakcore_audioparams_*` accessors the facade reads through are its
//!      own in-dylib implementations, re-exported by `tests/common`, the
//!      same mechanism the other family tests use);
//!   2. every one of the 27 `oakengine_task_*` / `oakengine_cli_task_*`
//!      exports is exercised on a legal path with the result asserted;
//!   3. legal-input matrix (compression flags, url counts, indices, buffer
//!      sizes) covers the meaningful combinations;
//!   4. illegal inputs (NULL, empty handles, out-of-range indices, zero /
//!      negative sizes, garbage flag values, wrong-family handles) always
//!      yield a negative error code or a documented NULL/0 no-op — never a
//!      crash;
//!   5. free contracts: free(NULL) and free(empty-handle) are clean error
//!      no-ops, and `oaktask_debug_alive_count()` returns to baseline.
//!
//! ## Serialization
//!
//! Two process-wide states force the tests into one thread: the facade's
//! lazily-created global task manager and the global undo stack
//! (`oakengine_project_new` clears it). Additionally the alive-count
//! assertions measure a process-wide module counter, so every test takes a
//! shared [`SERIAL`] mutex (the same pattern as the oaktask crate's own
//! `MANAGER_LOCK`).
//!
//! ## Ignored tests
//!
//! (None — the export run used to be environment-gated on GPU/encoder;
//! the real CPU render + statically linked FFmpeg encoder now works in
//! the test environment, see [`export_task_run_real_encoder`].)

use super::common;

use std::ffi::{c_char, c_int, c_void};
use std::sync::Mutex;

use crate::codec::{oakengine_encoding_params_create, oakengine_encoding_params_set_filename};
use crate::handle::{
	free_box, CHandle, OakEngineNode, OakEngineProject, OakEngineSequence, OakEngineTask,
};
use crate::node::{
	oakengine_node_free, oakengine_project_create, oakengine_project_filename,
	oakengine_project_free, oakengine_project_new, oakengine_project_root, oakengine_project_save,
	oakengine_project_set_filename,
};
use crate::task::{
	oakengine_cli_task_dialog_run, oakengine_task_cancel, oakengine_task_create_export,
	oakengine_task_create_project_import, oakengine_task_create_project_load,
	oakengine_task_create_project_load_otio, oakengine_task_create_project_save,
	oakengine_task_create_project_save_otio, oakengine_task_create_proxy, oakengine_task_error,
	oakengine_task_free, oakengine_task_import_file_count, oakengine_task_import_footage_at,
	oakengine_task_import_footage_count, oakengine_task_import_get_command,
	oakengine_task_import_invalid_file_at, oakengine_task_import_invalid_files_count,
	oakengine_task_is_cancelled, oakengine_task_load_take_project, oakengine_task_manager_add,
	oakengine_task_manager_cancel, oakengine_task_manager_count, oakengine_task_manager_first,
	oakengine_task_manager_handle, oakengine_task_save_get_project, oakengine_task_start_sync,
	oakengine_task_start_time, oakengine_task_subscribe, oakengine_task_title,
};
use crate::timeline::oakengine_sequence_new;
use crate::undo::oakengine_undo_command_free;

/// OAKTASK module error codes that pass through untranslated.
const OAKTASK_E_INVALID: c_int = -80001;
const OAKTASK_E_STATE: c_int = -80002;
const OAKTASK_E_NOT_FOUND: c_int = -80004;

/// Serializes every test in this binary (see the module docs). Poisoned by a
/// panicking test, the lock is recovered with `into_inner` so one failure
/// does not cascade into `PoisonError` failures in every later test.
static SERIAL: Mutex<()> = Mutex::new(());

/// Both lock guards held by [`serial`] (the local task lock plus the
/// facade-wide undo-stack lock).
pub(crate) struct SerialGuard {
	/// The [`SERIAL`] lock.
	_task: std::sync::MutexGuard<'static, ()>,
	/// The facade's process-wide undo-stack lock (it_undo's), so the
	/// `oakengine_project_new` calls in these tests (which clear the stack)
	/// never race the it_undo / it_storage stack tests.
	_stack: std::sync::MutexGuard<'static, ()>,
}

/// Take the [`SERIAL`] lock AND the global undo-stack lock, recovering
/// from any poisoning.
pub(crate) fn serial() -> SerialGuard {
	let _task = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	let _stack = super::it_undo::GLOBAL_STACK_LOCK
		.lock()
		.unwrap_or_else(|e| e.into_inner());
	SerialGuard { _task, _stack }
}

/// The facade's live task-payload counter (the engine-side replacement
/// of the deleted module alive counter; counts owned task payloads and
/// borrowed manager-view payloads).
fn alive_count() -> c_int {
	crate::stubs::task::oaktask_debug_alive_count()
}

/// Read a NUL-terminated two-stage buffer as a Rust `String`.
fn read_buf(buf: &[c_char]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) })
		.into_owned()
}

/// A facade task box wrapping an EMPTY module handle (`ctx == NULL`), the
/// "empty handle" state the C contract documents as invalid input.
fn empty_task_box() -> *mut OakEngineTask {
	Box::into_raw(Box::new(OakEngineTask {
		handle: CHandle::null(),
	}))
}

/// Reclaim a facade box that `oakengine_task_free` refused to consume
/// (NULL/empty handles are errors, so the box stays allocated).
///
/// # Safety
/// `ptr` must be a box produced by [`empty_task_box`] that was never freed.
unsafe fn reclaim_empty_task_box(ptr: *mut OakEngineTask) {
	unsafe { drop(Box::from_raw(ptr)) };
}

// ---------------------------------------------------------------------------
// NULL / empty-handle rejection (all 27 exports)
// ---------------------------------------------------------------------------

/// Every accessor rejects a NULL task with OAKENGINE_E_INVALID (-1) and
/// every pointer accessor returns NULL; creators return NULL for NULL /
/// invalid arguments; the CLI dialog returns 0 (the capi's "no task" is
/// not an error).
#[test]
fn null_handles_are_rejected() {
	let _g = serial();
	common::force_link();

	let mut buf = [0 as c_char; 256];

	// ---- manager family -----------------------------------------------------
	assert_eq!(
		unsafe { oakengine_task_manager_add(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_task_manager_cancel(std::ptr::null_mut()) },
		-1
	);

	// ---- task accessors -----------------------------------------------------
	assert_eq!(
		unsafe { oakengine_task_title(std::ptr::null_mut(), buf.as_mut_ptr(), 256) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_task_error(std::ptr::null_mut(), buf.as_mut_ptr(), 256) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_task_start_time(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_task_is_cancelled(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(unsafe { oakengine_task_cancel(std::ptr::null_mut()) }, -1);
	assert_eq!(
		unsafe { oakengine_task_start_sync(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(unsafe { oakengine_task_free(std::ptr::null_mut()) }, -1);

	// ---- import / save result accessors ------------------------------------
	assert_eq!(
		unsafe { oakengine_task_import_file_count(std::ptr::null_mut()) },
		-1
	);
	assert!(unsafe { oakengine_task_import_get_command(std::ptr::null_mut()) }.is_null());
	assert_eq!(
		unsafe { oakengine_task_import_footage_count(std::ptr::null_mut()) },
		-1
	);
	assert!(unsafe { oakengine_task_import_footage_at(std::ptr::null_mut(), 0) }.is_null());
	assert_eq!(
		unsafe { oakengine_task_import_invalid_files_count(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe {
			oakengine_task_import_invalid_file_at(std::ptr::null_mut(), 0, buf.as_mut_ptr(), 256)
		},
		-1
	);
	assert!(unsafe { oakengine_task_save_get_project(std::ptr::null_mut()) }.is_null());

	// ---- creators ------------------------------------------------------------
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
	assert!(unsafe {
		oakengine_task_create_project_import(std::ptr::null_mut(), std::ptr::null(), 0)
	}
	.is_null());
	assert!(unsafe { oakengine_task_create_proxy(std::ptr::null_mut()) }.is_null());
	assert!(
		unsafe { oakengine_task_create_export(std::ptr::null_mut(), std::ptr::null_mut()) }
			.is_null()
	);

	// ---- CLI dialog (0, not E_INVALID, for NULL) ----------------------------
	assert_eq!(
		unsafe { oakengine_cli_task_dialog_run(std::ptr::null_mut(), std::ptr::null_mut()) },
		0
	);

	// The manager handle is lazily created and never NULL (documented).
	assert!(!oakengine_task_manager_handle().is_null());
	// Finished tasks can linger in the process-wide manager queue (it has no
	// delete-finished export and other tests in this binary add to it), so
	// only assert that NULL-handle manager operations change nothing.
	let count = oakengine_task_manager_count();
	assert_eq!(
		unsafe { oakengine_task_manager_add(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_task_manager_cancel(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(oakengine_task_manager_count(), count);
}

/// Empty handles (`ctx == NULL` boxes) are rejected exactly like NULL:
/// -1 / NULL from every accessor and creator, and `oakengine_task_free`
/// reports E_INVALID without consuming the box.
#[test]
fn empty_handles_are_rejected() {
	let _g = serial();
	common::force_link();

	let mut buf = [0 as c_char; 256];

	// ---- task accessors on an empty-handle box ------------------------------
	let t = empty_task_box();
	assert_eq!(
		unsafe { oakengine_task_title(t, buf.as_mut_ptr(), 256) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_task_error(t, buf.as_mut_ptr(), 256) },
		-1
	);
	assert_eq!(unsafe { oakengine_task_start_time(t) }, -1);
	assert_eq!(unsafe { oakengine_task_is_cancelled(t) }, -1);
	assert_eq!(unsafe { oakengine_task_cancel(t) }, -1);
	assert_eq!(unsafe { oakengine_task_start_sync(t) }, -1);
	assert_eq!(unsafe { oakengine_task_manager_add(t) }, -1);
	assert_eq!(unsafe { oakengine_task_manager_cancel(t) }, -1);
	assert_eq!(unsafe { oakengine_task_import_file_count(t) }, -1);
	assert!(unsafe { oakengine_task_import_get_command(t) }.is_null());
	assert_eq!(unsafe { oakengine_task_import_footage_count(t) }, -1);
	assert!(unsafe { oakengine_task_import_footage_at(t, 0) }.is_null());
	assert_eq!(unsafe { oakengine_task_import_invalid_files_count(t) }, -1);
	assert_eq!(
		unsafe { oakengine_task_import_invalid_file_at(t, 0, buf.as_mut_ptr(), 256) },
		-1
	);
	assert!(unsafe { oakengine_task_save_get_project(t) }.is_null());

	// free refuses the empty handle with E_INVALID and leaves the box
	// allocated (the caller still owns it).
	assert_eq!(unsafe { oakengine_task_free(t) }, -1);
	unsafe { reclaim_empty_task_box(t) };

	// ---- creators with empty project / node / sequence handles --------------
	let empty_project = Box::into_raw(Box::new(OakEngineProject {
		handle: CHandle::null(),
	}));
	assert!(unsafe {
		oakengine_task_create_project_save(empty_project, 0, std::ptr::null(), std::ptr::null())
	}
	.is_null());
	assert!(unsafe { oakengine_task_create_project_save_otio(empty_project) }.is_null());
	unsafe { drop(Box::from_raw(empty_project)) };

	let empty_node = Box::into_raw(Box::new(OakEngineNode {
		handle: CHandle::null(),
	}));
	assert!(
		unsafe { oakengine_task_create_project_import(empty_node, std::ptr::null(), 0) }.is_null()
	);
	assert!(unsafe { oakengine_task_create_proxy(empty_node) }.is_null());
	unsafe { drop(Box::from_raw(empty_node)) };

	let empty_seq = Box::into_raw(Box::new(OakEngineSequence {
		handle: CHandle::null(),
	}));
	let params = oakengine_encoding_params_create();
	assert!(!params.is_null());
	// NULL result: the params handle is NOT consumed, so we own it still.
	assert!(unsafe { oakengine_task_create_export(empty_seq, params) }.is_null());
	unsafe { crate::codec::oakengine_encoding_params_destroy(params) };
	unsafe { drop(Box::from_raw(empty_seq)) };
}

// ---------------------------------------------------------------------------
// Task lifecycle: load tasks (no project, no manager state)
// ---------------------------------------------------------------------------

/// A project-load task with a bad filename: created, has the "Loading"
/// title, fails synchronously (0), reports a non-empty error, exposes the
/// facade-side start stamp after starting, and round-trips the cancel flag.
#[test]
fn load_task_lifecycle() {
	let _g = serial();
	common::force_link();

	let task = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert!(!task.is_null());

	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_task_title(task, buf.as_mut_ptr(), 256) };
	assert!(len > 0);
	assert!(read_buf(&mut buf).contains("Loading"));

	// A task that never ran has no start stamp and is not cancelled.
	assert_eq!(unsafe { oakengine_task_start_time(task) }, 0);
	assert_eq!(unsafe { oakengine_task_is_cancelled(task) }, 0);

	// The sync run fails (file does not exist).
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 0);
	assert_ne!(unsafe { oakengine_task_start_time(task) }, 0);
	assert_eq!(unsafe { oakengine_task_is_cancelled(task) }, 0);

	// The error string is populated by the failed run.
	let elen = unsafe { oakengine_task_error(task, buf.as_mut_ptr(), 256) };
	assert!(elen > 0);
	assert!(!read_buf(&mut buf).is_empty());

	// Cancel round-trip through the facade flag (module cancel succeeds).
	assert_eq!(unsafe { oakengine_task_cancel(task) }, 0);
	assert_eq!(unsafe { oakengine_task_is_cancelled(task) }, 1);

	// Re-running after a failed sync run is a legal no-op (fails again).
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 0);

	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}

/// The empty filename is legal input: a task is created with an empty
/// title suffix and fails when run (no such file).
#[test]
fn load_task_empty_filename() {
	let _g = serial();
	common::force_link();

	let task = unsafe { oakengine_task_create_project_load(c"".as_ptr()) };
	assert!(!task.is_null());

	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_task_title(task, buf.as_mut_ptr(), 256) };
	assert_eq!(len, 10);
	assert_eq!(read_buf(&mut buf), "Loading ''");

	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 0);

	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}

/// The OTIO load task: created for a valid filename, fails synchronously
/// (the document does not exist) and reports the load error.
#[test]
fn load_otio_task_lifecycle() {
	let _g = serial();
	common::force_link();

	let task =
		unsafe { oakengine_task_create_project_load_otio(c"/no/such/oak/project.otio".as_ptr()) };
	assert!(!task.is_null());

	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_task_title(task, buf.as_mut_ptr(), 256) };
	assert!(len > 0);
	assert!(read_buf(&mut buf).contains("Loading"));

	// Missing document -> failed run.
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 0);
	let elen = unsafe { oakengine_task_error(task, buf.as_mut_ptr(), 256) };
	assert!(elen > 0);
	assert!(!read_buf(&mut buf).is_empty());

	// An unknown extension is also a clean failure (format dispatch error).
	let task2 =
		unsafe { oakengine_task_create_project_load_otio(c"/no/such/oak/project.xyz".as_ptr()) };
	assert!(!task2.is_null());
	assert_eq!(unsafe { oakengine_task_start_sync(task2) }, 0);

	assert_eq!(unsafe { oakengine_task_free(task2) }, 0);
	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}

/// Two-stage string getters across the buffer-size matrix: a too-small or
/// NULL buffer still reports the length, an exact/large buffer also gets
/// the NUL-terminated content.
#[test]
fn string_getters_buffer_matrix() {
	let _g = serial();
	common::force_link();

	let task =
		unsafe { oakengine_task_create_project_load(c"/no/such/oak/it_task_buf.ove".as_ptr()) };
	assert!(!task.is_null());

	// Title length (facade convention: excludes the NUL).
	let expected = unsafe { oakengine_task_title(task, std::ptr::null_mut(), 0) };
	let expected_usize = expected as usize;
	assert!(expected > 0);

	// NULL buffer with a positive size still reports the length.
	assert_eq!(
		unsafe { oakengine_task_title(task, std::ptr::null_mut(), 256) },
		expected
	);
	// A negative size is a documented no-op size query, not an error.
	assert_eq!(
		unsafe { oakengine_task_title(task, std::ptr::null_mut(), -5) },
		expected
	);

	// Too-small buffer: length reported, nothing written (module writes only
	// when the buffer fits the string plus its NUL).
	let mut small = [0 as c_char; 4];
	assert_eq!(
		unsafe { oakengine_task_title(task, small.as_mut_ptr(), 4) },
		expected
	);
	assert_eq!(small[0], 0);

	// Exact string length but no NUL room: still nothing written.
	let mut exact_no_nul = vec![0 as c_char; expected_usize];
	assert_eq!(
		unsafe { oakengine_task_title(task, exact_no_nul.as_mut_ptr(), expected) },
		expected
	);
	assert_eq!(exact_no_nul[0], 0);

	// Exact length + NUL room: content is written.
	let mut exact = vec![0 as c_char; expected_usize + 1];
	assert_eq!(
		unsafe { oakengine_task_title(task, exact.as_mut_ptr(), expected + 1) },
		expected
	);
	assert_eq!(
		read_buf(&exact),
		format!("Loading '/no/such/oak/it_task_buf.ove'")
	);
	assert_eq!(exact[expected_usize], 0);

	// Large buffer: same content.
	let mut big = [0 as c_char; 512];
	assert_eq!(
		unsafe { oakengine_task_title(task, big.as_mut_ptr(), 512) },
		expected
	);
	assert_eq!(
		read_buf(&big),
		format!("Loading '/no/such/oak/it_task_buf.ove'")
	);

	// A task that never ran reports "Unknown error" (module fallback), the
	// same two-stage contract.
	let mut err_buf = [0 as c_char; 64];
	assert_eq!(
		unsafe { oakengine_task_error(task, err_buf.as_mut_ptr(), 64) },
		13
	);
	assert_eq!(read_buf(&err_buf), "Unknown error");
	assert_eq!(
		unsafe { oakengine_task_error(task, std::ptr::null_mut(), 0) },
		13
	);

	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}

/// Wrong-family handles: the import/save result accessors on a plain load
/// task return the module's clean negative codes / NULL — plugins may pass
/// any task handle.
#[test]
fn import_save_accessors_on_wrong_family_task() {
	let _g = serial();
	common::force_link();

	let task = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert!(!task.is_null());

	assert_eq!(
		unsafe { oakengine_task_import_file_count(task) },
		OAKTASK_E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_task_import_footage_count(task) },
		OAKTASK_E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_task_import_invalid_files_count(task) },
		OAKTASK_E_INVALID
	);
	assert!(unsafe { oakengine_task_import_get_command(task) }.is_null());
	assert!(unsafe { oakengine_task_import_footage_at(task, 0) }.is_null());
	let mut buf = [0 as c_char; 256];
	assert_eq!(
		unsafe { oakengine_task_import_invalid_file_at(task, 0, buf.as_mut_ptr(), 256) },
		OAKTASK_E_INVALID
	);
	assert!(unsafe { oakengine_task_save_get_project(task) }.is_null());

	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}

/// The CLI modal dialog is a sync-run wrapper: 0 for a failing task, 0 for
/// NULL, and it marks the task started.
#[test]
fn cli_dialog_runs_task_sync() {
	let _g = serial();
	common::force_link();

	let task = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert!(!task.is_null());
	assert_eq!(
		unsafe { oakengine_cli_task_dialog_run(task, std::ptr::null_mut()) },
		0
	);
	assert_ne!(unsafe { oakengine_task_start_time(task) }, 0);
	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}

// ---------------------------------------------------------------------------
// Free contracts and the module alive counter
// ---------------------------------------------------------------------------

/// `oakengine_task_free`: NULL and empty handles are clean E_INVALID
/// no-ops, a real task frees cleanly, and the module's alive counter
/// returns to baseline after every create/free round trip.
#[test]
fn free_contracts_and_alive_count() {
	let _g = serial();
	common::force_link();

	// NULL and empty-handle free are safe error no-ops.
	assert_eq!(unsafe { oakengine_task_free(std::ptr::null_mut()) }, -1);
	let t = empty_task_box();
	assert_eq!(unsafe { oakengine_task_free(t) }, -1);
	unsafe { reclaim_empty_task_box(t) };

	// A real create/free round trip keeps the alive counter at baseline.
	// NOTE: an actual double-free of the same facade box is out of contract
	// at the C ABI level (the box is destroyed on the first free, so a
	// second free is use-after-free by design); the safe double-free surface
	// is NULL/empty, covered above.
	let baseline = alive_count();
	let task = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert_eq!(alive_count(), baseline + 1);
	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
	assert_eq!(alive_count(), baseline);

	// A task that ran and was cancelled still accounts back to baseline.
	let task2 = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert_eq!(alive_count(), baseline + 1);
	assert_eq!(unsafe { oakengine_task_start_sync(task2) }, 0);
	assert_eq!(unsafe { oakengine_task_cancel(task2) }, 0);
	assert_eq!(unsafe { oakengine_task_free(task2) }, 0);
	assert_eq!(alive_count(), baseline);
}

// ---------------------------------------------------------------------------
// Project-backed tasks (serialized: `oakengine_project_new` clears the
// process-wide undo stack; the task manager is also process-wide)
// ---------------------------------------------------------------------------

/// Save tasks across the compression matrix (0, 1, and garbage flag
/// values), the no-filename failure path, `save_get_project`, save-otio
/// creation, and the alive-count accounting of a save task's borrowed
/// project.
#[test]
fn save_task_matrix() {
	let _g = serial();
	common::force_link();

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);

	let save_path = std::env::temp_dir().join("oakengine_it_task_save.ovexml");
	let save_c = std::ffi::CString::new(save_path.to_str().unwrap()).unwrap();
	let _ = std::fs::remove_file(&save_path);

	// ---- compression 0 and 1 (and garbage flag values -> treated as true)
	let baseline = alive_count();
	for compression in [0, 1, 2, -1] {
		let task = unsafe {
			oakengine_task_create_project_save(
				project,
				compression,
				save_c.as_ptr(),
				std::ptr::null(),
			)
		};
		assert!(!task.is_null(), "save with use_compression={compression}");

		let mut buf = [0 as c_char; 256];
		let len = unsafe { oakengine_task_title(task, buf.as_mut_ptr(), 256) };
		assert!(len > 0);
		assert!(read_buf(&mut buf).contains("Saving"));

		// Compression is a boolean in the module; every non-zero value is
		// "compressed", and the run still succeeds.
		assert_eq!(unsafe { oakengine_task_start_sync(task) }, 1);
		assert!(
			save_path.exists(),
			"compression={compression} must write the file"
		);
		assert_ne!(unsafe { oakengine_task_start_time(task) }, 0);

		unsafe { oakengine_task_free(task) };
	}
	assert_eq!(alive_count(), baseline);
	let _ = std::fs::remove_file(&save_path);

	// ---- save_get_project: a borrowed project handle per call ----------------
	let task = unsafe {
		oakengine_task_create_project_save(project, 0, save_c.as_ptr(), std::ptr::null())
	};
	assert!(!task.is_null());
	let saved = unsafe { oakengine_task_save_get_project(task) };
	assert!(!saved.is_null());
	unsafe { oakengine_project_free(saved) };
	// Every call returns a fresh borrowed handle.
	let saved2 = unsafe { oakengine_task_save_get_project(task) };
	assert!(!saved2.is_null());
	unsafe { oakengine_project_free(saved2) };
	unsafe { oakengine_task_free(task) };

	// ---- no filename: save with override NULL fails cleanly ------------------
	let task = unsafe {
		oakengine_task_create_project_save(project, 0, std::ptr::null(), std::ptr::null())
	};
	assert!(!task.is_null());
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 0);
	let mut buf = [0 as c_char; 256];
	let elen = unsafe { oakengine_task_error(task, buf.as_mut_ptr(), 256) };
	assert!(elen > 0);
	assert!(read_buf(&mut buf).contains("filename"));
	unsafe { oakengine_task_free(task) };

	// ---- save-otio: NULL without a project filename, real task with one ------
	assert!(unsafe { oakengine_task_create_project_save_otio(project) }.is_null());
	assert_eq!(
		unsafe {
			oakengine_project_set_filename(project, c"/tmp/oakengine_it_task_otio.otio".as_ptr())
		},
		0
	);
	let otio_task = unsafe { oakengine_task_create_project_save_otio(project) };
	assert!(!otio_task.is_null());
	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_task_title(otio_task, buf.as_mut_ptr(), 256) };
	assert!(len > 0);
	assert!(read_buf(&mut buf).contains("Saving"));
	unsafe { oakengine_task_free(otio_task) };

	unsafe { oakengine_project_free(project) };
	let _ = std::fs::remove_file(&save_path);
}

/// Import task creation against a real project and a real (non-decodable)
/// file, plus the zero-file run. The single-file run (with its
/// invalid-file result) is covered by [`import_run_single_file`].
///
/// A single-file import task is created, reports the documented pre-run
/// accessor states (empty footage / invalid lists, out-of-range codes,
/// no command yet), and frees cleanly. The zero-file task runs end to end
/// (nothing to import) and hands out its (empty) undo command.
#[test]
fn import_flow_with_real_file() {
	let _g = serial();
	common::force_link();

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let root = unsafe { oakengine_project_root(project) };
	assert!(!root.is_null());

	// A real file that cannot be decoded in the test environment (footage
	// probing of a non-media file never succeeds, so a run would mark the
	// file invalid).
	let media = std::env::temp_dir().join("oakengine_it_task_import_batch.tmp");
	std::fs::write(&media, b"not media").unwrap();
	let media_c = std::ffi::CString::new(media.to_str().unwrap()).unwrap();

	// ---- single-file import: creation + pre-run accessors --------------------
	let urls = [media_c.as_ptr()];
	let task = unsafe { oakengine_task_create_project_import(root, urls.as_ptr(), 1) };
	assert!(!task.is_null());

	// Before the run the imported-footage list is empty, so both count
	// exports report 0 (the facade maps `import_file_count` to the module's
	// footage count; documented deviation from the construction-time count).
	assert_eq!(unsafe { oakengine_task_import_file_count(task) }, 0);
	assert_eq!(unsafe { oakengine_task_import_footage_count(task) }, 0);
	assert_eq!(
		unsafe { oakengine_task_import_invalid_files_count(task) },
		0
	);

	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_task_title(task, buf.as_mut_ptr(), 256) };
	assert_eq!(len, 19);
	assert_eq!(read_buf(&mut buf), "Importing 1 file(s)");

	// Pre-run: nothing imported, no invalid entries, no command yet; every
	// index accessor reports the documented empty/out-of-range state.
	assert!(unsafe { oakengine_task_import_footage_at(task, 0) }.is_null());
	assert!(unsafe { oakengine_task_import_footage_at(task, -1) }.is_null());
	assert!(unsafe { oakengine_task_import_footage_at(task, 7) }.is_null());
	assert_eq!(
		unsafe { oakengine_task_import_invalid_file_at(task, 0, buf.as_mut_ptr(), 256) },
		OAKTASK_E_NOT_FOUND
	);
	assert_eq!(
		unsafe { oakengine_task_import_invalid_file_at(task, -1, buf.as_mut_ptr(), 256) },
		OAKTASK_E_NOT_FOUND
	);
	assert!(unsafe { oakengine_task_import_get_command(task) }.is_null());

	assert_eq!(unsafe { oakengine_task_free(task) }, 0);

	// ---- zero-file import: runs without touching the project handle -----------
	let zero = unsafe { oakengine_task_create_project_import(root, std::ptr::null(), 0) };
	assert!(!zero.is_null());
	assert_eq!(unsafe { oakengine_task_import_file_count(zero) }, 0);
	assert_eq!(unsafe { oakengine_task_import_footage_count(zero) }, 0);
	assert_eq!(
		unsafe { oakengine_task_import_invalid_files_count(zero) },
		0
	);

	// "Nothing to import" still counts as a successful run: the run creates
	// the (empty) multi undo command and returns OK.
	assert_eq!(unsafe { oakengine_task_start_sync(zero) }, 1);
	assert_eq!(unsafe { oakengine_task_import_footage_count(zero) }, 0);
	assert_eq!(
		unsafe { oakengine_task_import_invalid_files_count(zero) },
		0
	);
	let cmd = unsafe { oakengine_task_import_get_command(zero) };
	assert!(!cmd.is_null());
	unsafe { oakengine_undo_command_free(cmd) };
	assert!(unsafe { oakengine_task_import_get_command(zero) }.is_null());
	assert_eq!(unsafe { oakengine_task_free(zero) }, 0);

	// ---- illegal url arrays ---------------------------------------------------
	// Negative count -> NULL.
	assert!(unsafe { oakengine_task_create_project_import(root, std::ptr::null(), -1) }.is_null());
	// Non-NULL urls with a count but a NULL entry inside -> NULL.
	let bad_urls = [std::ptr::null()];
	assert!(unsafe { oakengine_task_create_project_import(root, bad_urls.as_ptr(), 1) }.is_null());
	// NULL urls with a positive count -> NULL.
	assert!(unsafe { oakengine_task_create_project_import(root, std::ptr::null(), 1) }.is_null());

	unsafe { oakengine_node_free(root) };
	unsafe { oakengine_project_free(project) };
	let _ = std::fs::remove_file(&media);
}

/// A single-file import task runs end to end: the run succeeds (1) and
/// records the undecodable file as invalid.
///
/// Regression for a former use-after-free: the facade's
/// `oakengine_task_create_project_import` (`src/task.rs`) used to hand the
/// borrowed project handle (from `oaknode_node_get_project`) to
/// `oaktask_create_project_import`, which stores it WITHOUT addref, and
/// then immediately called `oaknode_project_free` on it: the shared
/// `RefBox` refcount went 1→0 and the box was freed while the task's copy
/// still referenced it, so the run's `oaknode_footage_create(task.project,
/// …)` read the freed box → SIGSEGV.
///
/// The fix mirrors the save creator, which addrefs the project
/// (`meta.save_project = Some(ph.addref())`): the import creator now keeps
/// an addref'd copy in `TaskMeta::import_project`, released at free, so
/// the project stays alive for the task's lifetime.
#[test]
fn import_run_single_file() {
	let _g = serial();
	common::force_link();

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let root = unsafe { oakengine_project_root(project) };
	assert!(!root.is_null());

	let media = std::env::temp_dir().join("oakengine_it_task_import_batch.tmp");
	std::fs::write(&media, b"not media").unwrap();
	let media_c = std::ffi::CString::new(media.to_str().unwrap()).unwrap();

	// Creation succeeds; the run succeeds (1) and records the undecodable
	// file as invalid.
	let urls = [media_c.as_ptr()];
	let task = unsafe { oakengine_task_create_project_import(root, urls.as_ptr(), 1) };
	assert!(!task.is_null());
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 1);

	assert_eq!(
		unsafe { oakengine_task_import_invalid_files_count(task) },
		1
	);
	assert_eq!(unsafe { oakengine_task_free(task) }, 0);

	unsafe { oakengine_node_free(root) };
	unsafe { oakengine_project_free(project) };
	let _ = std::fs::remove_file(&media);
}

/// Export task creation against a real sequence and encoding params: the
/// task is created (the color manager is derived from the sequence's
/// project), titled, and freed; the params handle's ownership transfers to
/// the task.
#[test]
fn export_task_creation() {
	let _g = serial();
	common::force_link();

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let seq = unsafe { oakengine_sequence_new(project, c"Export Seq".as_ptr()) };
	assert!(!seq.is_null());

	// Minimal legal params: a fresh handle with a filename. The export task
	// takes ownership of the params box (destroyed at task free).
	let params = oakengine_encoding_params_create();
	assert!(!params.is_null());
	assert_eq!(
		unsafe {
			oakengine_encoding_params_set_filename(
				params,
				c"/tmp/oakengine_it_task_export.mov".as_ptr(),
			)
		},
		0
	);

	let task = unsafe { oakengine_task_create_export(seq, params) };
	assert!(
		!task.is_null(),
		"export creation must succeed without GPU (creation only)"
	);

	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_task_title(task, buf.as_mut_ptr(), 256) };
	assert!(len > 0);
	assert!(read_buf(&mut buf).contains("Exporting"));

	// NULL / empty inputs: clean NULL, and the params handle stays owned by
	// the caller on the rejected path.
	assert!(unsafe { oakengine_task_create_export(std::ptr::null_mut(), params) }.is_null());
	let params2 = oakengine_encoding_params_create();
	assert!(unsafe { oakengine_task_create_export(seq, std::ptr::null_mut()) }.is_null());
	let empty_seq = Box::into_raw(Box::new(OakEngineSequence {
		handle: CHandle::null(),
	}));
	assert!(unsafe { oakengine_task_create_export(empty_seq, params2) }.is_null());
	unsafe { crate::codec::oakengine_encoding_params_destroy(params2) };
	unsafe { drop(Box::from_raw(empty_seq)) };

	assert_eq!(unsafe { oakengine_task_free(task) }, 0);

	// Release the sequence's borrowed facade box (release only frees the box).
	unsafe { free_box::<OakEngineSequence>(seq) };
	unsafe { oakengine_project_free(project) };
}

/// Running an export end-to-end: the facade test clip in a project +
/// sequence with full encoding params (mp4 / H.264 / AAC), run
/// synchronously through the real CPU render and the statically linked
/// FFmpeg encoder, with the output file asserted. The creation path above
/// covers the params-ownership matrix; this run proves the run itself
/// works in the test environment (formerly environment-gated on GPU).
#[test]
fn export_task_run_real_encoder() {
	let _g = serial();
	common::force_link();
	// The import/add-track/add-clip commands below are undoable; disable
	// the write-through backend so they never touch a library.
	let _storage = common::storage_off_guard();

	let media = std::env::temp_dir().join(format!(
		"oakengine-it-task-export-src-{}.mp4",
		std::process::id()
	));
	let out = std::env::temp_dir().join(format!(
		"oakengine-it-task-export-run-{}.mp4",
		std::process::id()
	));
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);
	let media_c = std::ffi::CString::new(media.to_string_lossy().into_owned()).unwrap();
	assert_eq!(
		unsafe { crate::testmedia::oakengine_testmedia_write_clip(media_c.as_ptr(), 64, 64, 10, 10) },
		0,
		"generate the source clip"
	);

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let footage =
		unsafe { crate::node::oakengine_project_import_footage(project, media_c.as_ptr()) };
	assert!(!footage.is_null(), "import the test clip");
	let seq = unsafe { oakengine_sequence_new(project, c"Export Run".as_ptr()) };
	assert!(!seq.is_null());
	assert_eq!(
		unsafe {
			crate::timeline::oakengine_sequence_set_video_params(seq, 64, 64, 10, 1, 1, 1, 0, 4, 0)
		},
		0
	);
	let vt = unsafe { crate::timeline::oakengine_sequence_add_track(seq, 0 /* video */) };
	assert!(vt >= 0);
	let clip = unsafe {
		crate::timeline::oakengine_sequence_add_footage_clip_ex(seq, footage, 0 /* video */, vt, 0, 10, 0)
	};
	assert!(!clip.is_null(), "place the video clip");
	unsafe { crate::node::oakengine_footage_free(footage) };

	let params = oakengine_encoding_params_create();
	assert!(!params.is_null());
	let out_c = std::ffi::CString::new(out.to_string_lossy().into_owned()).unwrap();
	assert_eq!(
		unsafe { oakengine_encoding_params_set_filename(params, out_c.as_ptr()) },
		0
	);
	// mp4 / H.264 / AAC, 64x64 @ 10 fps, 1 s.
	assert_eq!(
		unsafe { crate::codec::oakengine_encoding_params_set_format(params, 2) },
		0
	);
	let pod = crate::common::OakVideoParamsPod {
		width: 64,
		height: 64,
		time_base_num: 1,
		time_base_den: 10,
		format: 0,
		pixel_aspect_num: 1,
		pixel_aspect_den: 1,
		interlacing: 0,
		color_range: 0,
		divider: 1,
		video_type: 0,
		premultiplied_alpha: 0,
	};
	assert_eq!(
		unsafe { crate::codec::oakengine_encoding_params_enable_video(params, &pod, 1 /* H264 */) },
		0
	);
	assert_eq!(
		unsafe { crate::codec::oakengine_encoding_params_enable_audio(params, 48000, 0x3, 0, 12 /* AAC */) },
		0
	);
	let task = unsafe { oakengine_task_create_export(seq, params) };
	assert!(!task.is_null());
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 1);
	unsafe { oakengine_task_free(task) };

	let meta = std::fs::metadata(&out);
	assert!(
		meta.is_ok() && meta.unwrap().len() > 0,
		"the export run must write the output file"
	);

	unsafe { free_box::<OakEngineSequence>(seq) };
	unsafe { oakengine_project_free(project) };
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);
}

/// The proxy creator is a documented stub (the oaktask module has no
/// proxy-task factory on its C ABI): NULL for every input, including a
/// valid node.
#[test]
fn proxy_stub_always_returns_null() {
	let _g = serial();
	common::force_link();

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let root = unsafe { oakengine_project_root(project) };
	assert!(!root.is_null());

	assert!(unsafe { oakengine_task_create_proxy(root) }.is_null());

	unsafe { oakengine_node_free(root) };
	unsafe { oakengine_project_free(project) };
}

// ---------------------------------------------------------------------------
// Global task manager (serialized: the manager is process-wide)
// ---------------------------------------------------------------------------

/// The global manager lifecycle: lazy creation, empty queue, task
/// hand-over (`manager_add`), borrowed first-task handle, double-add
/// rejection, cancel, and the alive accounting of the borrowed box.
#[test]
fn task_manager_lifecycle() {
	let _g = serial();
	common::force_link();

	// The facade initializes the manager on first use; the handle is stable.
	let handle = oakengine_task_manager_handle();
	assert!(!handle.is_null());
	assert_eq!(oakengine_task_manager_handle(), handle);
	// Other (serialized) tests may have left finished tasks in the queue.
	crate::stubs::task::oaktask_manager_delete_finished();
	assert_eq!(oakengine_task_manager_count(), 0);

	// An empty queue has no first task.
	assert!(oakengine_task_manager_first().is_null());

	let baseline = alive_count();

	let task = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert_eq!(alive_count(), baseline + 1);

	// Handing the task to the manager transfers ownership; the handle box
	// stays alive until freed.
	assert_eq!(unsafe { oakengine_task_manager_add(task) }, 0);
	assert_eq!(oakengine_task_manager_count(), 1);

	// A second add of the same task is rejected with the module's E_STATE.
	assert_eq!(unsafe { oakengine_task_manager_add(task) }, OAKTASK_E_STATE);

	// The first task is a borrowed handle: count goes up by one, and freeing
	// the box returns it to baseline without deleting the manager's task.
	let first = oakengine_task_manager_first();
	assert!(!first.is_null());
	assert_eq!(alive_count(), baseline + 2);
	assert_eq!(unsafe { oakengine_task_free(first) }, 0);
	assert_eq!(alive_count(), baseline + 1);

	// Cancelling through the manager succeeds (the load task fails fast on
	// the missing file; cancel of a finished task is a documented no-op).
	assert_eq!(unsafe { oakengine_task_manager_cancel(task) }, 0);

	// Adding the manager's own borrowed handle is rejected with E_STATE
	// (the task is already running on the manager).
	let first2 = oakengine_task_manager_first();
	assert!(!first2.is_null());
	assert_eq!(
		unsafe { oakengine_task_manager_add(first2) },
		OAKTASK_E_STATE
	);
	assert_eq!(unsafe { oakengine_task_free(first2) }, 0);

	// NULL / empty inputs on the manager family.
	assert_eq!(
		unsafe { oakengine_task_manager_add(std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_task_manager_cancel(std::ptr::null_mut()) },
		-1
	);
	let empty = empty_task_box();
	assert_eq!(unsafe { oakengine_task_manager_add(empty) }, -1);
	assert_eq!(unsafe { oakengine_task_manager_cancel(empty) }, -1);
	unsafe { reclaim_empty_task_box(empty) };

	// Releasing the (now borrowed) facade box is safe: the manager owns the
	// task object and deletes it on cleanup; the alive counter returns to
	// baseline.
	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
	assert_eq!(alive_count(), baseline);

	// Finished tasks stay in the queue until delete_finished (the facade
	// exposes no delete export), so the count is still 1.
	assert_eq!(oakengine_task_manager_count(), 1);
}

// ---------------------------------------------------------------------------
// Load-task result / event subscription (new facade exports)
// ---------------------------------------------------------------------------

/// `oakengine_task_load_take_project` — NULL for NULL/empty tasks and for
/// tasks that are not load tasks; after a successful load run the project
/// comes back owned (released with `oakengine_project_free`). The success
/// path uses a real `.ove` written through `oakengine_project_save`.
#[test]
fn task_load_take_project_lifecycle() {
	let _g = serial();
	common::force_link();

	// NULL / empty task → NULL.
	assert!(unsafe { oakengine_task_load_take_project(std::ptr::null_mut()) }.is_null());
	let empty = empty_task_box();
	assert!(unsafe { oakengine_task_load_take_project(empty) }.is_null());
	unsafe { reclaim_empty_task_box(empty) };

	// A load task that never ran / failed has no project to take.
	let failed = unsafe { oakengine_task_create_project_load(c"/no/such/oak/project.ove".as_ptr()) };
	assert!(!failed.is_null());
	assert!(unsafe { oakengine_task_load_take_project(failed) }.is_null());
	assert_eq!(unsafe { oakengine_task_start_sync(failed) }, 0);
	assert!(unsafe { oakengine_task_load_take_project(failed) }.is_null());
	assert_eq!(unsafe { oakengine_task_free(failed) }, 0);

	// A non-load task (save) is not a load task → NULL.
	let project = unsafe { oakengine_project_create() };
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let save_path = std::env::temp_dir().join("oakengine_it_task_load_take.ovexml");
	let save_c = std::ffi::CString::new(save_path.to_str().unwrap()).unwrap();
	let save_task =
		unsafe { oakengine_task_create_project_save(project, 0, save_c.as_ptr(), std::ptr::null()) };
	assert!(!save_task.is_null());
	assert!(unsafe { oakengine_task_load_take_project(save_task) }.is_null());
	unsafe { oakengine_task_free(save_task) };

	// Success path: save a real project file, load it through the task and
	// take the loaded project.
	let ove_path = std::env::temp_dir().join("oakengine_it_task_load_take.ove");
	let ove_c = std::ffi::CString::new(ove_path.to_str().unwrap()).unwrap();
	assert_eq!(unsafe { oakengine_project_save(project, ove_c.as_ptr()) }, 0);
	let load_task = unsafe { oakengine_task_create_project_load(ove_c.as_ptr()) };
	assert!(!load_task.is_null());
	assert_eq!(
		unsafe { oakengine_task_start_sync(load_task) },
		1,
		"loading the project file just saved must succeed"
	);
	let loaded = unsafe { oakengine_task_load_take_project(load_task) };
	assert!(!loaded.is_null(), "a successful load yields the project");
	// The loaded project is a real, owned handle (name readable, freeable).
	let mut buf = [0 as c_char; 256];
	let name_len = unsafe { oakengine_project_filename(loaded, buf.as_mut_ptr(), 256) };
	assert!(name_len >= 0);
	unsafe { oakengine_project_free(loaded) };
	// Taking again returns NULL (the project was taken).
	assert!(unsafe { oakengine_task_load_take_project(load_task) }.is_null());
	assert_eq!(unsafe { oakengine_task_free(load_task) }, 0);

	unsafe { oakengine_project_free(project) };
	let _ = std::fs::remove_file(&save_path);
	let _ = std::fs::remove_file(&ove_path);
}

/// `oakengine_task_subscribe` — NULL task / NULL callback → facade
/// `OAKENGINE_E_INVALID` (-1); a valid subscription returns 0 and delivers
/// STARTED (0) + FINISHED (2) to the callback during a sync run (the
/// listener is one-shot, so re-running does not re-fire).
#[test]
fn task_subscribe_lifecycle() {
	let _g = serial();
	common::force_link();

	// NULL / empty task and NULL callback → facade E_INVALID.
	assert_eq!(
		unsafe { oakengine_task_subscribe(std::ptr::null_mut(), None, std::ptr::null_mut()) },
		-1
	);
	let empty = empty_task_box();
	assert_eq!(
		unsafe { oakengine_task_subscribe(empty, None, std::ptr::null_mut()) },
		-1
	);
	unsafe { reclaim_empty_task_box(empty) };

	// The recorder counts STARTED (0) / FINISHED (2) events.
	static STARTED: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);
	static FINISHED: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);
	unsafe extern "C" fn recorder(event_id: c_int, _value: f64, _userdata: *mut c_void) {
		match event_id {
			0 => {
				STARTED.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
			}
			2 => {
				FINISHED.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
			}
			_ => {}
		}
	}

	let task = unsafe { oakengine_task_create_project_load(c"".as_ptr()) };
	assert!(!task.is_null());

	// NULL callback on a real task → E_INVALID (the facade validates it).
	assert_eq!(
		unsafe { oakengine_task_subscribe(task, None, std::ptr::null_mut()) },
		-1
	);

	// A valid subscription returns 0; a second one replaces the first.
	assert_eq!(
		unsafe { oakengine_task_subscribe(task, Some(recorder), std::ptr::null_mut()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_task_subscribe(task, Some(recorder), std::ptr::null_mut()) },
		0
	);

	// A sync run fires STARTED then FINISHED (the empty-filename load fails,
	// but the events fire on every run transition).
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 0);
	assert_eq!(STARTED.load(std::sync::atomic::Ordering::SeqCst), 1);
	assert_eq!(FINISHED.load(std::sync::atomic::Ordering::SeqCst), 1);

	// The subscription is one-shot: a second run does not re-fire.
	assert_eq!(unsafe { oakengine_task_start_sync(task) }, 0);
	assert_eq!(STARTED.load(std::sync::atomic::Ordering::SeqCst), 1);

	assert_eq!(unsafe { oakengine_task_free(task) }, 0);
}
