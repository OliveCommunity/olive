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

//! Project task factory tests (project.rs + ffi/project.rs): load, save,
//! import, OTIO load/save and pre-cache/export factories.

mod common;

use common::*;
use std::ffi::{c_char, c_int, c_void};
use std::sync::atomic::Ordering;

use oaktask::error::{OAKTASK_E_INVALID, OAKTASK_E_NOT_FOUND};
use oaktask::ffi::manager::{oaktask_manager_init, oaktask_manager_shutdown};
use oaktask::ffi::project::{
	oaktask_create_export, oaktask_create_precache, oaktask_create_project_import,
	oaktask_create_project_load, oaktask_create_project_load_otio, oaktask_create_project_save,
	oaktask_create_project_save_otio, oaktask_import_footage_at, oaktask_import_footage_count,
	oaktask_import_invalid_at, oaktask_import_invalid_count,
	oaktask_import_set_image_sequence_confirm_cb, oaktask_import_take_command,
	oaktask_load_otio_set_confirm_cb, oaktask_load_otio_take_project, oaktask_load_take_project,
};
use oaktask::ffi::task::{
	oaktask_debug_alive_count, oaktask_task_error, oaktask_task_free, oaktask_task_is_finished,
	oaktask_task_start_sync, oaktask_task_title,
};
use oaktask::handle::CHandle;

unsafe extern "C" fn accept_all(
	_seq: *const *const c_char,
	_count: c_int,
	_ud: *mut c_void,
) -> c_int {
	1
}

unsafe extern "C" fn reject_all(
	_seq: *const *const c_char,
	_count: c_int,
	_ud: *mut c_void,
) -> c_int {
	0
}

unsafe extern "C" fn image_seq_confirm(_filename: *const c_char, _ud: *mut c_void) -> c_int {
	1
}

fn free(t: &mut CHandle) {
	unsafe {
		oaktask_task_free(t);
	}
}

/// Given a valid project file, `create_project_load` returns a task whose
/// `start_sync` succeeds and whose `load_take_project` transfers an
/// initialized project with a root folder.
#[test]
fn project_load_transfers_initialized_project() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	let path = std::env::temp_dir().join("oak-load-ok.oakproj");
	std::fs::write(&path, b"oakproj-stub").unwrap();
	let path_str = path.to_string_lossy().into_owned();

	let mut task = unsafe { oaktask_create_project_load(common::cstr_of(&path_str)) };
	assert!(!task.ctx.is_null());

	// Title embeds the filename.
	let needed = unsafe { oaktask_task_title(task, std::ptr::null_mut(), 0) };
	assert!(needed > 10);

	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 1);

	// take_project transfers a non-empty project.
	let project = unsafe { oaktask_load_take_project(task) };
	assert!(!project.ctx.is_null(), "load_take_project returned empty");
	// Second take is empty (ownership transferred).
	let project2 = unsafe { oaktask_load_take_project(task) };
	assert!(project2.ctx.is_null());

	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
	let _ = std::fs::remove_file(&path);
}

/// Loading a missing file fails: start_sync=0, an error is set, and
/// take_project is empty.
#[test]
fn project_load_missing_file_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	let missing = "/tmp/oak-does-not-exist-12345.oakproj";
	let _ = std::fs::remove_file(missing);

	let mut task = unsafe { oaktask_create_project_load(common::cstr_of(missing)) };
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 1);

	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	assert!(needed > 0);
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(cstr_read(&buf).contains("Failed to read file"));

	let project = unsafe { oaktask_load_take_project(task) };
	assert!(project.ctx.is_null());

	// Null filename -> empty handle (factory failure path).
	let empty = unsafe { oaktask_create_project_load(std::ptr::null()) };
	assert!(empty.ctx.is_null());

	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Given a loaded project, `create_project_save` writes the project and the
