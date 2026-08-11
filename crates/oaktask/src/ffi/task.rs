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

//! `oaktask_task_*` export symbols mirroring `include/task/task.h`.
//!
//! Full symbol inventory (header-authoritative):
//!   - `oaktask_task_free(OakTaskTask *t) -> void`
//!   - `oaktask_task_start_sync(OakTaskTask t) -> int` (1 = succeeded)
//!   - `oaktask_task_start(OakTaskTask t) -> int`
//!   - `oaktask_task_cancel(OakTaskTask t) -> int`
//!   - `oaktask_task_wait(OakTaskTask t) -> int`
//!   - `oaktask_task_is_finished(OakTaskTask t) -> int`
//!   - `oaktask_task_succeeded(OakTaskTask t) -> int`
//!   - `oaktask_task_title(OakTaskTask t, char *buf, int buf_size) -> int` (two-stage)
//!   - `oaktask_task_error(OakTaskTask t, char *buf, int buf_size) -> int` (two-stage)
//!   - `oaktask_task_subscribe(OakTaskTask t, oaktask_event_fn fn, void *userdata) -> int64_t`
//!   - `oaktask_debug_alive_count(void) -> int`

use std::ffi::{c_char, c_int, c_void};
use std::sync::Arc;

use crate::error::OAKTASK_E_INVALID;
use crate::ffi::taskhandle::{copy_string, get_task, get_task_mut, userdata_usize};
use crate::handle::CHandle;
use crate::manager::TaskManager;
use crate::task::{SubscriberState, TaskEvent};

/// `oaktask_event_fn` callback (`include/task/task.h`).
pub type OakTaskEventFn = unsafe extern "C" fn(event_id: c_int, value: f64, userdata: *mut c_void);

/// Event ids (`include/task/task.h`).
const OAKTASK_EVENT_STARTED: c_int = 0;
const OAKTASK_EVENT_PROGRESS: c_int = 1;
const OAKTASK_EVENT_FINISHED: c_int = 2;

/// `oaktask_task_free` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_free(t: *mut CHandle) {
	if t.is_null() {
		return;
	}
	let handle = unsafe { &mut *t };
	if handle.ctx.is_null() {
		return;
	}
	// Release one reference, then clear ctx (NULL / empty-handle no-op).
	if let Some(release) = handle.release {
		unsafe {
			release(handle.ctx);
		}
	}
	handle.ctx = std::ptr::null_mut();
}

/// `oaktask_task_start_sync` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_start_sync(t: CHandle) -> c_int {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	let result = unsafe { (&mut *h.task).start() };
	if result.is_ok() {
		1
	} else {
		0
	}
}

/// `oaktask_task_start` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_start(t: CHandle) -> c_int {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	if !TaskManager::instance().is_some() {
		return crate::error::OAKTASK_E_STATE;
	}
	if h.running_on_manager {
		return crate::error::OAKTASK_E_STATE;
	}
	let h = match get_task_mut(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	if h.owner.is_none() {
		return crate::error::OAKTASK_E_STATE;
	}

	// Transfer ownership to the manager; releasing the handle afterwards
	// only frees the box. The manager was checked above and cannot vanish
	// between the check and the transfer in a single-threaded caller.
	let boxed = h.owner.take().unwrap();
	let task_ptr = h.task;
	TaskManager::with_manager_mut(|m| m.add_task(boxed));
	// The box moved into the manager; the raw pointer is unchanged.
	let h = get_task_mut(&t).unwrap();
	h.running_on_manager = true;
	h.owned = false;
	let _ = task_ptr;
	crate::error::OAKTASK_OK
}

/// `oaktask_task_cancel` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_cancel(t: CHandle) -> c_int {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	unsafe {
		(&mut *h.task).cancel();
	}
	crate::error::OAKTASK_OK
}

/// `oaktask_task_wait` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_wait(t: CHandle) -> c_int {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	if h.running_on_manager {
		let task_ptr = h.task;
		// C++ semantics: wait cancels the task and blocks until it
		// finishes. The join happens without holding the manager lock.
		TaskManager::with_manager_mut(|m| m.cancel_task_by_ptr(task_ptr));
		let handle = TaskManager::with_manager_mut(|m| m.take_thread_by_ptr(task_ptr)).flatten();
		if let Some(handle) = handle {
			let _ = handle.join();
		}
	}
	crate::error::OAKTASK_OK
}

/// `oaktask_task_is_finished` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_is_finished(t: CHandle) -> c_int {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	if unsafe { (*h.task).is_finished() } {
		1
	} else {
		0
	}
}

/// `oaktask_task_succeeded` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_succeeded(t: CHandle) -> c_int {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	if unsafe { (*h.task).succeeded() } {
		1
	} else {
		0
	}
}

/// `oaktask_task_title` (`include/task/task.h`, two-stage string getter).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_title(
	t: CHandle,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	copy_string(unsafe { (*h.task).title() }, buf, buf_size)
}

/// `oaktask_task_error` (`include/task/task.h`, two-stage string getter).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_error(
	t: CHandle,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID,
	};
	let value = unsafe { (*h.task).error() }.unwrap_or("Unknown error");
	copy_string(value, buf, buf_size)
}

/// `oaktask_task_subscribe` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_task_subscribe(
	t: CHandle,
	cb: Option<OakTaskEventFn>,
	userdata: *mut c_void,
) -> i64 {
	let h = match get_task(&t) {
		Some(h) => h,
		None => return OAKTASK_E_INVALID as i64,
	};
	let Some(cb) = cb else {
		return OAKTASK_E_INVALID as i64;
	};

	let state = Arc::new(SubscriberState::default());
	let ud = userdata_usize(userdata);

	// One subscription replaces the previous one (the C++ `listeners_`
	// vector holds at most the factory listener in practice; here the
	// factory listener is gone — the task tracks finished internally).
	unsafe {
		(*h.task).set_subscriber(state.clone());
		(*h.task).set_event_listener(Box::new(move |ev: TaskEvent| {
			let (event_id, value) = match ev {
				TaskEvent::Started => (
					OAKTASK_EVENT_STARTED,
					state.start_ms.load(std::sync::atomic::Ordering::SeqCst) as f64,
				),
				TaskEvent::Progress(p) => (OAKTASK_EVENT_PROGRESS, p),
				TaskEvent::Finished => (
					OAKTASK_EVENT_FINISHED,
					state
						.finished_value
						.load(std::sync::atomic::Ordering::SeqCst) as f64,
				),
			};
			cb(event_id, value, ud as *mut c_void);
		}));
	}
	0
}

/// `oaktask_debug_alive_count` (`include/task/task.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_debug_alive_count() -> c_int {
	crate::ffi::taskhandle::alive_count()
}
