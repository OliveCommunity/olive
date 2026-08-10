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

//! Test-only `#[no_mangle]` stubs for every extern C symbol the crate
//! imports. Integration test binaries link the crate's rlib, which
//! references the real module DLLs; these stubs satisfy the linker and let
//! the real task logic run against controllable behavior. Each stub family
//! exposes `set_*` helpers used by the tests to drive success/failure paths.

#![allow(non_snake_case, dead_code, unused_variables, clippy::missing_safety_doc)]

use std::ffi::{c_char, c_int, c_void};
use std::sync::atomic::{AtomicI32, AtomicI64, Ordering};
// AtomicUsize/Condvar are only used by the simulated oakrender ticket
// arena, which the `real-oakrender` feature compiles out.
#[cfg(not(feature = "real-oakrender"))]
use std::sync::atomic::AtomicUsize;
use std::sync::Mutex;
#[cfg(not(feature = "real-oakrender"))]
use std::sync::Condvar;

// ---------------------------------------------------------------------------
// Fake handle helpers
// ---------------------------------------------------------------------------

/// A fake refcounted handle: non-null ctx with no-op addref/release.
pub fn fake_handle() -> oaktask::handle::CHandle {
	unsafe extern "C" fn noop(_ctx: *mut c_void) {}
	oaktask::handle::CHandle {
		ctx: 1usize as *mut c_void,
		addref: Some(noop),
		release: Some(noop),
		abi_version: 1,
	}
}

pub fn empty_handle() -> oaktask::handle::CHandle {
	oaktask::handle::CHandle::null()
}

/// A fake OakCancelAtom (ctx non-null so `is_null()` is false).
pub fn fake_atom() -> oaktask::bridge::render::OakCancelAtom {
	unsafe extern "C" fn noop(_ctx: *mut c_void) {}
	oaktask::bridge::render::OakCancelAtom {
		ctx: 2usize as *mut c_void,
		addref: Some(noop),
		release: Some(noop),
		abi_version: 1,
	}
}

pub fn write_cstr(s: &str, buf: *mut c_char, size: i32) -> i32 {
	let bytes = s.as_bytes();
	let needed = bytes.len() as i32 + 1;
	if !buf.is_null() && size >= needed {
		unsafe {
			std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, buf, bytes.len());
			*buf.add(bytes.len()) = 0;
		}
	}
	needed
}

pub fn cstr_of(s: &str) -> *const c_char {
	let mut v = s.as_bytes().to_vec();
	v.push(0);
	v.leak().as_ptr() as *const c_char
}

// ---------------------------------------------------------------------------
// Control statics
// ---------------------------------------------------------------------------

pub static SERIALIZER_LOAD_CODE: AtomicI32 = AtomicI32::new(0); // OAKNODE_SERIALIZER_RESULT_*
pub static SERIALIZER_LOAD_RET: AtomicI32 = AtomicI32::new(0); // oaknode_serializer_load_from_file return
pub static SERIALIZER_SAVE_CODE: AtomicI32 = AtomicI32::new(0);
pub static SERIALIZER_DETAILS: Mutex<&'static str> = Mutex::new("");
pub static PROJECT_FILENAME: Mutex<String> = Mutex::new(String::new());
pub static NODE_LABEL: Mutex<String> = Mutex::new(String::new());
pub static FOOTAGE_FILENAME: Mutex<String> = Mutex::new(String::new());
pub static FOOTAGE_VALID: AtomicI32 = AtomicI32::new(1);
pub static FOOTAGE_STREAM_COUNT: AtomicI32 = AtomicI32::new(1);
pub static VIDEO_TYPE: AtomicI32 = AtomicI32::new(1); // OAKCOMMON_VIDEO_TYPE_STILL
pub static VIDEO_IS_VALID: AtomicI32 = AtomicI32::new(1);
pub static VIDEO_WIDTH: AtomicI32 = AtomicI32::new(640);
pub static VIDEO_HEIGHT: AtomicI32 = AtomicI32::new(360);
pub static VIDEO_TIME_BASE_NUM: AtomicI32 = AtomicI32::new(0); // 0 => render falls back to 1/1
pub static VIDEO_TIME_BASE_DEN: AtomicI32 = AtomicI32::new(1);
pub static VIDEO_LENGTH_NUM: AtomicI64 = AtomicI64::new(0);
pub static VIDEO_LENGTH_DEN: AtomicI64 = AtomicI64::new(1);
pub static SEQUENCE_LENGTH_NUM: AtomicI32 = AtomicI32::new(0);
pub static SEQUENCE_LENGTH_DEN: AtomicI32 = AtomicI32::new(1);
pub static CONNECTED_NODE: AtomicI64 = AtomicI64::new(1); // non-null => connected
pub static FOLDER_CHILD_COUNT: AtomicI32 = AtomicI32::new(0);
pub static REDO_DELTA: AtomicI32 = AtomicI32::new(0);
pub static MULTI_CHILDREN: AtomicI32 = AtomicI32::new(0);
pub static DECODER_OPEN_RESULT: AtomicI32 = AtomicI32::new(0);
pub static DECODER_CONFORM_RESULT: AtomicI32 = AtomicI32::new(0);
pub static DECODER_LAST_ERROR: Mutex<String> = Mutex::new(String::new());
pub static ENCODER_INIT_NULL: AtomicI32 = AtomicI32::new(0);
pub static ENCODER_OPEN_RESULT: AtomicI32 = AtomicI32::new(0);
pub static ENCODER_WRITE_VIDEO_RESULT: AtomicI32 = AtomicI32::new(0);
pub static ENCODER_WRITE_SUBTITLE_RESULT: AtomicI32 = AtomicI32::new(0);
pub static ENCODER_FLUSH_ERROR: AtomicI32 = AtomicI32::new(0); // non-zero => flush reports an error
pub static ENCODER_LAST_ERROR: Mutex<String> = Mutex::new(String::new());
pub static DESIRED_PIXEL_FORMAT: AtomicI32 = AtomicI32::new(0);
pub static IMAGE_SEQUENCE_DIGIT_COUNT: AtomicI32 = AtomicI32::new(0);
pub static CONFIG_DEFAULT_SEQ_FRAME_RATE: AtomicI32 = AtomicI32::new(0); // 0 => config_get returns nothing
pub static PROXY_FFMPEG_PATH: Mutex<String> = Mutex::new(String::new());
pub static SUBMIT_CB: Mutex<Option<unsafe extern "C" fn(*const oaktask::bridge::codec::OakCodecTaskRequest, *mut c_void) -> c_int>> =
	Mutex::new(None);
// Simulated-oakrender-arena state below is only linked in stub mode: the
// `real-oakrender` feature replaces the render stubs with the real
// oakrender exports, so these statics and the `#[no_mangle]` render stubs
// are compiled out together.
#[cfg(not(feature = "real-oakrender"))]
pub static ATOM_CANCELLED: AtomicI32 = AtomicI32::new(0);
#[cfg(not(feature = "real-oakrender"))]
pub static TICKET_FRAME_VALID: AtomicI32 = AtomicI32::new(1); // 0 => get_frame returns empty frame
#[cfg(not(feature = "real-oakrender"))]
pub static TICKET_GET_FRAME_RESULT: AtomicI32 = AtomicI32::new(0);
// Simulated ticket arena (bridge::render ticket stubs below).
// 0 = tickets complete synchronously on submit (the deterministic
// single-thread path the export/task tests rely on); 1 = tickets stay in
// flight until the test fires them with `stub_complete` (scrambled
// completion-order tests).
#[cfg(not(feature = "real-oakrender"))]
pub static TICKET_DEFER: AtomicI32 = AtomicI32::new(0);
pub static NODE_ID: Mutex<String> = Mutex::new(String::new());
pub static AUDIO_TRACK_LIST_NULL: AtomicI32 = AtomicI32::new(1); // 1 => sequence audio track list is empty (null handle)
pub static FOOTAGE_FOUND: AtomicI32 = AtomicI32::new(1); // 0 => oaknode_node_find_input_footage finds nothing
pub static BLOCK_KIND: AtomicI32 = AtomicI32::new(1); // OAKNODE_BLOCK_CLIP
pub static BLOCK_IN_NUM: AtomicI32 = AtomicI32::new(0);
pub static BLOCK_IN_DEN: AtomicI32 = AtomicI32::new(1);
pub static BLOCK_LENGTH_NUM: AtomicI32 = AtomicI32::new(0);
pub static BLOCK_LENGTH_DEN: AtomicI32 = AtomicI32::new(1);
pub static TRACK_BLOCK_COUNT: AtomicI32 = AtomicI32::new(0);
pub static TRACK_TYPE: AtomicI32 = AtomicI32::new(0); // OAKNODE_TRACK_TYPE_VIDEO
pub static TRACK_LENGTH_NUM: AtomicI32 = AtomicI32::new(0);
pub static TRACK_LENGTH_DEN: AtomicI32 = AtomicI32::new(1);
pub static TRACK_LIST_TRACK_COUNT: AtomicI32 = AtomicI32::new(1);
pub static VIDEO_FRAME_RATE_NUM: AtomicI32 = AtomicI32::new(0);
pub static VIDEO_FRAME_RATE_DEN: AtomicI32 = AtomicI32::new(1);
pub static VIDEO_DURATION: AtomicI64 = AtomicI64::new(0);
pub static APPENDED_BLOCKS: AtomicI32 = AtomicI32::new(0);
pub static FOOTAGE_CREATE_COUNT: AtomicI32 = AtomicI32::new(0);
pub static MEDIA_IN_NUM: AtomicI32 = AtomicI32::new(0);
pub static MEDIA_IN_DEN: AtomicI32 = AtomicI32::new(1);
pub static SET_LENGTH_NUM: AtomicI32 = AtomicI32::new(0);
pub static SET_LENGTH_DEN: AtomicI32 = AtomicI32::new(1);
pub static TRANSITION_IN_NUM: AtomicI32 = AtomicI32::new(0);
pub static TRANSITION_IN_DEN: AtomicI32 = AtomicI32::new(1);
pub static TRANSITION_OUT_NUM: AtomicI32 = AtomicI32::new(0);
pub static TRANSITION_OUT_DEN: AtomicI32 = AtomicI32::new(1);
pub static CONNECT_INPUT_IDS: Mutex<Vec<String>> = Mutex::new(Vec::new());

