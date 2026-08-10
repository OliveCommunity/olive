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

//! `engine/include/oakengine/plugin.h` over the oakplugin module.
//!
//! The active-viewer provider and progress-reporter factory are pure
//! facade state (module 00 analogues of the C++ capi's statics): the UI
//! registers C callbacks here, and the plugin host consumes them once the
//! module exposes the corresponding registration points
//! (`oakplugin_*_set_*_provider`). Until then the callbacks are stored
//! and reported as registered.

use std::ffi::{c_char, c_int, c_void};
use std::sync::{Mutex, OnceLock};

use crate::bridge::plugin as p;
use crate::error::Error;
use crate::handle::guard;

/// `oakengine_plugin_active_viewer_fn` — returns the active viewer node.
pub type ActiveViewerFn =
	unsafe extern "C" fn(userdata: *mut c_void) -> *mut crate::handle::OakEngineNode;

/// `oakengine_plugin_reporter_create_fn` — creates a UI progress reporter.
pub type ReporterCreateFn =
	unsafe extern "C" fn(message: *const c_char, title: *const c_char, userdata: *mut c_void) -> *mut c_void;
/// `oakengine_plugin_reporter_destroy_fn` — destroys a reporter.
pub type ReporterDestroyFn = unsafe extern "C" fn(reporter: *mut c_void, userdata: *mut c_void);
/// `oakengine_plugin_reporter_is_cancelled_fn` — 1 when cancelled.
pub type ReporterIsCancelledFn = unsafe extern "C" fn(reporter: *mut c_void, userdata: *mut c_void) -> c_int;
/// `oakengine_plugin_reporter_set_progress_fn` — progress update.
pub type ReporterSetProgressFn =
	unsafe extern "C" fn(reporter: *mut c_void, progress: f64, userdata: *mut c_void);

struct ProviderState {
	active_viewer: Option<(Option<ActiveViewerFn>, usize)>,
	reporter: Option<(
		Option<ReporterCreateFn>,
		Option<ReporterDestroyFn>,
		Option<ReporterIsCancelledFn>,
		Option<ReporterSetProgressFn>,
		usize,
	)>,
}

fn state() -> &'static Mutex<ProviderState> {
	static STATE: OnceLock<Mutex<ProviderState>> = OnceLock::new();
	STATE.get_or_init(|| {
		Mutex::new(ProviderState {
			active_viewer: None,
			reporter: None,
		})
	})
}

/// `oakengine_plugin_set_active_viewer_provider` — register the active
/// viewer callback (NULL clears it).
#[no_mangle]
pub extern "C" fn oakengine_plugin_set_active_viewer_provider(
	fn_: Option<ActiveViewerFn>,
	userdata: *mut c_void,
) -> c_int {
	guard(|| {
		let mut s = state().lock().unwrap_or_else(|e| e.into_inner());
		s.active_viewer = Some((fn_, userdata as usize));
		Ok(())
	})
}

/// `oakengine_plugin_set_progress_reporter_factory` — register the
/// progress-reporter factory callbacks (NULL clears them).
#[no_mangle]
pub extern "C" fn oakengine_plugin_set_progress_reporter_factory(
	create: Option<ReporterCreateFn>,
	destroy: Option<ReporterDestroyFn>,
	is_cancelled: Option<ReporterIsCancelledFn>,
	set_progress: Option<ReporterSetProgressFn>,
	userdata: *mut c_void,
) -> c_int {
	guard(|| {
		let mut s = state().lock().unwrap_or_else(|e| e.into_inner());
		s.reporter = Some((create, destroy, is_cancelled, set_progress, userdata as usize));
		Ok(())
	})
}

/// `oakengine_plugin_load_plugins` — scan the plugin bundle directory
/// `path` (oakplugin_host_scan).
#[no_mangle]
pub unsafe extern "C" fn oakengine_plugin_load_plugins(path: *const c_char) -> c_int {
	guard(|| unsafe {
		if path.is_null() {
			return Err(Error::Invalid);
		}
		let dirs: [*const c_char; 1] = [path];
		Error::from_module(p::oakplugin_host_scan(dirs.as_ptr(), 1))
	})
}

/// `oakengine_plugin_node_push_button_clicked` — not yet backed: the
/// oakplugin crate exposes no push-button API (the OFX button-param
/// trigger is C++-only). Returns `OAKENGINE_E_FAILED`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_plugin_node_push_button_clicked(
	_node: *mut crate::handle::OakEngineNode,
	_button_id: *const c_char,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}
