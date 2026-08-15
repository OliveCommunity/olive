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

//! `engine/include/oakengine/encoding.h` over the oakcodec module.
//!
//! Three parts:
//!
//! - The container/codec **metadata family** (format names/extensions,
//!   per-format codec lists, pixel/sample formats, filename helpers,
//!   transform matrix) maps directly onto the oakcodec crate's
//!   `oakcodec_encoding_*` exports (`include/codec/format.h`).
//! - The **encoding-params handle** is a facade-owned heap box over the
//!   `oakcodec_encoding_params` POD (`include/codec/encoder.h`): every
//!   engine getter/setter reads/writes a POD field, so the handle can be
//!   handed straight to `oakcodec_encoder_init` /
//!   `oakaudio_manager_start_recording`. The `format` field uses -1 as
//!   "unset" (the POD's 0 is a valid format, DNxHD); encoder-specific
//!   video options are kept in a facade-side map (the POD has no such
//!   field).
//! - The **exporter family** (`engine/include/oakengine/exporter.h`)
//!   assembles an encoding-params handle and drives the export task
//!   synchronously (see the family section at the bottom).
//!
//! Presets, preset load/save and the sequence-bound last-used entry
//! points remain deferred (see the stubs below and `deferred.rs`).

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::{c_char, c_double, c_int, c_void};

use crate::stubs::codec as k;
use crate::pods::{zeroed_encoding_params, EncodingParamsPOD};
use crate::common::OakVideoParamsPod;
use crate::error::{Error, Result};
use crate::handle::{guard, guard_int, string_result};

/// `engine/include/oakengine/encoding.h` — the opaque encoding-params
/// handle. The facade boxes a [`ParamsBox`] here.
pub struct OakEngineEncodingParams {
	_opaque: [u8; 0],
}

/// Facade-side box behind `OakEngineEncodingParams*`: the codec POD plus
/// the encoder-specific video options (key → value), which the POD cannot
/// carry.
struct ParamsBox {
	pod: EncodingParamsPOD,
	video_options: HashMap<String, String>,
	/// Raw scaling-method code exactly as the caller set it: the POD's
	/// `VideoScalingMethod` enum cannot carry garbage codes, and the
	/// facade contract accepts any `int` verbatim (round-trips 99 as 99).
	video_scaling_raw: c_int,
}

impl ParamsBox {
	fn new() -> Self {
		let mut pod = zeroed_encoding_params();
		pod.format = -1; // unset (POD 0 is a valid format, DNxHD)
		ParamsBox {
			pod,
			video_options: HashMap::new(),
			video_scaling_raw: 0,
		}
	}
}

/// Borrow the params box behind an engine handle (NULL → Invalid).
unsafe fn params_ref(ptr: *const OakEngineEncodingParams) -> Result<&'static ParamsBox> {
	unsafe {
		if ptr.is_null() {
			return Err(Error::Invalid);
		}
		Ok(&*(ptr as *const ParamsBox))
	}
}

/// Borrow the params box mutably.
unsafe fn params_mut(ptr: *mut OakEngineEncodingParams) -> Result<&'static mut ParamsBox> {
	unsafe {
		if ptr.is_null() {
			return Err(Error::Invalid);
		}
		Ok(&mut *(ptr as *mut ParamsBox))
	}
}

/// Read a NUL-terminated fixed array field as a `String`.
fn field_str(field: &[u8]) -> String {
	let bytes: Vec<u8> = field.iter().take_while(|c| **c != 0).copied().collect();
	String::from_utf8_lossy(&bytes).into_owned()
}

/// Write a string into a fixed array field (truncated, NUL-terminated).
fn write_field(field: &mut [u8], value: &str) {
	for (i, slot) in field.iter_mut().enumerate() {
		*slot = if i < value.len() {
			value.as_bytes()[i]
		} else {
			0
		};
	}
}

// ---------------------------------------------------------------------------
// Container format / codec metadata
// ---------------------------------------------------------------------------

/// `oakengine_encoding_format_count`.
#[no_mangle]
pub extern "C" fn oakengine_encoding_format_count() -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_format_count() }))
}

/// `oakengine_encoding_format_name` (buf/size; -1 invalid).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_format_name(
	format: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let rc = k::oakcodec_encoding_format_name(format, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_encoding_format_extension` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_format_extension(
	format: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let rc = k::oakcodec_encoding_format_extension(format, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_encoding_format_video_codec_count` (-1 invalid format).
#[no_mangle]
pub extern "C" fn oakengine_encoding_format_video_codec_count(format: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_format_video_codec_count(format) }))
}

/// `oakengine_encoding_format_video_codec_at` (-1 out of range).
#[no_mangle]
pub extern "C" fn oakengine_encoding_format_video_codec_at(format: c_int, index: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_format_video_codec_at(format, index) }))
}

/// `oakengine_encoding_format_audio_codec_count`.
#[no_mangle]
pub extern "C" fn oakengine_encoding_format_audio_codec_count(format: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_format_audio_codec_count(format) }))
}

/// `oakengine_encoding_format_audio_codec_at`.
#[no_mangle]
pub extern "C" fn oakengine_encoding_format_audio_codec_at(format: c_int, index: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_format_audio_codec_at(format, index) }))
}

