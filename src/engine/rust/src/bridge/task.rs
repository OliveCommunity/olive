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
pub type OakTaskOtioImportConfirmFn =
	unsafe extern "C" fn(sequence_names: *const *const c_char, count: c_int, userdata: *mut c_void) -> c_int;

/// `oaktask_image_sequence_confirm_fn` (`include/task/project.h`).
pub type OakTaskImageSequenceConfirmFn =
	unsafe extern "C" fn(filename: *const c_char, userdata: *mut c_void) -> c_int;

extern "C" {
	// ---- include/task/manager.h ---------------------------------------------
	/// `oaktask_manager_init` — start the global task manager.
	pub fn oaktask_manager_init() -> c_int;
	/// `oaktask_manager_shutdown`.
	pub fn oaktask_manager_shutdown();
	/// `oaktask_register_codec_submitter`.
	pub fn oaktask_register_codec_submitter() -> c_int;
	/// `oaktask_manager_count` — running plus failed-but-kept tasks.
	pub fn oaktask_manager_count() -> c_int;
	/// `oaktask_manager_at` — borrowed task handle at index.
	pub fn oaktask_manager_at(i: c_int) -> CHandle;
	/// `oaktask_manager_delete_finished`.
	pub fn oaktask_manager_delete_finished();

	// ---- include/task/task.h -------------------------------------------------
	/// `oaktask_task_free` — release one reference; NULL/empty no-op.
	pub fn oaktask_task_free(t: *mut CHandle);
	/// `oaktask_task_start_sync` — run in the calling thread; 1 = succeeded.
	pub fn oaktask_task_start_sync(t: CHandle) -> c_int;
	/// `oaktask_task_start` — run on the task manager (transfers ownership).
	pub fn oaktask_task_start(t: CHandle) -> c_int;
	/// `oaktask_task_cancel`.
	pub fn oaktask_task_cancel(t: CHandle) -> c_int;
	/// `oaktask_task_wait` — wait for an asynchronously started task.
	pub fn oaktask_task_wait(t: CHandle) -> c_int;
	/// `oaktask_task_is_finished`.
	pub fn oaktask_task_is_finished(t: CHandle) -> c_int;
	/// `oaktask_task_succeeded`.
	pub fn oaktask_task_succeeded(t: CHandle) -> c_int;
	/// `oaktask_task_title` (two-stage string).
	pub fn oaktask_task_title(t: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaktask_task_error` (two-stage string).
	pub fn oaktask_task_error(t: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaktask_task_subscribe` — subscription id >= 0 or a negative code.
	pub fn oaktask_task_subscribe(t: CHandle, cb: Option<OakTaskEventFn>, userdata: *mut c_void) -> i64;
	/// `oaktask_debug_alive_count`.
	pub fn oaktask_debug_alive_count() -> c_int;

	// ---- include/task/project.h ----------------------------------------------
	/// `oaktask_create_project_load` — owned `ProjectLoadTask`.
	pub fn oaktask_create_project_load(filename: *const c_char) -> CHandle;
	/// `oaktask_load_take_project` — ownership transfer of the loaded project.
	pub fn oaktask_load_take_project(t: CHandle) -> CHandle;
	/// `oaktask_create_project_save` — owned `ProjectSaveTask`.
	pub fn oaktask_create_project_save(project: CHandle, filename_or_null: *const c_char, use_compression: c_int) -> CHandle;
	/// `oaktask_create_project_import` — owned `ProjectImportTask`.
	pub fn oaktask_create_project_import(folder: CHandle, project: CHandle, urls: *const *const c_char, url_count: c_int) -> CHandle;
	/// `oaktask_import_take_command` — ownership transfer of the undo command.
	pub fn oaktask_import_take_command(t: CHandle) -> CHandle;
	/// `oaktask_import_footage_count`.
	pub fn oaktask_import_footage_count(t: CHandle) -> c_int;
	/// `oaktask_import_footage_at` — addref'd footage handle.
	pub fn oaktask_import_footage_at(t: CHandle, index: c_int) -> CHandle;
	/// `oaktask_import_invalid_count`.
	pub fn oaktask_import_invalid_count(t: CHandle) -> c_int;
	/// `oaktask_import_invalid_at` (two-stage string).
	pub fn oaktask_import_invalid_at(t: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaktask_create_project_load_otio` — owned `LoadOTIOTask`.
	pub fn oaktask_create_project_load_otio(filename: *const c_char) -> CHandle;
	/// `oaktask_load_otio_take_project` — ownership transfer.
	pub fn oaktask_load_otio_take_project(t: CHandle) -> CHandle;
	/// `oaktask_create_project_save_otio` — owned `SaveOTIOTask`.
	pub fn oaktask_create_project_save_otio(project: CHandle, filename: *const c_char) -> CHandle;
	/// `oaktask_load_otio_set_confirm_cb` — install/clear the confirm callback.
	pub fn oaktask_load_otio_set_confirm_cb(cb: Option<OakTaskOtioImportConfirmFn>, userdata: *mut c_void);
	/// `oaktask_create_precache` — owned `PreCacheTask`.
	pub fn oaktask_create_precache(footage: CHandle, index: c_int, sequence: CHandle) -> CHandle;
	/// `oaktask_create_export` — owned `ExportTask`; `params` is the
	/// encoding-params POD (facade's `EncodingParamsPOD`).
	pub fn oaktask_create_export(
		viewer: CHandle,
		color_manager: CHandle,
		params: *const crate::bridge::codec::EncodingParamsPOD,
	) -> CHandle;
	/// `oaktask_import_set_image_sequence_confirm_cb` — install/clear.
	pub fn oaktask_import_set_image_sequence_confirm_cb(cb: Option<OakTaskImageSequenceConfirmFn>, userdata: *mut c_void);
}
