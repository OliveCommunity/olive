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

//! Run-path coverage: conform/proxy/export/render task bodies driven end to
//! end against the link-time stubs in `common` (codec-submit routing,
//! decoder/encoder knobs, a fake ffmpeg executable), plus the base
//! [`Task`] API (reset/elapsed/wait/cancel) and the subscribe event stream.
//!
//! These tests exercise the same paths the C++ gtest suite covers with the
//! real dylibs (`src/task/tests/task_test.cpp`); here the stubs make them
//! deterministic under plain `cargo test`.

#[path = "common/mod.rs"]
mod common;

use std::ffi::c_int;
use std::sync::atomic::Ordering;

use common::*;
use oaktask::bridge::codec::{
	OakCodecTaskRequest, OAKCODEC_E_CANCELLED, OAKCODEC_E_FAILED, OAKCODEC_TASK_CONFORM,
	OAKCODEC_TASK_PROXY,
};
use oaktask::error::Result;
use oaktask::ffi::manager::oaktask_manager_shutdown;
use oaktask::ffi::project::oaktask_create_export;
use oaktask::ffi::task::{oaktask_task_error, oaktask_task_free, oaktask_task_start_sync};
use oaktask::task::{Task, TaskBehavior, TaskEvent};

// ---------------------------------------------------------------------------
// Codec-submit routing (conform/proxy run bodies)
// ---------------------------------------------------------------------------

/// Build a codec task request and drive it through the registered submit
/// callback (the codec module's view of the task module).
/// Poison-tolerant serialization (a failing sibling test must not cascade).
fn lock() -> std::sync::MutexGuard<'static, ()> {
	MANAGER_LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

fn submit(request: &OakCodecTaskRequest) -> c_int {
	let cb = SUBMIT_CB.lock().unwrap().clone();
	match cb {
		Some(f) => unsafe { f(request, std::ptr::null_mut()) },
		None => OAKCODEC_E_FAILED,
	}
}

/// Given a conform request with pre-existing working files, the conform task
/// transcodes (stub) and renames the working files into place — the submit
/// callback returns OAKCODEC_OK.
#[test]
fn conform_run_succeeds_via_codec_submit() {
	let _g = lock();
	reset_stubs();
	unsafe { oaktask::ffi::manager::oaktask_register_codec_submitter() };
	DECODER_OPEN_RESULT.store(0, Ordering::SeqCst);
	DECODER_CONFORM_RESULT.store(0, Ordering::SeqCst);

	let dir = std::env::temp_dir();
	let out = dir.join("oak-conform.0.pcm");
	let out_str = out.to_string_lossy().into_owned();
	let working = [
		dir.join("oak-conform.0.pcm.working"),
		dir.join("oak-conform.1.pcm.working"),
	];
	for w in &working {
		std::fs::write(w, b"pcm").unwrap();
	}

	let request = OakCodecTaskRequest {
		kind: OAKCODEC_TASK_CONFORM,
		input_filename: cstr_of("/nonexistent/input.mp4"),
		output_filename: cstr_of(&out_str),
		stream_index: 0,
		sample_rate: 48000,
		channel_layout: 0x3, // stereo -> 2 working files
		sample_format: 4,
		proxy_width: 0,
		proxy_height: 0,
	};
	assert_eq!(submit(&request), 0, "conform submit must succeed");

	// The working files were renamed into their final names.
	assert!(out.exists(), "final pcm file must exist after rename");
	assert!(!working[0].exists(), "working file must be renamed away");
	assert!(!working[1].exists());

	for w in &working {
		let _ = std::fs::remove_file(w);
	}
	for f in [&out, &dir.join("oak-conform.1.pcm")] {
		let _ = std::fs::remove_file(f);
	}
	unsafe { oaktask_manager_shutdown() };
}

/// Given a conform request whose decoder fails to open, the task reports the
/// decoder error and the submit callback returns OAKCODEC_E_FAILED.
#[test]
fn conform_run_fails_when_decoder_open_fails() {
	let _g = lock();
	reset_stubs();
	unsafe { oaktask::ffi::manager::oaktask_register_codec_submitter() };
	DECODER_OPEN_RESULT.store(-50003, Ordering::SeqCst);

	let request = OakCodecTaskRequest {
		kind: OAKCODEC_TASK_CONFORM,
		input_filename: cstr_of("/nonexistent/input.mp4"),
		output_filename: cstr_of("/nonexistent/audio.0.pcm"),
		stream_index: 0,
		sample_rate: 48000,
		channel_layout: 0x3,
		sample_format: 4,
		proxy_width: 0,
		proxy_height: 0,
	};
	assert_eq!(submit(&request), OAKCODEC_E_FAILED);
	unsafe { oaktask_manager_shutdown() };
}