/// `oakengine_encoding_format_subtitle_codec_count`.
#[no_mangle]
pub extern "C" fn oakengine_encoding_format_subtitle_codec_count(format: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_format_subtitle_codec_count(format) }))
}

/// `oakengine_encoding_format_subtitle_codec_at`.
#[no_mangle]
pub extern "C" fn oakengine_encoding_format_subtitle_codec_at(
	format: c_int,
	index: c_int,
) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_format_subtitle_codec_at(format, index) }))
}

/// `oakengine_encoding_codec_name` (buf/size; -1 invalid).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_codec_name(
	codec: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let rc = k::oakcodec_encoding_codec_name(codec, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_encoding_codec_is_still_image` (1/0).
#[no_mangle]
pub extern "C" fn oakengine_encoding_codec_is_still_image(codec: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_codec_is_still_image(codec) }))
}

/// `oakengine_encoding_codec_is_lossless` (1/0).
#[no_mangle]
pub extern "C" fn oakengine_encoding_codec_is_lossless(codec: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_codec_is_lossless(codec) }))
}

/// `oakengine_encoding_pix_fmt_count` (-1 invalid).
#[no_mangle]
pub extern "C" fn oakengine_encoding_pix_fmt_count(format: c_int, codec: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_pix_fmt_count(format, codec) }))
}

/// `oakengine_encoding_pix_fmt_at` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_pix_fmt_at(
	format: c_int,
	codec: c_int,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let rc = k::oakcodec_encoding_pix_fmt_at(format, codec, index, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_encoding_pix_fmt_index` (0 = preferred when absent).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_pix_fmt_index(
	codec: c_int,
	pix_fmt: *const c_char,
) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_pix_fmt_index(codec, pix_fmt) }))
}

/// `oakengine_encoding_sample_format_count` (-1 invalid).
#[no_mangle]
pub extern "C" fn oakengine_encoding_sample_format_count(format: c_int, codec: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_sample_format_count(format, codec) }))
}

/// `oakengine_encoding_sample_format_at` (-1 out of range).
#[no_mangle]
pub extern "C" fn oakengine_encoding_sample_format_at(
	format: c_int,
	codec: c_int,
	index: c_int,
) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_sample_format_at(format, codec, index) }))
}

/// `oakengine_encoding_filename_contains_digit_placeholder` (1/0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_filename_contains_digit_placeholder(
	filename: *const c_char,
) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_filename_contains_digit_placeholder(filename) }))
}

/// `oakengine_encoding_image_sequence_digit_count` (0 when none).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_image_sequence_digit_count(
	filename: *const c_char,
) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_image_sequence_digit_count(filename) }))
}

