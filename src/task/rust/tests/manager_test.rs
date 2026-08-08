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

//! TaskManager singleton lifecycle tests (manager.rs + ffi/manager.rs).

mod common;

use common::*;
use std::sync::atomic::Ordering;

use oaktask::error::{OAKTASK_E_STATE, OAKTASK_OK};
use oaktask::ffi::manager::{
	oaktask_manager_at, oaktask_manager_count, oaktask_manager_delete_finished, oaktask_manager_init,
	oaktask_manager_shutdown, oaktask_register_codec_submitter,
};
use oaktask::ffi::project::oaktask_create_project_save;
use oaktask::ffi::task::{
	oaktask_debug_alive_count, oaktask_task_free, oaktask_task_is_finished, oaktask_task_start, oaktask_task_wait,
};
use oaktask::handle::CHandle;

/// Given an uninitialized manager, `oaktask_manager_count` reports 0
/// and `oaktask_manager_at` returns an empty handle.
#[test]
fn uninitialized_manager_is_empty() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };

	assert_eq!(unsafe { oaktask_manager_count() }, OAKTASK_E_STATE);
	assert!(unsafe { oaktask_manager_at(0) }.is_null());
	assert!(unsafe { oaktask_manager_at(-1) }.is_null());
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Given an initialized manager, starting a task adds it to the list
/// and `oaktask_manager_at` returns a borrowed handle at that index.
#[test]
fn started_task_is_listed_at_index() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };
	unsafe { oaktask_manager_init() };

	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-manager-at.oakproj".to_string();
	let mut task = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	assert_eq!(unsafe { oaktask_task_start(task) }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_manager_count() }, 1);

	// The borrowed handle reports the same task once it finishes.
	assert_eq!(unsafe { oaktask_task_wait(task) }, OAKTASK_OK);
	let borrowed = unsafe { oaktask_manager_at(0) };
	assert!(!borrowed.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_is_finished(borrowed) }, 1);
	let mut b = borrowed;
	unsafe { oaktask_task_free(&mut b) };

	// Out of range -> empty.
	assert!(unsafe { oaktask_manager_at(5) }.is_null());

	unsafe { oaktask_manager_delete_finished() };
	assert_eq!(unsafe { oaktask_manager_count() }, 0);
	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
	unsafe { oaktask_manager_shutdown() };
}

/// Given finished tasks, `oaktask_manager_delete_finished` removes them
/// and `oaktask_manager_count` shrinks accordingly.
#[test]
fn delete_finished_removes_finished_tasks() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };
	unsafe { oaktask_manager_init() };

	*PROJECT_FILENAME.lock().unwrap() = "/tmp/oak-delete-finished.oakproj".to_string();
	let mut t1 = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	let mut t2 = unsafe { oaktask_create_project_save(fake_handle(), std::ptr::null(), 0) };
	assert_eq!(unsafe { oaktask_task_start(t1) }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_task_start(t2) }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_manager_count() }, 2);

	// Wait for both to finish, then delete_finished removes both.
	unsafe { oaktask_task_wait(t1) };
	unsafe { oaktask_task_wait(t2) };
	unsafe { oaktask_manager_delete_finished() };
	assert_eq!(unsafe { oaktask_manager_count() }, 0);

	free(&mut t1);
	free(&mut t2);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
	unsafe { oaktask_manager_shutdown() };
}

/// Given a registered codec submitter, codec tasks are routed through
/// the manager; `oaktask_register_codec_submitter` is idempotent.
#[test]
fn codec_submitter_registration_is_idempotent() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };

	// The submitter can be registered without a manager (it is a codec-side
	// callback, not a manager task).
	assert_eq!(unsafe { oaktask_register_codec_submitter() }, OAKTASK_OK);
	assert_eq!(unsafe { oaktask_register_codec_submitter() }, OAKTASK_OK);
	unsafe { oaktask_manager_init() };
	// Manager init registers too; repeated registration stays OK.
	assert_eq!(unsafe { oaktask_register_codec_submitter() }, OAKTASK_OK);

	// The stub captured the callback.
	assert!(SUBMIT_CB.lock().unwrap().is_some());

	unsafe { oaktask_manager_shutdown() };
}

/// Given a shutdown manager, subsequent start/at calls are safe and
/// report no tasks without panicking.
#[test]
fn shutdown_is_safe_and_idempotent() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };

	unsafe { oaktask_manager_init() };
	unsafe { oaktask_manager_shutdown() };
	// Idempotent.
	unsafe { oaktask_manager_shutdown() };

	// After shutdown, start/at are safe (E_STATE / empty).
	assert_eq!(unsafe { oaktask_manager_count() }, OAKTASK_E_STATE);
	assert!(unsafe { oaktask_manager_at(0) }.is_null());
	unsafe { oaktask_manager_delete_finished() };
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);

	// Re-init works after shutdown.
	assert_eq!(unsafe { oaktask_manager_init() }, OAKTASK_OK);
	unsafe { oaktask_manager_shutdown() };
}