/// Serialize manager-dependent tests in one binary (the singleton is
/// process-wide).
pub static MANAGER_LOCK: Mutex<()> = Mutex::new(());

pub fn reset_stubs() {
	SERIALIZER_LOAD_CODE.store(0, Ordering::SeqCst);
	SERIALIZER_LOAD_RET.store(0, Ordering::SeqCst);
	SERIALIZER_SAVE_CODE.store(0, Ordering::SeqCst);
	*SERIALIZER_DETAILS.lock().unwrap() = "";
	PROJECT_FILENAME.lock().unwrap().clear();
	NODE_LABEL.lock().unwrap().clear();
	FOOTAGE_FILENAME.lock().unwrap().clear();
	FOOTAGE_VALID.store(1, Ordering::SeqCst);
	FOOTAGE_STREAM_COUNT.store(1, Ordering::SeqCst);
	VIDEO_TYPE.store(1, Ordering::SeqCst);
	VIDEO_IS_VALID.store(1, Ordering::SeqCst);
	VIDEO_WIDTH.store(640, Ordering::SeqCst);
	VIDEO_HEIGHT.store(360, Ordering::SeqCst);
	VIDEO_TIME_BASE_NUM.store(0, Ordering::SeqCst);
	VIDEO_TIME_BASE_DEN.store(1, Ordering::SeqCst);
	VIDEO_LENGTH_NUM.store(0, Ordering::SeqCst);
	VIDEO_LENGTH_DEN.store(1, Ordering::SeqCst);
	SEQUENCE_LENGTH_NUM.store(0, Ordering::SeqCst);
	SEQUENCE_LENGTH_DEN.store(1, Ordering::SeqCst);
	CONNECTED_NODE.store(1, Ordering::SeqCst);
	FOLDER_CHILD_COUNT.store(0, Ordering::SeqCst);
	REDO_DELTA.store(0, Ordering::SeqCst);
	MULTI_CHILDREN.store(0, Ordering::SeqCst);
	DECODER_OPEN_RESULT.store(0, Ordering::SeqCst);
	DECODER_CONFORM_RESULT.store(0, Ordering::SeqCst);
	*DECODER_LAST_ERROR.lock().unwrap() = String::from("stub decoder error");
	*ENCODER_LAST_ERROR.lock().unwrap() = String::from("stub encoder error");
	ENCODER_INIT_NULL.store(0, Ordering::SeqCst);
	ENCODER_OPEN_RESULT.store(0, Ordering::SeqCst);
	ENCODER_WRITE_VIDEO_RESULT.store(0, Ordering::SeqCst);
	ENCODER_WRITE_SUBTITLE_RESULT.store(0, Ordering::SeqCst);
	ENCODER_FLUSH_ERROR.store(0, Ordering::SeqCst);
	DESIRED_PIXEL_FORMAT.store(0, Ordering::SeqCst);
	IMAGE_SEQUENCE_DIGIT_COUNT.store(0, Ordering::SeqCst);
	CONFIG_DEFAULT_SEQ_FRAME_RATE.store(0, Ordering::SeqCst);
	PROXY_FFMPEG_PATH.lock().unwrap().clear();
	*SUBMIT_CB.lock().unwrap() = None;
	#[cfg(not(feature = "real-oakrender"))]
	ATOM_CANCELLED.store(0, Ordering::SeqCst);
	#[cfg(not(feature = "real-oakrender"))]
	TICKET_FRAME_VALID.store(1, Ordering::SeqCst);
	#[cfg(not(feature = "real-oakrender"))]
	TICKET_GET_FRAME_RESULT.store(0, Ordering::SeqCst);
	// Ticket arena state: back to immediate-completion mode, no tickets.
	#[cfg(not(feature = "real-oakrender"))]
	TICKET_DEFER.store(0, Ordering::SeqCst);
	#[cfg(not(feature = "real-oakrender"))]
	TICKET_NEXT_ID.store(0, Ordering::SeqCst);
	#[cfg(not(feature = "real-oakrender"))]
	TICKET_SUBMITTED.store(0, Ordering::SeqCst);
	#[cfg(not(feature = "real-oakrender"))]
	TICKET_COMPLETED.store(0, Ordering::SeqCst);
	#[cfg(not(feature = "real-oakrender"))]
	TICKETS.lock().unwrap_or_else(|e| e.into_inner()).clear();
	NODE_ID.lock().unwrap().clear();
	AUDIO_TRACK_LIST_NULL.store(1, Ordering::SeqCst);
	FOOTAGE_FOUND.store(1, Ordering::SeqCst);
	BLOCK_KIND.store(1, Ordering::SeqCst);
	BLOCK_IN_NUM.store(0, Ordering::SeqCst);
	BLOCK_IN_DEN.store(1, Ordering::SeqCst);
	BLOCK_LENGTH_NUM.store(0, Ordering::SeqCst);
	BLOCK_LENGTH_DEN.store(1, Ordering::SeqCst);
	TRACK_BLOCK_COUNT.store(0, Ordering::SeqCst);
	TRACK_TYPE.store(0, Ordering::SeqCst);
	TRACK_LENGTH_NUM.store(0, Ordering::SeqCst);
	TRACK_LENGTH_DEN.store(1, Ordering::SeqCst);
	TRACK_LIST_TRACK_COUNT.store(1, Ordering::SeqCst);
	VIDEO_FRAME_RATE_NUM.store(0, Ordering::SeqCst);
	VIDEO_FRAME_RATE_DEN.store(1, Ordering::SeqCst);
	VIDEO_DURATION.store(0, Ordering::SeqCst);
	APPENDED_BLOCKS.store(0, Ordering::SeqCst);
	FOOTAGE_CREATE_COUNT.store(0, Ordering::SeqCst);
	MEDIA_IN_NUM.store(0, Ordering::SeqCst);
	MEDIA_IN_DEN.store(1, Ordering::SeqCst);
	SET_LENGTH_NUM.store(0, Ordering::SeqCst);
	SET_LENGTH_DEN.store(1, Ordering::SeqCst);
	TRANSITION_IN_NUM.store(0, Ordering::SeqCst);
	TRANSITION_IN_DEN.store(1, Ordering::SeqCst);
	TRANSITION_OUT_NUM.store(0, Ordering::SeqCst);
	TRANSITION_OUT_DEN.store(1, Ordering::SeqCst);
	CONNECT_INPUT_IDS.lock().unwrap().clear();
}

