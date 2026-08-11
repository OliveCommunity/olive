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

//! oaktimeline C ABI imports (sequence markers/work area, edit
//! commands used by sequence setup). dlsym-resolved (see [`super`]).

use crate::handle::CHandle;

/// `oaktimeline_marker_list_create`.
pub fn marker_list_create() -> Option<CHandle> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn() -> CHandle;
	dlsym::call::<F, CHandle>("oaktimeline_marker_list_create", |f| unsafe { f() })
}

/// `oaktimeline_marker_list_free`.
pub fn marker_list_free(list: *mut CHandle) {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*mut CHandle);
	if let Some(f) = dlsym::call::<F, ()>("oaktimeline_marker_list_free", |f| unsafe { f(list) }) {
		let _ = f;
	}
}

/// `oaktimeline_workarea_create`.
pub fn workarea_create() -> Option<CHandle> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn() -> CHandle;
	dlsym::call::<F, CHandle>("oaktimeline_workarea_create", |f| unsafe { f() })
}

/// `oaktimeline_workarea_free`.
pub fn workarea_free(w: *mut CHandle) {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*mut CHandle);
	if let Some(f) = dlsym::call::<F, ()>("oaktimeline_workarea_free", |f| unsafe { f(w) }) {
		let _ = f;
	}
}

/// `oaktimeline_add_track_command`.
pub fn add_track_command(list: CHandle) -> Option<CHandle> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle) -> CHandle;
	dlsym::call::<F, CHandle>("oaktimeline_add_track_command", |f| unsafe { f(list) })
}