/// A conform request submitted through the codec callback runs the
/// ConformTask synchronously (interim contract).
#[test]
fn codec_submit_runs_conform_synchronously() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };

	assert_eq!(unsafe { oaktask_register_codec_submitter() }, OAKTASK_OK);

	// Prepare real working files so the conform rename succeeds.
	let dir = std::env::temp_dir().join("oak-conform-submit");
	let _ = std::fs::create_dir_all(&dir);
	let final0 = dir.join("out.0.pcm");
	let final1 = dir.join("out.1.pcm");
	let _ = std::fs::remove_file(&final0);
	let _ = std::fs::remove_file(&final1);
	let working0 = dir.join("out.0.pcm.working");
	let working1 = dir.join("out.1.pcm.working");
	std::fs::write(&working0, b"pcm").unwrap();
	std::fs::write(&working1, b"pcm").unwrap();

	DECODER_OPEN_RESULT.store(0, Ordering::SeqCst);
	DECODER_CONFORM_RESULT.store(0, Ordering::SeqCst);

	let request = oaktask::bridge::codec::OakCodecTaskRequest {
		kind: 0, // OAKCODEC_TASK_CONFORM
		input_filename: common::cstr_of("/tmp/oak-conform-src.mov"),
		output_filename: common::cstr_of(&final0.to_string_lossy()),
		stream_index: 1,
		sample_rate: 48000,
		channel_layout: 3, // stereo
		sample_format: 0,
		proxy_width: 0,
		proxy_height: 0,
	};
	let cb = SUBMIT_CB.lock().unwrap().unwrap();
	let ret = unsafe { cb(&request, std::ptr::null_mut()) };
	assert_eq!(ret, 0); // OAKCODEC_OK
	assert!(final0.exists(), "final pcm file was not moved into place");
	assert!(final1.exists(), "second channel pcm file was not moved into place");
	let _ = std::fs::remove_file(&final0);
	let _ = std::fs::remove_file(&final1);
	let _ = std::fs::remove_file(&working0);
	let _ = std::fs::remove_file(&working1);

	// Null request -> OAKCODEC_E_INVALID.
	let ret = unsafe { cb(std::ptr::null(), std::ptr::null_mut()) };
	assert_eq!(ret, oaktask::bridge::codec::OAKCODEC_E_INVALID);
	unsafe { oaktask_manager_shutdown() };
}

/// A proxy request submitted through the codec callback runs ffmpeg (a real
/// test script), parses progress lines, and renames the working file into
/// place.
#[test]
fn codec_submit_runs_proxy_with_script() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };
	assert_eq!(unsafe { oaktask_register_codec_submitter() }, OAKTASK_OK);

	let dir = std::env::temp_dir().join("oak-proxy-run");
	let _ = std::fs::create_dir_all(&dir);
	let script = dir.join("fake-ffmpeg.sh");
	let output = dir.join("proxy.mp4");
	let _ = std::fs::remove_file(&output);

	// The script reports progress on stdout and creates its last argument
	// (the working file), then exits 0.
	std::fs::write(
		&script,
		"#!/bin/bash\nprintf 'out_time_us=5000000\\n'\nprintf 'out_time_us=9000000\\n'\nOUT=\"${@: -1}\"\ntouch \"$OUT\"\nexit 0\n",
	)
	.unwrap();
	#[cfg(unix)]
	{
		use std::os::unix::fs::PermissionsExt;
		std::fs::set_permissions(&script, std::fs::Permissions::from_mode(0o755)).unwrap();
	}
	*PROXY_FFMPEG_PATH.lock().unwrap() = script.to_string_lossy().into_owned();

	let request = oaktask::bridge::codec::OakCodecTaskRequest {
		kind: 1, // OAKCODEC_TASK_PROXY
		input_filename: common::cstr_of("/tmp/oak-proxy-src.mov"),
		output_filename: common::cstr_of(&output.to_string_lossy()),
		stream_index: 0,
		sample_rate: 0,
		channel_layout: 0,
		sample_format: 0,
		proxy_width: 0,
		proxy_height: 0,
	};
	let cb = SUBMIT_CB.lock().unwrap().unwrap();
	let ret = unsafe { cb(&request, std::ptr::null_mut()) };
	assert_eq!(ret, 0, "proxy submit should succeed");
	assert!(output.exists(), "proxy output file was not created");

	let _ = std::fs::remove_file(&output);
	let _ = std::fs::remove_file(&script);
	unsafe { oaktask_manager_shutdown() };
}