/// task succeeds with compression on and off.
#[test]
fn project_save_writes_with_and_without_compression() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	let path = std::env::temp_dir().join("oak-save-compressed.oakproj");
	let path_str = path.to_string_lossy().into_owned();
	let _ = std::fs::remove_file(&path);

	// Without compression.
	let mut task =
		unsafe { oaktask_create_project_save(fake_handle(), common::cstr_of(&path_str), 0) };
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert!(path.exists(), "saved file was not written");
	free(&mut task);

	// With compression (overwrites).
	let _ = std::fs::remove_file(&path);
	let mut task =
		unsafe { oaktask_create_project_save(fake_handle(), common::cstr_of(&path_str), 1) };
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert!(path.exists());
	free(&mut task);

	// Null project -> empty handle.
	let empty =
		unsafe { oaktask_create_project_save(CHandle::null(), common::cstr_of(&path_str), 0) };
	assert!(empty.ctx.is_null());

	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Given an import folder and a media URL, `create_project_import` runs to
/// completion, records one footage and exposes it via `import_footage_at`.
#[test]
fn project_import_records_and_exposes_footage() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	let file = std::env::temp_dir().join("oak-import-ok.mp4");
	std::fs::write(&file, b"media").unwrap();
	let file_str = file.to_string_lossy().into_owned();

	let urls = [common::cstr_of(&file_str)];
	let mut task = unsafe {
		oaktask_create_project_import(
			fake_handle(),
			fake_handle(),
			urls.as_ptr(),
			urls.len() as c_int,
		)
	};
	assert!(!task.ctx.is_null());

	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(unsafe { oaktask_import_footage_count(task) }, 1);
	assert_eq!(unsafe { oaktask_import_invalid_count(task) }, 0);

	// footage_at returns an addref'd handle for in-range indices.
	let footage = unsafe { oaktask_import_footage_at(task, 0) };
	assert!(!footage.ctx.is_null());
	// Out of range -> empty.
	assert!(unsafe { oaktask_import_footage_at(task, 5) }.ctx.is_null());
	assert!(unsafe { oaktask_import_footage_at(task, -1) }.ctx.is_null());

	// The undo command is produced; redo adds the folder child, undo
	// removes it again.
	let mut command = unsafe { oaktask_import_take_command(task) };
	assert!(!command.ctx.is_null());
	// Second take is empty.
	assert!(unsafe { oaktask_import_take_command(task) }.ctx.is_null());

	assert_eq!(
		unsafe { oaktask::bridge::undo::oakundo_command_redo_now(command) },
		0
	);
	assert_eq!(
		unsafe { oaktask::bridge::node::oaknode_folder_child_count(fake_handle()) },
		1
	);
	assert_eq!(
		unsafe { oaktask::bridge::undo::oakundo_command_undo_now(command) },
		0
	);
	assert_eq!(
		unsafe { oaktask::bridge::node::oaknode_folder_child_count(fake_handle()) },
		0
	);

	unsafe {
		oaktask::bridge::undo::oakundo_command_free(&mut command);
	}
	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
	let _ = std::fs::remove_file(&file);
}

