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
//! the FFI boundary.
//!
//! The oakcore audio parameters use a pointer-based C ABI instead of the
//! by-value handle convention: `oakcore_audioparams_*` take and return
//! `OakAudioParams *` / `OakRational *` pointers (`core/include/olive/
//! core/oakcore/audioparams.h`, `rational.h`). Those are bridged as raw
//! pointers to the crate's handle structs; `oakcore_audioparams_time_base`
//! returns a newly allocated rational the caller must release with
//! `oakcore_rational_free`.
//!
//! # ABI discipline (single-lib era)
//!
//! The frozen `oakcommon` C ABI (see `crates/oakengine/include/common/`)
//! uses **out-pointer getters** and **instance handles** for the module
//! families. Earlier codec-era bridges declared value-style signatures
//! (C++-era headers); those declarations were wrong against the frozen
//! contract and are fixed here. To keep the crate's call sites
//! unchanged, each true ABI symbol is declared under `#[link_name]` with
//! its real signature and wrapped in an adapter of the old value-style
//! shape (an M12 P0 decode-path fix: the mismatch made every decoded
//! frame invalid).

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

/// Module instance handles (filefunctions / oiioutils take `self_`).
pub type OakFileFunctions = CHandle;
pub type OakOIIOUtils = CHandle;

// The handle structs are opaque refcounted handles pointing into a C
// library; the boxed objects are independently synchronized there, so
// moving a handle between threads is sound.

