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

//! oakcommon / oakcore C ABI imports. The task module reaches the shared
//! value types through `include/common/videoparams.h`,
//! `include/common/colortransform.h` and the oakcore audio-params C ABI.
//! Signatures mirror the headers verbatim.

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

/// Mirror of `OakVideoParams` (`include/common/videoparams.h`).
///
/// A by-value handle (shared_ptr semantics); its `{ctx, addref, release,
/// abi_version}` layout is identical to [`CHandle`], so it is a type alias —
/// every oakcommon handle is created and destroyed inside the oakcommon DLL
/// and only crosses the FFI boundary as this opaque by-value struct.
pub type OakVideoParams = CHandle;

/// Mirror of `OakColorTransform` (`include/common/colortransform.h`); see
/// [`OakVideoParams`] for the alias rationale.
pub type OakColorTransform = CHandle;

/// Mirror of `OakAudioParams` — oakcore audio parameters. Declared as a
/// `CHandle`-shaped opaque; the oakcore C ABI header that owns its layout is
/// the authoritative source once the oakcore C ABI ships.
pub type OakAudioParams = CHandle;

/// `OAKCOMMON_VIDEO_TYPE_STILL` (`include/common/videoparams.h`).
pub const OAKCOMMON_VIDEO_TYPE_STILL: c_int = 1;
/// `OAKCOMMON_VIDEO_TYPE_IMAGE_SEQUENCE` (`include/common/videoparams.h`).
pub const OAKCOMMON_VIDEO_TYPE_IMAGE_SEQUENCE: c_int = 2;

extern "C" {
	// --- videoparams.h ---
	/// `oakcommon_videoparams_init`.
	pub fn oakcommon_videoparams_init() -> OakVideoParams;
	/// `oakcommon_videoparams_init_basic`.
	pub fn oakcommon_videoparams_init_basic(
		width: c_int,
		height: c_int,
		pixel_format: c_int,
		nb_channels: c_int,
		pixel_aspect_num: c_int,
		pixel_aspect_den: c_int,
		interlacing: c_int,
		divider: c_int,
	) -> OakVideoParams;
	/// `oakcommon_videoparams_free`.
	pub fn oakcommon_videoparams_free(params: *mut OakVideoParams);
	/// `oakcommon_videoparams_get_width`.
	pub fn oakcommon_videoparams_get_width(params: OakVideoParams, width: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_height`.
	pub fn oakcommon_videoparams_get_height(params: OakVideoParams, height: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_format`.
	pub fn oakcommon_videoparams_get_format(params: OakVideoParams, format: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_format`.
	pub fn oakcommon_videoparams_set_format(params: OakVideoParams, format: c_int) -> c_int;
	/// `oakcommon_videoparams_get_time_base`.
	pub fn oakcommon_videoparams_get_time_base(
		params: OakVideoParams,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_set_time_base`.
	pub fn oakcommon_videoparams_set_time_base(
		params: OakVideoParams,
		numerator: c_int,
		denominator: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_frame_rate_as_time_base` — frame rate flipped
	/// to a frame duration rational.
	pub fn oakcommon_videoparams_frame_rate_as_time_base(
		params: OakVideoParams,
		out_num: *mut c_int,
		out_den: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_set_frame_rate`.
	pub fn oakcommon_videoparams_set_frame_rate(
		params: OakVideoParams,
		numerator: c_int,
		denominator: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_video_type`.
	pub fn oakcommon_videoparams_get_video_type(params: OakVideoParams, out_type: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_video_type`.
	pub fn oakcommon_videoparams_set_video_type(params: OakVideoParams, video_type: c_int) -> c_int;
	/// `oakcommon_videoparams_set_start_time`.
	pub fn oakcommon_videoparams_set_start_time(params: OakVideoParams, start: i64) -> c_int;
	/// `oakcommon_videoparams_set_duration`.
	pub fn oakcommon_videoparams_set_duration(params: OakVideoParams, duration: i64) -> c_int;
	/// `oakcommon_videoparams_get_is_valid`.
	pub fn oakcommon_videoparams_get_is_valid(params: OakVideoParams, out_valid: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_frame_rate`.
	pub fn oakcommon_videoparams_get_frame_rate(
		params: OakVideoParams,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_duration`.
	pub fn oakcommon_videoparams_get_duration(params: OakVideoParams, duration: *mut i64) -> c_int;

	// --- colortransform.h ---
	/// `oakcommon_colortransform_init_display`.
	pub fn oakcommon_colortransform_init_display(
		display: *const c_char,
		view: *const c_char,
		look: *const c_char,
	) -> OakColorTransform;
	/// `oakcommon_colortransform_init_output`.
	pub fn oakcommon_colortransform_init_output(
		output: *const c_char,
		display: *const c_char,
		view: *const c_char,
		look: *const c_char,
	) -> OakColorTransform;
	/// `oakcommon_colortransform_free`.
	pub fn oakcommon_colortransform_free(transform: *mut OakColorTransform);

	// --- config.h ---
	/// `oakcommon_config_get` (buf/size getter; returns needed size).
	pub fn oakcommon_config_get(
		group: *const c_char,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_config_get_int`.
	pub fn oakcommon_config_get_int(group: *const c_char, key: *const c_char, default: c_int) -> c_int;
	/// `oakcommon_config_get_bool`.
	pub fn oakcommon_config_get_bool(group: *const c_char, key: *const c_char, default: c_int) -> c_int;
}
