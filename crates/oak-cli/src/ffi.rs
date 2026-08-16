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

//! The `oakengine_*` C ABI surface oak-cli consumes — **declared and
//! linked**.
//!
//! This module mirrors — verbatim — every function, opaque handle and POD
//! struct from the engine headers that the C++ `cli/main.cpp` touches:
//!
//!   - `engine/include/oakengine/project.h`   (project lifecycle + queries)
//!   - `engine/include/oakengine/footage.h`   (probe / stream info / import)
//!   - `engine/include/oakengine/timeline.h`  (sequence + track/clip editing)
//!   - `engine/include/oakengine/renderer.h`  (render manager init, renderer,
//!     frame + audio buffer)
//!   - `engine/include/oakengine/videoparams.h` (sequence video params)
//!
//! The `#[link(name = "oakengine", kind = "dylib")]` block resolves every
//! symbol against the built `liboakengine` cdylib at link time (the
//! search path comes from `build.rs`; see the Cargo.toml comment for the
//! build order). Five symbols from the original declaration surface are
//! NOT exported by the current Rust facade — `oakengine_init`,
//! `oakengine_shutdown` (init.h) and the exporter trio
//! `oakengine_export_render` / `oakengine_export_last_error` /
//! `oakengine_export_set_progress_callback` (exporter.h). Those live in
//! [`crate::optional`], which resolves them with `dlsym` at call time so
//! the prescribed call sequences keep working when the facade grows them.
//!
//! The subcommands in `src/cmd/` call this surface directly — no deferral
//! gate, no module-crate calls.

#![allow(dead_code)]
#![allow(non_camel_case_types)]
#![allow(clippy::missing_safety_doc)]

use std::ffi::{c_char, c_double, c_int, c_void};

// ---------------------------------------------------------------------------
// Opaque engine handle types (engine/include/oakengine/*.h).
// ---------------------------------------------------------------------------

#[repr(C)]
pub struct OakEngineProject {
	_opaque: [u8; 0],
}

#[repr(C)]
pub struct OakEngineSequence {
	_opaque: [u8; 0],
}

#[repr(C)]
pub struct OakEngineRenderer {
	_opaque: [u8; 0],
}

#[repr(C)]
pub struct OakEngineFrame {
	_opaque: [u8; 0],
}

#[repr(C)]
pub struct OakEngineAudioBuffer {
	_opaque: [u8; 0],
}

#[repr(C)]
pub struct OakEngineFootage {
	_opaque: [u8; 0],
}

#[repr(C)]
pub struct OakEngineClip {
	_opaque: [u8; 0],
}

// ---------------------------------------------------------------------------
// POD structs (footage.h / exporter.h).
// ---------------------------------------------------------------------------

/// `oak_footage_video_info` (engine/include/oakengine/footage.h).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakFootageVideoInfo {
	pub stream_index: c_int,
	pub width: c_int,
	pub height: c_int,
	pub frame_rate_num: c_int,
	pub frame_rate_den: c_int,
	pub duration_ts: i64,
	pub time_base_num: c_int,
	pub time_base_den: c_int,
	pub color_primaries: c_int,
	pub color_trc: c_int,
	pub interlaced: c_int,
}

/// `oak_footage_audio_info` (engine/include/oakengine/footage.h).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakFootageAudioInfo {
	pub stream_index: c_int,
	pub sample_rate: c_int,
	pub channel_layout: u64,
	pub channel_count: c_int,
	pub duration_ts: i64,
	pub time_base_num: c_int,
	pub time_base_den: c_int,
}

/// `oak_export_options` (engine/include/oakengine/exporter.h).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakExportOptions {
	pub video_codec: c_int,
	pub audio_codec: c_int,
	pub video_bit_rate: i64,
	pub audio_sample_rate: c_int,
	pub audio_channel_count: c_int,
}

/// `oakengine_export_progress_fn` (exporter.h).
pub type OakEngineExportProgressFn =
	Option<unsafe extern "C" fn(fraction: c_double, userdata: *mut c_void)>;

// ---------------------------------------------------------------------------
// Constants (verbatim values from the engine headers).
// ---------------------------------------------------------------------------

/// OAKENGINE_OK / OAKENGINE_E_* (init.h).
pub const OAKENGINE_OK: c_int = 0;
pub const OAKENGINE_E_INVALID: c_int = -1;
pub const OAKENGINE_E_STATE: c_int = -2;
pub const OAKENGINE_E_FAILED: c_int = -3;
pub const OAKENGINE_E_NOT_FOUND: c_int = -4;

/// OAKENGINE_INIT_* (init.h).
pub const OAKENGINE_INIT_HEADLESS: c_int = 0x01;
pub const OAKENGINE_INIT_RENDER: c_int = 0x02;

/// olive::core::PixelFormat::f32, the renderer's frame pixel format
/// (`k_pixel_format_f32` in cli/main.cpp).
pub const PIXEL_FORMAT_F32: c_int = 4;

