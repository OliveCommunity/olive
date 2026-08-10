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

//! C ABI export layer: implements M10 §2.2/§2.3 verbatim
//! (`oakstorage_probe/open/save/project_free/project_take_project/
//! project_project/project_uri/last_error/debug_alive_count` and the
//! backend vtable registration pair).

use std::ffi::{c_char, c_int, c_uint, c_void};

use crate::handle::CHandle;

/// Save options bitmask: compress the ove-xml container.
pub const OAKSTORAGE_SAVE_COMPRESS: c_uint = 0x1;

/// `oakstorage_probe`: which backend claims this URI (two-stage
/// string; negative = E_NO_BACKEND).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_probe(uri: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int {
	todo!()
}

/// `oakstorage_open`: open a project session by URI (owned handle;
/// `result_code` receives an OAKSTORAGE_* code incl. the positive
/// version info codes).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_open(uri: *const c_char, result_code: *mut c_int) -> CHandle {
	todo!()
}

/// `oakstorage_save`: save a project to a URI (options bitmask;
/// unknown bits ignored by backends).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_save(
	project: CHandle,
	uri: *const c_char,
	options: c_uint,
) -> c_int {
	todo!()
}

/// `oakstorage_project_free` (NULL/empty no-op).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_project_free(session: *mut CHandle) {
	todo!()
}

/// `oakstorage_project_take_project`: ownership transfer out of the
/// session (the session becomes an empty shell; still free it).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_project_take_project(session: CHandle) -> CHandle {
	todo!()
}

/// `oakstorage_project_project`: borrowed project handle (empty after
/// take).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_project_project(session: CHandle) -> CHandle {
	todo!()
}

/// `oakstorage_project_uri` (two-stage string).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_project_uri(
	session: CHandle,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	todo!()
}

/// `oakstorage_last_error` (two-stage string; per-thread last error
/// detail).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_last_error(buf: *mut c_char, buf_size: c_int) -> c_int {
	todo!()
}

/// `oakstorage_debug_alive_count` (leak assertions in tests).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_debug_alive_count() -> c_int {
	todo!()
}

/// C-side backend vtable (M10 §2.3; the layout is the C header's
/// struct — implementer copies it field-for-field).
#[repr(C)]
pub struct OakStorageBackendVtable {
	/// Backend name (static string).
	pub name: *const c_char,
	/// URI scheme served (static string).
	pub uri_scheme: *const c_char,
	/// can_handle.
	pub can_handle: Option<unsafe extern "C" fn(uri: *const c_char) -> c_int>,
	/// load.
	pub load: Option<
		unsafe extern "C" fn(
			uri: *const c_char,
			result_code: *mut c_int,
			err_buf: *mut c_char,
			err_buf_size: c_int,
		) -> CHandle,
	>,
	/// save.
	pub save: Option<
		unsafe extern "C" fn(
			project: CHandle,
			uri: *const c_char,
			options: c_uint,
			err_buf: *mut c_char,
			err_buf_size: c_int,
		) -> c_int,
	>,
}

/// `oakstorage_backend_register`: register a foreign backend (the
/// vtable is not copied — the caller guarantees it outlives
/// unregistration).
#[no_mangle]
pub unsafe extern "C" fn oakstorage_backend_register(
	backend: *const OakStorageBackendVtable,
) -> c_int {
	todo!()
}

/// `oakstorage_backend_unregister`.
#[no_mangle]
pub unsafe extern "C" fn oakstorage_backend_unregister(name: *const c_char) -> c_int {
	todo!()
}