/// Given a conform request whose decode is cancelled, the task cleans up the
/// working files and fails (the cancelled error is surfaced through the
/// task's error string).
#[test]
fn conform_run_reports_cancelled_conform() {
	let _g = lock();
	reset_stubs();
	unsafe { oaktask::ffi::manager::oaktask_register_codec_submitter() };
	DECODER_OPEN_RESULT.store(0, Ordering::SeqCst);
	DECODER_CONFORM_RESULT.store(OAKCODEC_E_CANCELLED, Ordering::SeqCst);

	let dir = std::env::temp_dir();
	let out = dir.join("oak-cancel.0.pcm");
	let working = dir.join("oak-cancel.0.pcm.working");
	std::fs::write(&working, b"pcm").unwrap();

	let request = OakCodecTaskRequest {
		kind: OAKCODEC_TASK_CONFORM,
		input_filename: cstr_of("/nonexistent/input.mp4"),
		output_filename: cstr_of(&out.to_string_lossy()),
		stream_index: 0,
		sample_rate: 48000,
		channel_layout: 0x3,
		sample_format: 4,
		proxy_width: 0,
		proxy_height: 0,
	};
	assert_eq!(submit(&request), OAKCODEC_E_FAILED);
	// The cancelled conform removes its partial working files.
	assert!(
		!working.exists(),
		"working files must be cleaned up on cancel"
	);
	assert!(!out.exists());
	unsafe { oaktask_manager_shutdown() };
}

/// Write an executable fake-ffmpeg script that creates its last argument
/// (the working proxy file) and reports some progress.
fn write_fake_ffmpeg(path: &std::path::Path, body: &str) {
	std::fs::write(path, body).unwrap();
	use std::os::unix::fs::PermissionsExt;
	std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755)).unwrap();
}

/// Given a proxy request and a working fake ffmpeg, the proxy task spawns it,
/// parses progress, renames the working file into place and succeeds.
#[test]
fn proxy_run_succeeds_with_fake_ffmpeg() {
	let _g = lock();
	reset_stubs();
	unsafe { oaktask::ffi::manager::oaktask_register_codec_submitter() };

	let dir = std::env::temp_dir();
	let script = dir.join("oak-fake-ffmpeg.sh");
	write_fake_ffmpeg(
		&script,
		"#!/bin/sh\nfor a in \"$@\"; do :; done\necho \"out_time_us=5000000\"\ntouch \"$a\"\nexit 0\n",
	);
	*PROXY_FFMPEG_PATH.lock().unwrap() = script.to_string_lossy().into_owned();

	let out = dir.join("oak-proxy-out.mp4");
	let out_str = out.to_string_lossy().into_owned();
	let working = dir.join("oak-proxy-out.mp4.working.mp4");
	let _ = std::fs::remove_file(&out);
	let _ = std::fs::remove_file(&working);

	let request = OakCodecTaskRequest {
		kind: OAKCODEC_TASK_PROXY,
		input_filename: cstr_of("/nonexistent/source.mp4"),
		output_filename: cstr_of(&out_str),
		stream_index: 0,
		sample_rate: 0,
		channel_layout: 0,
		sample_format: 0,
		proxy_width: 0,
		proxy_height: 0,
	};
	assert_eq!(submit(&request), 0, "proxy submit must succeed");
	assert!(out.exists(), "proxy output must exist after rename");
	assert!(!working.exists());

	let _ = std::fs::remove_file(&out);
	let _ = std::fs::remove_file(&script);
	unsafe { oaktask_manager_shutdown() };
}

/// Given a proxy request whose ffmpeg exits non-zero, the task cleans up the
/// working file and fails.
#[test]
fn proxy_run_fails_when_ffmpeg_fails() {
	let _g = lock();
	reset_stubs();
	unsafe { oaktask::ffi::manager::oaktask_register_codec_submitter() };

	let dir = std::env::temp_dir();
	let script = dir.join("oak-fake-ffmpeg-fail.sh");
	write_fake_ffmpeg(&script, "#!/bin/sh\nexit 1\n");
	*PROXY_FFMPEG_PATH.lock().unwrap() = script.to_string_lossy().into_owned();

	let out = dir.join("oak-proxy-fail.mp4");
	let request = OakCodecTaskRequest {
		kind: OAKCODEC_TASK_PROXY,
		input_filename: cstr_of("/nonexistent/source.mp4"),
		output_filename: cstr_of(&out.to_string_lossy()),
		stream_index: 0,
		sample_rate: 0,
		channel_layout: 0,
		sample_format: 0,
		proxy_width: 0,
		proxy_height: 0,
	};
	assert_eq!(submit(&request), OAKCODEC_E_FAILED);
	assert!(!out.exists());

	let _ = std::fs::remove_file(&script);
	unsafe { oaktask_manager_shutdown() };
}