// ---------------------------------------------------------------------------
// bridge::codec stubs
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn oakcodec_set_task_submit_cb(
	cb: Option<unsafe extern "C" fn(*const oaktask::bridge::codec::OakCodecTaskRequest, *mut c_void) -> c_int>,
	_userdata: *mut c_void,
) {
	*SUBMIT_CB.lock().unwrap() = cb;
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_task_submit_is_registered() -> c_int {
	if SUBMIT_CB.lock().unwrap().is_some() {
		1
	} else {
		0
	}
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_init() -> oaktask::bridge::codec::OakDecoder {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_free(_decoder: *mut oaktask::bridge::codec::OakDecoder) {}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_open(
	_decoder: oaktask::bridge::codec::OakDecoder,
	_filename: *const c_char,
	_stream_index: c_int,
) -> c_int {
	DECODER_OPEN_RESULT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_close(_decoder: oaktask::bridge::codec::OakDecoder) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_is_open(_decoder: oaktask::bridge::codec::OakDecoder) -> c_int {
	1
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_decode_video(
	_decoder: oaktask::bridge::codec::OakDecoder,
	_numerator: c_int,
	_denominator: c_int,
) -> oaktask::bridge::codec::OakDecoder {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_decode_audio(
	_decoder: oaktask::bridge::codec::OakDecoder,
	_in_num: c_int,
	_in_den: c_int,
	_out_num: c_int,
	_out_den: c_int,
	_sample_rate: c_int,
	_channel_layout: u64,
	_buf: *mut f32,
	_buf_frames: c_int,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_conform_audio(
	_decoder: oaktask::bridge::codec::OakDecoder,
	_output_filenames: *const *const c_char,
	_filename_count: c_int,
	_sample_rate: c_int,
	_channel_layout: u64,
	_sample_format: c_int,
	_cancelled: oaktask::bridge::render::OakCancelAtom,
) -> c_int {
	DECODER_CONFORM_RESULT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_last_error(
	_decoder: oaktask::bridge::codec::OakDecoder,
	buf: *mut c_char,
	size: c_int,
) -> c_int {
	write_cstr(&DECODER_LAST_ERROR.lock().unwrap().clone(), buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_get_image_sequence_digit_count(_filename: *const c_char) -> c_int {
	IMAGE_SEQUENCE_DIGIT_COUNT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_get_image_sequence_index(filename: *const c_char) -> i64 {
	// Trailing digits of the stem (before the extension).
	let s = if filename.is_null() {
		String::new()
	} else {
		std::ffi::CStr::from_ptr(filename).to_string_lossy().into_owned()
	};
	let stem = match s.rfind('.') {
		Some(dot) => &s[..dot],
		None => s.as_str(),
	};
	let digits: String = stem.chars().rev().take_while(|c| c.is_ascii_digit()).collect();
	digits.chars().rev().collect::<String>().parse().unwrap_or(-1)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_decoder_transform_image_sequence_file_name(
	filename: *const c_char,
	number: i64,
	buf: *mut c_char,
	size: c_int,
) -> c_int {
	let s = if filename.is_null() {
		String::new()
	} else {
		std::ffi::CStr::from_ptr(filename).to_string_lossy().into_owned()
	};
	let (stem, ext) = match s.rfind('.') {
		Some(dot) => (&s[..dot], &s[dot..]),
		None => (s.as_str(), ""),
	};
	let digits: String = stem.chars().rev().take_while(|c| c.is_ascii_digit()).collect();
	let count = digits.len().max(1);
	let prefix = &stem[..stem.len() - digits.len()];
	let out = format!("{prefix}{:0count$}{ext}", number);
	write_cstr(&out, buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_init(
	_params: *const oaktask::bridge::codec::OakCodecEncodingParams,
) -> oaktask::bridge::codec::OakEncoder {
	if ENCODER_INIT_NULL.load(Ordering::SeqCst) != 0 {
		empty_handle()
	} else {
		fake_handle()
	}
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_free(_encoder: *mut oaktask::bridge::codec::OakEncoder) {}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_set_video_option(
	_encoder: oaktask::bridge::codec::OakEncoder,
	_key: *const c_char,
	_value: *const c_char,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_open(_encoder: oaktask::bridge::codec::OakEncoder) -> c_int {
	ENCODER_OPEN_RESULT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_write_video(
	_encoder: oaktask::bridge::codec::OakEncoder,
	_frame: oaktask::bridge::codec::OakFrame,
) -> c_int {
	ENCODER_WRITE_VIDEO_RESULT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_write_audio(
	_encoder: oaktask::bridge::codec::OakEncoder,
	_samples: *const f32,
	_frame_count: c_int,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_write_subtitle(
	_encoder: oaktask::bridge::codec::OakEncoder,
	_text: *const c_char,
	_in_seconds: f64,
	_out_seconds: f64,
) -> c_int {
	ENCODER_WRITE_SUBTITLE_RESULT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_flush(_encoder: oaktask::bridge::codec::OakEncoder) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_last_error(
	_encoder: oaktask::bridge::codec::OakEncoder,
	buf: *mut c_char,
	size: c_int,
) -> c_int {
	if ENCODER_FLUSH_ERROR.load(Ordering::SeqCst) != 0 {
		write_cstr(&ENCODER_LAST_ERROR.lock().unwrap().clone(), buf, size)
	} else {
		0
	}
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_get_desired_pixel_format(_encoder: oaktask::bridge::codec::OakEncoder) -> c_int {
	DESIRED_PIXEL_FORMAT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_generate_matrix(
	_method: c_int,
	_src_width: c_int,
	_src_height: c_int,
	_dst_width: c_int,
	_dst_height: c_int,
	_out_matrix: *mut f64,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_export_format_get_extension(_format: c_int, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("srt", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_find_ffmpeg(
	_configured_path: *const c_char,
	buf: *mut c_char,
	size: c_int,
) -> c_int {
	let path = PROXY_FFMPEG_PATH.lock().unwrap().clone();
	if path.is_empty() {
		return 0;
	}
	write_cstr(&path, buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_params_default(
	out: *mut oaktask::bridge::codec::OakCodecProxyParams,
) -> c_int {
	if out.is_null() {
		return -1;
	}
	(*out).width = 1280;
	(*out).height = 720;
	(*out).divider = 1;
	(*out).version = 0;
	(*out).crf = 23;
	(*out).include_audio = 1;
	write_cstr("mp4", (*out).extension.as_mut_ptr(), 32);
	write_cstr("veryfast", (*out).preset.as_mut_ptr(), 32);
	0
}

// ---------------------------------------------------------------------------
// bridge::common stubs
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_init() -> oaktask::bridge::common::OakVideoParams {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_init_basic(
	_width: c_int,
	_height: c_int,
	_pixel_format: c_int,
	_nb_channels: c_int,
	_pixel_aspect_num: c_int,
	_pixel_aspect_den: c_int,
	_interlacing: c_int,
	_divider: c_int,
) -> oaktask::bridge::common::OakVideoParams {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_free(_params: *mut oaktask::bridge::common::OakVideoParams) {}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_get_width(_params: oaktask::bridge::common::OakVideoParams, width: *mut c_int) -> c_int {
	if !width.is_null() {
		*width = VIDEO_WIDTH.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_get_height(_params: oaktask::bridge::common::OakVideoParams, height: *mut c_int) -> c_int {
	if !height.is_null() {
		*height = VIDEO_HEIGHT.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_get_format(_params: oaktask::bridge::common::OakVideoParams, format: *mut c_int) -> c_int {
	if !format.is_null() {
		*format = 0;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_set_format(_params: oaktask::bridge::common::OakVideoParams, _format: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_get_time_base(
	_params: oaktask::bridge::common::OakVideoParams,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	if !numerator.is_null() {
		*numerator = VIDEO_TIME_BASE_NUM.load(Ordering::SeqCst);
	}
	if !denominator.is_null() {
		*denominator = VIDEO_TIME_BASE_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_set_time_base(
	_params: oaktask::bridge::common::OakVideoParams,
	_numerator: c_int,
	_denominator: c_int,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_frame_rate_as_time_base(
	_params: oaktask::bridge::common::OakVideoParams,
	out_num: *mut c_int,
	out_den: *mut c_int,
) -> c_int {
	if !out_num.is_null() {
		*out_num = VIDEO_TIME_BASE_NUM.load(Ordering::SeqCst);
	}
	if !out_den.is_null() {
		*out_den = VIDEO_TIME_BASE_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_set_frame_rate(
	_params: oaktask::bridge::common::OakVideoParams,
	_numerator: c_int,
	_denominator: c_int,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_get_video_type(
	_params: oaktask::bridge::common::OakVideoParams,
	out_type: *mut c_int,
) -> c_int {
	if !out_type.is_null() {
		*out_type = VIDEO_TYPE.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_set_video_type(_params: oaktask::bridge::common::OakVideoParams, _video_type: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_set_start_time(_params: oaktask::bridge::common::OakVideoParams, _start: i64) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_set_duration(_params: oaktask::bridge::common::OakVideoParams, _duration: i64) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_get_is_valid(
	_params: oaktask::bridge::common::OakVideoParams,
	out_valid: *mut c_int,
) -> c_int {
	if !out_valid.is_null() {
		*out_valid = VIDEO_IS_VALID.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_get_frame_rate(
	_params: oaktask::bridge::common::OakVideoParams,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	if !numerator.is_null() {
		*numerator = VIDEO_FRAME_RATE_NUM.load(Ordering::SeqCst);
	}
	if !denominator.is_null() {
		*denominator = VIDEO_FRAME_RATE_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_videoparams_get_duration(
	_params: oaktask::bridge::common::OakVideoParams,
	duration: *mut i64,
) -> c_int {
	if !duration.is_null() {
		*duration = VIDEO_DURATION.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_colortransform_init_display(
	_display: *const c_char,
	_view: *const c_char,
	_look: *const c_char,
) -> oaktask::bridge::common::OakColorTransform {
	oaktask::bridge::common::OakColorTransform {
		ctx: 3usize as *mut c_void,
		addref: None,
		release: None,
		abi_version: 1,
	}
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_colortransform_init_output(
	_output: *const c_char,
	_display: *const c_char,
	_view: *const c_char,
	_look: *const c_char,
) -> oaktask::bridge::common::OakColorTransform {
	oaktask::bridge::common::OakColorTransform {
		ctx: 3usize as *mut c_void,
		addref: None,
		release: None,
		abi_version: 1,
	}
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_colortransform_free(_transform: *mut oaktask::bridge::common::OakColorTransform) {}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_config_get(
	_group: *const c_char,
	key: *const c_char,
	buf: *mut c_char,
	size: c_int,
) -> c_int {
	if key.is_null() {
		return 0;
	}
	let key = std::ffi::CStr::from_ptr(key).to_string_lossy().into_owned();
	if key == "DefaultSequenceFrameRate" && CONFIG_DEFAULT_SEQ_FRAME_RATE.load(Ordering::SeqCst) != 0 {
		write_cstr("25/1", buf, size)
	} else {
		0
	}
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_config_get_int(_group: *const c_char, _key: *const c_char, default: c_int) -> c_int {
	default
}

#[no_mangle]
pub unsafe extern "C" fn oakcommon_config_get_bool(_group: *const c_char, _key: *const c_char, default: c_int) -> c_int {
	default
}

// ---------------------------------------------------------------------------
// bridge::node stubs
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_init() -> oaktask::bridge::node::OakNodeProject {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_free(_project: *mut oaktask::bridge::node::OakNodeProject) {}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_initialize(_project: oaktask::bridge::node::OakNodeProject) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_clear(_project: oaktask::bridge::node::OakNodeProject) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_root(_project: oaktask::bridge::node::OakNodeProject) -> oaktask::bridge::node::OakNodeFolder {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_name(_project: oaktask::bridge::node::OakNodeProject, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("proj", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_filename(_project: oaktask::bridge::node::OakNodeProject, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr(&PROJECT_FILENAME.lock().unwrap().clone(), buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_pretty_filename(_project: oaktask::bridge::node::OakNodeProject, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("pretty", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_set_filename(_project: oaktask::bridge::node::OakNodeProject, _filename: *const c_char) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_is_modified(_project: oaktask::bridge::node::OakNodeProject) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_set_modified(_project: oaktask::bridge::node::OakNodeProject, _modified: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_is_new(_project: oaktask::bridge::node::OakNodeProject) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_cache_path(_project: oaktask::bridge::node::OakNodeProject, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("/tmp/oak-cache", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_copy_settings(_dst: oaktask::bridge::node::OakNodeProject, _src: oaktask::bridge::node::OakNodeProject) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_get_cache_location_setting(_project: oaktask::bridge::node::OakNodeProject) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_set_cache_location_setting(_project: oaktask::bridge::node::OakNodeProject, _setting: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_get_custom_cache_path(_project: oaktask::bridge::node::OakNodeProject, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_set_custom_cache_path(_project: oaktask::bridge::node::OakNodeProject, _path: *const c_char) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_get_uuid(_project: oaktask::bridge::node::OakNodeProject, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("uuid", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_add_node(_project: oaktask::bridge::node::OakNodeProject, _node: oaktask::bridge::node::OakNodeNode) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_remove_node(_project: oaktask::bridge::node::OakNodeProject, _node: oaktask::bridge::node::OakNodeNode) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_node_count(_project: oaktask::bridge::node::OakNodeProject) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_project_node_at(_project: oaktask::bridge::node::OakNodeProject, _index: c_int) -> oaktask::bridge::node::OakNodeNode {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_create(_project: oaktask::bridge::node::OakNodeProject) -> oaktask::bridge::node::OakNodeFolder {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_child_count(_folder: oaktask::bridge::node::OakNodeFolder) -> c_int {
	FOLDER_CHILD_COUNT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_child_at(_folder: oaktask::bridge::node::OakNodeFolder, _index: c_int) -> oaktask::bridge::node::OakNodeNode {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_add_child(_folder: oaktask::bridge::node::OakNodeFolder, _child: oaktask::bridge::node::OakNodeNode) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_as_node(folder: oaktask::bridge::node::OakNodeFolder) -> oaktask::bridge::node::OakNodeNode {
	folder
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_command_create_folder_add_child(
	_folder: oaktask::bridge::node::OakNodeFolder,
	_child: oaktask::bridge::node::OakNodeNode,
) -> oaktask::bridge::undo::OakUndoCommand {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_command_create_remove_node(_node: oaktask::bridge::node::OakNodeNode) -> oaktask::bridge::undo::OakUndoCommand {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_remove_child(_folder: oaktask::bridge::node::OakNodeFolder, _child: oaktask::bridge::node::OakNodeNode) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_move_children(_nodes: *const oaktask::bridge::node::OakNodeNode, _count: c_int, _dest: oaktask::bridge::node::OakNodeFolder) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_has_child_recursive(_folder: oaktask::bridge::node::OakNodeFolder, _child: oaktask::bridge::node::OakNodeNode) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_index_of_child(_folder: oaktask::bridge::node::OakNodeFolder, _child: oaktask::bridge::node::OakNodeNode) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_folder_parent_of(_node: oaktask::bridge::node::OakNodeNode) -> oaktask::bridge::node::OakNodeFolder {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_create(
	_project: oaktask::bridge::node::OakNodeProject,
	_filename: *const c_char,
) -> oaktask::bridge::node::OakNodeFootage {
	FOOTAGE_CREATE_COUNT.fetch_add(1, Ordering::SeqCst);
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_as_node(footage: oaktask::bridge::node::OakNodeFootage) -> oaktask::bridge::node::OakNodeNode {
	footage
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_filename(_footage: oaktask::bridge::node::OakNodeFootage, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr(&FOOTAGE_FILENAME.lock().unwrap().clone(), buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_set_filename(
	_footage: oaktask::bridge::node::OakNodeFootage,
	filename: *const c_char,
) -> c_int {
	if !filename.is_null() {
		*FOOTAGE_FILENAME.lock().unwrap() =
			std::ffi::CStr::from_ptr(filename).to_string_lossy().into_owned();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_is_valid(_footage: oaktask::bridge::node::OakNodeFootage) -> c_int {
	FOOTAGE_VALID.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_timestamp(_footage: oaktask::bridge::node::OakNodeFootage, _out_timestamp: *mut i64) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_set_timestamp(_footage: oaktask::bridge::node::OakNodeFootage, _timestamp: i64) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_decoder(_footage: oaktask::bridge::node::OakNodeFootage, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("ffmpeg", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_total_stream_count(_footage: oaktask::bridge::node::OakNodeFootage) -> c_int {
	FOOTAGE_STREAM_COUNT.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_video_stream_count(_footage: oaktask::bridge::node::OakNodeFootage) -> c_int {
	1
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_audio_stream_count(_footage: oaktask::bridge::node::OakNodeFootage) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_subtitle_stream_count(_footage: oaktask::bridge::node::OakNodeFootage) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_duration(
	_footage: oaktask::bridge::node::OakNodeFootage,
	out_numerator: *mut c_int,
	out_denominator: *mut c_int,
) -> c_int {
	if !out_numerator.is_null() {
		*out_numerator = 0;
	}
	if !out_denominator.is_null() {
		*out_denominator = 1;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_proxy_enabled(_footage: oaktask::bridge::node::OakNodeFootage) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_set_proxy_enabled(_footage: oaktask::bridge::node::OakNodeFootage, _enabled: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_proxy_path(_footage: oaktask::bridge::node::OakNodeFootage, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_proxy_state(_footage: oaktask::bridge::node::OakNodeFootage) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_set_proxy(
	_footage: oaktask::bridge::node::OakNodeFootage,
	_path: *const c_char,
	_state: c_int,
	_video_stream_index: c_int,
	_preset_version: c_int,
	_enabled: c_int,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_clear_proxy(_footage: oaktask::bridge::node::OakNodeFootage) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_get_video_params(
	_footage: oaktask::bridge::node::OakNodeFootage,
	_index: c_int,
	out: *mut oaktask::bridge::common::OakVideoParams,
) -> c_int {
	if !out.is_null() {
		*out = oakcommon_videoparams_init();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_set_video_params(
	_footage: oaktask::bridge::node::OakNodeFootage,
	_index: c_int,
	_params: *const oaktask::bridge::common::OakVideoParams,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_get_video_length(
	_footage: oaktask::bridge::node::OakNodeFootage,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	if !out_num.is_null() {
		*out_num = VIDEO_LENGTH_NUM.load(Ordering::SeqCst);
	}
	if !out_den.is_null() {
		*out_den = VIDEO_LENGTH_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_footage_set_cancel_atom(
	_footage: oaktask::bridge::node::OakNodeFootage,
	_atom: oaktask::bridge::render::OakCancelAtom,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_create() -> oaktask::bridge::node::OakNodeSequence {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_free(_sequence: *mut oaktask::bridge::node::OakNodeSequence) {}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_set_default_parameters(_sequence: oaktask::bridge::node::OakNodeSequence) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_as_node(sequence: oaktask::bridge::node::OakNodeSequence) -> oaktask::bridge::node::OakNodeNode {
	sequence
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_from_node(node: oaktask::bridge::node::OakNodeNode) -> oaktask::bridge::node::OakNodeSequence {
	node
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_track_list(
	_sequence: oaktask::bridge::node::OakNodeSequence,
	r#type: c_int,
	out: *mut oaktask::bridge::node::OakNodeTrackList,
) -> c_int {
	if !out.is_null() {
		if r#type == 1 && AUDIO_TRACK_LIST_NULL.load(Ordering::SeqCst) != 0 {
			// Audio track list requested but the test marks it empty.
			*out = empty_handle();
		} else {
			*out = fake_handle();
		}
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_track_count(_sequence: oaktask::bridge::node::OakNodeSequence, _type: c_int, count: *mut c_int) -> c_int {
	if !count.is_null() {
		*count = 0;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_track_at(
	_sequence: oaktask::bridge::node::OakNodeSequence,
	_type: c_int,
	_index: c_int,
	out: *mut oaktask::bridge::node::OakNodeTrack,
) -> c_int {
	if !out.is_null() {
		*out = fake_handle();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_all_track_count(_sequence: oaktask::bridge::node::OakNodeSequence, count: *mut c_int) -> c_int {
	if !count.is_null() {
		*count = 0;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_all_track_at(
	_sequence: oaktask::bridge::node::OakNodeSequence,
	_index: c_int,
	out: *mut oaktask::bridge::node::OakNodeTrack,
) -> c_int {
	if !out.is_null() {
		*out = fake_handle();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_playhead(_sequence: oaktask::bridge::node::OakNodeSequence, n: *mut c_int, d: *mut c_int) -> c_int {
	if !n.is_null() {
		*n = 0;
	}
	if !d.is_null() {
		*d = 1;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_set_playhead(_sequence: oaktask::bridge::node::OakNodeSequence, _n: c_int, _d: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_length(_sequence: oaktask::bridge::node::OakNodeSequence, n: *mut c_int, d: *mut c_int) -> c_int {
	if !n.is_null() {
		*n = SEQUENCE_LENGTH_NUM.load(Ordering::SeqCst);
	}
	if !d.is_null() {
		*d = SEQUENCE_LENGTH_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_video_length(_sequence: oaktask::bridge::node::OakNodeSequence, n: *mut c_int, d: *mut c_int) -> c_int {
	if !n.is_null() {
		*n = 0;
	}
	if !d.is_null() {
		*d = 1;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_audio_length(_sequence: oaktask::bridge::node::OakNodeSequence, n: *mut c_int, d: *mut c_int) -> c_int {
	if !n.is_null() {
		*n = 0;
	}
	if !d.is_null() {
		*d = 1;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_verify_length(_sequence: oaktask::bridge::node::OakNodeSequence) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_video_stream_count(_sequence: oaktask::bridge::node::OakNodeSequence, count: *mut c_int) -> c_int {
	if !count.is_null() {
		*count = 1;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_audio_stream_count(_sequence: oaktask::bridge::node::OakNodeSequence, count: *mut c_int) -> c_int {
	if !count.is_null() {
		*count = 0;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_video_params(
	_sequence: oaktask::bridge::node::OakNodeSequence,
	_index: c_int,
	out: *mut oaktask::bridge::common::OakVideoParams,
) -> c_int {
	if !out.is_null() {
		*out = oakcommon_videoparams_init();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_set_video_params(
	_sequence: oaktask::bridge::node::OakNodeSequence,
	_index: c_int,
	_params: oaktask::bridge::common::OakVideoParams,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_get_audio_params(
	_sequence: oaktask::bridge::node::OakNodeSequence,
	_index: c_int,
	out: *mut *const c_void,
) -> c_int {
	if !out.is_null() {
		*out = std::ptr::null();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_sequence_set_audio_params(_sequence: oaktask::bridge::node::OakNodeSequence, _index: c_int, _params: *const c_void) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_as_node(track: oaktask::bridge::node::OakNodeTrack) -> oaktask::bridge::node::OakNodeNode {
	track
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_create(_type: c_int) -> oaktask::bridge::node::OakNodeTrack {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_free(_track: *mut oaktask::bridge::node::OakNodeTrack) {}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_get_type(_track: oaktask::bridge::node::OakNodeTrack, r#type: *mut c_int) -> c_int {
	if !r#type.is_null() {
		*r#type = TRACK_TYPE.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_set_type(_track: oaktask::bridge::node::OakNodeTrack, _type: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_get_index(_track: oaktask::bridge::node::OakNodeTrack, _index: *mut c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_set_index(_track: oaktask::bridge::node::OakNodeTrack, _index: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_get_sequence(_track: oaktask::bridge::node::OakNodeTrack, out: *mut oaktask::bridge::node::OakNodeSequence) -> c_int {
	if !out.is_null() {
		*out = fake_handle();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_get_sequence(_list: oaktask::bridge::node::OakNodeTrackList, out: *mut oaktask::bridge::node::OakNodeSequence) -> c_int {
	if !out.is_null() {
		*out = fake_handle();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_get_track_input_id(_list: oaktask::bridge::node::OakNodeTrackList, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("sub_in", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_array_append(_list: oaktask::bridge::node::OakNodeTrackList) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_array_remove_last(_list: oaktask::bridge::node::OakNodeTrackList) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_get_array_index_from_cache_index(_list: oaktask::bridge::node::OakNodeTrackList, _ci: c_int, out: *mut c_int) -> c_int {
	if !out.is_null() {
		*out = _ci;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_get_type(_list: oaktask::bridge::node::OakNodeTrackList, _type: *mut c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_get_track_count(_list: oaktask::bridge::node::OakNodeTrackList, count: *mut c_int) -> c_int {
	if !count.is_null() {
		if _list.ctx.is_null() {
			*count = 0;
		} else {
			*count = TRACK_LIST_TRACK_COUNT.load(Ordering::SeqCst);
		}
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_get_track_at(_list: oaktask::bridge::node::OakNodeTrackList, _index: c_int, out: *mut oaktask::bridge::node::OakNodeTrack) -> c_int {
	if !out.is_null() {
		*out = fake_handle();
	}
	0
}

// ---------------------------------------------------------------------------
// bridge::node block stubs (node/block.h)
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn oaknode_block_clip_create() -> oaktask::bridge::node::OakNodeBlock {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_block_gap_create() -> oaktask::bridge::node::OakNodeBlock {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_block_transition_create(_kind: c_int) -> oaktask::bridge::node::OakNodeBlock {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_block_as_node(block: oaktask::bridge::node::OakNodeBlock) -> oaktask::bridge::node::OakNodeNode {
	block
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_block_get_kind(_block: oaktask::bridge::node::OakNodeBlock, out_kind: *mut c_int) -> c_int {
	if !out_kind.is_null() {
		*out_kind = BLOCK_KIND.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_block_get_in(
	_block: oaktask::bridge::node::OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	if !numerator.is_null() {
		*numerator = BLOCK_IN_NUM.load(Ordering::SeqCst);
	}
	if !denominator.is_null() {
		*denominator = BLOCK_IN_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_block_get_length(
	_block: oaktask::bridge::node::OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	if !numerator.is_null() {
		*numerator = BLOCK_LENGTH_NUM.load(Ordering::SeqCst);
	}
	if !denominator.is_null() {
		*denominator = BLOCK_LENGTH_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_block_set_length_and_media_out(
	_block: oaktask::bridge::node::OakNodeBlock,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	SET_LENGTH_NUM.store(numerator, Ordering::SeqCst);
	SET_LENGTH_DEN.store(denominator, Ordering::SeqCst);
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_clip_set_media_in(
	_clip: oaktask::bridge::node::OakNodeBlock,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	MEDIA_IN_NUM.store(numerator, Ordering::SeqCst);
	MEDIA_IN_DEN.store(denominator, Ordering::SeqCst);
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_transition_get_in_offset(
	_transition: oaktask::bridge::node::OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	if !numerator.is_null() {
		*numerator = TRANSITION_IN_NUM.load(Ordering::SeqCst);
	}
	if !denominator.is_null() {
		*denominator = TRANSITION_IN_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_transition_get_out_offset(
	_transition: oaktask::bridge::node::OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	if !numerator.is_null() {
		*numerator = TRANSITION_OUT_NUM.load(Ordering::SeqCst);
	}
	if !denominator.is_null() {
		*denominator = TRANSITION_OUT_DEN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_transition_set_offsets_and_length(
	_transition: oaktask::bridge::node::OakNodeBlock,
	in_num: c_int,
	in_den: c_int,
	out_num: c_int,
	out_den: c_int,
) -> c_int {
	TRANSITION_IN_NUM.store(in_num, Ordering::SeqCst);
	TRANSITION_IN_DEN.store(in_den, Ordering::SeqCst);
	TRANSITION_OUT_NUM.store(out_num, Ordering::SeqCst);
	TRANSITION_OUT_DEN.store(out_den, Ordering::SeqCst);
	0
}

// ---------------------------------------------------------------------------
// bridge::node track block stubs (node/track.h)
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_append_block(_track: oaktask::bridge::node::OakNodeTrack, _block: oaktask::bridge::node::OakNodeBlock) -> c_int {
	APPENDED_BLOCKS.fetch_add(1, Ordering::SeqCst);
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_get_block_count(_track: oaktask::bridge::node::OakNodeTrack, count: *mut c_int) -> c_int {
	if !count.is_null() {
		*count = TRACK_BLOCK_COUNT.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_get_block_at(_track: oaktask::bridge::node::OakNodeTrack, _index: c_int, out: *mut oaktask::bridge::node::OakNodeBlock) -> c_int {
	if !out.is_null() {
		*out = fake_handle();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_track_get_length(_track: oaktask::bridge::node::OakNodeTrack, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	if !numerator.is_null() {
		*numerator = TRACK_LENGTH_NUM.load(Ordering::SeqCst);
	}
	if !denominator.is_null() {
		*denominator = TRACK_LENGTH_DEN.load(Ordering::SeqCst);
	}
	0
}

// ---------------------------------------------------------------------------
// bridge::timeline stubs (timeline/edit.h)
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn oaktimeline_add_track_command(_list: oaktask::bridge::node::OakNodeTrackList) -> oaktask::bridge::undo::OakUndoCommand {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_get_total_length(_list: oaktask::bridge::node::OakNodeTrackList, n: *mut c_int, d: *mut c_int) -> c_int {
	if !n.is_null() {
		*n = 0;
	}
	if !d.is_null() {
		*d = 1;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_get_array_size(_list: oaktask::bridge::node::OakNodeTrackList, size: *mut c_int) -> c_int {
	if !size.is_null() {
		*size = 0;
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_add_track(_list: oaktask::bridge::node::OakNodeTrackList, _track: oaktask::bridge::node::OakNodeTrack) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_tracklist_remove_track(_list: oaktask::bridge::node::OakNodeTrackList, _track: oaktask::bridge::node::OakNodeTrack) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_colormanager_init(_project: oaktask::bridge::node::OakNodeProject) -> oaktask::bridge::node::OakNodeColorManager {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_colormanager_free(_manager: *mut oaktask::bridge::node::OakNodeColorManager) {}

#[no_mangle]
pub unsafe extern "C" fn oaknode_colormanager_wrap_borrowed(_native: *mut c_void) -> oaktask::bridge::node::OakNodeColorManager {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_colormanager_initialize(_manager: oaktask::bridge::node::OakNodeColorManager) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_colormanager_set_up_default_config() -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_initialize() -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_shutdown() {}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_savedata_create(
	_load_type: c_int,
	_project: oaktask::bridge::node::OakNodeProject,
) -> oaktask::bridge::node::OakNodeSerializerSaveData {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_savedata_free(_save_data: *mut oaktask::bridge::node::OakNodeSerializerSaveData) {}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_savedata_set_nodes(
	_save_data: oaktask::bridge::node::OakNodeSerializerSaveData,
	_nodes: *const oaktask::bridge::node::OakNodeNode,
	_count: c_int,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_savedata_set_property(
	_save_data: oaktask::bridge::node::OakNodeSerializerSaveData,
	_node: oaktask::bridge::node::OakNodeNode,
	_key: *const c_char,
	_value: *const c_char,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_save_to_xml(_save_data: oaktask::bridge::node::OakNodeSerializerSaveData, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr("<oakproj/>", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_load_from_xml(
	_project: oaktask::bridge::node::OakNodeProject,
	_xml: *const c_char,
	_load_type: c_int,
	out_result: *mut c_int,
	_out_load_data: *mut oaktask::bridge::node::OakNodeSerializerLoadData,
	_details: *mut c_char,
	_details_size: c_int,
) -> c_int {
	if !out_result.is_null() {
		*out_result = SERIALIZER_LOAD_CODE.load(Ordering::SeqCst);
	}
	if !_out_load_data.is_null() {
		*_out_load_data = fake_handle();
	}
	write_cstr(&SERIALIZER_DETAILS.lock().unwrap().clone(), _details, _details_size);
	SERIALIZER_LOAD_RET.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_loaddata_free(_load_data: *mut oaktask::bridge::node::OakNodeSerializerLoadData) {}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_loaddata_node_count(_load_data: oaktask::bridge::node::OakNodeSerializerLoadData) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_loaddata_node_at(_load_data: oaktask::bridge::node::OakNodeSerializerLoadData, _index: c_int) -> oaktask::bridge::node::OakNodeNode {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_loaddata_get_property(
	_load_data: oaktask::bridge::node::OakNodeSerializerLoadData,
	_node: oaktask::bridge::node::OakNodeNode,
	_key: *const c_char,
	buf: *mut c_char,
	size: c_int,
) -> c_int {
	write_cstr("", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_loaddata_connection_count(_load_data: oaktask::bridge::node::OakNodeSerializerLoadData) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_loaddata_connection_at(
	_load_data: oaktask::bridge::node::OakNodeSerializerLoadData,
	_index: c_int,
	_out_output: *mut oaktask::bridge::node::OakNodeNode,
	_out_input: *mut oaktask::bridge::node::OakNodeNode,
	_input_id: *mut c_char,
	_input_id_size: c_int,
	_out_element: *mut c_int,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_save_to_file(
	_project: oaktask::bridge::node::OakNodeProject,
	filename: *const c_char,
	_use_compression: c_int,
	out_code: *mut c_int,
	_details: *mut c_char,
	_details_size: c_int,
) -> c_int {
	let code = SERIALIZER_SAVE_CODE.load(Ordering::SeqCst);
	if !out_code.is_null() {
		*out_code = code;
	}
	write_cstr(&SERIALIZER_DETAILS.lock().unwrap().clone(), _details, _details_size);
	if code == 0 && !filename.is_null() {
		// Success path writes a real file so tests can assert existence.
		let name = std::ffi::CStr::from_ptr(filename).to_string_lossy().into_owned();
		let _ = std::fs::write(&name, b"oakproj-stub");
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_serializer_load_from_file(
	_project: oaktask::bridge::node::OakNodeProject,
	filename: *const c_char,
	out_code: *mut c_int,
	_details: *mut c_char,
	_details_size: c_int,
) -> c_int {
	// A missing file is a file error (mirrors the real serializer); the
	// code is controllable otherwise.
	let mut code = SERIALIZER_LOAD_CODE.load(Ordering::SeqCst);
	if !filename.is_null() {
		let name = std::ffi::CStr::from_ptr(filename).to_string_lossy().into_owned();
		if !std::path::Path::new(&name).exists() {
			code = 4; // OAKNODE_SERIALIZER_RESULT_FILE_ERROR
		}
	}
	if !out_code.is_null() {
		*out_code = code;
	}
	write_cstr(&SERIALIZER_DETAILS.lock().unwrap().clone(), _details, _details_size);
	SERIALIZER_LOAD_RET.load(Ordering::SeqCst)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_get_label(_node: oaktask::bridge::node::OakNodeNode, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr(&NODE_LABEL.lock().unwrap().clone(), buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_set_label(_node: oaktask::bridge::node::OakNodeNode, _label: *const c_char) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_get_id(_node: oaktask::bridge::node::OakNodeNode, buf: *mut c_char, size: c_int) -> c_int {
	write_cstr(&NODE_ID.lock().unwrap().clone(), buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_set_context_position(
	_node: oaktask::bridge::node::OakNodeNode,
	_context: oaktask::bridge::node::OakNodeNode,
	_x: f64,
	_y: f64,
	_expanded: c_int,
) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_find_input_footage(
	_node: oaktask::bridge::node::OakNodeNode,
	out: *mut oaktask::bridge::node::OakNodeFootage,
) -> c_int {
	if !out.is_null() {
		if FOOTAGE_FOUND.load(Ordering::SeqCst) != 0 {
			*out = fake_handle();
		} else {
			*out = empty_handle();
		}
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_get_project(_node: oaktask::bridge::node::OakNodeNode, out: *mut oaktask::bridge::node::OakNodeProject) -> c_int {
	if !out.is_null() {
		*out = fake_handle();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_input_get_connected_node(
	_node: oaktask::bridge::node::OakNodeNode,
	_input_id: *const c_char,
	out: *mut oaktask::bridge::node::OakNodeNode,
) -> c_int {
	if !out.is_null() {
		if CONNECTED_NODE.load(Ordering::SeqCst) != 0 {
			*out = fake_handle();
		} else {
			*out = empty_handle();
		}
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_get_input_string(
	_node: oaktask::bridge::node::OakNodeNode,
	_input_id: *const c_char,
	buf: *mut c_char,
	size: c_int,
) -> c_int {
	write_cstr("subtitle text", buf, size)
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_create_copy(_node: oaktask::bridge::node::OakNodeNode) -> oaktask::bridge::node::OakNodeNode {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_copy_inputs(_dst: oaktask::bridge::node::OakNodeNode, _src: oaktask::bridge::node::OakNodeNode, _include_connections: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_connect(
	_output: oaktask::bridge::node::OakNodeNode,
	_input: oaktask::bridge::node::OakNodeNode,
	input_id: *const c_char,
) -> c_int {
	if !input_id.is_null() {
		CONNECT_INPUT_IDS
			.lock()
			.unwrap()
			.push(std::ffi::CStr::from_ptr(input_id).to_string_lossy().into_owned());
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_set_value_hint_track(_node: oaktask::bridge::node::OakNodeNode, _input_id: *const c_char, _track_type: c_int, _track_index: c_int) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_get_video_frame_cache(
	_node: oaktask::bridge::node::OakNodeNode,
	out: *mut oaktask::bridge::render::OakRenderCache,
) -> c_int {
	if !out.is_null() {
		*out = oakrender_cache_from_fake();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_node_free(_node: *mut oaktask::bridge::node::OakNodeNode) {}

#[no_mangle]
pub unsafe extern "C" fn oaknode_factory_create_from_id(_type_id: *const c_char) -> oaktask::bridge::node::OakNodeNode {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_viewer_set_video_params(_viewer: oaktask::bridge::node::OakNodeNode, _params: *const oaktask::bridge::common::OakVideoParams) -> c_int {
	0
}

#[no_mangle]
pub unsafe extern "C" fn oaknode_viewer_set_audio_params(_viewer: oaktask::bridge::node::OakNodeNode, _params: *const c_void) -> c_int {
	0
}

// ---------------------------------------------------------------------------
// bridge::render stubs
// ---------------------------------------------------------------------------

fn oakrender_cache_from_fake() -> oaktask::bridge::render::OakRenderCache {
	unsafe extern "C" fn noop(_ctx: *mut c_void) {}
	oaktask::bridge::render::OakRenderCache {
		ctx: 5usize as *mut c_void,
		addref: Some(noop),
		release: Some(noop),
		abi_version: 1,
	}
}

// ---------------------------------------------------------------------------
// Simulated oakrender ticket arena (include/render/ticket.h contract)
//
// Every submitted ticket gets a unique id (assigned in submission order)
// and a distinct handle ctx. The finished callback (`cb`) is the ticket's
// return channel; by default it fires synchronously on submit (the
// deterministic single-thread path). With TICKET_DEFER = 1 the ticket
// stays in flight until the test fires it with stub_complete(), so tests
// can drive arbitrary (scrambled) completion orders. Cancelling a ticket —
// or the cancel atom — finishes it exactly like the real arena: the
// completion still fires exactly once.
// ---------------------------------------------------------------------------

/// OAKRENDER_TICKET_VIDEO (include/render/ticket.h).
#[cfg(not(feature = "real-oakrender"))]
const OAKRENDER_TICKET_VIDEO: c_int = 0;
/// OAKRENDER_TICKET_AUDIO (include/render/ticket.h).
#[cfg(not(feature = "real-oakrender"))]
const OAKRENDER_TICKET_AUDIO: c_int = 1;
/// Base of the ticket handle ctx (0x6000 + id).
#[cfg(not(feature = "real-oakrender"))]
const STUB_TICKET_CTX_BASE: usize = 0x6000;
/// Base of the frame handle ctx (0x10000 + ticket id), so tests can tell
/// which frame a delivered result came from.
#[cfg(not(feature = "real-oakrender"))]
const STUB_FRAME_CTX_BASE: usize = 0x10000;

/// Next ticket id (submission order).
#[cfg(not(feature = "real-oakrender"))]
static TICKET_NEXT_ID: AtomicUsize = AtomicUsize::new(0);
/// Tickets submitted so far (for [`stub_wait_submitted`]).
#[cfg(not(feature = "real-oakrender"))]
static TICKET_SUBMITTED: AtomicUsize = AtomicUsize::new(0);
/// Completions fired so far (callbacks invoked).
#[cfg(not(feature = "real-oakrender"))]
static TICKET_COMPLETED: AtomicUsize = AtomicUsize::new(0);
/// Submitted-count condvar (test sync point).
#[cfg(not(feature = "real-oakrender"))]
static TICKET_SUBMIT_CV: (Mutex<()>, Condvar) = (Mutex::new(()), Condvar::new());

/// A simulated in-flight ticket.
#[cfg(not(feature = "real-oakrender"))]
struct StubTicket {
	id: usize,
	kind: c_int,
	time_num: i64,
	time_den: i64,
	range: (i64, i64, i64, i64),
	cb: Option<oaktask::bridge::render::OakRenderTicketFinishedFn>,
	userdata: *mut c_void,
	finished: bool,
}

// The userdata is an opaque pointer handed through the ticket's finished
// callback (the same contract as the real C ABI, where tickets complete on
// worker threads); it is never dereferenced by the stub.
#[cfg(not(feature = "real-oakrender"))]
unsafe impl Send for StubTicket {}
#[cfg(not(feature = "real-oakrender"))]
unsafe impl Sync for StubTicket {}

/// The registry (guarded; ticket queries/cancel/wait race the render loop).
#[cfg(not(feature = "real-oakrender"))]
static TICKETS: Mutex<Vec<StubTicket>> = Mutex::new(Vec::new());
/// Wakes `oakrender_ticket_wait` callers when a ticket finishes.
#[cfg(not(feature = "real-oakrender"))]
static TICKET_CV: Condvar = Condvar::new();

#[cfg(not(feature = "real-oakrender"))]
fn stub_ticket_handle(id: usize) -> oaktask::bridge::render::OakRenderTicket {
	unsafe extern "C" fn noop(_ctx: *mut c_void) {}
	oaktask::bridge::render::OakRenderTicket {
		ctx: (STUB_TICKET_CTX_BASE + id) as *mut c_void,
		addref: Some(noop),
		release: Some(noop),
		abi_version: 1,
	}
}

#[cfg(not(feature = "real-oakrender"))]
fn stub_ticket_id(ticket: oaktask::bridge::render::OakRenderTicket) -> Option<usize> {
	let v = ticket.ctx as usize;
	if v >= STUB_TICKET_CTX_BASE && v < STUB_TICKET_CTX_BASE + 1_000_000 {
		Some(v - STUB_TICKET_CTX_BASE)
	} else {
		None
	}
}

#[cfg(not(feature = "real-oakrender"))]
fn stub_frame_handle(id: usize) -> oaktask::bridge::codec::OakFrame {
	unsafe extern "C" fn noop(_ctx: *mut c_void) {}
	oaktask::bridge::codec::OakFrame {
		ctx: (STUB_FRAME_CTX_BASE + id) as *mut c_void,
		addref: Some(noop),
		release: Some(noop),
		abi_version: 1,
	}
}

/// Register a submitted ticket; in immediate mode its completion fires
/// synchronously (before this returns).
#[cfg(not(feature = "real-oakrender"))]
fn stub_register_ticket(
	kind: c_int,
	time_num: i64,
	time_den: i64,
	range: (i64, i64, i64, i64),
	cb: Option<oaktask::bridge::render::OakRenderTicketFinishedFn>,
	userdata: *mut c_void,
) -> oaktask::bridge::render::OakRenderTicket {
	let id = TICKET_NEXT_ID.fetch_add(1, Ordering::SeqCst);
	{
		let mut tickets = TICKETS.lock().unwrap_or_else(|e| e.into_inner());
		tickets.push(StubTicket {
			id,
			kind,
			time_num,
			time_den,
			range,
			cb,
			userdata,
			finished: false,
		});
	}
	{
		let _g = TICKET_SUBMIT_CV.0.lock().unwrap_or_else(|e| e.into_inner());
		TICKET_SUBMITTED.fetch_add(1, Ordering::SeqCst);
		TICKET_SUBMIT_CV.1.notify_all();
	}
	let handle = stub_ticket_handle(id);
	if TICKET_DEFER.load(Ordering::SeqCst) == 0 {
		stub_complete(id);
	}
	handle
}

/// Fire the finished callback of ticket `id` (exactly once). Tests call
/// this to drive completion in any order.
#[cfg(not(feature = "real-oakrender"))]
pub fn stub_complete(id: usize) {
	let to_fire = {
		let mut tickets = TICKETS.lock().unwrap_or_else(|e| e.into_inner());
		match tickets.iter_mut().find(|t| t.id == id) {
			Some(t) if !t.finished => {
				t.finished = true;
				TICKET_COMPLETED.fetch_add(1, Ordering::SeqCst);
				TICKET_CV.notify_all();
				Some((t.cb, t.userdata))
			}
			_ => None,
		}
	};
	if let Some((cb, userdata)) = to_fire {
		if let Some(cb) = cb {
			unsafe { cb(stub_ticket_handle(id), userdata) };
		}
	}
}

/// Block until at least `count` tickets have been submitted.
#[cfg(not(feature = "real-oakrender"))]
pub fn stub_wait_submitted(count: usize) {
	let mut guard = TICKET_SUBMIT_CV.0.lock().unwrap_or_else(|e| e.into_inner());
	while TICKET_SUBMITTED.load(Ordering::SeqCst) < count {
		guard = TICKET_SUBMIT_CV.1.wait(guard).unwrap_or_else(|e| e.into_inner());
	}
}

/// Tickets submitted so far.
#[cfg(not(feature = "real-oakrender"))]
pub fn stub_submitted_count() -> usize {
	TICKET_SUBMITTED.load(Ordering::SeqCst)
}

/// Completions fired so far (callbacks invoked).
#[cfg(not(feature = "real-oakrender"))]
pub fn stub_completed_count() -> usize {
	TICKET_COMPLETED.load(Ordering::SeqCst)
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_cancelatom_init() -> oaktask::bridge::render::OakCancelAtom {
	fake_atom()
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_cancelatom_free(_atom: *mut oaktask::bridge::render::OakCancelAtom) {}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_cancelatom_cancel(_atom: oaktask::bridge::render::OakCancelAtom) -> c_int {
	ATOM_CANCELLED.store(1, Ordering::SeqCst);
	// Simulate cancellation propagating into in-flight renders: every
	// pending ticket finishes (its completion fires exactly once), waking
	// any render loop waiting on the completion channel. This is what the
	// real oakrender does when a cancel atom is shared into the render.
	let ids: Vec<usize> = {
		let tickets = TICKETS.lock().unwrap_or_else(|e| e.into_inner());
		tickets.iter().filter(|t| !t.finished).map(|t| t.id).collect()
	};
	for id in ids {
		stub_complete(id);
	}
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_cancelatom_is_cancelled(_atom: oaktask::bridge::render::OakCancelAtom, cancelled: *mut c_int) -> c_int {
	if !cancelled.is_null() {
		*cancelled = ATOM_CANCELLED.load(Ordering::SeqCst);
	}
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_cancelatom_heard_cancel(_atom: oaktask::bridge::render::OakCancelAtom, heard: *mut c_int) -> c_int {
	if !heard.is_null() {
		*heard = 0;
	}
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_render_frame(
	params: *const oaktask::bridge::render::OakRenderVideoTicketParams,
	cb: Option<oaktask::bridge::render::OakRenderTicketFinishedFn>,
	userdata: *mut c_void,
) -> oaktask::bridge::render::OakRenderTicket {
	let (time_num, time_den) = if params.is_null() {
		(0, 1)
	} else {
		((*params).time_num, (*params).time_den)
	};
	stub_register_ticket(OAKRENDER_TICKET_VIDEO, time_num, time_den, (0, 1, 0, 1), cb, userdata)
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_render_audio(
	_output_node: oaktask::bridge::node::OakNodeNode,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
	_params: *const oaktask::bridge::common::OakAudioParams,
	_mode: c_int,
	cb: Option<oaktask::bridge::render::OakRenderTicketFinishedFn>,
	userdata: *mut c_void,
) -> oaktask::bridge::render::OakRenderTicket {
	stub_register_ticket(
		OAKRENDER_TICKET_AUDIO,
		in_num,
		in_den,
		(in_num, in_den, out_num, out_den),
		cb,
		userdata,
	)
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_is_finished(ticket: oaktask::bridge::render::OakRenderTicket) -> c_int {
	match stub_ticket_id(ticket) {
		Some(id) => {
			let tickets = TICKETS.lock().unwrap_or_else(|e| e.into_inner());
			if tickets.iter().any(|t| t.id == id && t.finished) {
				1
			} else {
				0
			}
		}
		None => 0,
	}
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_wait(ticket: oaktask::bridge::render::OakRenderTicket) -> c_int {
	if let Some(id) = stub_ticket_id(ticket) {
		let mut tickets = TICKETS.lock().unwrap_or_else(|e| e.into_inner());
		while !tickets.iter().any(|t| t.id == id && t.finished) {
			tickets = TICKET_CV.wait(tickets).unwrap_or_else(|e| e.into_inner());
		}
	}
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_cancel(ticket: oaktask::bridge::render::OakRenderTicket) -> c_int {
	if let Some(id) = stub_ticket_id(ticket) {
		// The real cancel contract: the ticket finishes and its completion
		// still fires exactly once (a NULL result via get_frame).
		stub_complete(id);
	}
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_get_type(ticket: oaktask::bridge::render::OakRenderTicket) -> c_int {
	match stub_ticket_id(ticket) {
		Some(id) => {
			let tickets = TICKETS.lock().unwrap_or_else(|e| e.into_inner());
			match tickets.iter().find(|t| t.id == id) {
				Some(t) => t.kind,
				None => OAKRENDER_TICKET_VIDEO,
			}
		}
		None => OAKRENDER_TICKET_VIDEO,
	}
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_get_frame(
	ticket: oaktask::bridge::render::OakRenderTicket,
	out: *mut oaktask::bridge::codec::OakFrame,
) -> c_int {
	if !out.is_null() {
		if TICKET_FRAME_VALID.load(Ordering::SeqCst) != 0 {
			// The frame handle's ctx encodes the ticket id so tests can
			// assert the delivery order of frames.
			match stub_ticket_id(ticket) {
				Some(id) => *out = stub_frame_handle(id),
				None => *out = fake_handle(),
			}
		} else {
			*out = empty_handle();
		}
	}
	TICKET_GET_FRAME_RESULT.load(Ordering::SeqCst)
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_get_time(
	ticket: oaktask::bridge::render::OakRenderTicket,
	n: *mut i64,
	d: *mut i64,
) -> c_int {
	let info = match stub_ticket_id(ticket) {
		Some(id) => {
			let tickets = TICKETS.lock().unwrap_or_else(|e| e.into_inner());
			tickets.iter().find(|t| t.id == id).map(|t| (t.time_num, t.time_den))
		}
		None => None,
	};
	if let Some((num, den)) = info {
		if !n.is_null() {
			*n = num;
		}
		if !d.is_null() {
			*d = den;
		}
	}
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_get_range(
	ticket: oaktask::bridge::render::OakRenderTicket,
	a: *mut i64,
	b: *mut i64,
	c: *mut i64,
	d: *mut i64,
) -> c_int {
	let info = match stub_ticket_id(ticket) {
		Some(id) => {
			let tickets = TICKETS.lock().unwrap_or_else(|e| e.into_inner());
			tickets.iter().find(|t| t.id == id).map(|t| t.range)
		}
		None => None,
	};
	if let Some((in_num, in_den, out_num, out_den)) = info {
		if !a.is_null() {
			*a = in_num;
		}
		if !b.is_null() {
			*b = in_den;
		}
		if !c.is_null() {
			*c = out_num;
		}
		if !d.is_null() {
			*d = out_den;
		}
	}
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_get_samples(_ticket: oaktask::bridge::render::OakRenderTicket, out: *mut *mut c_void) -> c_int {
	if !out.is_null() {
		*out = std::ptr::null_mut();
	}
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_ticket_free(_ticket: *mut oaktask::bridge::render::OakRenderTicket) {}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_project_copier_create() -> oaktask::bridge::render::OakRenderProjectCopier {
	unsafe extern "C" fn noop(_ctx: *mut c_void) {}
	oaktask::bridge::render::OakRenderProjectCopier {
		ctx: 7usize as *mut c_void,
		addref: Some(noop),
		release: Some(noop),
		abi_version: 1,
	}
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_project_copier_free(_copier: *mut oaktask::bridge::render::OakRenderProjectCopier) {}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_project_copier_set_project(_copier: oaktask::bridge::render::OakRenderProjectCopier, _project: oaktask::bridge::node::OakNodeProject) -> c_int {
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_project_copier_get_copy(_copier: oaktask::bridge::render::OakRenderProjectCopier, original: oaktask::bridge::node::OakNodeNode) -> oaktask::bridge::node::OakNodeNode {
	original
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_project_copier_get_copied_project(_copier: oaktask::bridge::render::OakRenderProjectCopier) -> oaktask::bridge::node::OakNodeProject {
	fake_handle()
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_color_processor_create(
	_src_space: *const c_char,
	_dst_transform: *const c_char,
	_direction: c_int,
) -> oaktask::bridge::render::OakColorProcessor {
	unsafe extern "C" fn noop(_ctx: *mut c_void) {}
	oaktask::bridge::render::OakColorProcessor {
		ctx: 8usize as *mut c_void,
		addref: Some(noop),
		release: Some(noop),
		abi_version: 1,
	}
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_color_processor_free(_processor: *mut oaktask::bridge::render::OakColorProcessor) {}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_color_processor_is_valid(_processor: oaktask::bridge::render::OakColorProcessor) -> c_int {
	1
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_codec_frame_free(_frame: *mut oaktask::bridge::codec::OakFrame) {}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_manager_set_aggressive_gc(_enabled: c_int) -> c_int {
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_cache_get_invalidated_ranges(
	_cache: oaktask::bridge::render::OakRenderCache,
	_in_num: i64,
	_in_den: i64,
	_out_num: i64,
	_out_den: i64,
	_flat: *mut i64,
	_flat_size: c_int,
) -> c_int {
	0
}

#[cfg(not(feature = "real-oakrender"))]
#[no_mangle]
pub unsafe extern "C" fn oakrender_cache_free(_cache: *mut oaktask::bridge::render::OakRenderCache) {}

// ---------------------------------------------------------------------------
// bridge::undo stubs
// ---------------------------------------------------------------------------

#[no_mangle]
pub unsafe extern "C" fn oakundo_command_init(
	_vtable: *const oaktask::bridge::undo::OakUndoCommandVtable,
	_userdata: *mut c_void,
) -> oaktask::bridge::undo::OakUndoCommand {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oakundo_command_init_multi() -> oaktask::bridge::undo::OakUndoCommand {
	fake_handle()
}

#[no_mangle]
pub unsafe extern "C" fn oakundo_command_multi_add_child(_multi: oaktask::bridge::undo::OakUndoCommand, _child: oaktask::bridge::undo::OakUndoCommand) -> c_int {
	MULTI_CHILDREN.fetch_add(1, Ordering::SeqCst);
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakundo_command_multi_child_count(_multi: oaktask::bridge::undo::OakUndoCommand, out: *mut c_int) -> c_int {
	if !out.is_null() {
		*out = MULTI_CHILDREN.load(Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakundo_command_multi_child(_multi: oaktask::bridge::undo::OakUndoCommand, _index: c_int, out: *mut oaktask::bridge::undo::OakUndoCommand) -> c_int {
	if !out.is_null() {
		*out = fake_handle();
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakundo_command_redo_now(_command: oaktask::bridge::undo::OakUndoCommand) -> c_int {
	// Approximate redo: the folder gains the accumulated children.
	let delta = MULTI_CHILDREN.load(Ordering::SeqCst);
	if delta > 0 {
		FOLDER_CHILD_COUNT.fetch_add(delta, Ordering::SeqCst);
		REDO_DELTA.store(delta, Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakundo_command_undo_now(_command: oaktask::bridge::undo::OakUndoCommand) -> c_int {
	let delta = REDO_DELTA.load(Ordering::SeqCst);
	if delta > 0 {
		FOLDER_CHILD_COUNT.fetch_sub(delta, Ordering::SeqCst);
		REDO_DELTA.store(0, Ordering::SeqCst);
	}
	0
}

#[no_mangle]
pub unsafe extern "C" fn oakundo_command_free(_command: *mut oaktask::bridge::undo::OakUndoCommand) {}