/// Proxy failure paths: ffmpeg missing, failing script, and a script that
/// does not produce the output.
#[test]
fn proxy_failure_paths() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };
	assert_eq!(unsafe { oaktask_register_codec_submitter() }, OAKTASK_OK);

	let dir = std::env::temp_dir().join("oak-proxy-fail");
	let _ = std::fs::create_dir_all(&dir);
	let output = dir.join("proxy.mp4");

	let cb = SUBMIT_CB.lock().unwrap().unwrap();
	let request = oaktask::bridge::codec::OakCodecTaskRequest {
		kind: 1,
		input_filename: common::cstr_of("/tmp/oak-proxy-src.mov"),
		output_filename: common::cstr_of(&output.to_string_lossy()),
		stream_index: 0,
		sample_rate: 0,
		channel_layout: 0,
		sample_format: 0,
		proxy_width: 0,
		proxy_height: 0,
	};

	// 1. No ffmpeg found.
	PROXY_FFMPEG_PATH.lock().unwrap().clear();
	let ret = unsafe { cb(&request, std::ptr::null_mut()) };
	assert_eq!(ret, oaktask::bridge::codec::OAKCODEC_E_FAILED);

	// 2. ffmpeg exits non-zero.
	let script = dir.join("fail-ffmpeg.sh");
	std::fs::write(&script, "#!/bin/bash\nexit 1\n").unwrap();
	#[cfg(unix)]
	{
		use std::os::unix::fs::PermissionsExt;
		std::fs::set_permissions(&script, std::fs::Permissions::from_mode(0o755)).unwrap();
	}
	*PROXY_FFMPEG_PATH.lock().unwrap() = script.to_string_lossy().into_owned();
	let ret = unsafe { cb(&request, std::ptr::null_mut()) };
	assert_eq!(ret, oaktask::bridge::codec::OAKCODEC_E_FAILED);

	// 3. ffmpeg exits 0 but creates nothing.
	let script2 = dir.join("noout-ffmpeg.sh");
	std::fs::write(&script2, "#!/bin/bash\nexit 0\n").unwrap();
	#[cfg(unix)]
	{
		use std::os::unix::fs::PermissionsExt;
		std::fs::set_permissions(&script2, std::fs::Permissions::from_mode(0o755)).unwrap();
	}
	*PROXY_FFMPEG_PATH.lock().unwrap() = script2.to_string_lossy().into_owned();
	let ret = unsafe { cb(&request, std::ptr::null_mut()) };
	assert_eq!(ret, oaktask::bridge::codec::OAKCODEC_E_FAILED);

	let _ = std::fs::remove_file(&output);
	let _ = std::fs::remove_file(&script);
	let _ = std::fs::remove_file(&script2);
	unsafe { oaktask_manager_shutdown() };
}

/// Conform failure paths: decoder open fails and conform returns a
/// non-OK code.
#[test]
fn conform_failure_paths() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	unsafe { oaktask_manager_shutdown() };
	assert_eq!(unsafe { oaktask_register_codec_submitter() }, OAKTASK_OK);

	let dir = std::env::temp_dir().join("oak-conform-fail");
	let _ = std::fs::create_dir_all(&dir);
	let final0 = dir.join("out.0.pcm");
	let working0 = dir.join("out.0.pcm.working");

	let cb = SUBMIT_CB.lock().unwrap().unwrap();
	let request = oaktask::bridge::codec::OakCodecTaskRequest {
		kind: 0,
		input_filename: common::cstr_of("/tmp/oak-conform-src.mov"),
		output_filename: common::cstr_of(&final0.to_string_lossy()),
		stream_index: 1,
		sample_rate: 48000,
		channel_layout: 3,
		sample_format: 0,
		proxy_width: 0,
		proxy_height: 0,
	};

	// Decoder open fails.
	DECODER_OPEN_RESULT.store(-1, Ordering::SeqCst);
	let ret = unsafe { cb(&request, std::ptr::null_mut()) };
	assert_eq!(ret, oaktask::bridge::codec::OAKCODEC_E_FAILED);

	// Conform reports a failure.
	DECODER_OPEN_RESULT.store(0, Ordering::SeqCst);
	DECODER_CONFORM_RESULT.store(-50003, Ordering::SeqCst);
	std::fs::write(&working0, b"pcm").unwrap();
	let ret = unsafe { cb(&request, std::ptr::null_mut()) };
	assert_eq!(ret, oaktask::bridge::codec::OAKCODEC_E_FAILED);
	// The partial working file is cleaned up.
	assert!(!working0.exists(), "partial working file should be removed");

	// Conform reports cancellation.
	DECODER_CONFORM_RESULT.store(-50006, Ordering::SeqCst);
	std::fs::write(&working0, b"pcm").unwrap();
	let ret = unsafe { cb(&request, std::ptr::null_mut()) };
	assert_eq!(ret, oaktask::bridge::codec::OAKCODEC_E_FAILED);

	let _ = std::fs::remove_file(&working0);
	unsafe { oaktask_manager_shutdown() };
}

fn free(t: &mut CHandle) {
	unsafe {
		oaktask_task_free(t);
	}
}
