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

//! Integration tests for the **exporter family** (`src/codec.rs`,
//! "Exporter family"; module contract
//! `engine/include/oakengine/exporter.h`).
//!
//! Coverage rules (see the family test charter):
//!   1. no mocks — every call goes through the real facade into the real
//!      module crates (the only stubs are the host-provided `oakcore_*`
//!      symbols in `tests/common`); the output file is a REAL mp4 written
//!      by the statically linked FFmpeg (oakcodec encoder), asserted by
//!      its `ftyp` box;
//!   2. every exporter-family export is exercised on a legal path with the
//!      result asserted;
//!   3. illegal inputs (NULL seq/path, negative ranges, unknown codecs)
//!      always yield a negative error code — never a crash;
//!   4. the progress callback receives at least one update during a run,
//!      with the installed userdata.
//!
//! ## Serialization
//!
//! The tests assemble projects and run export tasks, both of which touch
//! process-wide state (the undo stack cleared by `oakengine_project_new`,
//! the global task manager), so every test takes a shared [`SERIAL`] mutex
//! (the same pattern as `it_task`).

use super::common;

use std::ffi::{c_char, c_double, c_int, c_void};
use std::io::Read;
use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};

use crate::codec::{
	oakengine_encoding_params_create, oakengine_encoding_params_enable_audio,
	oakengine_encoding_params_enable_video, oakengine_encoding_params_set_filename,
	oakengine_encoding_params_set_format, oakengine_export_last_error, oakengine_export_render,
	oakengine_export_render_with_params, oakengine_export_set_progress_callback,
};
use crate::common::OakVideoParamsPod;
use crate::handle::{OakEngineProject, OakEngineSequence, free_box};
use crate::node::{
	oakengine_footage_free, oakengine_project_create, oakengine_project_free,
	oakengine_project_import_footage, oakengine_project_new,
};
use crate::pods::OakExportOptions;
use crate::testmedia::oakengine_testmedia_write_clip;
use crate::timeline::{
	oakengine_sequence_add_footage_clip_ex, oakengine_sequence_add_track, oakengine_sequence_new,
	oakengine_sequence_set_video_params,
};

/// `OAKENGINE_TRACK_TYPE_*` (timeline.h).
const TRACK_VIDEO: c_int = 0;
const TRACK_AUDIO: c_int = 1;
/// `olive::ExportFormat::Format` ids (mp4 = MPEG-4 video).
const FORMAT_MP4: c_int = 2;
/// `olive::ExportCodec::Codec` ids.
const CODEC_H264: c_int = 1;
const CODEC_AAC: c_int = 12;

/// Serializes every test in this binary (see the module docs). Poisoned by
/// a panicking test, the lock is recovered with `into_inner` so one failure
/// does not cascade into `PoisonError` failures in every later test.
static SERIAL: Mutex<()> = Mutex::new(());

/// Take the [`SERIAL`] lock, recovering from any poisoning.
fn serial() -> std::sync::MutexGuard<'static, ()> {
	SERIAL.lock().unwrap_or_else(|e| e.into_inner())
}

/// A unique temp path (per-process, so parallel test binaries never
/// collide).
fn temp_path(kind: &str) -> std::path::PathBuf {
	std::env::temp_dir().join(format!("oakengine-it-export-{kind}-{}.mp4", std::process::id()))
}

