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

//! Smoke tests for the plugin family (`engine/include/oakengine/plugin.h`).

#[path = "common/mod.rs"]
mod common;

use oakengine::plugin::{
	oakengine_plugin_load_plugins, oakengine_plugin_node_push_button_clicked,
	oakengine_plugin_set_active_viewer_provider, oakengine_plugin_set_progress_reporter_factory,
};

/// Callback registration round-trips (NULL clears).
#[test]
fn provider_registration() {
	unsafe extern "C" fn viewer(_userdata: *mut std::ffi::c_void) -> *mut oakengine::handle::OakEngineNode {
		std::ptr::null_mut()
	}
	assert_eq!(unsafe {
		oakengine_plugin_set_active_viewer_provider(Some(viewer), std::ptr::null_mut())
	}, 0);
	assert_eq!(unsafe {
		oakengine_plugin_set_active_viewer_provider(None, std::ptr::null_mut())
	}, 0);

	unsafe extern "C" fn create(
		_message: *const std::ffi::c_char,
		_title: *const std::ffi::c_char,
		_userdata: *mut std::ffi::c_void,
	) -> *mut std::ffi::c_void {
		std::ptr::null_mut()
	}
	assert_eq!(unsafe {
		oakengine_plugin_set_progress_reporter_factory(
			Some(create), None, None, None, std::ptr::null_mut(),
		)
	}, 0);
	assert_eq!(unsafe {
		oakengine_plugin_set_progress_reporter_factory(None, None, None, None, std::ptr::null_mut())
	}, 0);
}

/// NULL path fails with E_INVALID.
#[test]
fn load_plugins_null_path() {
	assert_eq!(unsafe { oakengine_plugin_load_plugins(std::ptr::null()) }, -1);
}

/// Push-button click is a documented stub (oakplugin has no button API).
#[test]
fn push_button_unbacked() {
	assert_eq!(
		unsafe { oakengine_plugin_node_push_button_clicked(std::ptr::null_mut(), c"btn".as_ptr()) },
		-3
	);
}
