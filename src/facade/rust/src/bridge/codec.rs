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

//! oakcodec C ABI imports, mirroring the oakcodec crate's exports
//! (`src/codec/rust/src/ffi/{format,encoder}.rs`; headers
//! `include/codec/{format,encoder}.h`). Also carries the
//! `oakcodec_encoding_params` POD the facade's encoding-params handle
//! wraps, and the oakaudio recording-params pointer pass-through.

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

/// `include/codec/encoder.h` — the encoding-params POD, a complete mirror
/// of `olive::EncodingParams`. The facade's `OakEngineEncodingParams`
/// handle is a heap box over exactly this struct, so every engine getter/
/// setter reads/writes a field and `encoder_init`/recording can consume
/// the pointer directly.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct EncodingParamsPOD {
	/// Output filename (NUL-terminated).
	pub filename: [c_char; 1024],
	/// Output container format id.
	pub format: c_int,
	/// Whether video is enabled (1/0).
	pub video_enabled: c_int,
	/// Video codec id.
	pub video_codec: c_int,
	/// Video width in pixels.
	pub video_width: c_int,
	/// Video height in pixels.
	pub video_height: c_int,
	/// Frame duration numerator.
	pub video_time_base_num: c_int,
	/// Frame duration denominator.
	pub video_time_base_den: c_int,
	/// Delivery pixel format (`OakPixelFormat`).
	pub video_pixel_format: c_int,
	/// Interlacing mode (`OAKCODEC_INTERLACE_*`).
	pub video_interlacing: c_int,
	/// Pixel aspect ratio numerator.
	pub video_pixel_aspect_num: c_int,
	/// Pixel aspect ratio denominator.
	pub video_pixel_aspect_den: c_int,
	/// Video bit rate in bits per second (0 = codec default).
	pub video_bit_rate: i64,
	/// Minimum video bit rate.
	pub video_min_bit_rate: i64,
	/// Maximum video bit rate.
	pub video_max_bit_rate: i64,
	/// Video buffer size in bytes.
	pub video_buffer_size: i64,
	/// Encoder threads (0 = auto).
	pub video_threads: c_int,
	/// Encoded pixel format name ("yuv420p", NUL-terminated).
	pub video_pix_fmt: [c_char; 64],
	/// Whether video is an image sequence (1/0).
	pub video_is_image_sequence: c_int,
	/// Scaling method (`OAKCODEC_ENCODING_SCALING_*`).
	pub video_scaling_method: c_int,
	/// Whether audio is enabled (1/0).
	pub audio_enabled: c_int,
	/// Audio codec id.
	pub audio_codec: c_int,
	/// Audio sample rate in Hz.
	pub audio_sample_rate: c_int,
	/// Audio channel layout mask.
	pub audio_channel_layout: u64,
	/// Audio sample format (`SampleFormat::Format`).
	pub audio_sample_format: c_int,
	/// Audio bit rate in bits per second.
	pub audio_bit_rate: i64,
	/// Whether subtitles are enabled (1/0).
	pub subtitles_enabled: c_int,
	/// Subtitle codec id.
	pub subtitles_codec: c_int,
	/// Whether subtitles are a sidecar file (1/0).
	pub subtitles_are_sidecar: c_int,
	/// Sidecar subtitle format (`ExportFormat::Format`).
	pub subtitles_sidecar_format: c_int,
	/// Output OCIO colorspace name (empty = reference space).
	pub color_transform_output: [c_char; 256],
	/// Export length in seconds (rational), numerator.
	pub export_length_num: c_int,
	/// Export length in seconds (rational), denominator.
	pub export_length_den: c_int,
	/// Whether a custom export range is set (1/0).
	pub has_custom_range: c_int,
	/// Custom range in point numerator (seconds).
	pub custom_range_in_num: i64,
	/// Custom range in point denominator (seconds).
	pub custom_range_in_den: i64,
	/// Custom range out point numerator (seconds).
	pub custom_range_out_num: i64,
	/// Custom range out point denominator (seconds).
	pub custom_range_out_den: i64,
}

impl EncodingParamsPOD {
	/// Zeroed POD: all tracks disabled, format unset.
	pub fn zeroed() -> Self {
		unsafe { std::mem::zeroed() }
	}
}