/// Encode the facade test clip and assemble a project + sequence carrying
/// it: one video track/clip and one audio track/clip, both spanning
/// `0..frame_count` at `fps` frames per second (mirrors the CLI's
/// `assemble_project`). Returns `(project, sequence)` — the caller
/// releases the sequence box with `free_box` and the project with
/// `oakengine_project_free`.
///
/// # Safety
/// The returned handles must be released by the caller exactly once.
unsafe fn assemble_test_sequence(
	media: &std::path::Path,
	width: c_int,
	height: c_int,
	frame_count: i64,
	fps: c_int,
) -> (*mut OakEngineProject, *mut OakEngineSequence) {
	let media_c = std::ffi::CString::new(media.to_string_lossy().into_owned()).unwrap();
	assert_eq!(
		oakengine_testmedia_write_clip(media_c.as_ptr(), width, height, frame_count as c_int, fps),
		0,
		"generate the source clip"
	);

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);

	let footage = unsafe { oakengine_project_import_footage(project, media_c.as_ptr()) };
	assert!(!footage.is_null(), "import the test clip");

	let seq = unsafe { oakengine_sequence_new(project, c"Export Test".as_ptr()) };
	assert!(!seq.is_null());

	assert_eq!(
		unsafe {
			oakengine_sequence_set_video_params(
				seq, width, height, fps, 1, 1, 1, 0, 4, // PIXEL_FORMAT_F32
				0,
			)
		},
		0,
		"set the sequence frame rate"
	);

	let vt = unsafe { oakengine_sequence_add_track(seq, TRACK_VIDEO) };
	assert!(vt >= 0, "add the video track");
	let vclip = unsafe {
		oakengine_sequence_add_footage_clip_ex(seq, footage, TRACK_VIDEO, vt, 0, frame_count, 0)
	};
	assert!(!vclip.is_null(), "place the video clip");

	let at = unsafe { oakengine_sequence_add_track(seq, TRACK_AUDIO) };
	assert!(at >= 0, "add the audio track");
	let aclip = unsafe {
		oakengine_sequence_add_footage_clip_ex(seq, footage, TRACK_AUDIO, at, 0, frame_count, 0)
	};
	assert!(!aclip.is_null(), "place the audio clip");

	unsafe { oakengine_footage_free(footage) };
	(project, seq)
}

/// Read the facade's thread-local export last-error (the string the
/// assertion messages embed).
fn export_last_error_str() -> String {
	let mut buf = [0 as c_char; 512];
	let n = unsafe { oakengine_export_last_error(buf.as_mut_ptr(), 512) };
	if n <= 0 {
		return String::new();
	}
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	String::from_utf8_lossy(unsafe {
		std::slice::from_raw_parts(buf.as_ptr() as *const u8, len)
	})
	.into_owned()
}

/// Assert the exported file exists, is non-empty and starts with the MP4
/// `ftyp` box.
fn assert_real_mp4(path: &std::path::Path) {
	let meta = std::fs::metadata(path).expect("the exported file must exist");
	assert!(meta.len() > 0, "the exported file must be non-empty");
	let mut head = [0u8; 12];
	std::fs::File::open(path)
		.expect("reopen the exported file")
		.read_exact(&mut head)
		.expect("read the file head");
	assert_eq!(&head[4..8], b"ftyp", "MP4 files start with the ftyp box");
}

/// Release the assembly returned by [`assemble_test_sequence`].
///
/// # Safety
/// The handles must be the ones returned by [`assemble_test_sequence`].
unsafe fn drop_test_sequence(project: *mut OakEngineProject, seq: *mut OakEngineSequence) {
	unsafe {
		free_box::<OakEngineSequence>(seq);
		oakengine_project_free(project);
	}
}

// ---------------------------------------------------------------------------
// Legal paths (real mp4 output)
// ---------------------------------------------------------------------------

/// `oakengine_export_render` end-to-end: a 1 s test clip (10 frames at
/// 10 fps, 64x64) in a project + sequence is exported to H.264/AAC MP4
/// with explicit `OakExportOptions`; the file exists, is non-empty, starts
/// with the `ftyp` box, and `oakengine_export_last_error` is empty after
/// the success.
#[test]
fn export_render_writes_real_mp4() {
	let _g = serial();
	common::force_link();

	let media = temp_path("src");
	let out = temp_path("out");
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);

	let (project, seq) = unsafe { assemble_test_sequence(&media, 64, 64, 10, 10) };

	let opts = OakExportOptions {
		video_codec: 0, // OAKENGINE_EXPORT_VIDEO_H264
		audio_codec: 0, // OAKENGINE_EXPORT_AUDIO_AAC
		video_bit_rate: 0,
		audio_sample_rate: 48000,
		audio_channel_count: 2,
	};
	let out_c = std::ffi::CString::new(out.to_string_lossy().into_owned()).unwrap();
	let rc = unsafe { oakengine_export_render(seq, out_c.as_ptr(), 0, 10, 64, 64, &opts) };
	assert_eq!(
		rc,
		0,
		"the mp4 export must succeed with the real encoder (last_error: {})",
		export_last_error_str()
	);

	assert_real_mp4(&out);

	// The thread-local last-error slot is empty after a success.
	let mut buf = [0 as c_char; 256];
	let n = unsafe { oakengine_export_last_error(buf.as_mut_ptr(), 256) };
	assert_eq!(n, 0, "last_error must be empty after a successful export");

	unsafe { drop_test_sequence(project, seq) };
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);
}