extern "C" {
	/// `oakcommon_videoparams_init`.
	pub fn oakcommon_videoparams_init() -> OakVideoParams;
	/// `oakcommon_videoparams_init_basic`.
	#[link_name = "oakcommon_videoparams_init_basic"]
	fn videoparams_init_basic_abi(
		width: c_int,
		height: c_int,
		pixel_format: c_int,
		nb_channels: c_int,
		pixel_aspect_num: c_int,
		pixel_aspect_den: c_int,
		interlacing: c_int,
		divider: c_int,
	) -> OakVideoParams;
	/// `oakcommon_videoparams_init_with_time_base`.
	#[link_name = "oakcommon_videoparams_init_with_time_base"]
	fn videoparams_init_with_time_base_abi(
		width: c_int,
		height: c_int,
		time_base_num: c_int,
		time_base_den: c_int,
		pixel_format: c_int,
		nb_channels: c_int,
		pixel_aspect_num: c_int,
		pixel_aspect_den: c_int,
		interlacing: c_int,
		divider: c_int,
	) -> OakVideoParams;
	/// `oakcommon_videoparams_free` (NULL/empty no-op).
	pub fn oakcommon_videoparams_free(params: *mut OakVideoParams);
	/// `oakcommon_videoparams_get_width`.
	#[link_name = "oakcommon_videoparams_get_width"]
	fn videoparams_get_width_abi(params: OakVideoParams, width: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_height`.
	#[link_name = "oakcommon_videoparams_get_height"]
	fn videoparams_get_height_abi(params: OakVideoParams, height: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_format`.
	#[link_name = "oakcommon_videoparams_get_format"]
	fn videoparams_get_format_abi(params: OakVideoParams, format: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_time_base` (num/den out).
	#[link_name = "oakcommon_videoparams_get_time_base"]
	fn videoparams_get_time_base_abi(
		params: OakVideoParams,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_set_width`.
	pub fn oakcommon_videoparams_set_width(params: OakVideoParams, width: c_int);
	/// `oakcommon_videoparams_set_height`.
	pub fn oakcommon_videoparams_set_height(params: OakVideoParams, height: c_int);
	/// `oakcommon_videoparams_set_format`.
	pub fn oakcommon_videoparams_set_format(params: OakVideoParams, format: c_int);
	/// `oakcommon_videoparams_get_is_valid`.
	#[link_name = "oakcommon_videoparams_get_is_valid"]
	fn videoparams_get_is_valid_abi(params: OakVideoParams, is_valid: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_equals`.
	#[link_name = "oakcommon_videoparams_equals"]
	fn videoparams_equals_abi(
		params: OakVideoParams,
		other: OakVideoParams,
		equal: *mut c_int,
	) -> c_int;
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
	#[link_name = "oakcommon_videoparams_static_get_bytes_per_pixel"]
	fn videoparams_static_get_bytes_per_pixel_abi(format: c_int, channels: c_int) -> c_int;
	/// `oakcommon_videoparams_frame_rate_as_time_base`.
	pub fn oakcommon_videoparams_frame_rate_as_time_base(
		frame_rate_num: i64,
		frame_rate_den: i64,
		out_num: *mut i64,
		out_den: *mut i64,
	);
	/// `oakcommon_videoparams_get_stream_index`.
	#[link_name = "oakcommon_videoparams_get_stream_index"]
	fn videoparams_get_stream_index_abi(params: OakVideoParams, index: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_stream_index`.
	pub fn oakcommon_videoparams_set_stream_index(params: OakVideoParams, index: c_int);
	/// `oakcommon_videoparams_get_divider`.
	#[link_name = "oakcommon_videoparams_get_divider"]
	fn videoparams_get_divider_abi(params: OakVideoParams, divider: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_divider`.
	pub fn oakcommon_videoparams_set_divider(params: OakVideoParams, divider: c_int);
	/// `oakcommon_videoparams_get_frame_rate` (frame-rate num/den out).
	#[link_name = "oakcommon_videoparams_get_frame_rate"]
	fn videoparams_get_frame_rate_abi(
		params: OakVideoParams,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_duration` (time-base units).
	#[link_name = "oakcommon_videoparams_get_duration"]
	fn videoparams_get_duration_abi(params: OakVideoParams, duration: *mut i64) -> c_int;
	/// `oakcommon_videoparams_get_channel_count`.
	#[link_name = "oakcommon_videoparams_get_channel_count"]
	fn videoparams_get_channel_count_abi(params: OakVideoParams, count: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_color_primaries`.
	#[link_name = "oakcommon_videoparams_get_color_primaries"]
	fn videoparams_get_color_primaries_abi(params: OakVideoParams, primaries: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_color_transfer`.
	#[link_name = "oakcommon_videoparams_get_color_transfer"]
	fn videoparams_get_color_transfer_abi(params: OakVideoParams, transfer: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_interlacing` (`Interlacing` value).
	#[link_name = "oakcommon_videoparams_get_interlacing"]
	fn videoparams_get_interlacing_abi(params: OakVideoParams, interlacing: *mut c_int) -> c_int;
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
	#[link_name = "oakcommon_subtitleparams_get_stream_index"]
	fn subtitleparams_get_stream_index_abi(
		params: OakSubtitleParams,
		index: *mut c_int,
	) -> c_int;
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
		fallback: c_int,
	) -> c_int;
	/// `oakcommon_config_get_bool`.
	pub fn oakcommon_config_get_bool(
		group: *const c_char,
		key: *const c_char,
		fallback: c_int,
	) -> c_int;
	/// `oakcommon_config_get` (two-stage string access).
	pub fn oakcommon_config_get(
		group: *const c_char,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_filefunctions_init`.
	pub fn oakcommon_filefunctions_init() -> OakFileFunctions;
	/// `oakcommon_filefunctions_free`.
	pub fn oakcommon_filefunctions_free(self_: *mut OakFileFunctions);
	/// `oakcommon_filefunctions_get_configuration_location` (two-stage).
	#[link_name = "oakcommon_filefunctions_get_configuration_location"]
	fn filefunctions_get_configuration_location_abi(
		self_: OakFileFunctions,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_filefunctions_get_unique_file_identifier`.
	#[link_name = "oakcommon_filefunctions_get_unique_file_identifier"]
	fn filefunctions_get_unique_file_identifier_abi(
		self_: OakFileFunctions,
		filename: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_filefunctions_get_application_path` (two-stage).
	#[link_name = "oakcommon_filefunctions_get_application_path"]
	fn filefunctions_get_application_path_abi(
		self_: OakFileFunctions,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_colortransform_init_output`.
	pub fn oakcommon_colortransform_init_output(output: *const c_char) -> OakVideoParams;
	/// `oakcommon_colortransform_get_output` (two-stage string).
	pub fn oakcommon_colortransform_get_output(
		transform: OakVideoParams,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_colortransform_free`.
	pub fn oakcommon_colortransform_free(params: *mut OakVideoParams);
	/// `oakcommon_ffmpegutils_get_native_sample_format`.
	#[link_name = "oakcommon_ffmpegutils_get_native_sample_format"]
	fn ffmpegutils_get_native_sample_format_abi(smp_fmt: c_int, out: *mut c_int) -> c_int;
	/// `oakcommon_ffmpegutils_get_compatible_pixel_format`.
	#[link_name = "oakcommon_ffmpegutils_get_compatible_pixel_format"]
	fn ffmpegutils_get_compatible_pixel_format_abi(pix_fmt: c_int, out: *mut c_int) -> c_int;
	/// `oakcommon_ffmpegutils_get_ffmpeg_pixel_format`.
	#[link_name = "oakcommon_ffmpegutils_get_ffmpeg_pixel_format"]
	fn ffmpegutils_get_ffmpeg_pixel_format_abi(
		pix_fmt: c_int,
		channel_count: c_int,
		out: *mut c_int,
	) -> c_int;
	/// `oakcommon_ffmpegutils_get_ffmpeg_sample_format`.
	#[link_name = "oakcommon_ffmpegutils_get_ffmpeg_sample_format"]
	fn ffmpegutils_get_ffmpeg_sample_format_abi(smp_fmt: c_int, out: *mut c_int) -> c_int;
	/// `oakcommon_ffmpegutils_get_compatible_bridge_pixel_format`.
	#[link_name = "oakcommon_ffmpegutils_get_compatible_bridge_pixel_format"]
	fn ffmpegutils_get_compatible_bridge_pixel_format_abi(
		pix_fmt: c_int,
		maximum_pix_fmt: c_int,
		out: *mut c_int,
	) -> c_int;
	/// `oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space`.
	#[link_name = "oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space"]
	fn ffmpegutils_convert_jpeg_space_to_regular_space_abi(
		pix_fmt: c_int,
		out: *mut c_int,
	) -> c_int;
	/// `oakcommon_oiioutils_init`.
	pub fn oakcommon_oiioutils_init() -> OakOIIOUtils;
	/// `oakcommon_oiioutils_free`.
	pub fn oakcommon_oiioutils_free(self_: *mut OakOIIOUtils);
	/// `oakcommon_oiioutils_get_oiio_base_type_from_format`.
	#[link_name = "oakcommon_oiioutils_get_oiio_base_type_from_format"]
	fn oiioutils_get_oiio_base_type_from_format_abi(
		self_: OakOIIOUtils,
		pixel_format: c_int,
		out_base_type: *mut c_int,
	) -> c_int;
	/// `oakcommon_oiioutils_get_format_from_oiio_basetype`.
	#[link_name = "oakcommon_oiioutils_get_format_from_oiio_basetype"]
	fn oiioutils_get_format_from_oiio_basetype_abi(
		self_: OakOIIOUtils,
		base_type: c_int,
		out_pixel_format: *mut c_int,
	) -> c_int;
	/// `oakcommon_oiioutils_get_pixel_aspect_ratio` (num/den out).
	#[link_name = "oakcommon_oiioutils_get_pixel_aspect_ratio"]
	fn oiioutils_get_pixel_aspect_ratio_abi(
		self_: OakOIIOUtils,
		pixel_aspect_ratio: f64,
		out_numerator: *mut c_int,
		out_denominator: *mut c_int,
	) -> c_int;
}

// ---------------------------------------------------------------------------
// Value-style adapters over the frozen out-pointer ABI (old call shapes)
// ---------------------------------------------------------------------------

/// `oakcommon_videoparams_init_basic` — value-style shape kept for call
/// sites; defaults: U8 format, 4 channels, 1:1 aspect, progressive,
/// divider 1.
pub fn oakcommon_videoparams_init_basic(
	width: c_int,
	height: c_int,
	pixel_format: c_int,
	nb_channels: c_int,
	pixel_aspect_num: c_int,
	pixel_aspect_den: c_int,
	interlacing: c_int,
	divider: c_int,
) -> OakVideoParams {
	unsafe { videoparams_init_basic_abi(width, height, pixel_format, nb_channels, pixel_aspect_num, pixel_aspect_den, interlacing, divider) }
}

/// `oakcommon_videoparams_init_with_time_base` — value-style shape kept
/// for call sites.
pub fn oakcommon_videoparams_init_with_time_base(
	width: c_int,
	height: c_int,
	time_base_num: c_int,
	time_base_den: c_int,
	pixel_format: c_int,
	nb_channels: c_int,
	pixel_aspect_num: c_int,
	pixel_aspect_den: c_int,
	interlacing: c_int,
	divider: c_int,
) -> OakVideoParams {
	unsafe {
		videoparams_init_with_time_base_abi(
			width,
			height,
			time_base_num,
			time_base_den,
			pixel_format,
			nb_channels,
			pixel_aspect_num,
			pixel_aspect_den,
			interlacing,
			divider,
		)
	}
}

/// Value-style `oakcommon_videoparams_get_width`.
pub fn oakcommon_videoparams_get_width(params: OakVideoParams) -> c_int {
	let mut w: c_int = 0;
	unsafe { videoparams_get_width_abi(params, &mut w) };
	w
}

/// Value-style `oakcommon_videoparams_get_height`.
pub fn oakcommon_videoparams_get_height(params: OakVideoParams) -> c_int {
	let mut h: c_int = 0;
	unsafe { videoparams_get_height_abi(params, &mut h) };
	h
}

/// Value-style `oakcommon_videoparams_get_format`.
pub fn oakcommon_videoparams_get_format(params: OakVideoParams) -> c_int {
	let mut f: c_int = -1;
	unsafe { videoparams_get_format_abi(params, &mut f) };
	f
}

/// Value-style `oakcommon_videoparams_get_is_valid`.
pub fn oakcommon_videoparams_get_is_valid(params: OakVideoParams) -> c_int {
	let mut v: c_int = 0;
	unsafe { videoparams_get_is_valid_abi(params, &mut v) };
	v
}

/// Value-style `oakcommon_videoparams_equals` (1 when equal).
pub fn oakcommon_videoparams_equals(a: OakVideoParams, b: OakVideoParams) -> c_int {
	let mut eq: c_int = 0;
	unsafe { videoparams_equals_abi(a, b, &mut eq) };
	eq
}

/// Value-style `oakcommon_videoparams_get_stream_index`.
pub fn oakcommon_videoparams_get_stream_index(params: OakVideoParams) -> c_int {
	let mut i: c_int = -1;
	unsafe { videoparams_get_stream_index_abi(params, &mut i) };
	i
}

/// Value-style `oakcommon_videoparams_get_divider`.
pub fn oakcommon_videoparams_get_divider(params: OakVideoParams) -> c_int {
	let mut d: c_int = 0;
	unsafe { videoparams_get_divider_abi(params, &mut d) };
	d
}

/// Value-style `oakcommon_videoparams_get_duration`.
pub fn oakcommon_videoparams_get_duration(params: OakVideoParams) -> i64 {
	let mut d: i64 = 0;
	unsafe { videoparams_get_duration_abi(params, &mut d) };
	d
}

/// Value-style `oakcommon_videoparams_get_channel_count`.
pub fn oakcommon_videoparams_get_channel_count(params: OakVideoParams) -> c_int {
	let mut c: c_int = 0;
	unsafe { videoparams_get_channel_count_abi(params, &mut c) };
	c
}

/// Value-style `oakcommon_videoparams_get_color_primaries`.
pub fn oakcommon_videoparams_get_color_primaries(params: OakVideoParams) -> c_int {
	let mut p: c_int = 0;
	unsafe { videoparams_get_color_primaries_abi(params, &mut p) };
	p
}

/// Value-style `oakcommon_videoparams_get_color_transfer`.
pub fn oakcommon_videoparams_get_color_transfer(params: OakVideoParams) -> c_int {
	let mut t: c_int = 0;
	unsafe { videoparams_get_color_transfer_abi(params, &mut t) };
	t
}

/// Value-style `oakcommon_videoparams_get_interlacing`.
pub fn oakcommon_videoparams_get_interlacing(params: OakVideoParams) -> c_int {
	let mut i: c_int = 0;
	unsafe { videoparams_get_interlacing_abi(params, &mut i) };
	i
}

/// Value-style `oakcommon_videoparams_get_time_base` (num/den out as
/// `i64`, widened from the frozen `i32` outs).
pub fn oakcommon_videoparams_get_time_base(
	params: OakVideoParams,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	let mut num: c_int = 0;
	let mut den: c_int = 0;
	let rc = unsafe { videoparams_get_time_base_abi(params, &mut num, &mut den) };
	if !out_num.is_null() {
		unsafe { *out_num = num as i64 };
	}
	if !out_den.is_null() {
		unsafe { *out_den = den as i64 };
	}
	rc
}

/// Value-style `oakcommon_videoparams_get_frame_rate` (num/den out).
pub fn oakcommon_videoparams_get_frame_rate(
	params: OakVideoParams,
	out_num: *mut c_int,
	out_den: *mut c_int,
) -> c_int {
	unsafe { videoparams_get_frame_rate_abi(params, out_num, out_den) }
}

/// Value-style `oakcommon_videoparams_static_get_bytes_per_pixel`
/// (4 channels assumed; the frozen ABI takes channels explicitly).
pub fn oakcommon_videoparams_static_get_bytes_per_pixel(format: c_int) -> c_int {
	unsafe { videoparams_static_get_bytes_per_pixel_abi(format, 4) }
}

/// Value-style `oakcommon_subtitleparams_get_stream_index`.
pub fn oakcommon_subtitleparams_get_stream_index(params: OakSubtitleParams) -> c_int {
	let mut i: c_int = -1;
	unsafe { subtitleparams_get_stream_index_abi(params, &mut i) };
	i
}

/// Value-style `oakcommon_ffmpegutils_get_native_sample_format`.
pub fn oakcommon_ffmpegutils_get_native_sample_format(sample_format: c_int) -> c_int {
	let mut out: c_int = -1;
	unsafe { ffmpegutils_get_native_sample_format_abi(sample_format, &mut out) };
	out
}

/// Value-style `oakcommon_ffmpegutils_get_compatible_pixel_format`.
pub fn oakcommon_ffmpegutils_get_compatible_pixel_format(format: c_int) -> c_int {
	let mut out: c_int = -1;
	unsafe { ffmpegutils_get_compatible_pixel_format_abi(format, &mut out) };
	out
}

/// Value-style `oakcommon_ffmpegutils_get_ffmpeg_pixel_format`
/// (4 channels assumed).
pub fn oakcommon_ffmpegutils_get_ffmpeg_pixel_format(format: c_int) -> c_int {
	let mut out: c_int = -1;
	unsafe { ffmpegutils_get_ffmpeg_pixel_format_abi(format, 4, &mut out) };
	out
}

/// Value-style `oakcommon_ffmpegutils_get_ffmpeg_sample_format`.
pub fn oakcommon_ffmpegutils_get_ffmpeg_sample_format(format: c_int) -> c_int {
	let mut out: c_int = -1;
	unsafe { ffmpegutils_get_ffmpeg_sample_format_abi(format, &mut out) };
	out
}

/// Value-style `oakcommon_ffmpegutils_get_compatible_bridge_pixel_format`
/// (no maximum constraint).
pub fn oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(format: c_int) -> c_int {
	let mut out: c_int = -1;
	unsafe { ffmpegutils_get_compatible_bridge_pixel_format_abi(format, -1, &mut out) };
	out
}

/// Value-style `oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space`.
pub fn oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(format: c_int) -> c_int {
	let mut out: c_int = -1;
	unsafe { ffmpegutils_convert_jpeg_space_to_regular_space_abi(format, &mut out) };
	out
}

/// The process-wide filefunctions instance (lazily created; freed on
/// process exit is not required — the singleton outlives all users).
fn filefunctions_instance() -> OakFileFunctions {
	static FF: std::sync::OnceLock<OakFileFunctions> = std::sync::OnceLock::new();
	*FF.get_or_init(|| unsafe { oakcommon_filefunctions_init() })
}

/// Value-style `oakcommon_filefunctions_get_application_path`
/// (two-stage string getter on the shared instance).
pub fn oakcommon_filefunctions_get_application_path(buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { filefunctions_get_application_path_abi(filefunctions_instance(), buf, buf_size) }
}

/// Value-style `oakcommon_filefunctions_get_configuration_location`.
pub fn oakcommon_filefunctions_get_configuration_location(
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe {
		filefunctions_get_configuration_location_abi(filefunctions_instance(), buf, buf_size)
	}
}

/// Value-style `oakcommon_filefunctions_get_unique_file_identifier`
/// (the frozen ABI writes a string; the value-style call sites expect an
/// `i64`, so the string is folded into a stable 64-bit FNV hash).
pub fn oakcommon_filefunctions_get_unique_file_identifier(path: *const c_char) -> i64 {
	let mut buf = [0 as c_char; 512];
	// Two-stage contract: 0 = written, positive = required size, negative
	// = error. The adapter's value-style call sites get the i64 fold.
	let n = unsafe {
		filefunctions_get_unique_file_identifier_abi(
			filefunctions_instance(),
			path,
			buf.as_mut_ptr(),
			buf.len() as c_int,
		)
	};
	if n < 0 {
		return 0;
	}
	let avail = if n == 0 { buf.len() } else { (n as usize).min(buf.len()) };
	let bytes: Vec<u8> = buf[..avail]
		.iter()
		.take_while(|&&b| b != 0)
		.map(|&b| b as u8)
		.collect();
	let mut hash: u64 = 0xcbf29ce484222325;
	for &b in &bytes {
		hash ^= b as u64;
		hash = hash.wrapping_mul(0x100000001b3);
	}
	hash as i64
}

/// The process-wide oiioutils instance (lazily created).
fn oiioutils_instance() -> OakOIIOUtils {
	static OU: std::sync::OnceLock<OakOIIOUtils> = std::sync::OnceLock::new();
	*OU.get_or_init(|| unsafe { oakcommon_oiioutils_init() })
}

/// Value-style `oakcommon_oiioutils_get_oiio_base_type_from_format`.
pub fn oakcommon_oiioutils_get_oiio_base_type_from_format(format: c_int) -> c_int {
	let mut out: c_int = -1;
	unsafe { oiioutils_get_oiio_base_type_from_format_abi(oiioutils_instance(), format, &mut out) };
	out
}

/// Value-style `oakcommon_oiioutils_get_format_from_oiio_basetype`.
pub fn oakcommon_oiioutils_get_format_from_oiio_basetype(basetype: c_int) -> c_int {
	let mut out: c_int = -1;
	unsafe { oiioutils_get_format_from_oiio_basetype_abi(oiioutils_instance(), basetype, &mut out) };
	out
}

/// Value-style `oakcommon_oiioutils_get_pixel_aspect_ratio`
/// (the frozen ABI takes the aspect ratio as `f64`; the value-style call
/// sites pass width/height and get num/den back).
pub fn oakcommon_oiioutils_get_pixel_aspect_ratio(
	width: c_int,
	height: c_int,
	out_num: *mut c_int,
	out_den: *mut c_int,
) -> c_int {
	let ratio = if height != 0 {
		f64::from(width) / f64::from(height)
	} else {
		1.0
	};
	unsafe {
		oiioutils_get_pixel_aspect_ratio_abi(oiioutils_instance(), ratio, out_num, out_den)
	}
}
