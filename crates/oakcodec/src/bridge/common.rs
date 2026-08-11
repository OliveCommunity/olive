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

//! oakcommon / oakcore C ABI imports (videoparams, audioparams, rational,
//! subtitleparams, config, filefunctions, ffmpegutils, oiioutils,
//! colortransform).
//!
//! The by-value handle structs (`OakVideoParams`, `OakAudioParams`,
//! `OakSubtitleParams`, `OakNodeBlock`) mirror the `{ctx, addref,
//! release, abi_version}` layout from `include/common/handle.h`, so the
//! codec module can store them by value and pass them straight across
//! the FFI boundary. Function signatures match the public headers
//! verbatim; symbols resolve at link time.
//!
//! The oakcore audio parameters use a pointer-based C ABI instead of the
//! by-value handle convention: `oakcore_audioparams_*` take and return
//! `OakAudioParams *` / `OakRational *` pointers (`core/include/olive/
//! core/oakcore/audioparams.h`, `rational.h`). Those are bridged as raw
//! pointers to the crate's handle structs; `oakcore_audioparams_time_base`
//! returns a newly allocated rational the caller must release with
//! `oakcore_rational_free`.

use std::ffi::{c_char, c_int, c_void};

use crate::handle::CHandle;

/// `OakVideoParams` — refcounted video-parameter handle.
pub type OakVideoParams = CHandle;

/// `OakAudioParams` — refcounted audio-parameter handle.
pub type OakAudioParams = CHandle;

/// `OakSubtitleParams` — refcounted subtitle-parameter handle.
pub type OakSubtitleParams = CHandle;

/// `OakNodeBlock` — opaque node-block handle (owned elsewhere; codec
/// only stores and forwards it).
pub type OakNodeBlock = CHandle;

// The handle structs are opaque refcounted handles pointing into a C
// library; the boxed objects are independently synchronized there, so
// moving a handle between threads is sound.

