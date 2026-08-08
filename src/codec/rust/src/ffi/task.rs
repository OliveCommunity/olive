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

//! `include/codec/task.h` exports.
//!
//! Complete inventory: set_task_submit_cb / task_submit_is_registered.
//! The callback typedef and request struct are mirrored in
//! [`crate::task`].

use std::ffi::c_int;

use crate::handle;

/// `oakcodec_set_task_submit_cb`: register (or replace, or clear with
/// NULL) the global task submit callback.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_set_task_submit_cb(
	cb: Option<crate::task::OakCodecTaskSubmitFn>,
	userdata: *mut std::ffi::c_void,
) {
	handle::guard_void(|| {
		crate::task::set_task_submit_cb_extern(cb, userdata);
	})
}

/// `oakcodec_task_submit_is_registered`: 1 when a callback is set.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_task_submit_is_registered() -> c_int {
	handle::guard_raw(|| {
		if crate::task::task_submit_is_registered() {
			1
		} else {
			0
		}
	})
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::conformmanager::test_util::{accept_cb, REG_LOCK};
	use crate::task::set_task_submit_cb_extern;

	#[test]
	fn register_query_clear() {
		let _g = REG_LOCK.lock().unwrap();

		assert_eq!(unsafe { oakcodec_task_submit_is_registered() }, 0);

		unsafe { oakcodec_set_task_submit_cb(Some(accept_cb), std::ptr::null_mut()) };
		assert_eq!(unsafe { oakcodec_task_submit_is_registered() }, 1);

		unsafe { oakcodec_set_task_submit_cb(None, std::ptr::null_mut()) };
		assert_eq!(unsafe { oakcodec_task_submit_is_registered() }, 0);

		// Restore a clean slate for the other modules.
		set_task_submit_cb_extern(None, std::ptr::null_mut());
	}
}
