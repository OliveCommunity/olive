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

//! `oaktask_manager_*` export symbols mirroring `include/task/manager.h`.
//!
//! Full symbol inventory (header-authoritative):
//!   - `oaktask_manager_init(void) -> int`
//!   - `oaktask_manager_shutdown(void) -> void`
//!   - `oaktask_register_codec_submitter(void) -> int`
//!   - `oaktask_manager_count(void) -> int`
//!   - `oaktask_manager_at(int i) -> OakTaskTask`
//!   - `oaktask_manager_delete_finished(void) -> void`

use std::ffi::c_int;

use crate::error::{OAKTASK_E_STATE, OAKTASK_OK};
use crate::ffi::taskhandle::wrap_borrowed;
use crate::handle::CHandle;
use crate::manager::TaskManager;

/// `oaktask_manager_init` (`include/task/manager.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_manager_init() -> c_int {
	if let Err(_) = TaskManager::init() {
		return OAKTASK_E_STATE;
	}
	// Register the codec task submitter (mirrors the C++ init sequence).
	let _ = crate::codecbridge::register_codec_task_submitter();
	TaskManager::with_manager_mut(|m| m.set_codec_submitter_registered(true));
	OAKTASK_OK
}

/// `oaktask_manager_shutdown` (`include/task/manager.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_manager_shutdown() {
	TaskManager::with_manager_mut(|m| m.set_codec_submitter_registered(false));
	let _ = crate::codecbridge::unregister_codec_task_submitter();
	TaskManager::shutdown();
}

/// `oaktask_register_codec_submitter` (`include/task/manager.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_register_codec_submitter() -> c_int {
	match crate::codecbridge::register_codec_task_submitter() {
		Ok(()) => OAKTASK_OK,
		Err(_) => OAKTASK_OK,
	}
}

/// `oaktask_manager_count` (`include/task/manager.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_manager_count() -> c_int {
	match TaskManager::with_manager(|m| m.get_task_count()) {
		Some(count) => count as c_int,
		None => OAKTASK_E_STATE,
	}
}

/// `oaktask_manager_at` (`include/task/manager.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_manager_at(i: c_int) -> CHandle {
	// No manager (or out of range) -> empty handle, mirroring the C++
	// `OakTaskTask{}` return.
	let ptr = match TaskManager::with_manager(|m| m.task_ptr_at(i as usize)) {
		Some(Ok(ptr)) => ptr,
		_ => return CHandle::null(),
	};
	wrap_borrowed(ptr)
}

/// `oaktask_manager_delete_finished` (`include/task/manager.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_manager_delete_finished() {
	// Collect the finished entries and join their threads without holding
	// the manager lock (a worker's finish callback may call back into the
	// manager).
	let entries = TaskManager::with_manager_mut(|m| m.drain_finished()).unwrap_or_default();
	for (_task, handle) in entries {
		let _ = handle.join();
	}
}
