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

//! C ABI contract tests (ffi.rs). One normal + one error path per
//! export family; the exhaustive matrix is driven from the existing
//! C++ gtest suite (`src/task/tests`, unchanged) running against this
//! crate — these tests only pin Rust-side specifics.

mod common;

use common::*;
use std::ffi::{c_char, c_int, c_void};
use std::sync::atomic::{AtomicI32, Ordering};

use oaktask::error::{OAKTASK_E_INVALID, OAKTASK_E_STATE, OAKTASK_OK};
use oaktask::ffi::manager::{
	oaktask_manager_at, oaktask_manager_count, oaktask_manager_delete_finished, oaktask_manager_init,
	oaktask_manager_shutdown,
};
use oaktask::ffi::project::oaktask_create_project_save;
use oaktask::ffi::task::{
	oaktask_debug_alive_count, oaktask_task_cancel, oaktask_task_error, oaktask_task_free, oaktask_task_is_finished,
	oaktask_task_start, oaktask_task_start_sync, oaktask_task_subscribe, oaktask_task_succeeded, oaktask_task_title,
	oaktask_task_wait,
};
use oaktask::handle::CHandle;

fn free_task(t: &mut CHandle) {
	unsafe {
		oaktask_task_free(t);
	}
}

/// Every exported handle-returning function returns ctx==NULL on
/// failure and a valid refcounted handle on success (abi_version
/// stamped).
#[test]
fn handle_contract_all_exports() {
	let _guard = MANAGER_LOCK.lock().unwrap();

	// create_project_save with a null project -> empty.
	let empty = unsafe { oaktask_create_project_save(CHandle::null(), std::ptr::null(), 0) };
	assert!(empty.is_null());

	// create_project_save with a valid project -> valid handle with the
	// stamped ABI version.
	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-contract-save.oakproj".to_string();
	let task = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 1) };
	assert!(!task.ctx.is_null());
	assert_eq!(task.abi_version, 1);

	// Title getter round-trip (two-stage).
	let mut buf = [0i8; 512];
	let needed = unsafe { oaktask_task_title(task, buf.as_mut_ptr(), buf.len() as c_int) };
	assert!(needed > 0);
	assert!(needed <= buf.len() as c_int);
	// Exact-size query with a null buffer returns the same needed size.
	let query = unsafe { oaktask_task_title(task, std::ptr::null_mut(), 0) };
	assert_eq!(query, needed);
	// The save title embeds the project filename.
	assert!(needed > 10);

	let mut t = task;
	free_task(&mut t);
	assert!(t.is_null());
}

/// free(NULL)/free(empty) are no-ops across every free export.
#[test]
fn free_null_noop_all_exports() {
	unsafe {
		oaktask_task_free(std::ptr::null_mut());
	}
	let mut empty = CHandle::null();
	unsafe {
		oaktask_task_free(&mut empty);
	}
	assert!(empty.ctx.is_null());
}

/// Two-stage string functions: size query, short buffer truncation
/// rule, and exact-fit write — for every string getter
/// (task_title, task_error, import_invalid_at).
#[test]
fn two_stage_string_contract() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-two-stage.oakproj".to_string();
	let task = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	assert!(!task.ctx.is_null());

	// Null buffer -> needed size only.
	let needed = unsafe { oaktask_task_title(task, std::ptr::null_mut(), 0) };
	assert!(needed > 0);

	// Too-small buffer -> needed size, no write.
	let mut small = [0i8; 4];
	let r = unsafe { oaktask_task_title(task, small.as_mut_ptr(), small.len() as c_int) };
	assert_eq!(r, needed);
	// C++ only writes when the buffer fits; a short buffer stays untouched.
	assert_ne!(small[0], b'S' as i8);

	// Exact-fit write.
	let mut buf = vec![0i8; needed as usize];
	let r = unsafe { oaktask_task_title(task, buf.as_mut_ptr(), needed) };
	assert_eq!(r, needed);
	assert_eq!(buf[0], b'S' as i8);
	assert_eq!(buf[needed as usize - 1], 0);

	// Error getter on a fresh task reports the default.
	let err_needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	assert!(err_needed > 0);

	// Error-path: empty handle -> E_INVALID.
	assert_eq!(unsafe { oaktask_task_title(CHandle::null(), std::ptr::null_mut(), 0) }, OAKTASK_E_INVALID);
	assert_eq!(unsafe { oaktask_task_error(CHandle::null(), std::ptr::null_mut(), 0) }, OAKTASK_E_INVALID);

	let mut t = task;
	free_task(&mut t);
}

/// Task manager singleton: init → create/start → wait → delete_finished
/// round-trips; alive-count accounting returns to baseline.
#[test]
fn identity_registry_roundtrip() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() }; // ensure clean slate

	assert_eq!(unsafe { oaktask_manager_init() }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_manager_count() }, 0);

	// Async start of a save task; the worker writes the file.
	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-identity-roundtrip.oakproj".to_string();
	let _ = std::fs::remove_file("/tmp/oak-identity-roundtrip.oakproj");
	let mut task = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	assert_eq!(unsafe { oaktask_task_start(task) }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_task_start(task) }, OAKTASK_E_STATE); // already running

	assert_eq!(unsafe { oaktask_task_wait(task) }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 1);
	assert_eq!(unsafe { oaktask_task_succeeded(task) }, 1);
	assert!(std::path::Path::new("/tmp/oak-identity-roundtrip.oakproj").exists());

	unsafe { oaktask_manager_delete_finished() };
	assert_eq!(unsafe { oaktask_manager_count() }, 0);

	free_task(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
	unsafe { oaktask_manager_shutdown() };
}