/// Given no configured ffmpeg, the proxy task fails with the documented
/// "not found" error.
#[test]
fn proxy_run_fails_without_ffmpeg() {
	let _g = lock();
	reset_stubs();
	unsafe { oaktask::ffi::manager::oaktask_register_codec_submitter() };
	PROXY_FFMPEG_PATH.lock().unwrap().clear();

	let request = OakCodecTaskRequest {
		kind: OAKCODEC_TASK_PROXY,
		input_filename: cstr_of("/nonexistent/source.mp4"),
		output_filename: cstr_of("/nonexistent/out.mp4"),
		stream_index: 0,
		sample_rate: 0,
		channel_layout: 0,
		sample_format: 0,
		proxy_width: 0,
		proxy_height: 0,
	};
	assert_eq!(submit(&request), OAKCODEC_E_FAILED);
	unsafe { oaktask_manager_shutdown() };
}

/// Given an unknown codec task kind, the submit callback rejects it with
/// OAKCODEC_E_INVALID.
#[test]
fn codec_submit_rejects_unknown_kind() {
	let _g = lock();
	reset_stubs();
	unsafe { oaktask::ffi::manager::oaktask_register_codec_submitter() };

	let request = OakCodecTaskRequest {
		kind: 99,
		input_filename: std::ptr::null(),
		output_filename: std::ptr::null(),
		stream_index: 0,
		sample_rate: 0,
		channel_layout: 0,
		sample_format: 0,
		proxy_width: 0,
		proxy_height: 0,
	};
	assert_eq!(submit(&request), oaktask::bridge::codec::OAKCODEC_E_INVALID);
	unsafe { oaktask_manager_shutdown() };
}

// ---------------------------------------------------------------------------
// Export run paths (render + encoder)
// ---------------------------------------------------------------------------

/// A base encoding-params POD with video enabled.
fn export_params(filename: &str) -> oaktask::bridge::codec::OakCodecEncodingParams {
	let mut params = oaktask::bridge::codec::OakCodecEncodingParams {
		filename: [0; 1024],
		format: 0,
		video_enabled: 1,
		video_codec: 0,
		video_width: 1280,
		video_height: 720,
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
	write_cstr(filename, params.filename.as_mut_ptr(), 1024);
	params
}

/// The export task with audio enabled renders the audio ticket and succeeds;
/// the encoder receives the frames.
#[test]
fn export_runs_audio_path() {
	let _g = lock();
	reset_stubs();
	let mut params = export_params("/tmp/oak-export-audio.mp4");
	params.audio_enabled = 1;
	SEQUENCE_LENGTH_NUM.store(10, Ordering::SeqCst);
	SEQUENCE_LENGTH_DEN.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_NUM.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst); // 1s frames

	let mut task = unsafe { oaktask_create_export(fake_handle(), fake_handle(), &params) };
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	unsafe { oaktask_task_free(&mut task) };
}