extern "C" {
	/// `oakcommon_videoparams_init`.
	pub fn oakcommon_videoparams_init() -> OakVideoParams;
	/// `oakcommon_videoparams_init_basic`.
	pub fn oakcommon_videoparams_init_basic(width: c_int, height: c_int) -> OakVideoParams;
	/// `oakcommon_videoparams_init_with_time_base`.
	pub fn oakcommon_videoparams_init_with_time_base(
		width: c_int,
		height: c_int,
		time_base_num: i64,
		time_base_den: i64,
	) -> OakVideoParams;
	/// `oakcommon_videoparams_free` (NULL/empty no-op).
	pub fn oakcommon_videoparams_free(params: *mut OakVideoParams);
	/// `oakcommon_videoparams_get_width`.
	pub fn oakcommon_videoparams_get_width(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_get_height`.
	pub fn oakcommon_videoparams_get_height(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_get_format`.
	pub fn oakcommon_videoparams_get_format(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_get_time_base` (num/den out).
	pub fn oakcommon_videoparams_get_time_base(
		params: OakVideoParams,
		out_num: *mut i64,
		out_den: *mut i64,
	) -> c_int;
	/// `oakcommon_videoparams_set_width`.
	pub fn oakcommon_videoparams_set_width(params: OakVideoParams, width: c_int);
	/// `oakcommon_videoparams_set_height`.
	pub fn oakcommon_videoparams_set_height(params: OakVideoParams, height: c_int);
	/// `oakcommon_videoparams_set_format`.
	pub fn oakcommon_videoparams_set_format(params: OakVideoParams, format: c_int);
	/// `oakcommon_videoparams_get_is_valid`.
	pub fn oakcommon_videoparams_get_is_valid(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_equals`.
	pub fn oakcommon_videoparams_equals(a: OakVideoParams, b: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_set_time_base`.
	pub fn oakcommon_videoparams_set_time_base(params: OakVideoParams, num: i64, den: i64);
	/// `oakcommon_videoparams_set_frame_rate`.
	pub fn oakcommon_videoparams_set_frame_rate(params: OakVideoParams, num: i64, den: i64);
	/// `oakcommon_videoparams_set_pixel_aspect_ratio`.
	pub fn oakcommon_videoparams_set_pixel_aspect_ratio(params: OakVideoParams, num: i64, den: i64);
	/// `oakcommon_videoparams_set_interlacing`.
	pub fn oakcommon_videoparams_set_interlacing(params: OakVideoParams, interlacing: c_int);
	/// `oakcommon_videoparams_set_duration`.
	pub fn oakcommon_videoparams_set_duration(params: OakVideoParams, duration: i64);
	/// `oakcommon_videoparams_set_start_time`.
	pub fn oakcommon_videoparams_set_start_time(params: OakVideoParams, start_time: i64);
	/// `oakcommon_videoparams_set_color_range`.
	pub fn oakcommon_videoparams_set_color_range(params: OakVideoParams, color_range: c_int);
	/// `oakcommon_videoparams_set_video_type`.
	pub fn oakcommon_videoparams_set_video_type(params: OakVideoParams, video_type: c_int);
	/// `oakcommon_videoparams_set_channel_count`.
	pub fn oakcommon_videoparams_set_channel_count(params: OakVideoParams, channels: c_int);
	/// `oakcommon_videoparams_set_color_primaries`.
	pub fn oakcommon_videoparams_set_color_primaries(params: OakVideoParams, primaries: c_int);
	/// `oakcommon_videoparams_set_color_transfer`.
	pub fn oakcommon_videoparams_set_color_transfer(params: OakVideoParams, transfer: c_int);
	/// `oakcommon_videoparams_set_premultiplied_alpha`.
	pub fn oakcommon_videoparams_set_premultiplied_alpha(
		params: OakVideoParams,
		premultiplied: c_int,
	);
	/// `oakcommon_videoparams_set_enabled`.
	pub fn oakcommon_videoparams_set_enabled(params: OakVideoParams, enabled: c_int);
	/// `oakcommon_videoparams_static_get_bytes_per_pixel`.
	pub fn oakcommon_videoparams_static_get_bytes_per_pixel(format: c_int) -> c_int;
	/// `oakcommon_videoparams_frame_rate_as_time_base`.
	pub fn oakcommon_videoparams_frame_rate_as_time_base(
		frame_rate_num: i64,
		frame_rate_den: i64,
		out_num: *mut i64,
		out_den: *mut i64,
	);
	/// `oakcommon_videoparams_get_stream_index`.
	pub fn oakcommon_videoparams_get_stream_index(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_set_stream_index`.
	pub fn oakcommon_videoparams_set_stream_index(params: OakVideoParams, index: c_int);
	/// `oakcommon_videoparams_get_divider`.
	pub fn oakcommon_videoparams_get_divider(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_set_divider`.
	pub fn oakcommon_videoparams_set_divider(params: OakVideoParams, divider: c_int);
	// NOTE: the remaining video getters below take the value-style form the
	// crate's existing bridge uses (the real oakcommon headers use out-pointer
	// args); `get_frame_rate` needs both values so it keeps the out pair.
	/// `oakcommon_videoparams_get_frame_rate` (frame-rate num/den out).
	pub fn oakcommon_videoparams_get_frame_rate(
		params: OakVideoParams,
		out_num: *mut c_int,
		out_den: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_duration` (time-base units).
	pub fn oakcommon_videoparams_get_duration(params: OakVideoParams) -> i64;
	/// `oakcommon_videoparams_get_channel_count`.
	pub fn oakcommon_videoparams_get_channel_count(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_get_color_primaries`.
	pub fn oakcommon_videoparams_get_color_primaries(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_get_color_transfer`.
	pub fn oakcommon_videoparams_get_color_transfer(params: OakVideoParams) -> c_int;
	/// `oakcommon_videoparams_get_interlacing` (`Interlacing` value).
	pub fn oakcommon_videoparams_get_interlacing(params: OakVideoParams) -> c_int;
	/// `oakcore_audioparams_create` (pointer-based; timebase 1/sample_rate).
	pub fn oakcore_audioparams_create(
		sample_rate: c_int,
		channel_layout: u64,
		format: c_int,
	) -> *mut OakAudioParams;
	/// `oakcore_audioparams_free` (NULL no-op).
	pub fn oakcore_audioparams_free(params: *mut OakAudioParams);
	/// `oakcore_audioparams_sample_rate`.
	pub fn oakcore_audioparams_sample_rate(params: *const OakAudioParams) -> c_int;
	/// `oakcore_audioparams_set_sample_rate`.
	pub fn oakcore_audioparams_set_sample_rate(params: *mut OakAudioParams, sample_rate: c_int);
	/// `oakcore_audioparams_channel_layout`.
	pub fn oakcore_audioparams_channel_layout(params: *const OakAudioParams) -> u64;
	/// `oakcore_audioparams_set_channel_layout`.
	pub fn oakcore_audioparams_set_channel_layout(params: *mut OakAudioParams, layout: u64);
	/// `oakcore_audioparams_set_time_base`.
	pub fn oakcore_audioparams_set_time_base(params: *mut OakAudioParams, num: c_int, den: c_int);
	/// `oakcore_audioparams_set_format`.
	pub fn oakcore_audioparams_set_format(params: *mut OakAudioParams, format: c_int);
	/// `oakcore_audioparams_set_stream_index`.
	pub fn oakcore_audioparams_set_stream_index(params: *mut OakAudioParams, index: c_int);
	/// `oakcore_audioparams_set_duration`.
	pub fn oakcore_audioparams_set_duration(params: *mut OakAudioParams, duration: i64);
	/// `oakcore_audioparams_channel_count`.
	pub fn oakcore_audioparams_channel_count(params: *const OakAudioParams) -> c_int;
	/// `oakcore_audioparams_format`.
	pub fn oakcore_audioparams_format(params: *const OakAudioParams) -> c_int;
	/// `oakcore_audioparams_stream_index`.
	pub fn oakcore_audioparams_stream_index(params: *const OakAudioParams) -> c_int;
	/// `oakcore_audioparams_duration`.
	pub fn oakcore_audioparams_duration(params: *const OakAudioParams) -> i64;
	/// `oakcore_audioparams_is_valid`.
	pub fn oakcore_audioparams_is_valid(params: *const OakAudioParams) -> c_int;
	/// `oakcore_audioparams_time_base` (newly allocated rational; caller
	/// releases with `oakcore_rational_free`).
	pub fn oakcore_audioparams_time_base(params: *const OakAudioParams) -> *mut c_void;
	/// `oakcore_rational_numerator`.
	pub fn oakcore_rational_numerator(rational: *const c_void) -> c_int;
	/// `oakcore_rational_denominator`.
	pub fn oakcore_rational_denominator(rational: *const c_void) -> c_int;
	/// `oakcore_rational_free` (NULL no-op).
	pub fn oakcore_rational_free(rational: *mut c_void);
	/// `oakcommon_subtitleparams_get_stream_index`.
	pub fn oakcommon_subtitleparams_get_stream_index(params: OakSubtitleParams) -> c_int;
	/// `oakcommon_subtitleparams_generate_ass_header`.
	pub fn oakcommon_subtitleparams_generate_ass_header(
		params: OakSubtitleParams,
		width: c_int,
		height: c_int,
	);
	/// `oakcommon_subtitleparams_add_subtitle`.
	pub fn oakcommon_subtitleparams_add_subtitle(params: OakSubtitleParams, text: *const c_char);
	/// `oakcommon_config_get_int`.
	pub fn oakcommon_config_get_int(
		group: *const c_char,
		key: *const c_char,
		default: c_int,
	) -> c_int;
	/// `oakcommon_config_get_bool`.
	pub fn oakcommon_config_get_bool(
		group: *const c_char,
		key: *const c_char,
		default: c_int,
	) -> c_int;
	/// `oakcommon_config_get` (two-stage string access).
	pub fn oakcommon_config_get(
		group: *const c_char,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_filefunctions_init`.
	pub fn oakcommon_filefunctions_init();
	/// `oakcommon_filefunctions_get_configuration_location` (two-stage).
	pub fn oakcommon_filefunctions_get_configuration_location(
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_filefunctions_get_unique_file_identifier`.
	pub fn oakcommon_filefunctions_get_unique_file_identifier(path: *const c_char) -> i64;
	/// `oakcommon_filefunctions_get_application_path` (two-stage).
	pub fn oakcommon_filefunctions_get_application_path(buf: *mut c_char, buf_size: c_int)
		-> c_int;
	/// `oakcommon_filefunctions_free` (frees an internally cached string).
	pub fn oakcommon_filefunctions_free(ptr: *mut c_void);
	/// `oakcommon_colortransform_init_output`.
	pub fn oakcommon_colortransform_init_output(
		src_colorspace: c_int,
		src_trc: c_int,
		dst_colorspace: c_int,
		dst_trc: c_int,
		premultiplied: c_int,
		chroma_coeffs: *const c_void,
	) -> OakVideoParams;
	/// `oakcommon_colortransform_get_output`.
	pub fn oakcommon_colortransform_get_output(params: OakVideoParams, out: *mut OakVideoParams);
	/// `oakcommon_colortransform_free`.
	pub fn oakcommon_colortransform_free(params: *mut OakVideoParams);
	/// `oakcommon_ffmpegutils_get_native_sample_format`.
	pub fn oakcommon_ffmpegutils_get_native_sample_format(sample_format: c_int) -> c_int;
	/// `oakcommon_ffmpegutils_get_compatible_pixel_format`.
	pub fn oakcommon_ffmpegutils_get_compatible_pixel_format(format: c_int) -> c_int;
	/// `oakcommon_ffmpegutils_get_ffmpeg_pixel_format`.
	pub fn oakcommon_ffmpegutils_get_ffmpeg_pixel_format(format: c_int) -> c_int;
	/// `oakcommon_ffmpegutils_get_ffmpeg_sample_format`.
	pub fn oakcommon_ffmpegutils_get_ffmpeg_sample_format(format: c_int) -> c_int;
	/// `oakcommon_ffmpegutils_get_compatible_bridge_pixel_format`.
	pub fn oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(format: c_int) -> c_int;
	/// `oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space`.
	pub fn oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(format: c_int) -> c_int;
	/// `oakcommon_oiioutils_init`.
	pub fn oakcommon_oiioutils_init();
	/// `oakcommon_oiioutils_get_oiio_base_type_from_format`.
	pub fn oakcommon_oiioutils_get_oiio_base_type_from_format(format: c_int) -> c_int;
	/// `oakcommon_oiioutils_get_format_from_oiio_basetype`.
	pub fn oakcommon_oiioutils_get_format_from_oiio_basetype(basetype: c_int) -> c_int;
	/// `oakcommon_oiioutils_get_pixel_aspect_ratio` (num/den out).
	pub fn oakcommon_oiioutils_get_pixel_aspect_ratio(
		width: c_int,
		height: c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
	) -> c_int;
	/// `oakcommon_oiioutils_free`.
	pub fn oakcommon_oiioutils_free();
}
