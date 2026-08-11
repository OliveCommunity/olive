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

//! The boxed control block behind every `OakTaskTask` handle, mirroring
//! `src/task/c_api/taskhandle.h` (`oaktask_capi::TaskHandle`).
//!
//! `owns_task` is the owns role: true for factory-created tasks (the last
//! release drops the task) and false once the task runs on the manager
//! (`oaktask_task_start()` flips it; the manager drops the task) or for
//! borrowed wrappers (`oaktask_manager_at()`): releasing those only
//! destroys the box.
//!
//! The C++ `TaskHandle` carries its own `finished`/`succeeded` atomics that
//! the factory listener updates. The Rust [`crate::task::Task`] tracks these
//! internally (race-free through its `done` condvar), so the box reads them
//! from the task instead — the box only needs the downcast pointers for the
//! `take_*` accessors (Rust has no `dynamic_cast`).
//!
//! CPP-PARITY: src/task/c_api/taskhandle.h

use std::ffi::c_void;
use std::sync::atomic::{AtomicI32, Ordering};

use crate::handle::{make_owned, CHandle};
use crate::project::import::ProjectImportTask;
use crate::project::load::ProjectLoadBaseTask;
use crate::task::Task;

/// Concrete-task downcast, standing in for the C++ `dynamic_cast` in the
/// `take_*` accessors.
pub enum TaskImpl {
	/// Not a project load/import task.
	None,
	/// A `ProjectLoadBaseTask` (native or OTIO loader).
	LoadBase(*mut ProjectLoadBaseTask),
	/// A `ProjectImportTask`.
	Import(*mut ProjectImportTask),
}

/// Control block behind an `OakTaskTask` (see the module docs).
pub struct TaskHandleBox {
	/// The task the handle wraps (the outer task owning the behavior).
	pub task: *mut Task,
	/// Whether releasing the last reference drops the task.
	pub owned: bool,
	/// Whether the task has been handed to the manager.
	pub running_on_manager: bool,
	/// The owned task (factory case); taken by `oaktask_task_start`.
	pub owner: Option<Box<Task>>,
	/// Downcast target for the `take_*` accessors.
	pub impl_kind: TaskImpl,
}

impl Drop for TaskHandleBox {
	fn drop(&mut self) {
		// Mirrors `task_release`: the box is destroyed at zero refs, which
		// always decrements the alive count.
		ALIVE.fetch_sub(1, Ordering::SeqCst);
	}
}

// Safety: the box is only ever accessed through the C ABI entry points
// (never concurrently with its own mutation); the raw pointers inside are
// stable while the box lives.
unsafe impl Send for TaskHandleBox {}

/// Alive-count for leak assertions in tests (mirrors
/// `oaktask_capi::alive()`).
static ALIVE: AtomicI32 = AtomicI32::new(0);

/// Current alive handle count.
pub fn alive_count() -> i32 {
	ALIVE.load(Ordering::SeqCst)
}

/// Wrap an owned task (reference count 1). The handle owns the task and
/// drops it on the final release.
///
/// CPP-PARITY: src/task/c_api/taskhandle.h (wrap)
pub fn wrap_owned(task: Box<Task>) -> CHandle {
	wrap_owned_with_impl(task, TaskImpl::None)
}

/// Wrap an owned task with a downcast target for the `take_*` accessors.
pub fn wrap_owned_with_impl(task: Box<Task>, impl_kind: TaskImpl) -> CHandle {
	let ptr = Box::into_raw(task);
	ALIVE.fetch_add(1, Ordering::SeqCst);
	// Safety: `ptr` is a live, uniquely owned `Box<Task>`.
	let handle = make_owned(TaskHandleBox {
		task: ptr,
		owned: true,
		running_on_manager: false,
		owner: unsafe { Some(Box::from_raw(ptr)) },
		impl_kind,
	});
	handle
}

/// Wrap a manager-owned (borrowed) task. Releasing the handle does NOT drop
/// the task.
///
/// CPP-PARITY: src/task/c_api/taskhandle.h (wrap_borrowed)
pub fn wrap_borrowed(ptr: *mut Task) -> CHandle {
	if ptr.is_null() {
		return CHandle::null();
	}
	ALIVE.fetch_add(1, Ordering::SeqCst);
	make_owned(TaskHandleBox {
		task: ptr,
		owned: false,
		running_on_manager: true,
		owner: None,
		impl_kind: TaskImpl::None,
	})
}

/// Typed view of a task handle box; `None` for empty handles.
///
/// # Safety
/// The handle must have been created by this module.
pub fn get_task(h: &CHandle) -> Option<&TaskHandleBox> {
	unsafe { crate::handle::get::<TaskHandleBox>(h) }
}

/// Typed mutable view of a task handle box; `None` for empty handles.
///
/// # Safety
/// The handle must have been created by this module and not be shared with
/// a concurrent mutable access.
pub fn get_task_mut(h: &CHandle) -> Option<&mut TaskHandleBox> {
	unsafe { crate::handle::get_mut::<TaskHandleBox>(h) }
}

/// Two-stage string copy matching `oaktask_capi::copy_string`: returns the
/// needed size (`len + 1`); writes only when the buffer is non-null and
/// large enough.
///
/// CPP-PARITY: src/task/c_api/taskhandle.h (copy_string)
pub fn copy_string(value: &str, buf: *mut std::ffi::c_char, buf_size: i32) -> i32 {
	let needed = value.len() as i32 + 1;
	if !buf.is_null() && buf_size >= needed {
		unsafe {
			std::ptr::copy_nonoverlapping(
				value.as_ptr() as *const std::ffi::c_char,
				buf,
				value.len(),
			);
			*buf.add(value.len()) = 0;
		}
	}
	needed
}

/// Read a NUL-terminated C string into a Rust `String` (lossy); empty when
/// the pointer is null.
///
/// # Safety
/// `ptr` must be a valid NUL-terminated C string or null.
pub unsafe fn cstr_to_string(ptr: *const std::ffi::c_char) -> String {
	if ptr.is_null() {
		return String::new();
	}
	unsafe { std::ffi::CStr::from_ptr(ptr) }
		.to_string_lossy()
		.into_owned()
}

/// Build a NUL-terminated C string for the duration of the call (leaked).
pub fn cstr(s: &str) -> *const std::ffi::c_char {
	let mut bytes = s.as_bytes().to_vec();
	bytes.push(0);
	bytes.leak().as_ptr() as *const std::ffi::c_char
}

/// `userdata` payload converted to a `Send`-friendly form for closures.
pub fn userdata_usize(userdata: *mut c_void) -> usize {
	userdata as usize
}
