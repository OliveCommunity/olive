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

//! oaktask C ABI bridge: direct Rust calls into the `oaktask` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oaktask`'s `ffi` (the
//! `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them). Handles cross as the shared
//! [`crate::handle::CHandle`]. Exceptions that keep an `extern "C"`
//! declaration (resolved at link time against the sibling crate in the
//! same dylib) are the host `oakcore_*` symbols and the encoding-params
//! C ABI POD crossings (the facade keeps its own POD mirrors there).

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

//! oaktask C ABI imports, mirroring the oaktask crate's exports
//! (`src/task/rust/src/ffi/{manager,task,project}.rs`; headers
//! `include/task/*.h`).
//!
//! Every handle crosses as [`crate::handle::CHandle`]. `oaktask_create_export`
//! takes the encoding-params POD ([`crate::bridge::codec::EncodingParamsPOD`],
//! field-identical to the task crate's `OakCodecEncodingParams`). String
//! getters report the size **including** the NUL; the facade converts with
//! [`crate::handle::string_result`].

use std::ffi::{c_char, c_int, c_void};

use crate::handle::CHandle;

/// `oaktask_event_fn` callback (`include/task/task.h`): `event_id` is an
/// `OakTaskEvent` (0=started, 1=progress, 2=finished), `value` 0..1 (or
/// start-ms / success flag), `userdata` the subscription token.
pub type OakTaskEventFn = unsafe extern "C" fn(event_id: c_int, value: f64, userdata: *mut c_void);

/// `oaktask_otio_import_confirm_fn` (`include/task/project.h`).
pub type OakTaskOtioImportConfirmFn = unsafe extern "C" fn(
	sequence_names: *const *const c_char,
	count: c_int,
	userdata: *mut c_void,
) -> c_int;

/// `oaktask_image_sequence_confirm_fn` (`include/task/project.h`).
pub type OakTaskImageSequenceConfirmFn =
	unsafe extern "C" fn(filename: *const c_char, userdata: *mut c_void) -> c_int;

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_manager_init() -> c_int {
	unsafe { oaktask::ffi::manager::oaktask_manager_init() }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_manager_shutdown() {
	unsafe { oaktask::ffi::manager::oaktask_manager_shutdown() }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_register_codec_submitter() -> c_int {
	unsafe { oaktask::ffi::manager::oaktask_register_codec_submitter() }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_manager_count() -> c_int {
	unsafe { oaktask::ffi::manager::oaktask_manager_count() }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_manager_at(i: c_int) -> CHandle {
	unsafe { oaktask::ffi::manager::oaktask_manager_at(i) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_manager_delete_finished() {
	unsafe { oaktask::ffi::manager::oaktask_manager_delete_finished() }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_free(t: *mut CHandle) {
	unsafe { oaktask::ffi::task::oaktask_task_free(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_start_sync(t: CHandle) -> c_int {
	unsafe { oaktask::ffi::task::oaktask_task_start_sync(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_start(t: CHandle) -> c_int {
	unsafe { oaktask::ffi::task::oaktask_task_start(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_cancel(t: CHandle) -> c_int {
	unsafe { oaktask::ffi::task::oaktask_task_cancel(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_wait(t: CHandle) -> c_int {
	unsafe { oaktask::ffi::task::oaktask_task_wait(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_is_finished(t: CHandle) -> c_int {
	unsafe { oaktask::ffi::task::oaktask_task_is_finished(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_succeeded(t: CHandle) -> c_int {
	unsafe { oaktask::ffi::task::oaktask_task_succeeded(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_title(t: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaktask::ffi::task::oaktask_task_title(t, buf, buf_size) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_error(t: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaktask::ffi::task::oaktask_task_error(t, buf, buf_size) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_task_subscribe(
	t: CHandle,
	cb: Option<OakTaskEventFn>,
	userdata: *mut c_void,
) -> i64 {
	unsafe { oaktask::ffi::task::oaktask_task_subscribe(t, cb, userdata) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_debug_alive_count() -> c_int {
	unsafe { oaktask::ffi::task::oaktask_debug_alive_count() }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_create_project_load(filename: *const c_char) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_create_project_load(filename) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_load_take_project(t: CHandle) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_load_take_project(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_create_project_save(
	project: CHandle,
	filename_or_null: *const c_char,
	use_compression: c_int,
) -> CHandle {
	unsafe {
		oaktask::ffi::project::oaktask_create_project_save(
			project,
			filename_or_null,
			use_compression,
		)
	}
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_create_project_import(
	folder: CHandle,
	project: CHandle,
	urls: *const *const c_char,
	url_count: c_int,
) -> CHandle {
	unsafe {
		oaktask::ffi::project::oaktask_create_project_import(folder, project, urls, url_count)
	}
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_import_take_command(t: CHandle) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_import_take_command(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_import_footage_count(t: CHandle) -> c_int {
	unsafe { oaktask::ffi::project::oaktask_import_footage_count(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_import_footage_at(t: CHandle, index: c_int) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_import_footage_at(t, index) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_import_invalid_count(t: CHandle) -> c_int {
	unsafe { oaktask::ffi::project::oaktask_import_invalid_count(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_import_invalid_at(
	t: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaktask::ffi::project::oaktask_import_invalid_at(t, index, buf, buf_size) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_create_project_load_otio(filename: *const c_char) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_create_project_load_otio(filename) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_load_otio_take_project(t: CHandle) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_load_otio_take_project(t) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_create_project_save_otio(project: CHandle, filename: *const c_char) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_create_project_save_otio(project, filename) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_load_otio_set_confirm_cb(
	cb: Option<OakTaskOtioImportConfirmFn>,
	userdata: *mut c_void,
) {
	unsafe { oaktask::ffi::project::oaktask_load_otio_set_confirm_cb(cb, userdata) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_create_precache(footage: CHandle, index: c_int, sequence: CHandle) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_create_precache(footage, index, sequence) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
/// `oaktask_create_export` — direct call into the `oaktask` crate
/// (single-lib unification; the encoding-params POD is the shared
/// oakcodec type).
pub fn oaktask_create_export(
	viewer: CHandle,
	color_manager: CHandle,
	params: *const crate::bridge::codec::EncodingParamsPOD,
) -> CHandle {
	unsafe { oaktask::ffi::project::oaktask_create_export(viewer, color_manager, params) }
}

/// Direct call into the `oaktask` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktask_import_set_image_sequence_confirm_cb(
	cb: Option<OakTaskImageSequenceConfirmFn>,
	userdata: *mut c_void,
) {
	unsafe { oaktask::ffi::project::oaktask_import_set_image_sequence_confirm_cb(cb, userdata) }
}