/// `oakengine_export_render_with_params` end-to-end: the same assembly
/// driven through a caller-built encoding-params handle (the path the
/// app's `start_export` uses). The handle is consumed by the export task.
#[test]
fn export_render_with_params_writes_real_mp4() {
	let _g = serial();
	common::force_link();

	let media = temp_path("srcwp");
	let out = temp_path("outwp");
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);

	let (project, seq) = unsafe { assemble_test_sequence(&media, 64, 64, 10, 10) };

	let params = unsafe { oakengine_encoding_params_create() };
	assert!(!params.is_null());
	let out_c = std::ffi::CString::new(out.to_string_lossy().into_owned()).unwrap();
	assert_eq!(unsafe { oakengine_encoding_params_set_filename(params, out_c.as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_set_format(params, FORMAT_MP4) }, 0);
	let pod = OakVideoParamsPod {
		width: 64,
		height: 64,
		time_base_num: 1,
		time_base_den: 10, // 10 fps → frame duration 1/10
		format: 0,
		pixel_aspect_num: 1,
		pixel_aspect_den: 1,
		interlacing: 0,
		color_range: 0,
		divider: 1,
		video_type: 0,
		premultiplied_alpha: 0,
	};
	assert_eq!(unsafe { oakengine_encoding_params_enable_video(params, &pod, CODEC_H264) }, 0);
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_audio(params, 48000, 0x3, 0, CODEC_AAC) },
		0
	);
	// Export length: 10 frames at 10 fps = 1 s.
	unsafe {
		crate::codec::oakengine_encoding_params_set_export_length(params, 1, 1);
	}

	let rc = unsafe { oakengine_export_render_with_params(seq, params) };
	assert_eq!(
		rc,
		0,
		"the with_params export must succeed with the real encoder (last_error: {})",
		export_last_error_str()
	);
	// The params handle was consumed by the task; do NOT destroy it here.

	assert_real_mp4(&out);

	unsafe { drop_test_sequence(project, seq) };
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);
}

/// `oakengine_export_render` with NULL opts selects the documented
/// defaults (H.264/AAC mp4), and the installed progress callback receives
/// at least one update with the installed userdata during the run.
#[test]
fn export_render_defaults_and_progress_callback() {
	let _g = serial();
	common::force_link();

	let media = temp_path("srcprog");
	let out = temp_path("outprog");
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);

	let (project, seq) = unsafe { assemble_test_sequence(&media, 64, 64, 10, 10) };

	// Progress callbacks are thread-local on the exporting thread; the
	// sync run fires them there, so the atomics are safe to reset while
	// holding the serial lock.
	PROGRESS_CALLS.store(0, Ordering::SeqCst);
	PROGRESS_USERDATA_HIT.store(0, Ordering::SeqCst);
	let out_c = std::ffi::CString::new(out.to_string_lossy().into_owned()).unwrap();
	unsafe {
		oakengine_export_set_progress_callback(Some(count_progress), PROGRESS_TOKEN as *mut c_void);
	}
	let rc = unsafe { oakengine_export_render(seq, out_c.as_ptr(), 0, 10, 64, 64, std::ptr::null()) };
	assert_eq!(
		rc,
		0,
		"NULL opts must select the H.264/AAC defaults (last_error: {})",
		export_last_error_str()
	);
	assert_real_mp4(&out);
	assert!(
		PROGRESS_CALLS.load(Ordering::SeqCst) > 0,
		"progress must be reported during a run"
	);
	assert!(
		PROGRESS_USERDATA_HIT.load(Ordering::SeqCst) > 0,
		"the installed userdata must reach the callback"
	);

	// NULL disables the callback for subsequent runs.
	unsafe { oakengine_export_set_progress_callback(None, std::ptr::null_mut()) };

	unsafe { drop_test_sequence(project, seq) };
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);
}