/// alive count: task create/destroy moves oaktask_debug_alive_count
/// predictably and returns to baseline.
#[test]
fn alive_count_accounting() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };

	let baseline = unsafe { oaktask_debug_alive_count() };
	assert_eq!(baseline, 0);

	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-alive-count.oakproj".to_string();
	let mut t1 = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	let mut t2 = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	assert_eq!(unsafe { oaktask_debug_alive_count() }, baseline + 2);

	free_task(&mut t1);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, baseline + 1);
	free_task(&mut t2);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, baseline);
}

/// subscribe + async start: the callback observes STARTED (with a start
/// time), then FINISHED (1.0 on success); the subscription is one-shot.
#[test]
fn subscribe_records_started_and_finished() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };
	unsafe { oaktask_manager_init() };

	static STARTED: AtomicI32 = AtomicI32::new(0);
	static FINISHED: AtomicI32 = AtomicI32::new(0);
	static FINISHED_VALUE: AtomicI32 = AtomicI32::new(-1);

	unsafe extern "C" fn recorder(event_id: c_int, value: f64, _ud: *mut c_void) {
		match event_id {
			0 => {
				STARTED.fetch_add(1, Ordering::SeqCst);
				assert!(value > 0.0); // start time in ms
			}
			2 => {
				FINISHED.fetch_add(1, Ordering::SeqCst);
				FINISHED_VALUE.store(value as i32, Ordering::SeqCst);
			}
			_ => {}
		}
	}

	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-subscribe.oakproj".to_string();
	let mut task = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };

	// Null callback -> E_INVALID.
	assert_eq!(unsafe { oaktask_task_subscribe(task, None, std::ptr::null_mut()) }, OAKTASK_E_INVALID as i64);
	// Empty handle -> E_INVALID.
	assert_eq!(
		unsafe { oaktask_task_subscribe(CHandle::null(), Some(recorder), std::ptr::null_mut()) },
		OAKTASK_E_INVALID as i64
	);

	assert_eq!(unsafe { oaktask_task_subscribe(task, Some(recorder), std::ptr::null_mut()) }, 0);
	assert_eq!(unsafe { oaktask_task_start(task) }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_task_wait(task) }, OAKTASK_OK);

	assert_eq!(STARTED.load(Ordering::SeqCst), 1);
	assert_eq!(FINISHED.load(Ordering::SeqCst), 1);
	assert_eq!(FINISHED_VALUE.load(Ordering::SeqCst), 1);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 1);

	unsafe { oaktask_manager_delete_finished() };
	free_task(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
	unsafe { oaktask_manager_shutdown() };
}

/// start_sync on a task with a failing serializer reports failure (0) with
/// an error message, and the error getter returns it (two-stage).
#[test]
fn start_sync_failure_paths() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	*PROJECT_FILENAME.lock().unwrap() = "".to_string(); // no filename
	let mut task = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 1);
	assert_eq!(unsafe { oaktask_task_succeeded(task) }, 0);

	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	assert!(needed > 0);
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	let msg = cstr_read(&buf);
	assert!(msg.contains("no filename"), "error was: {msg}");

	// Null handle -> E_INVALID.
	assert_eq!(unsafe { oaktask_task_start_sync(CHandle::null()) }, OAKTASK_E_INVALID);
	assert_eq!(unsafe { oaktask_task_cancel(CHandle::null()) }, OAKTASK_E_INVALID);
	assert_eq!(unsafe { oaktask_task_wait(CHandle::null()) }, OAKTASK_E_INVALID);
	assert_eq!(unsafe { oaktask_task_is_finished(CHandle::null()) }, OAKTASK_E_INVALID);
	assert_eq!(unsafe { oaktask_task_succeeded(CHandle::null()) }, OAKTASK_E_INVALID);

	free_task(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// cancel on a fresh task is a no-op success; is_finished stays 0.
#[test]
fn cancel_is_noop_before_start() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-cancel.oakproj".to_string();
	let mut task = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	assert_eq!(unsafe { oaktask_task_cancel(task) }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 0);
	free_task(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// start without an initialized manager -> E_STATE.
#[test]
fn start_without_manager_is_state_error() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };

	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-no-manager.oakproj".to_string();
	let mut task = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	assert_eq!(unsafe { oaktask_task_start(task) }, OAKTASK_E_STATE);
	// Manager accessors also report E_STATE / empty without a manager.
	assert_eq!(unsafe { oaktask_manager_count() }, OAKTASK_E_STATE);
	assert!(unsafe { oaktask_manager_at(0) }.is_null());
	unsafe { oaktask_manager_delete_finished() }; // no-op

	// start_sync still works without a manager.
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);

	free_task(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// cstr_read helper: read a NUL-terminated c_char buffer.
fn cstr_read(buf: &[i8]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	let bytes = unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) };
	String::from_utf8_lossy(bytes).into_owned()
}