extern "C" {
	// ---- include/codec/format.h (encoding metadata) ------------------------
	/// `oakcodec_encoding_format_count`.
	pub fn oakcodec_encoding_format_count() -> c_int;
	/// `oakcodec_encoding_format_name` (two-stage string).
	pub fn oakcodec_encoding_format_name(format: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakcodec_encoding_format_extension` (two-stage string).
	pub fn oakcodec_encoding_format_extension(
		format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcodec_encoding_format_video_codec_count`.
	pub fn oakcodec_encoding_format_video_codec_count(format: c_int) -> c_int;
	/// `oakcodec_encoding_format_video_codec_at`.
	pub fn oakcodec_encoding_format_video_codec_at(format: c_int, index: c_int) -> c_int;
	/// `oakcodec_encoding_format_audio_codec_count`.
	pub fn oakcodec_encoding_format_audio_codec_count(format: c_int) -> c_int;
	/// `oakcodec_encoding_format_audio_codec_at`.
	pub fn oakcodec_encoding_format_audio_codec_at(format: c_int, index: c_int) -> c_int;
	/// `oakcodec_encoding_format_subtitle_codec_count`.
	pub fn oakcodec_encoding_format_subtitle_codec_count(format: c_int) -> c_int;
	/// `oakcodec_encoding_format_subtitle_codec_at`.
	pub fn oakcodec_encoding_format_subtitle_codec_at(format: c_int, index: c_int) -> c_int;
	/// `oakcodec_encoding_codec_name` (two-stage string).
	pub fn oakcodec_encoding_codec_name(codec: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakcodec_encoding_codec_is_still_image`.
	pub fn oakcodec_encoding_codec_is_still_image(codec: c_int) -> c_int;
	/// `oakcodec_encoding_codec_is_lossless`.
	pub fn oakcodec_encoding_codec_is_lossless(codec: c_int) -> c_int;
	/// `oakcodec_encoding_pix_fmt_count`.
	pub fn oakcodec_encoding_pix_fmt_count(format: c_int, codec: c_int) -> c_int;
	/// `oakcodec_encoding_pix_fmt_at` (two-stage string).
	pub fn oakcodec_encoding_pix_fmt_at(
		format: c_int,
		codec: c_int,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcodec_encoding_pix_fmt_index`.
	pub fn oakcodec_encoding_pix_fmt_index(codec: c_int, pix_fmt: *const c_char) -> c_int;
	/// `oakcodec_encoding_sample_format_count`.
	pub fn oakcodec_encoding_sample_format_count(format: c_int, codec: c_int) -> c_int;
	/// `oakcodec_encoding_sample_format_at`.
	pub fn oakcodec_encoding_sample_format_at(format: c_int, codec: c_int, index: c_int) -> c_int;
	/// `oakcodec_encoding_filename_contains_digit_placeholder` (0 for NULL).
	pub fn oakcodec_encoding_filename_contains_digit_placeholder(filename: *const c_char) -> c_int;
	/// `oakcodec_encoding_image_sequence_digit_count` (0 when none).
	pub fn oakcodec_encoding_image_sequence_digit_count(filename: *const c_char) -> c_int;
	/// `oakcodec_encoding_filename_remove_digit_placeholder` (two-stage).
	pub fn oakcodec_encoding_filename_remove_digit_placeholder(
		filename: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcodec_encoding_generate_matrix` — writes 16 `f64` (the engine
	/// surface uses `f32`; the facade converts).
	pub fn oakcodec_encoding_generate_matrix(
		method: c_int,
		src_width: c_int,
		src_height: c_int,
		dst_width: c_int,
		dst_height: c_int,
		out_matrix: *mut f64,
	) -> c_int;

	// ---- include/codec/encoder.h -------------------------------------------
	/// `oakcodec_encoder_init` — encoder over a params POD (refcount 1).
	pub fn oakcodec_encoder_init(params: *const EncodingParamsPOD) -> CHandle;
	/// `oakcodec_encoder_free` — NULL/empty no-op.
	pub fn oakcodec_encoder_free(encoder: *mut CHandle);
}