// ---------------------------------------------------------------------------
// Illegal inputs (negative codes, no crash)
// ---------------------------------------------------------------------------

/// Every invalid argument combination of `oakengine_export_render` returns
/// `OAKENGINE_E_INVALID` (-1) with a non-empty last-error, and the
/// `oakengine_export_render_with_params` NULL paths return E_INVALID
/// without consuming the caller's params handle.
#[test]
fn export_render_illegal_arguments() {
	let _g = serial();
	common::force_link();

	let media = temp_path("srcbad");
	let out = temp_path("outbad");
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);

	let (project, seq) = unsafe { assemble_test_sequence(&media, 64, 64, 10, 10) };

	let opts = OakExportOptions {
		video_codec: 0,
		audio_codec: 0,
		video_bit_rate: 0,
		audio_sample_rate: 48000,
		audio_channel_count: 2,
	};
	let out_c = std::ffi::CString::new(out.to_string_lossy().into_owned()).unwrap();

	// NULL seq / path.
	assert_eq!(
		unsafe { oakengine_export_render(std::ptr::null_mut(), out_c.as_ptr(), 0, 10, 64, 64, &opts) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_export_render(seq, std::ptr::null(), 0, 10, 64, 64, &opts) },
		-1
	);
	// Negative / inverted ranges.
	assert_eq!(
		unsafe { oakengine_export_render(seq, out_c.as_ptr(), -1, 10, 64, 64, &opts) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_export_render(seq, out_c.as_ptr(), 5, 5, 64, 64, &opts) },
		-1
	);
	// Unknown codec ids.
	let bad_video = OakExportOptions { video_codec: 99, ..opts };
	assert_eq!(
		unsafe { oakengine_export_render(seq, out_c.as_ptr(), 0, 10, 64, 64, &bad_video) },
		-1
	);
	let bad_audio = OakExportOptions { audio_codec: 99, ..opts };
	assert_eq!(
		unsafe { oakengine_export_render(seq, out_c.as_ptr(), 0, 10, 64, 64, &bad_audio) },
		-1
	);
	// An unsupported audio channel count.
	let bad_channels = OakExportOptions { audio_channel_count: 3, ..opts };
	assert_eq!(
		unsafe { oakengine_export_render(seq, out_c.as_ptr(), 0, 10, 64, 64, &bad_channels) },
		-1
	);
	// The failures record a reason.
	let mut buf = [0 as c_char; 256];
	let n = unsafe { oakengine_export_last_error(buf.as_mut_ptr(), 256) };
	assert!(n > 0, "a failed export must record a last error");

	// `with_params`: NULL arguments are rejected without consuming the
	// valid handle (the caller destroys it afterwards).
	let params = unsafe { oakengine_encoding_params_create() };
	assert!(!params.is_null());
	assert_eq!(
		unsafe { oakengine_export_render_with_params(std::ptr::null_mut(), params) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_export_render_with_params(seq, std::ptr::null()) },
		-1
	);
	unsafe { crate::codec::oakengine_encoding_params_destroy(params) };

	assert!(!out.exists(), "a rejected export must not write the output");

	unsafe { drop_test_sequence(project, seq) };
	let _ = std::fs::remove_file(&media);
	let _ = std::fs::remove_file(&out);
}

// ---------------------------------------------------------------------------
// Progress callback bookkeeping
// ---------------------------------------------------------------------------

/// Sentinel userdata the progress test installs.
const PROGRESS_TOKEN: usize = 0x0A0A_5EED;

/// Number of progress callback invocations (reset per test).
static PROGRESS_CALLS: AtomicUsize = AtomicUsize::new(0);
/// Number of invocations that received the sentinel userdata.
static PROGRESS_USERDATA_HIT: AtomicUsize = AtomicUsize::new(0);

/// Counts every progress callback and checks the userdata round-trip.
unsafe extern "C" fn count_progress(_fraction: c_double, userdata: *mut c_void) {
	PROGRESS_CALLS.fetch_add(1, Ordering::SeqCst);
	if userdata as usize == PROGRESS_TOKEN {
		PROGRESS_USERDATA_HIT.fetch_add(1, Ordering::SeqCst);
	}
}