/// Given an unreadable import URL, `create_project_import` records the
/// invalid filename and `import_invalid_at` reports it (two-stage).
#[test]
fn project_import_reports_invalid_urls() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	FOOTAGE_VALID.store(0, Ordering::SeqCst);

	let bad = "/tmp/oak-invalid-media-12345.mp4";
	let _ = std::fs::remove_file(bad);
	let urls = [common::cstr_of(bad)];
	let mut task = unsafe {
		oaktask_create_project_import(
			fake_handle(),
			fake_handle(),
			urls.as_ptr(),
			urls.len() as c_int,
		)
	};
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(unsafe { oaktask_import_invalid_count(task) }, 1);
	assert_eq!(unsafe { oaktask_import_footage_count(task) }, 0);

	// Two-stage invalid filename getter.
	let needed = unsafe { oaktask_import_invalid_at(task, 0, std::ptr::null_mut(), 0) };
	assert!(needed > 0);
	let mut buf = vec![0i8; needed as usize];
	let r = unsafe { oaktask_import_invalid_at(task, 0, buf.as_mut_ptr(), needed) };
	assert_eq!(r, needed);
	assert_eq!(cstr_read(&buf), bad);

	// Out of range -> E_NOT_FOUND.
	assert_eq!(
		unsafe { oaktask_import_invalid_at(task, 3, std::ptr::null_mut(), 0) },
		OAKTASK_E_NOT_FOUND
	);

	// Factory failure paths: null folder, null project, negative count.
	assert!(unsafe {
		oaktask_create_project_import(CHandle::null(), fake_handle(), std::ptr::null(), 0)
	}
	.ctx
	.is_null());
	assert!(unsafe {
		oaktask_create_project_import(fake_handle(), CHandle::null(), std::ptr::null(), 0)
	}
	.ctx
	.is_null());
	let urls_null = [std::ptr::null()];
	assert!(unsafe {
		oaktask_create_project_import(fake_handle(), fake_handle(), urls_null.as_ptr(), 1)
	}
	.ctx
	.is_null());

	// Accessors on a non-import task -> E_INVALID / empty.
	let mut save_task =
		unsafe { oaktask_create_project_save(fake_handle(), common::cstr_of("/tmp/x.oakproj"), 0) };
	assert_eq!(
		unsafe { oaktask_import_footage_count(save_task) },
		OAKTASK_E_INVALID
	);
	assert!(unsafe { oaktask_import_take_command(save_task) }
		.ctx
		.is_null());
	free(&mut save_task);

	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// An image sequence with a confirmation callback imports once and prunes
