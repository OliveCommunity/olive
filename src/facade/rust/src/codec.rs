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
//! Two parts:
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
//!
//! Presets, preset load/save and the sequence-bound last-used/export
//! entry points are deferred (see the stubs below and `deferred.rs`).

use std::collections::HashMap;
use std::ffi::{c_char, c_int, c_void};

use crate::bridge::codec as k;
use crate::bridge::codec::EncodingParamsPOD;
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
}

impl ParamsBox {
	fn new() -> Self {
		let mut pod = EncodingParamsPOD::zeroed();
		pod.format = -1; // unset (POD 0 is a valid format, DNxHD)
		ParamsBox {
			pod,
			video_options: HashMap::new(),
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
fn field_str(field: &[c_char]) -> String {
	let bytes: Vec<u8> = field
		.iter()
		.take_while(|c| **c != 0)
		.map(|c| *c as u8)
		.collect();
	String::from_utf8_lossy(&bytes).into_owned()
}

/// Write a string into a fixed array field (truncated, NUL-terminated).
fn write_field(field: &mut [c_char], value: &str) {
	for (i, slot) in field.iter_mut().enumerate() {
		*slot = if i < value.len() {
			value.as_bytes()[i] as c_char
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
pub extern "C" fn oakengine_encoding_format_subtitle_codec_at(format: c_int, index: c_int) -> c_int {
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
pub unsafe extern "C" fn oakengine_encoding_pix_fmt_index(codec: c_int, pix_fmt: *const c_char) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_pix_fmt_index(codec, pix_fmt) }))
}

/// `oakengine_encoding_sample_format_count` (-1 invalid).
#[no_mangle]
pub extern "C" fn oakengine_encoding_sample_format_count(format: c_int, codec: c_int) -> c_int {
	guard_int(|| Ok(unsafe { k::oakcodec_encoding_sample_format_count(format, codec) }))
}

/// `oakengine_encoding_sample_format_at` (-1 out of range).
#[no_mangle]
pub extern "C" fn oakengine_encoding_sample_format_at(format: c_int, codec: c_int, index: c_int) -> c_int {
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
		Ok(if p.pod.video_enabled != 0 || p.pod.audio_enabled != 0 || p.pod.subtitles_enabled != 0 {
			1
		} else {
			0
		})
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
		Ok(crate::handle::write_string(&field_str(&p.pod.filename), buf, buf_size))
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
		p.pod.video_pixel_format = v.format;
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
		p.pod.audio_sample_format = sample_format;
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
		(*out).format = p.pod.video_pixel_format;
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
			*sample_format = p.pod.audio_sample_format;
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

params_i64_field!(oakengine_encoding_params_set_video_bit_rate, oakengine_encoding_params_video_bit_rate, video_bit_rate);
params_i64_field!(oakengine_encoding_params_set_video_min_bit_rate, oakengine_encoding_params_video_min_bit_rate, video_min_bit_rate);
params_i64_field!(oakengine_encoding_params_set_video_max_bit_rate, oakengine_encoding_params_video_max_bit_rate, video_max_bit_rate);
params_i64_field!(oakengine_encoding_params_set_video_buffer_size, oakengine_encoding_params_video_buffer_size, video_buffer_size);
params_i64_field!(oakengine_encoding_params_set_audio_bit_rate, oakengine_encoding_params_audio_bit_rate, audio_bit_rate);

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
		Ok(crate::handle::write_string(&field_str(&p.pod.video_pix_fmt), buf, buf_size))
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
		p.pod.video_scaling_method = method;
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
		Ok(p.pod.video_scaling_method)
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
		p.video_options
			.insert(crate::handle::read_cstr(key), crate::handle::read_cstr(value));
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
pub unsafe extern "C" fn oakengine_encoding_preset_path(_buf: *mut c_char, _buf_size: c_int) -> c_int {
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

/// `oakengine_export_render_with_params` — **not backed** (the exporter
/// family is facade-only; see `deferred.rs`). Returns OAKENGINE_E_FAILED.
#[no_mangle]
pub unsafe extern "C" fn oakengine_export_render_with_params(
	_seq: *mut crate::handle::OakEngineSequence,
	_params: *const OakEngineEncodingParams,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
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
		let rc = crate::bridge::audio::oakaudio_manager_start_recording(
			m,
			&p.pod as *const EncodingParamsPOD as *const c_void as *const crate::bridge::audio::EncodingParams,
			errbuf,
			errbuf_size,
		);
		Error::from_module(rc)
	})
}