/// OAKENGINE_TRACK_TYPE_* (timeline.h).
pub const OAKENGINE_TRACK_TYPE_VIDEO: c_int = 0;
pub const OAKENGINE_TRACK_TYPE_AUDIO: c_int = 1;

/// OAKENGINE_EXPORT_VIDEO_* / OAKENGINE_EXPORT_AUDIO_* (exporter.h).
pub const OAKENGINE_EXPORT_VIDEO_H264: c_int = 0;
pub const OAKENGINE_EXPORT_AUDIO_AAC: c_int = 0;

// ---------------------------------------------------------------------------
// The facade surface (the symbols the built liboakengine exports; see the
// module docs — the five missing ones live in crate::optional).
// ---------------------------------------------------------------------------

#[link(name = "oakengine", kind = "dylib")]
extern "C" {
	// ---- project.h -------------------------------------------------------
	pub fn oakengine_project_create() -> *mut OakEngineProject;
	pub fn oakengine_project_free(self_: *mut OakEngineProject);
	pub fn oakengine_project_new(self_: *mut OakEngineProject) -> c_int;
	pub fn oakengine_project_load(
		self_: *mut OakEngineProject,
		path: *const c_char,
		err: *mut c_char,
		err_size: c_int,
	) -> c_int;
	pub fn oakengine_project_name(
		self_: *const OakEngineProject,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	pub fn oakengine_project_filename(
		self_: *const OakEngineProject,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	pub fn oakengine_project_is_modified(self_: *const OakEngineProject) -> c_int;
	pub fn oakengine_project_sequence_count(self_: *const OakEngineProject) -> c_int;
	pub fn oakengine_project_sequence_at(
		self_: *const OakEngineProject,
		index: c_int,
	) -> *mut OakEngineSequence;
	pub fn oakengine_project_footage_count(self_: *const OakEngineProject) -> c_int;
	pub fn oakengine_project_footage_filename(
		self_: *const OakEngineProject,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	pub fn oakengine_project_footage_is_online(
		self_: *const OakEngineProject,
		index: c_int,
	) -> c_int;

	// ---- footage.h -------------------------------------------------------
	pub fn oakengine_project_import_footage(
		project: *mut OakEngineProject,
		path: *const c_char,
	) -> *mut OakEngineFootage;
	pub fn oakengine_footage_probe(path: *const c_char) -> *mut OakEngineFootage;
	pub fn oakengine_footage_free(self_: *mut OakEngineFootage);
	pub fn oakengine_footage_last_error(buf: *mut c_char, buf_size: c_int) -> c_int;
	pub fn oakengine_footage_get_decoder_name(
		self_: *mut OakEngineFootage,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	pub fn oakengine_footage_get_duration(
		self_: *mut OakEngineFootage,
		seconds: *mut c_double,
	) -> c_int;
	pub fn oakengine_footage_get_video_stream_count(self_: *const OakEngineFootage) -> c_int;
	pub fn oakengine_footage_get_video_stream_info(
		self_: *mut OakEngineFootage,
		index: c_int,
		out: *mut OakFootageVideoInfo,
	) -> c_int;
	pub fn oakengine_footage_get_audio_stream_count(self_: *const OakEngineFootage) -> c_int;
	pub fn oakengine_footage_get_audio_stream_info(
		self_: *mut OakEngineFootage,
		index: c_int,
		out: *mut OakFootageAudioInfo,
	) -> c_int;
	pub fn oakengine_footage_get_subtitle_stream_count(self_: *const OakEngineFootage) -> c_int;

	// ---- timeline.h ------------------------------------------------------
	pub fn oakengine_sequence_new(
		project: *mut OakEngineProject,
		name: *const c_char,
	) -> *mut OakEngineSequence;
	pub fn oakengine_sequence_name(
		self_: *const OakEngineSequence,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	pub fn oakengine_sequence_get_length(
		self_: *const OakEngineSequence,
		seconds: *mut c_double,
	) -> c_int;
	pub fn oakengine_sequence_get_length_rational(
		self_: *const OakEngineSequence,
		num: *mut c_int,
		den: *mut c_int,
	) -> c_int;
	pub fn oakengine_sequence_get_frame_rate(
		self_: *const OakEngineSequence,
		num: *mut c_int,
		den: *mut c_int,
	) -> c_int;
	pub fn oakengine_sequence_get_video_params(
		self_: *const OakEngineSequence,
		width: *mut c_int,
		height: *mut c_int,
		par_num: *mut c_int,
		par_den: *mut c_int,
	) -> c_int;
	pub fn oakengine_sequence_set_video_params(
		self_: *mut OakEngineSequence,
		width: c_int,
		height: c_int,
		fps_num: c_int,
		fps_den: c_int,
		par_num: c_int,
		par_den: c_int,
		interlacing: c_int,
		format: c_int,
		undoable: c_int,
	) -> c_int;
	pub fn oakengine_sequence_track_count(
		self_: *const OakEngineSequence,
		video: *mut c_int,
		audio: *mut c_int,
		subtitle: *mut c_int,
	) -> c_int;
	pub fn oakengine_sequence_get_playhead(
		self_: *const OakEngineSequence,
		timestamp: *mut i64,
	) -> c_int;
	pub fn oakengine_sequence_get_playhead_seconds(
		self_: *const OakEngineSequence,
		seconds: *mut c_double,
	) -> c_int;
	pub fn oakengine_sequence_add_track(self_: *mut OakEngineSequence, track_type: c_int) -> c_int;
	pub fn oakengine_sequence_add_footage_clip(
		seq: *mut OakEngineSequence,
		footage: *mut OakEngineFootage,
		track_type: c_int,
		track_index: c_int,
		in_ts: i64,
		out_ts: i64,
		media_in: i64,
	) -> *mut OakEngineClip;
	/// Like `oakengine_sequence_add_footage_clip` but skips the
	/// same-project check: sequences created through `oakengine_sequence_new`
	/// live in their own scratch project (documented engine deviation), so
	/// the CLI's transcode flow (footage in the real project, sequence in
	/// the scratch project) needs the `_ex` variant.
	pub fn oakengine_sequence_add_footage_clip_ex(
		seq: *mut OakEngineSequence,
		footage: *mut OakEngineFootage,
		track_type: c_int,
		track_index: c_int,
		in_ts: i64,
		out_ts: i64,
		media_in: i64,
	) -> *mut OakEngineClip;
	pub fn oakengine_sequence_last_error(buf: *mut c_char, buf_size: c_int) -> c_int;

	// ---- renderer.h (render manager: the Rust facade's equivalent of the
	// ---- C++ OAKENGINE_INIT_RENDER engine-core render boot) -------------
	pub fn oakengine_render_manager_init() -> c_int;
	pub fn oakengine_render_manager_shutdown() -> c_int;

	// ---- renderer.h ------------------------------------------------------
	pub fn oakengine_renderer_create(
		seq: *mut OakEngineSequence,
		width: c_int,
		height: c_int,
		pixel_format: c_int,
		frame_rate_num: c_int,
		frame_rate_den: c_int,
		output_colorspace: *const c_char,
	) -> *mut OakEngineRenderer;
	pub fn oakengine_renderer_free(self_: *mut OakEngineRenderer);
	pub fn oakengine_renderer_last_error(
		self_: *const OakEngineRenderer,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	pub fn oakengine_renderer_render_frame(
		self_: *mut OakEngineRenderer,
		timestamp: i64,
	) -> *mut OakEngineFrame;
	pub fn oakengine_renderer_render_audio(
		self_: *mut OakEngineRenderer,
		start_timestamp: i64,
		length_timestamp: i64,
	) -> *mut OakEngineAudioBuffer;

	// ---- renderer.h (OakEngineFrame) -------------------------------------
	pub fn oakengine_frame_width(self_: *const OakEngineFrame) -> c_int;
	pub fn oakengine_frame_height(self_: *const OakEngineFrame) -> c_int;
	pub fn oakengine_frame_format(self_: *const OakEngineFrame) -> c_int;
	pub fn oakengine_frame_channel_count(self_: *const OakEngineFrame) -> c_int;
	pub fn oakengine_frame_linesize_bytes(self_: *const OakEngineFrame) -> c_int;
	pub fn oakengine_frame_data(self_: *const OakEngineFrame) -> *const c_void;
	pub fn oakengine_frame_free(self_: *mut OakEngineFrame);

	// ---- renderer.h (OakEngineAudioBuffer) --------------------------------
	pub fn oakengine_audio_sample_rate(self_: *const OakEngineAudioBuffer) -> c_int;
	pub fn oakengine_audio_channel_count(self_: *const OakEngineAudioBuffer) -> c_int;
	pub fn oakengine_audio_sample_count(self_: *const OakEngineAudioBuffer) -> i64;
	pub fn oakengine_audio_data(self_: *const OakEngineAudioBuffer, channel: c_int) -> *const f32;
	pub fn oakengine_audio_free(self_: *mut OakEngineAudioBuffer);
}

/// Read a facade string (buf/size convention) into an owned `String`,
/// mirroring `facade_string()` in cli/main.cpp: a negative return is an
/// error/empty string, otherwise the getter is called twice (size query,
/// then fill) and the trailing NUL is stripped.
///
/// `fill` must be one of the `oakengine_*` string getters (handle
/// getters closed over their live handle, last-error getters applied
/// directly).
pub fn string_get(mut fill: impl FnMut(*mut c_char, c_int) -> c_int) -> String {
	let size = fill(std::ptr::null_mut(), 0);
	if size < 0 {
		return String::new();
	}
	let mut s = vec![0u8; size as usize + 1];
	let n = fill(s.as_mut_ptr() as *mut c_char, size + 1);
	s.truncate(n.max(0) as usize);
	String::from_utf8_lossy(&s).into_owned()
}