/// the remaining frames from the list.
#[test]
fn project_import_image_sequence_confirmed() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	let dir = std::env::temp_dir().join("oak-seq");
	let _ = std::fs::create_dir_all(&dir);
	let files = ["seq001.png", "seq002.png", "seq003.png"];
	let mut paths = Vec::new();
	for f in &files {
		let p = dir.join(f);
		std::fs::write(&p, b"png").unwrap();
		paths.push(p.to_string_lossy().into_owned());
	}

	IMAGE_SEQUENCE_DIGIT_COUNT.store(3, Ordering::SeqCst);
	CONFIG_DEFAULT_SEQ_FRAME_RATE.store(1, Ordering::SeqCst);
	unsafe {
		oaktask_import_set_image_sequence_confirm_cb(Some(image_seq_confirm), std::ptr::null_mut())
	};

	let urls: Vec<*const c_char> = paths.iter().map(|p| common::cstr_of(p)).collect();
	let mut task = unsafe {
		oaktask_create_project_import(
			fake_handle(),
			fake_handle(),
			urls.as_ptr(),
			urls.len() as c_int,
		)
	};
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);

	// The three frames collapse into a single footage import.
	assert_eq!(unsafe { oaktask_import_footage_count(task) }, 1);
	assert_eq!(unsafe { oaktask_import_invalid_count(task) }, 0);

	unsafe { oaktask_import_set_image_sequence_confirm_cb(None, std::ptr::null_mut()) };
	IMAGE_SEQUENCE_DIGIT_COUNT.store(0, Ordering::SeqCst);
	free(&mut task);
	for p in &paths {
		let _ = std::fs::remove_file(p);
	}
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Given a valid OTIO file, `create_project_load_otio` runs to completion and
/// `load_otio_take_project` transfers an initialized project; a missing file
/// fails cleanly. (OTIO parsing now happens inside the task through the
/// `oakotio` binding — README decision #6.)
#[test]
fn otio_load_transfers_project_with_default_confirm() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	// A minimal Timeline document (no tracks).
	let path = std::env::temp_dir().join("oak-load-ok.otio");
	let otio = r#"{
    "OTIO_SCHEMA": "Timeline.1",
    "metadata": {},
    "name": "T",
    "global_start_time": null,
    "tracks": {
        "OTIO_SCHEMA": "Stack.1",
        "metadata": {},
        "name": "tracks",
        "source_range": null,
        "effects": [],
        "markers": [],
        "enabled": true,
        "children": []
    }
}"#;
	std::fs::write(&path, otio).unwrap();
	let path_str = path.to_string_lossy().into_owned();

	let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 1);
	assert!(!(unsafe { oaktask_load_otio_take_project(task) })
		.ctx
		.is_null());

	free(&mut task);

	// Missing file -> failure with a clear error.
	let missing = std::env::temp_dir().join("oak-missing-otio.otio");
	let missing_str = missing.to_string_lossy().into_owned();
	let _ = std::fs::remove_file(&missing);
	let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&missing_str)) };
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	assert!(needed > 0);
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(
		cstr_read(&buf).contains("Failed to load OpenTimelineIO"),
		"error was: {}",
		cstr_read(&buf)
	);
	assert!(unsafe { oaktask_load_otio_take_project(task) }
		.ctx
		.is_null());

	// The confirm callback setters are no-ops (null clears).
	unsafe {
		oaktask_load_otio_set_confirm_cb(Some(accept_all), std::ptr::null_mut());
		oaktask_load_otio_set_confirm_cb(Some(reject_all), std::ptr::null_mut());
		oaktask_load_otio_set_confirm_cb(None, std::ptr::null_mut());
	}

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Given a project with a sequence, `create_project_save_otio` writes the
/// OTIO file and the task succeeds; a project without sequences fails.
#[test]
fn otio_save_writes_file() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	// One sequence in the root folder, one video track with a single clip.
	FOLDER_CHILD_COUNT.store(1, Ordering::SeqCst);
	*NODE_ID.lock().unwrap() = "org.olivevideoeditor.Olive.sequence".to_string();
	*NODE_LABEL.lock().unwrap() = "My Sequence".to_string();
	VIDEO_FRAME_RATE_NUM.store(25, Ordering::SeqCst);
	VIDEO_FRAME_RATE_DEN.store(1, Ordering::SeqCst);
	TRACK_LIST_TRACK_COUNT.store(1, Ordering::SeqCst);
	TRACK_BLOCK_COUNT.store(1, Ordering::SeqCst);
	BLOCK_KIND.store(1, Ordering::SeqCst); // OAKNODE_BLOCK_CLIP
	BLOCK_IN_NUM.store(0, Ordering::SeqCst);
	BLOCK_IN_DEN.store(1, Ordering::SeqCst);
	BLOCK_LENGTH_NUM.store(25, Ordering::SeqCst);
	BLOCK_LENGTH_DEN.store(1, Ordering::SeqCst);
	*FOOTAGE_FILENAME.lock().unwrap() = "file:///tmp/video.mp4".to_string();

	let out = std::env::temp_dir().join("oak-otio-out.otio");
	let out_str = out.to_string_lossy().into_owned();
	let _ = std::fs::remove_file(&out);

	let mut task =
		unsafe { oaktask_create_project_save_otio(fake_handle(), common::cstr_of(&out_str)) };
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert!(out.exists(), "OTIO file was not written");

	// Failure paths: null project or null filename.
	assert!(unsafe {
		oaktask_create_project_save_otio(CHandle::null(), common::cstr_of("/tmp/x.otio"))
	}
	.ctx
	.is_null());
	assert!(
		unsafe { oaktask_create_project_save_otio(fake_handle(), std::ptr::null()) }
			.ctx
			.is_null()
	);

	free(&mut task);
	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Given a footage/sequence pair, `create_precache` runs to completion and
/// succeeds.
#[test]
fn precache_runs_and_succeeds() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	FOOTAGE_FILENAME.lock().unwrap().clear();
	let mut task = unsafe { oaktask_create_precache(fake_handle(), 0, fake_handle()) };
	assert!(!task.ctx.is_null());
	let needed = unsafe { oaktask_task_title(task, std::ptr::null_mut(), 0) };
	assert!(needed > 0);
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 1);

	// Failure path: null footage/sequence.
	assert!(
		unsafe { oaktask_create_precache(CHandle::null(), 0, fake_handle()) }
			.ctx
			.is_null()
	);
	assert!(
		unsafe { oaktask_create_precache(fake_handle(), 0, CHandle::null()) }
			.ctx
			.is_null()
	);

	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// create_export runs the full render+encoder path against the stubs and
