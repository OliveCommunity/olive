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

//! `oaktask_create_*` export symbols mirroring `include/task/project.h`.
//!
//! Full symbol inventory (header-authoritative):
//!   - `oaktask_create_project_load(const char *filename) -> OakTaskTask`
//!   - `oaktask_load_take_project(OakTaskTask t) -> OakNodeProject`
//!   - `oaktask_create_project_save(OakNodeProject, const char *filename_or_NULL, int use_compression) -> OakTaskTask`
//!   - `oaktask_create_project_import(OakNodeFolder, OakNodeProject, const char *const *urls, int url_count) -> OakTaskTask`
//!   - `oaktask_import_take_command(OakTaskTask t) -> OakUndoCommand`
//!   - `oaktask_import_footage_count(OakTaskTask t) -> int`
//!   - `oaktask_import_footage_at(OakTaskTask t, int index) -> OakNodeFootage`
//!   - `oaktask_import_invalid_count(OakTaskTask t) -> int`
//!   - `oaktask_import_invalid_at(OakTaskTask t, int index, char *buf, int buf_size) -> int` (two-stage)
//!   - `oaktask_create_project_load_otio(const char *filename) -> OakTaskTask`
//!   - `oaktask_load_otio_take_project(OakTaskTask t) -> OakNodeProject`
//!   - `oaktask_create_project_save_otio(OakNodeProject, const char *filename) -> OakTaskTask`
//!   - `oaktask_load_otio_set_confirm_cb(oaktask_otio_import_confirm_fn fn, void *userdata) -> void`
//!   - `oaktask_create_precache(OakNodeFootage, int index, OakNodeSequence) -> OakTaskTask`
//!   - `oaktask_create_export(OakNodeNode viewer, OakNodeColorManager, const oakcodec_encoding_params *) -> OakTaskTask`
//!   - `oaktask_import_set_image_sequence_confirm_cb(oaktask_image_sequence_confirm_fn fn, void *userdata) -> void`

use std::ffi::{c_char, c_int, c_void};
use std::sync::Mutex;

use crate::bridge::codec::OakCodecEncodingParams;
use crate::error::{OAKTASK_E_INVALID, OAKTASK_E_NOT_FOUND};
use crate::export::{EncodingParams, ExportTask};
use crate::ffi::taskhandle::{
	copy_string, cstr, cstr_to_string, get_task, wrap_owned, wrap_owned_with_impl, TaskImpl,
};
use crate::handle::CHandle;
use crate::precache::PreCacheTask;
use crate::project::import::{import_file_count, import_title, ImageSequenceConfirmFn, ProjectImportTask};
use crate::project::load::{ProjectLoadBaseTask, ProjectLoadTask};
use crate::project::loadotio::{set_import_confirm_callback, LoadOTIOTask};
use crate::project::save::{project_filename, ProjectSaveTask};
use crate::project::saveotio::SaveOTIOTask;
use crate::task::Task;

/// `oaktask_otio_import_confirm_fn` (`include/task/project.h`).
pub type OakTaskOtioImportConfirmFn =
	unsafe extern "C" fn(sequence_names: *const *const c_char, count: c_int, userdata: *mut c_void) -> c_int;

/// `oaktask_image_sequence_confirm_fn` (`include/task/project.h`).
pub type OakTaskImageSequenceConfirmFn =
	unsafe extern "C" fn(filename: *const c_char, userdata: *mut c_void) -> c_int;

/// Global image-sequence confirm callback (facade concern; `null` clears
/// it). Snapshot into each import task at creation.
static IMAGE_SEQUENCE_CONFIRM: Mutex<Option<ImageSequenceConfirmFn>> = Mutex::new(None);

/// `oaktask_create_project_load` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_create_project_load(filename: *const c_char) -> CHandle {
	if filename.is_null() {
		return CHandle::null();
	}
	let filename = unsafe { cstr_to_string(filename) };
	let title = format!("Loading '{filename}'");

	let inner = Task::new(&title, CHandle::null());
	let base = ProjectLoadBaseTask::new(inner, filename.clone());
	let load_task = ProjectLoadTask { base };
	let atom = load_task.base.base.get_cancel_atom();
	let mut boxed = Box::new(load_task);
	let base_ptr = &mut boxed.base as *mut ProjectLoadBaseTask;
	let mut outer = Task::new(&title, atom);
	outer.set_behavior(boxed);
	wrap_owned_with_impl(Box::new(outer), TaskImpl::LoadBase(base_ptr))
}

/// `oaktask_load_take_project` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_load_take_project(t: CHandle) -> CHandle {
	load_take_project(&t)
}

/// Shared implementation of `load_take_project`/`load_otio_take_project`.
fn load_take_project(t: &CHandle) -> CHandle {
	let Some(h) = get_task(t) else {
		return CHandle::null();
	};
	let TaskImpl::LoadBase(base_ptr) = h.impl_kind else {
		return CHandle::null();
	};
	match unsafe { (&mut *base_ptr).take_project() } {
		Ok(project) => project,
		Err(_) => CHandle::null(),
	}
}