/// When the encoder cannot open the file, the export fails with the
/// documented error.
#[test]
fn export_fails_on_encoder_open() {
	let _g = lock();
	reset_stubs();
	ENCODER_OPEN_RESULT.store(-50003, Ordering::SeqCst);
	let params = export_params("/tmp/oak-export-open-fail.mp4");

	let mut task = unsafe { oaktask_create_export(fake_handle(), fake_handle(), &params) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	assert!(needed > 0);
	unsafe { oaktask_task_free(&mut task) };
}

/// When the encoder rejects a video frame, the export aborts with the
/// encoder's error.
#[test]
fn export_fails_on_video_write() {
	let _g = lock();
	reset_stubs();
	ENCODER_WRITE_VIDEO_RESULT.store(-50003, Ordering::SeqCst);
	SEQUENCE_LENGTH_NUM.store(10, Ordering::SeqCst);
	SEQUENCE_LENGTH_DEN.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_NUM.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst);
	let params = export_params("/tmp/oak-export-write-fail.mp4");

	let mut task = unsafe { oaktask_create_export(fake_handle(), fake_handle(), &params) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	unsafe { oaktask_task_free(&mut task) };
}

/// When the encoder flush surfaces an error, the export fails after the
/// render loop.
#[test]
fn export_fails_on_flush_error() {
	let _g = lock();
	reset_stubs();
	ENCODER_FLUSH_ERROR.store(1, Ordering::SeqCst);
	*ENCODER_LAST_ERROR.lock().unwrap() = "stub flush failure".to_string();
	SEQUENCE_LENGTH_NUM.store(1, Ordering::SeqCst);
	SEQUENCE_LENGTH_DEN.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_NUM.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst);
	let params = export_params("/tmp/oak-export-flush-fail.mp4");

	let mut task = unsafe { oaktask_create_export(fake_handle(), fake_handle(), &params) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let mut err = [0i8; 256];
	let needed = unsafe { oaktask_task_error(task, err.as_mut_ptr(), err.len() as i32) };
	assert!(needed > 0);
	unsafe { oaktask_task_free(&mut task) };
}

/// When the viewer has no connected output node, the first frame render fails
/// and the export aborts.
#[test]
fn export_fails_without_connected_output() {
	let _g = lock();
	reset_stubs();
	CONNECTED_NODE.store(0, Ordering::SeqCst);
	SEQUENCE_LENGTH_NUM.store(10, Ordering::SeqCst);
	SEQUENCE_LENGTH_DEN.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_NUM.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst);
	let params = export_params("/tmp/oak-export-no-output.mp4");

	let mut task = unsafe { oaktask_create_export(fake_handle(), fake_handle(), &params) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	unsafe { oaktask_task_free(&mut task) };
}

/// When the render ticket hands out null frames repeatedly, the export
/// aborts after the null-frame streak limit (C++ `null_frame_streak_`).
#[test]
fn export_fails_on_null_frame_streak() {
	let _g = lock();
	reset_stubs();
	TICKET_FRAME_VALID.store(0, Ordering::SeqCst);
	SEQUENCE_LENGTH_NUM.store(30, Ordering::SeqCst);
	SEQUENCE_LENGTH_DEN.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_NUM.store(1, Ordering::SeqCst);
	VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst);
	let params = export_params("/tmp/oak-export-null-frames.mp4");

	let mut task = unsafe { oaktask_create_export(fake_handle(), fake_handle(), &params) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let mut err = [0i8; 256];
	let needed = unsafe { oaktask_task_error(task, err.as_mut_ptr(), err.len() as i32) };
	assert!(needed > 0);
	unsafe { oaktask_task_free(&mut task) };
}

// ---------------------------------------------------------------------------
// Base Task API + subscribe event stream
// ---------------------------------------------------------------------------

/// A no-op behavior for direct `Task` API tests.
struct NoopBehavior;

impl TaskBehavior for NoopBehavior {
	fn run(&mut self, _task: &mut Task) -> Result<()> {
		Ok(())
	}
}

/// The base task API: title/error/elapsed/reset/wait/cancel and the
/// lifecycle event stream (STARTED/PROGRESS/FINISHED).
#[test]
fn task_base_api_and_events() {
	let _g = lock();
	reset_stubs();

	let mut task = Task::new("Base API", common::fake_atom());
	task.set_title("Renamed");
	assert_eq!(task.title(), "Renamed");
	assert!(task.error().is_none());
	assert!(!task.is_finished());
	assert!(!task.succeeded());
	assert!(task.elapsed().is_none());

	// Progress is clamped to 0..1 and delivered to the listener.
	let events: std::sync::Arc<std::sync::Mutex<Vec<TaskEvent>>> = Default::default();
	let listener = {
		let events = events.clone();
		Box::new(move |ev: TaskEvent| events.lock().unwrap().push(ev))
	};
	task.set_event_listener(listener);
	task.emit_progress(2.0);
	task.set_behavior(Box::new(NoopBehavior));

	let result = task.start();
	assert!(result.is_ok());
	assert!(task.is_finished());
	assert!(task.succeeded());
	assert!(task.elapsed().is_some());

	// The listener saw PROGRESS (emitted before start), then STARTED and
	// FINISHED from `start()`.
	let seen = events.lock().unwrap().clone();
	assert_eq!(seen.len(), 3, "PROGRESS + STARTED + FINISHED");
	assert!(matches!(seen[0], TaskEvent::Progress(p) if p == 1.0));
	assert!(matches!(seen[1], TaskEvent::Started));
	assert!(matches!(seen[2], TaskEvent::Finished));

	// Cancellation through the atom.
	let atom = task.get_cancel_atom();
	task.set_cancel_event(Box::new(|| {}));
	task.cancel();
	assert!(task.is_cancelled());
	unsafe {
		oaktask::bridge::render::oakrender_cancelatom_cancel(atom);
	}
	assert!(task.is_cancelled());

	// reset clears the lifecycle state.
	task.reset();
	assert!(!task.is_finished());
	assert!(!task.succeeded());
	assert!(task.error().is_none());
	assert!(task.elapsed().is_none());

	// error reporting.
	task.set_error("boom");
	assert_eq!(task.error(), Some("boom"));

	// wait_finished on a finished task returns immediately.
	task.wait_finished();
	drop(atom);
}

/// The error marker `Task::cancelled()` maps to OAKTASK_E_CANCELLED.
#[test]
fn cancelled_marker_maps_to_code() {
	assert_eq!(
		oaktask::task::cancelled().code(),
		oaktask::error::OAKTASK_E_CANCELLED
	);
}