/// `oakengine_encoding_filename_remove_digit_placeholder` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_filename_remove_digit_placeholder(
	filename: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let rc = k::oakcodec_encoding_filename_remove_digit_placeholder(filename, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_encoding_generate_matrix` — fit/stretch/crop matrix into
/// `out16` (16 `f32`, QMatrix4x4 layout). The module computes in `f64`;
/// the facade narrows.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_generate_matrix(
	method: c_int,
	src_width: c_int,
	src_height: c_int,
	dest_width: c_int,
	dest_height: c_int,
	out16: *mut f32,
) -> c_int {
	guard(|| unsafe {
		if out16.is_null() {
			return Err(Error::Invalid);
		}
		let mut m = [0.0_f64; 16];
		Error::from_module(k::oakcodec_encoding_generate_matrix(
			method,
			src_width,
			src_height,
			dest_width,
			dest_height,
			m.as_mut_ptr(),
		))?;
		let m16 = out16 as *mut f32;
		for i in 0..16 {
			*m16.add(i) = m[i] as f32;
		}
		Ok(())
	})
}

// ---------------------------------------------------------------------------
// Encoding parameters handle
// ---------------------------------------------------------------------------

/// `oakengine_encoding_params_create` — empty handle (all tracks disabled,
/// format unset).
#[no_mangle]
pub extern "C" fn oakengine_encoding_params_create() -> *mut OakEngineEncodingParams {
	crate::handle::guard_ptr(|| {
		Ok(Box::into_raw(Box::new(ParamsBox::new())) as *mut OakEngineEncodingParams)
	})
}

/// `oakengine_encoding_params_destroy` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_destroy(params: *mut OakEngineEncodingParams) {
	crate::handle::guard_void(|| unsafe {
		if params.is_null() {
			return;
		}
		drop(Box::from_raw(params as *mut ParamsBox));
	})
}

/// `oakengine_encoding_params_is_valid` — 1 when any track is enabled.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_is_valid(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(
			if p.pod.video_enabled != 0 || p.pod.audio_enabled != 0 || p.pod.subtitles_enabled != 0
			{
				1
			} else {
				0
			},
		)
	})
}

/// `oakengine_encoding_params_set_filename`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_filename(
	params: *mut OakEngineEncodingParams,
	filename: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		if filename.is_null() {
			return Err(Error::Invalid);
		}
		write_field(&mut p.pod.filename, &crate::handle::read_cstr(filename));
		Ok(())
	})
}

/// `oakengine_encoding_params_filename` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_filename(
	params: *const OakEngineEncodingParams,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(crate::handle::write_string(
			&field_str(&p.pod.filename),
			buf,
			buf_size,
		))
	})
}

/// `oakengine_encoding_params_set_format` — rejects out-of-range values.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_format(
	params: *mut OakEngineEncodingParams,
	format: c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		let count = k::oakcodec_encoding_format_count();
		if format < 0 || format >= count {
			return Err(Error::Invalid);
		}
		p.pod.format = format;
		Ok(())
	})
}

/// `oakengine_encoding_params_format` — -1 when unset.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_format(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.format)
	})
}

/// `oakengine_encoding_params_enable_video` — copy the POD-carryable
/// fields of `video` and enable the video track. (The POD has no divider
/// field; documented deviation.)
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_enable_video(
	params: *mut OakEngineEncodingParams,
	video: *const OakVideoParamsPod,
	codec: c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		if video.is_null() {
			return Err(Error::Invalid);
		}
		let v = &*video;
		p.pod.video_enabled = 1;
		p.pod.video_codec = codec;
		p.pod.video_width = v.width;
		p.pod.video_height = v.height;
		p.pod.video_time_base_num = v.time_base_num;
		p.pod.video_time_base_den = v.time_base_den;
		p.pod.video_pixel_format = crate::pods::pixel_format_from_code(v.format);
		p.pod.video_interlacing = v.interlacing;
		p.pod.video_pixel_aspect_num = v.pixel_aspect_num;
		p.pod.video_pixel_aspect_den = v.pixel_aspect_den;
		Ok(())
	})
}

/// `oakengine_encoding_params_enable_audio`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_enable_audio(
	params: *mut OakEngineEncodingParams,
	sample_rate: c_int,
	channel_layout: u64,
	sample_format: c_int,
	codec: c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		p.pod.audio_enabled = 1;
		p.pod.audio_codec = codec;
		p.pod.audio_sample_rate = sample_rate;
		p.pod.audio_channel_layout = channel_layout;
		p.pod.audio_sample_format = crate::pods::sample_format_from_code(sample_format);
		Ok(())
	})
}

/// `oakengine_encoding_params_enable_subtitles`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_enable_subtitles(
	params: *mut OakEngineEncodingParams,
	codec: c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		p.pod.subtitles_enabled = 1;
		p.pod.subtitles_codec = codec;
		p.pod.subtitles_are_sidecar = 0;
		Ok(())
	})
}

/// `oakengine_encoding_params_enable_sidecar_subtitles`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_enable_sidecar_subtitles(
	params: *mut OakEngineEncodingParams,
	format: c_int,
	codec: c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		p.pod.subtitles_enabled = 1;
		p.pod.subtitles_codec = codec;
		p.pod.subtitles_are_sidecar = 1;
		p.pod.subtitles_sidecar_format = format;
		Ok(())
	})
}

/// `oakengine_encoding_params_disable_video`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_disable_video(
	params: *mut OakEngineEncodingParams,
) {
	crate::handle::guard_void(|| unsafe {
		if let Ok(p) = params_mut(params) {
			p.pod.video_enabled = 0;
		}
	})
}

/// `oakengine_encoding_params_disable_audio`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_disable_audio(
	params: *mut OakEngineEncodingParams,
) {
	crate::handle::guard_void(|| unsafe {
		if let Ok(p) = params_mut(params) {
			p.pod.audio_enabled = 0;
		}
	})
}

/// `oakengine_encoding_params_disable_subtitles`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_disable_subtitles(
	params: *mut OakEngineEncodingParams,
) {
	crate::handle::guard_void(|| unsafe {
		if let Ok(p) = params_mut(params) {
			p.pod.subtitles_enabled = 0;
		}
	})
}

/// `oakengine_encoding_params_video_enabled` (1/0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_video_enabled(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.video_enabled)
	})
}

/// `oakengine_encoding_params_video_codec`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_video_codec(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.video_codec)
	})
}

/// `oakengine_encoding_params_get_video_params` — OAKENGINE_E_STATE when
/// video is disabled.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_get_video_params(
	params: *const OakEngineEncodingParams,
	out: *mut OakVideoParamsPod,
) -> c_int {
	guard(|| unsafe {
		let p = params_ref(params)?;
		if p.pod.video_enabled == 0 {
			return Err(Error::State);
		}
		if out.is_null() {
			return Err(Error::Invalid);
		}
		(*out).width = p.pod.video_width;
		(*out).height = p.pod.video_height;
		(*out).time_base_num = p.pod.video_time_base_num;
		(*out).time_base_den = p.pod.video_time_base_den;
		(*out).format = p.pod.video_pixel_format as i32;
		(*out).interlacing = p.pod.video_interlacing;
		(*out).pixel_aspect_num = p.pod.video_pixel_aspect_num;
		(*out).pixel_aspect_den = p.pod.video_pixel_aspect_den;
		Ok(())
	})
}

/// `oakengine_encoding_params_audio_enabled` (1/0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_audio_enabled(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.audio_enabled)
	})
}

/// `oakengine_encoding_params_audio_codec`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_audio_codec(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.audio_codec)
	})
}

/// `oakengine_encoding_params_get_audio_params` — OAKENGINE_E_STATE when
/// audio is disabled.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_get_audio_params(
	params: *const OakEngineEncodingParams,
	sample_rate: *mut c_int,
	channel_layout: *mut u64,
	sample_format: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_ref(params)?;
		if p.pod.audio_enabled == 0 {
			return Err(Error::State);
		}
		if !sample_rate.is_null() {
			*sample_rate = p.pod.audio_sample_rate;
		}
		if !channel_layout.is_null() {
			*channel_layout = p.pod.audio_channel_layout;
		}
		if !sample_format.is_null() {
			*sample_format = p.pod.audio_sample_format as i32;
		}
		Ok(())
	})
}

/// `oakengine_encoding_params_subtitles_enabled` (1/0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_subtitles_enabled(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.subtitles_enabled)
	})
}

/// `oakengine_encoding_params_subtitles_are_sidecar` (1/0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_subtitles_are_sidecar(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.subtitles_are_sidecar)
	})
}

/// `oakengine_encoding_params_subtitles_sidecar_format`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_subtitles_sidecar_format(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.subtitles_sidecar_format)
	})
}

/// `oakengine_encoding_params_subtitles_codec`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_subtitles_codec(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.subtitles_codec)
	})
}

macro_rules! params_i64_field {
	($set:ident, $get:ident, $field:ident) => {
		/// Setter for an int64 params field (see the engine header).
		#[no_mangle]
		pub unsafe extern "C" fn $set(params: *mut OakEngineEncodingParams, value: i64) {
			crate::handle::guard_void(|| unsafe {
				if let Ok(p) = params_mut(params) {
					p.pod.$field = value;
				}
			})
		}

		/// Getter for an int64 params field (see the engine header).
		#[no_mangle]
		pub unsafe extern "C" fn $get(params: *const OakEngineEncodingParams) -> i64 {
			crate::handle::guard_i64(|| unsafe {
				let p = params_ref(params)?;
				Ok(p.pod.$field)
			})
		}
	};
}

params_i64_field!(
	oakengine_encoding_params_set_video_bit_rate,
	oakengine_encoding_params_video_bit_rate,
	video_bit_rate
);
params_i64_field!(
	oakengine_encoding_params_set_video_min_bit_rate,
	oakengine_encoding_params_video_min_bit_rate,
	video_min_bit_rate
);
params_i64_field!(
	oakengine_encoding_params_set_video_max_bit_rate,
	oakengine_encoding_params_video_max_bit_rate,
	video_max_bit_rate
);
params_i64_field!(
	oakengine_encoding_params_set_video_buffer_size,
	oakengine_encoding_params_video_buffer_size,
	video_buffer_size
);
params_i64_field!(
	oakengine_encoding_params_set_audio_bit_rate,
	oakengine_encoding_params_audio_bit_rate,
	audio_bit_rate
);

/// `oakengine_encoding_params_set_video_threads`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_video_threads(
	params: *mut OakEngineEncodingParams,
	threads: c_int,
) {
	crate::handle::guard_void(|| unsafe {
		if let Ok(p) = params_mut(params) {
			p.pod.video_threads = threads;
		}
	})
}

/// `oakengine_encoding_params_video_threads`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_video_threads(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.video_threads)
	})
}

/// `oakengine_encoding_params_set_video_pix_fmt`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_video_pix_fmt(
	params: *mut OakEngineEncodingParams,
	pix_fmt: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		if pix_fmt.is_null() {
			return Err(Error::Invalid);
		}
		write_field(&mut p.pod.video_pix_fmt, &crate::handle::read_cstr(pix_fmt));
		Ok(())
	})
}

/// `oakengine_encoding_params_video_pix_fmt` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_video_pix_fmt(
	params: *const OakEngineEncodingParams,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(crate::handle::write_string(
			&field_str(&p.pod.video_pix_fmt),
			buf,
			buf_size,
		))
	})
}

/// `oakengine_encoding_params_set_video_is_image_sequence`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_video_is_image_sequence(
	params: *mut OakEngineEncodingParams,
	is_image_sequence: c_int,
) {
	crate::handle::guard_void(|| unsafe {
		if let Ok(p) = params_mut(params) {
			p.pod.video_is_image_sequence = is_image_sequence;
		}
	})
}

/// `oakengine_encoding_params_video_is_image_sequence` (1/0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_video_is_image_sequence(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.video_is_image_sequence)
	})
}

/// `oakengine_encoding_params_set_color_transform`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_color_transform(
	params: *mut OakEngineEncodingParams,
	output_name: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		let name = if output_name.is_null() {
			String::new()
		} else {
			crate::handle::read_cstr(output_name)
		};
		write_field(&mut p.pod.color_transform_output, &name);
		Ok(())
	})
}

/// `oakengine_encoding_params_color_transform_output` (buf/size).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_color_transform_output(
	params: *const OakEngineEncodingParams,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(crate::handle::write_string(
			&field_str(&p.pod.color_transform_output),
			buf,
			buf_size,
		))
	})
}

/// `oakengine_encoding_params_set_export_length`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_export_length(
	params: *mut OakEngineEncodingParams,
	num: c_int,
	den: c_int,
) {
	crate::handle::guard_void(|| unsafe {
		if let Ok(p) = params_mut(params) {
			p.pod.export_length_num = num;
			p.pod.export_length_den = den;
		}
	})
}

/// `oakengine_encoding_params_get_export_length`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_get_export_length(
	params: *const OakEngineEncodingParams,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_ref(params)?;
		if !num.is_null() {
			*num = p.pod.export_length_num;
		}
		if !den.is_null() {
			*den = p.pod.export_length_den;
		}
		Ok(())
	})
}

/// `oakengine_encoding_params_set_custom_range`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_custom_range(
	params: *mut OakEngineEncodingParams,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
) {
	crate::handle::guard_void(|| unsafe {
		if let Ok(p) = params_mut(params) {
			p.pod.has_custom_range = 1;
			p.pod.custom_range_in_num = in_num;
			p.pod.custom_range_in_den = in_den;
			p.pod.custom_range_out_num = out_num;
			p.pod.custom_range_out_den = out_den;
		}
	})
}

/// `oakengine_encoding_params_has_custom_range` (1/0).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_has_custom_range(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.pod.has_custom_range)
	})
}

/// `oakengine_encoding_params_get_custom_range` — E_NOT_FOUND when no
/// custom range is set.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_get_custom_range(
	params: *const OakEngineEncodingParams,
	in_num: *mut i64,
	in_den: *mut i64,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	guard(|| unsafe {
		let p = params_ref(params)?;
		if p.pod.has_custom_range == 0 {
			return Err(Error::NotFound);
		}
		if !in_num.is_null() {
			*in_num = p.pod.custom_range_in_num;
		}
		if !in_den.is_null() {
			*in_den = p.pod.custom_range_in_den;
		}
		if !out_num.is_null() {
			*out_num = p.pod.custom_range_out_num;
		}
		if !out_den.is_null() {
			*out_den = p.pod.custom_range_out_den;
		}
		Ok(())
	})
}

/// `oakengine_encoding_params_set_video_scaling_method`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_video_scaling_method(
	params: *mut OakEngineEncodingParams,
	method: c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		// The raw code round-trips verbatim (garbage codes included);
		// the POD carries the nearest legal enum for the encoder.
		p.video_scaling_raw = method;
		p.pod.video_scaling_method = crate::pods::scaling_from_code(method);
		Ok(())
	})
}

/// `oakengine_encoding_params_video_scaling_method`.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_video_scaling_method(
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		Ok(p.video_scaling_raw)
	})
}

/// `oakengine_encoding_params_set_video_option` — stored in the
/// facade-side options map (the POD has no option fields).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_video_option(
	params: *mut OakEngineEncodingParams,
	key: *const c_char,
	value: *const c_char,
) -> c_int {
	guard(|| unsafe {
		let p = params_mut(params)?;
		if key.is_null() || value.is_null() {
			return Err(Error::Invalid);
		}
		p.video_options.insert(
			crate::handle::read_cstr(key),
			crate::handle::read_cstr(value),
		);
		Ok(())
	})
}

/// `oakengine_encoding_params_video_option` — would-be length, or
/// OAKENGINE_E_NOT_FOUND when the key is unset.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_video_option(
	params: *const OakEngineEncodingParams,
	key: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let p = params_ref(params)?;
		if key.is_null() {
			return Err(Error::Invalid);
		}
		match p.video_options.get(&crate::handle::read_cstr(key)) {
			Some(v) => Ok(crate::handle::write_string(v, buf, buf_size)),
			None => Err(Error::NotFound),
		}
	})
}

// ---------------------------------------------------------------------------
// Presets / load-save / sequence-bound entry points (deferred)
// ---------------------------------------------------------------------------

/// `oakengine_encoding_preset_path` — **not backed** (no preset API in the
/// oakcodec crate). Returns OAKENGINE_E_FAILED.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_preset_path(
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_encoding_preset_count` — **not backed**. Returns 0.
#[no_mangle]
pub extern "C" fn oakengine_encoding_preset_count() -> c_int {
	0
}

/// `oakengine_encoding_preset_name` — **not backed**. Returns
/// OAKENGINE_E_FAILED.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_preset_name(
	_index: c_int,
	_buf: *mut c_char,
	_buf_size: c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_encoding_params_load_file` — **not backed** (no params
/// load/save in the oakcodec crate). Returns OAKENGINE_E_FAILED.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_load_file(
	_params: *mut OakEngineEncodingParams,
	_path: *const c_char,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_encoding_params_save_file` — **not backed**. Returns
/// OAKENGINE_E_FAILED.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_save_file(
	_params: *const OakEngineEncodingParams,
	_path: *const c_char,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

// ---------------------------------------------------------------------------
// Exporter family (exporter.h)
// ---------------------------------------------------------------------------

// Thread-local reason for the last failed export on this thread (the C++
// `g_last_error`, `engine/src/capi/export.cpp`). Cleared at the start of
// every export call; read by [`oakengine_export_last_error`].
thread_local! {
	static EXPORT_LAST_ERROR: RefCell<String> = const { RefCell::new(String::new()) };
}

// Thread-local progress callback installed by
// [`oakengine_export_set_progress_callback`] (the C++ `g_progress_fn` /
// `g_progress_userdata`). Per-thread like the C++: the synchronous export
// runs on the installing thread, so the module task events arrive there.
thread_local! {
	static EXPORT_PROGRESS: RefCell<
		Option<(unsafe extern "C" fn(c_double, *mut c_void), *mut c_void)>,
	> = const { RefCell::new(None) };
}

/// The module task progress event id (`OAKTASK_EVENT_PROGRESS`, see
/// `oakengine_task_subscribe`).
const EXPORT_EVENT_PROGRESS: c_int = 1;

fn export_last_error_set(msg: String) {
	EXPORT_LAST_ERROR.with(|e| *e.borrow_mut() = msg);
}

/// Forward the progress events of a running export to the installed
/// callback. Installed as the task subscription only while a callback is
/// set; the module passes the callback's own `userdata` through.
unsafe extern "C" fn export_progress_event(event_id: c_int, value: f64, userdata: *mut c_void) {
	if event_id != EXPORT_EVENT_PROGRESS {
		return;
	}
	EXPORT_PROGRESS.with(|slot| {
		if let Some((cb, _)) = *slot.borrow() {
			// SAFETY: the callback + userdata follow the installer's
			// contract; the task emits on its running thread.
			unsafe { cb(value, userdata) };
		}
	});
}

/// Run an export task synchronously on the calling thread — the shared
/// tail of every exporter-family entry point: create the task (taking
/// ownership of `params`), subscribe the installed progress callback, run
/// through [`oakengine_task_start_sync`], read the task error into the
/// thread-local last-error slot, and free the task.
///
/// Returns OAKENGINE_OK on success, OAKENGINE_E_FAILED otherwise. On the
/// task-creation failure path `params` ownership stays with the caller
/// (mirroring [`oakengine_task_create_export`]).
fn export_run_sync(seq: *mut crate::handle::OakEngineSequence, params: *mut OakEngineEncodingParams) -> c_int {
	let task = unsafe { crate::task::oakengine_task_create_export(seq, params) };
	if task.is_null() {
		export_last_error_set("failed to create the export task".into());
		return crate::error::OAKENGINE_E_FAILED;
	}
	// Progress events through the same module subscription the app's
	// `start_export` uses (`oakengine_task_subscribe`).
	EXPORT_PROGRESS.with(|slot| {
		if let Some((_, userdata)) = *slot.borrow() {
			unsafe {
				crate::task::oakengine_task_subscribe(task, Some(export_progress_event), userdata);
			}
		}
	});
	let ok = unsafe { crate::task::oakengine_task_start_sync(task) };
	let rc = if ok == 1 {
		crate::error::OAKENGINE_OK
	} else {
		let err = export_task_error(task);
		export_last_error_set(if err.is_empty() {
			"export failed".into()
		} else {
			err
		});
		crate::error::OAKENGINE_E_FAILED
	};
	unsafe { crate::task::oakengine_task_free(task) };
	rc
}

/// Two-stage read of a task's error string (empty when none).
fn export_task_error(task: *mut crate::handle::OakEngineTask) -> String {
	unsafe {
		let needed = crate::task::oakengine_task_error(task, std::ptr::null_mut(), 0);
		if needed <= 0 {
			return String::new();
		}
		let mut buf = vec![0 as c_char; needed as usize + 1];
		let n = crate::task::oakengine_task_error(task, buf.as_mut_ptr(), buf.len() as c_int);
		if n < 0 {
			return String::new();
		}
		let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
		String::from_utf8_lossy(unsafe {
			std::slice::from_raw_parts(buf.as_ptr() as *const u8, len)
		})
		.into_owned()
	}
}

/// `oakengine_export_render_with_params` — render `seq` to the file the
/// encoding params describe, through the same synchronous export path the
/// app's `start_export` drives (`oakengine_task_create_export` +
/// `oakengine_task_start_sync` + free).
///
/// Takes ownership of `params` on success (destroyed with the export
/// task, mirroring [`oakengine_task_create_export`]); on the
/// task-creation failure path the caller keeps ownership. The sequence
/// handle is validated for non-NULL only — the C++ "handle is a sequence
/// of the active project" walk has no Rust analogue (created sequences
/// live in their own scratch project, see `oakengine_sequence_new`).
///
/// Returns OAKENGINE_OK on success; OAKENGINE_E_INVALID for NULL
/// arguments; OAKENGINE_E_FAILED for creation/run failures (see
/// [`oakengine_export_last_error`]).
#[no_mangle]
pub unsafe extern "C" fn oakengine_export_render_with_params(
	seq: *mut crate::handle::OakEngineSequence,
	params: *const OakEngineEncodingParams,
) -> c_int {
	guard(|| unsafe {
		export_last_error_set(String::new());
		if seq.is_null() || params.is_null() {
			export_last_error_set("invalid arguments".into());
			return Err(Error::Invalid);
		}
		if export_run_sync(seq, params as *mut OakEngineEncodingParams) == crate::error::OAKENGINE_OK {
			Ok(())
		} else {
			Err(Error::Failed("export failed".into()))
		}
	})
}

/// `oakengine_export_render` — render `seq`'s [in_ts, out_ts) range
/// offline and encode it to `path`.
///
/// `in_ts`/`out_ts` are frame timestamps in the sequence's frame-rate
/// timebase (the export frame rate is the sequence frame rate). `width`/
/// `height` <= 0 fall back to the sequence's video dimensions; when they
/// differ the frames are scaled to fit. Video is encoded with the
/// options' codec (default H.264 in an MP4 container), audio with the
/// options' codec (default AAC) at the requested rate/layout (defaults:
/// 48 kHz stereo — the engine has no sequence-audio getter, so the
/// header's "sequence rate/layout" fallback mirrors the app's export
/// dialog instead). The options' codec fields carry the exporter.h
/// `OAKENGINE_EXPORT_VIDEO_*` / `OAKENGINE_EXPORT_AUDIO_*` values,
/// mapped here onto the engine's `ExportFormat` / `ExportCodec` ids.
///
/// The call blocks until the export finishes; progress is reported
/// through the callback set with [`oakengine_export_set_progress_callback`].
///
/// Deviations from the C++ header: no `OAKENGINE_INIT_RENDER`
/// requirement (the Rust render path is CPU-only and self-contained, see
/// `oakengine_render_manager_init`) and no "sequence is part of a
/// project" check (created sequences live in a scratch project).
///
/// Returns OAKENGINE_OK on success; OAKENGINE_E_INVALID for bad
/// arguments; OAKENGINE_E_FAILED for render/encode failures (see
/// [`oakengine_export_last_error`]).
#[no_mangle]
pub unsafe extern "C" fn oakengine_export_render(
	seq: *mut crate::handle::OakEngineSequence,
	path: *const c_char,
	in_ts: i64,
	out_ts: i64,
	width: c_int,
	height: c_int,
	opts: *const crate::pods::OakExportOptions,
) -> c_int {
	guard(|| unsafe {
		export_last_error_set(String::new());
		if seq.is_null() || path.is_null() || in_ts < 0 || out_ts <= in_ts {
			export_last_error_set("invalid arguments".into());
			return Err(Error::Invalid);
		}
		let o = if opts.is_null() {
			crate::pods::OakExportOptions {
				video_codec: crate::pods::OAKENGINE_EXPORT_VIDEO_H264,
				audio_codec: crate::pods::OAKENGINE_EXPORT_AUDIO_AAC,
				video_bit_rate: 0,
				audio_sample_rate: 0,
				audio_channel_count: 0,
			}
		} else {
			*opts
		};

		// Map the exporter.h codec ids onto the engine's enum ids.
		let (format, vcodec) = match o.video_codec {
			crate::pods::OAKENGINE_EXPORT_VIDEO_H264 => (
				oakcodec::exportformat::Format::MPEG4Video as i32,
				oakcodec::exportcodec::Codec::H264 as i32,
			),
			crate::pods::OAKENGINE_EXPORT_VIDEO_H265 => (
				oakcodec::exportformat::Format::MPEG4Video as i32,
				oakcodec::exportcodec::Codec::H265 as i32,
			),
			crate::pods::OAKENGINE_EXPORT_VIDEO_PNG_SEQUENCE => (
				oakcodec::exportformat::Format::PNG as i32,
				oakcodec::exportcodec::Codec::PNG as i32,
			),
			_ => {
				export_last_error_set(format!("unknown video codec {}", o.video_codec));
				return Err(Error::Invalid);
			}
		};
		let audio_enabled = o.audio_codec != crate::pods::OAKENGINE_EXPORT_AUDIO_NONE;
		let acodec = if audio_enabled {
			match o.audio_codec {
				crate::pods::OAKENGINE_EXPORT_AUDIO_AAC => oakcodec::exportcodec::Codec::AAC as i32,
				crate::pods::OAKENGINE_EXPORT_AUDIO_PCM => oakcodec::exportcodec::Codec::PCM as i32,
				_ => {
					export_last_error_set(format!("unknown audio codec {}", o.audio_codec));
					return Err(Error::Invalid);
				}
			}
		} else {
			0
		};

		// Sequence geometry + frame rate (the export frame rate is the
		// sequence's).
		let mut sw: c_int = 0;
		let mut sh: c_int = 0;
		let mut par_num: c_int = 1;
		let mut par_den: c_int = 1;
		Error::from_module(crate::timeline::oakengine_sequence_get_video_params(
			seq,
			&mut sw,
			&mut sh,
			&mut par_num,
			&mut par_den,
		))?;
		let mut rate_num: c_int = 0;
		let mut rate_den: c_int = 1;
		Error::from_module(crate::timeline::oakengine_sequence_get_frame_rate(
			seq,
			&mut rate_num,
			&mut rate_den,
		))?;
		if rate_num <= 0 || rate_den <= 0 {
			export_last_error_set("sequence has no valid frame rate".into());
			return Err(Error::Invalid);
		}
		let out_w = if width > 0 { width } else { sw };
		let out_h = if height > 0 { height } else { sh };
		if out_w <= 0 || out_h <= 0 {
			export_last_error_set("sequence has no valid video dimensions".into());
			return Err(Error::Invalid);
		}
		let sample_rate = if o.audio_sample_rate > 0 { o.audio_sample_rate } else { 48000 };
		let layout: u64 = if o.audio_channel_count > 0 {
			match o.audio_channel_count {
				1 => 0x4, // AV_CH_LAYOUT_MONO
				2 => 0x3, // AV_CH_LAYOUT_STEREO
				n => {
					export_last_error_set(format!(
						"unsupported audio channel count {n} (1 = mono, 2 = stereo)"
					));
					return Err(Error::Invalid);
				}
			}
		} else {
			0x3
		};

		// Assemble the encoding params through the public setters (the same
		// path the app's `start_export` uses); the task consumes the handle
		// once created.
		let params = oakengine_encoding_params_create();
		if params.is_null() {
			export_last_error_set("failed to create encoding params".into());
			return Err(Error::Failed("failed to create encoding params".into()));
		}
		let fail = |msg: &str| -> Result<()> {
			oakengine_encoding_params_destroy(params);
			export_last_error_set(msg.into());
			Err(Error::Failed(msg.into()))
		};
		let cpath = std::ffi::CString::new(crate::handle::read_cstr(path))
			.map_err(|_| Error::Failed("invalid path (NUL byte)".into()))?;
		if oakengine_encoding_params_set_filename(params, cpath.as_ptr()) != 0 {
			return fail("failed to set the export filename");
		}
		if oakengine_encoding_params_set_format(params, format) != 0 {
			return fail("failed to set the export format");
		}
		let pod = OakVideoParamsPod {
			width: out_w,
			height: out_h,
			time_base_num: rate_den,
			time_base_den: rate_num,
			format: 0,
			pixel_aspect_num: par_num.max(1),
			pixel_aspect_den: par_den.max(1),
			interlacing: 0,
			color_range: 0,
			divider: 1,
			video_type: 0,
			premultiplied_alpha: 0,
		};
		if oakengine_encoding_params_enable_video(params, &pod, vcodec) != 0 {
			return fail("failed to enable video");
		}
		if audio_enabled && oakengine_encoding_params_enable_audio(params, sample_rate, layout, 0, acodec) != 0 {
			return fail("failed to enable audio");
		}
		if o.video_bit_rate > 0 {
			oakengine_encoding_params_set_video_bit_rate(params, o.video_bit_rate);
		}
		// Fit scaling (the header's documented behavior when the output
		// size differs from the sequence's).
		if oakengine_encoding_params_set_video_scaling_method(params, 0) != 0 {
			return fail("failed to set the video scaling method");
		}
		// Export range as seconds rationals: frame timestamps in the
		// sequence's frame-rate timebase (frame duration = rate_den/rate_num).
		let tb_num = i64::from(rate_den);
		let tb_den = i64::from(rate_num);
		oakengine_encoding_params_set_custom_range(params, in_ts * tb_num, tb_den, out_ts * tb_num, tb_den);
		oakengine_encoding_params_set_export_length(params, ((out_ts - in_ts) * tb_num) as c_int, rate_num);

		if export_run_sync(seq, params) == crate::error::OAKENGINE_OK {
			Ok(())
		} else {
			Err(Error::Failed("export failed".into()))
		}
	})
}

/// `oakengine_export_last_error` — the reason for the last failed export
/// on this thread (buf/size; empty after a successful export).
#[no_mangle]
pub unsafe extern "C" fn oakengine_export_last_error(buf: *mut c_char, buf_size: c_int) -> c_int {
	guard_int(|| {
		let err = EXPORT_LAST_ERROR.with(|e| e.borrow().clone());
		Ok(unsafe { crate::handle::write_string(&err, buf, buf_size) })
	})
}

/// `oakengine_export_set_progress_callback` — install the progress
/// callback used by subsequent [`oakengine_export_render`] /
/// [`oakengine_export_render_with_params`] calls on this thread (NULL
/// disables). The callback receives `fraction` in [0, 1] and is invoked
/// on the exporting thread during the synchronous run.
#[no_mangle]
pub unsafe extern "C" fn oakengine_export_set_progress_callback(
	f: Option<unsafe extern "C" fn(c_double, *mut c_void)>,
	userdata: *mut c_void,
) {
	crate::handle::guard_void(|| {
		EXPORT_PROGRESS.with(|slot| *slot.borrow_mut() = f.map(|cb| (cb, userdata)));
	});
}

/// `oakengine_encoding_params_get_last_used` — **not backed** (sequence
/// binding needs the deferred node/timeline families). Returns NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_get_last_used(
	_seq: *mut crate::handle::OakEngineSequence,
) -> *mut OakEngineEncodingParams {
	std::ptr::null_mut()
}

/// `oakengine_encoding_params_set_last_used` — **not backed** (NULL
/// no-op; non-NULL is ignored).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_params_set_last_used(
	_seq: *mut crate::handle::OakEngineSequence,
	_params: *const OakEngineEncodingParams,
) {
}

/// `oakengine_encoding_start_audio_recording` — hand the params POD to
/// the audio manager's recording entry point (oakaudio).
#[no_mangle]
pub unsafe extern "C" fn oakengine_encoding_start_audio_recording(
	params: *const OakEngineEncodingParams,
	errbuf: *mut c_char,
	errbuf_size: c_int,
) -> c_int {
	guard(|| unsafe {
		let p = params_ref(params)?;
		let m = crate::audio::audio_manager_handle_raw();
		if m.is_null() {
			return Err(Error::State);
		}
		let rc = crate::stubs::audio::oakaudio_manager_start_recording(
			m,
			&p.pod as *const crate::pods::EncodingParamsPOD,
			errbuf,
			errbuf_size,
		);
		Error::from_module(rc)
	})
}