/// `oaktask_create_project_save` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_create_project_save(
	project: CHandle,
	filename_or_null: *const c_char,
	use_compression: c_int,
) -> CHandle {
	if project.ctx.is_null() {
		return CHandle::null();
	}
	let title = format!("Saving '{}'", project_filename(project));
	let mut save_task = ProjectSaveTask {
		base: Task::new(&title, CHandle::null()),
		project,
		override_filename: None,
		use_compression: use_compression != 0,
	};
	if !filename_or_null.is_null() {
		save_task.set_override_filename(&unsafe { cstr_to_string(filename_or_null) });
	}
	let atom = save_task.base.get_cancel_atom();
	let boxed = Box::new(save_task);
	let mut outer = Task::new(&title, atom);
	outer.set_behavior(boxed);
	wrap_owned(Box::new(outer))
}

/// `oaktask_create_project_import` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_create_project_import(
	folder: CHandle,
	project: CHandle,
	urls: *const *const c_char,
	url_count: c_int,
) -> CHandle {
	if folder.ctx.is_null() || project.ctx.is_null() {
		return CHandle::null();
	}
	if url_count < 0 || (urls.is_null() && url_count > 0) {
		return CHandle::null();
	}
	let mut filenames = Vec::with_capacity(url_count as usize);
	for i in 0..url_count {
		let url = unsafe { *urls.add(i as usize) };
		if url.is_null() {
			return CHandle::null();
		}
		filenames.push(unsafe { cstr_to_string(url) });
	}

	let title = import_title(&filenames);
	let file_count = import_file_count(&filenames);
	let import_task = ProjectImportTask::new(
		Task::new(&title, CHandle::null()),
		folder,
		project,
		filenames,
		IMAGE_SEQUENCE_CONFIRM.lock().unwrap().take(),
		file_count,
	);
	let atom = import_task.base.get_cancel_atom();
	let mut boxed = Box::new(import_task);
	let import_ptr = &mut *boxed as *mut ProjectImportTask;
	let mut outer = Task::new(&title, atom);
	outer.set_behavior(boxed);
	wrap_owned_with_impl(Box::new(outer), TaskImpl::Import(import_ptr))
}

/// Borrow the `ProjectImportTask` behind the handle (the C++ `import_impl`
/// dynamic_cast equivalent).
fn import_impl(t: &CHandle) -> Option<*mut ProjectImportTask> {
	let h = get_task(t)?;
	match h.impl_kind {
		TaskImpl::Import(p) => Some(p),
		_ => None,
	}
}

/// `oaktask_import_take_command` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_import_take_command(t: CHandle) -> CHandle {
	let Some(p) = import_impl(&t) else {
		return CHandle::null();
	};
	match unsafe { (&mut *p).take_command() } {
		Ok(command) => command,
		Err(_) => CHandle::null(),
	}
}

/// `oaktask_import_footage_count` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_import_footage_count(t: CHandle) -> c_int {
	let Some(p) = import_impl(&t) else {
		return OAKTASK_E_INVALID;
	};
	unsafe { (*p).get_file_count() as c_int }
}

/// `oaktask_import_footage_at` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_import_footage_at(t: CHandle, index: c_int) -> CHandle {
	let Some(p) = import_impl(&t) else {
		return CHandle::null();
	};
	if index < 0 {
		return CHandle::null();
	}
	match unsafe { (*p).get_imported_footage(index as usize) } {
		Ok(footage) => footage,
		Err(_) => CHandle::null(),
	}
}

/// `oaktask_import_invalid_count` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_import_invalid_count(t: CHandle) -> c_int {
	let Some(p) = import_impl(&t) else {
		return OAKTASK_E_INVALID;
	};
	unsafe { (*p).get_invalid_file_count() as c_int }
}

/// `oaktask_import_invalid_at` (`include/task/project.h`, two-stage string getter).
#[no_mangle]
pub unsafe extern "C" fn oaktask_import_invalid_at(
	t: CHandle,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	let Some(p) = import_impl(&t) else {
		return OAKTASK_E_INVALID;
	};
	let task = unsafe { &*p };
	let count = task.get_invalid_file_count();
	if index < 0 || index as usize >= count {
		return OAKTASK_E_NOT_FOUND;
	}
	copy_string(task.invalid_file_at(index as usize), buf, buf_size)
}

/// `oaktask_create_project_load_otio` (`include/task/project.h`). The
/// interchange format is inferred from the filename extension (`.otio` /
/// `.fcpxml`, case-insensitive; see `crate::project::format`), so the C
/// ABI needs no format parameter.
#[no_mangle]
pub unsafe extern "C" fn oaktask_create_project_load_otio(filename: *const c_char) -> CHandle {
	if filename.is_null() {
		return CHandle::null();
	}
	let filename = unsafe { cstr_to_string(filename) };
	let title = format!("Loading '{filename}'");

	let inner = Task::new(&title, CHandle::null());
	let base = ProjectLoadBaseTask::new(inner, filename.clone());
	let otio_task = LoadOTIOTask::new(base);
	let atom = otio_task.base.base.get_cancel_atom();
	let mut boxed = Box::new(otio_task);
	let base_ptr = &mut boxed.base as *mut ProjectLoadBaseTask;
	let mut outer = Task::new(&title, atom);
	outer.set_behavior(boxed);
	wrap_owned_with_impl(Box::new(outer), TaskImpl::LoadBase(base_ptr))
}