/// succeeds; the encoder-failure path fails cleanly.
#[test]
fn export_runs_with_stubbed_encoder() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-export-test.mp4".to_string();
	NODE_LABEL.lock().unwrap().clear();

	let mut params = oaktask::bridge::codec::OakCodecEncodingParams {
		filename: [0; 1024],
		format: 0,
		video_enabled: 1,
		video_codec: 0,
		video_width: 1920,
		video_height: 1080,
		video_time_base_num: 1,
		video_time_base_den: 25,
		video_pixel_format: 0,
		video_interlacing: 0,
		video_pixel_aspect_num: 1,
		video_pixel_aspect_den: 1,
		video_bit_rate: 0,
		video_min_bit_rate: 0,
		video_max_bit_rate: 0,
		video_buffer_size: 0,
		video_threads: 0,
		video_pix_fmt: [0; 64],
		video_is_image_sequence: 0,
		video_scaling_method: 0,
		audio_enabled: 0,
		audio_codec: 0,
		audio_sample_rate: 0,
		audio_channel_layout: 0,
		audio_sample_format: 0,
		audio_bit_rate: 0,
		subtitles_enabled: 0,
		subtitles_codec: 0,
		subtitles_are_sidecar: 0,
		subtitles_sidecar_format: 0,
		color_transform_output: [0; 256],
		export_length_num: 0,
		export_length_den: 0,
		has_custom_range: 0,
		custom_range_in_num: 0,
		custom_range_in_den: 0,
		custom_range_out_num: 0,
		custom_range_out_den: 0,
	};
	let filename = std::env::temp_dir()
		.join("oak-export-test.mp4")
		.to_string_lossy()
		.into_owned();
	write_cstr(&filename, params.filename.as_mut_ptr(), 1024);

	ENCODER_INIT_NULL.store(0, Ordering::SeqCst);
	ENCODER_OPEN_RESULT.store(0, Ordering::SeqCst);
	ENCODER_WRITE_VIDEO_RESULT.store(0, Ordering::SeqCst);
	SEQUENCE_LENGTH_NUM.store(25, Ordering::SeqCst);
	SEQUENCE_LENGTH_DEN.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_NUM.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst); // 1s frames -> 25 frames

	let mut task = unsafe { oaktask_create_export(fake_handle(), fake_handle(), &params) };
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(unsafe { oaktask_task_is_finished(task) }, 1);
	free(&mut task);

	// Failure path: encoder init fails.
	ENCODER_INIT_NULL.store(1, Ordering::SeqCst);
	let mut task = unsafe { oaktask_create_export(fake_handle(), fake_handle(), &params) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	assert!(needed > 0);
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(cstr_read(&buf).contains("Failed to create encoder"));
	free(&mut task);

	// Failure path: null viewer / null params.
	assert!(
		unsafe { oaktask_create_export(CHandle::null(), fake_handle(), &params) }
			.ctx
			.is_null()
	);
	assert!(
		unsafe { oaktask_create_export(fake_handle(), fake_handle(), std::ptr::null()) }
			.ctx
			.is_null()
	);

	reset_stubs();
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// The manager is needed for nothing in these factories; a fresh
/// init/shutdown cycle leaves the alive count at zero.
#[test]
fn manager_cycle_leaves_no_leaks() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };
	assert_eq!(unsafe { oaktask_manager_init() }, 0);
	unsafe { oaktask_manager_shutdown() };
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// cstr_read helper: read a NUL-terminated c_char buffer.
fn cstr_read(buf: &[i8]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	let bytes = unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) };
	String::from_utf8_lossy(bytes).into_owned()
}