/// `oaktask_load_otio_take_project` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_load_otio_take_project(t: CHandle) -> CHandle {
	load_take_project(&t)
}

/// `oaktask_create_project_save_otio` (`include/task/project.h`). The
/// interchange format is inferred from the filename extension (`.otio` /
/// `.fcpxml`, case-insensitive; see `crate::project::format`), so the C
/// ABI needs no format parameter.
#[no_mangle]
pub unsafe extern "C" fn oaktask_create_project_save_otio(project: CHandle, filename: *const c_char) -> CHandle {
	if project.ctx.is_null() || filename.is_null() {
		return CHandle::null();
	}
	let filename = unsafe { cstr_to_string(filename) };
	let title = format!("Saving '{filename}'");
	let task = SaveOTIOTask {
		base: Task::new(&title, CHandle::null()),
		project,
		filename,
	};
	let atom = task.base.get_cancel_atom();
	let boxed = Box::new(task);
	let mut outer = Task::new(&title, atom);
	outer.set_behavior(boxed);
	wrap_owned(Box::new(outer))
}

/// `oaktask_load_otio_set_confirm_cb` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_load_otio_set_confirm_cb(cb: Option<OakTaskOtioImportConfirmFn>, userdata: *mut c_void) {
	let Some(cb) = cb else {
		set_import_confirm_callback(None);
		return;
	};
	let userdata = userdata as usize;
	set_import_confirm_callback(Some(Box::new(move |sequence_names: &[String]| {
		let ptrs: Vec<*const c_char> = sequence_names.iter().map(|n| cstr(n)).collect();
		unsafe { cb(ptrs.as_ptr(), ptrs.len() as c_int, userdata as *mut c_void) != 0 }
	})));
}

/// `oaktask_create_precache` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_create_precache(footage: CHandle, index: c_int, sequence: CHandle) -> CHandle {
	if footage.ctx.is_null() || sequence.ctx.is_null() {
		return CHandle::null();
	}
	let precache = PreCacheTask::new(footage, index, sequence);
	let title = precache.render.base.title().to_string();
	let atom = precache.render.base.get_cancel_atom();
	let boxed = Box::new(precache);
	let mut outer = Task::new(&title, atom);
	outer.set_behavior(boxed);
	wrap_owned(Box::new(outer))
}

/// `oaktask_create_export` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_create_export(
	viewer: CHandle,
	color_manager: CHandle,
	params: *const OakCodecEncodingParams,
) -> CHandle {
	if viewer.ctx.is_null() || params.is_null() {
		return CHandle::null();
	}
	let encoding = unsafe { convert_encoding_params(&*params) };
	let export = ExportTask::new(viewer, color_manager, encoding);
	let title = export.render.base.title().to_string();
	let atom = export.render.base.get_cancel_atom();
	let boxed = Box::new(export);
	let mut outer = Task::new(&title, atom);
	outer.set_behavior(boxed);
	wrap_owned(Box::new(outer))
}

/// `oaktask_import_set_image_sequence_confirm_cb` (`include/task/project.h`).
#[no_mangle]
pub unsafe extern "C" fn oaktask_import_set_image_sequence_confirm_cb(
	cb: Option<OakTaskImageSequenceConfirmFn>,
	userdata: *mut c_void,
) {
	let mut guard = IMAGE_SEQUENCE_CONFIRM.lock().unwrap();
	let Some(cb) = cb else {
		*guard = None;
		return;
	};
	let userdata = userdata as usize;
	*guard = Some(Box::new(move |filename: &str, _other: &str| unsafe {
		cb(cstr(filename), userdata as *mut c_void) != 0
	}));
}

/// Copy the C-ABI encoding params into the Rust mirror (subset read by the
/// export task).
unsafe fn convert_encoding_params(p: &OakCodecEncodingParams) -> EncodingParams {
	EncodingParams {
		filename: unsafe { c_char_array_to_string(&p.filename) },
		format: p.format,
		video_enabled: p.video_enabled != 0,
		video_codec: p.video_codec,
		video_width: p.video_width,
		video_height: p.video_height,
		video_time_base_num: p.video_time_base_num,
		video_time_base_den: p.video_time_base_den,
		video_pixel_format: p.video_pixel_format,
		audio_enabled: p.audio_enabled != 0,
		audio_codec: p.audio_codec,
		subtitles_enabled: p.subtitles_enabled != 0,
		export_length_num: p.export_length_num,
		export_length_den: p.export_length_den,
	}
}

/// Read a NUL-terminated char array into a String (lossy).
unsafe fn c_char_array_to_string(buf: &[u8]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(&buf[..len]).into_owned()
}
