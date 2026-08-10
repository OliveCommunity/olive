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

//! `include/codec/encoder.h` exports.
//!
//! Complete inventory: init / free / set_video_option / open / write_video
//! / write_audio / write_subtitle / flush / last_error /
//! get_desired_pixel_format / export_format_get_extension /
//! encoding_generate_matrix.
//!
//! # CPP-PARITY
//! The C++ `c_api/encoder.cpp` box holds an `olive::Encoder` plus the
//! flattened `EncodingParams`; the Rust equivalent boxes `Mutex<EncoderBox>`
//! and carries an extra `last_error` field because the Rust `Encoder` trait
//! has no `get_error()` (the C++ reads the message off the encoder). The
//! `oakcodec_encoding_params` POD below mirrors the header verbatim; note
//! that the crate's `EncodingParams::video_pixel_format` / `audio_sample_format`
//! are typed enums, so `to_native` converts the int fields.

use std::ffi::{c_char, c_int};
use std::sync::{Arc, Mutex};

use oakcore_rs::PixelFormat;

use crate::encodingparams::EncodingParams;
use crate::encoder::Encoder;
use crate::handle::{self, CHandle};

/// `oakcodec_encoding_params` — flattened POD mirror of `include/codec/
/// encoder.h` (all fields; a zeroed struct describes an all-tracks-disabled
/// configuration). Field names and order mirror the header verbatim.
#[allow(missing_docs)]
#[repr(C)]
pub struct oakcodec_encoding_params {
	pub filename: [u8; 1024],
	pub format: c_int,
	pub video_enabled: c_int,
	pub video_codec: c_int,
	pub video_width: c_int,
	pub video_height: c_int,
	pub video_time_base_num: c_int,
	pub video_time_base_den: c_int,
	pub video_pixel_format: c_int,
	pub video_interlacing: c_int,
	pub video_pixel_aspect_num: c_int,
	pub video_pixel_aspect_den: c_int,
	pub video_bit_rate: i64,
	pub video_min_bit_rate: i64,
	pub video_max_bit_rate: i64,
	pub video_buffer_size: i64,
	pub video_threads: c_int,
	pub video_pix_fmt: [u8; 64],
	pub video_is_image_sequence: c_int,
	pub video_scaling_method: c_int,
	pub audio_enabled: c_int,
	pub audio_codec: c_int,
	pub audio_sample_rate: c_int,
	pub audio_channel_layout: u64,
	pub audio_sample_format: c_int,
	pub audio_bit_rate: i64,
	pub subtitles_enabled: c_int,
	pub subtitles_codec: c_int,
	pub subtitles_are_sidecar: c_int,
	pub subtitles_sidecar_format: c_int,
	pub color_transform_output: [u8; 256],
	pub export_length_num: c_int,
	pub export_length_den: c_int,
	pub has_custom_range: c_int,
	pub custom_range_in_num: i64,
	pub custom_range_in_den: i64,
	pub custom_range_out_num: i64,
	pub custom_range_out_den: i64,
}

/// Box behind an encoder handle (`EncoderBox` in `c_api/encoder.cpp`).
struct EncoderBox {
	encoder: Option<Arc<dyn Encoder>>,
	params: EncodingParams,
	/// Per-codec video options set via `oakcodec_encoder_set_video_option`
	/// between init and open. Kept here (not in [`EncodingParams`], which
	/// is a byte-exact mirror of the C POD).
	video_opts: Vec<(String, String)>,
	open: bool,
	flushed: bool,
	/// Last error detail (the C++ reads it off the encoder's `get_error`).
	last_error: String,
}

/// Convert an `OakPixelFormat` int code to a [`PixelFormat`].
fn pixel_format_from_i32(v: c_int) -> PixelFormat {
	match v {
		0 => PixelFormat::U8,
		1 => PixelFormat::U10,
		2 => PixelFormat::U16,
		3 => PixelFormat::F16,
		4 => PixelFormat::F32,
		_ => PixelFormat::Invalid,
	}
}

/// Flatten the C POD into the crate's [`EncodingParams`]
/// (`to_native` in `c_api/encoder.cpp`).
fn to_native(p: &oakcodec_encoding_params) -> EncodingParams {
	let mut n = EncodingParams::default();
	n.filename = p.filename;
	n.format = p.format;

	n.video_enabled = p.video_enabled;
	n.video_codec = p.video_codec;
	n.video_width = p.video_width;
	n.video_height = p.video_height;
	n.video_time_base_num = p.video_time_base_num;
	n.video_time_base_den = p.video_time_base_den;
	n.video_pixel_format = pixel_format_from_i32(p.video_pixel_format);
	n.video_interlacing = p.video_interlacing;
	n.video_pixel_aspect_num = p.video_pixel_aspect_num;
	n.video_pixel_aspect_den = p.video_pixel_aspect_den;
	n.video_bit_rate = p.video_bit_rate;
	n.video_min_bit_rate = p.video_min_bit_rate;
	n.video_max_bit_rate = p.video_max_bit_rate;
	n.video_buffer_size = p.video_buffer_size;
	n.video_threads = p.video_threads;
	n.video_pix_fmt = p.video_pix_fmt;
	n.video_is_image_sequence = p.video_is_image_sequence;
	n.video_scaling_method = crate::encodingparams::scaling_from_i32(p.video_scaling_method);

	n.audio_enabled = p.audio_enabled;
	n.audio_codec = p.audio_codec;
	n.audio_sample_rate = p.audio_sample_rate;
	n.audio_channel_layout = p.audio_channel_layout;
	n.audio_sample_format = crate::encodingparams::sample_format_from_i32(p.audio_sample_format);
	n.audio_bit_rate = p.audio_bit_rate;

	n.subtitles_enabled = p.subtitles_enabled;
	n.subtitles_codec = p.subtitles_codec;
	n.subtitles_are_sidecar = p.subtitles_are_sidecar;
	n.subtitles_sidecar_format = p.subtitles_sidecar_format;

	n.color_transform_output = p.color_transform_output;
	n.export_length_num = p.export_length_num;
	n.export_length_den = p.export_length_den;
	n.has_custom_range = p.has_custom_range;
	n.custom_range_in_num = p.custom_range_in_num;
	n.custom_range_in_den = p.custom_range_in_den;
	n.custom_range_out_num = p.custom_range_out_num;
	n.custom_range_out_den = p.custom_range_out_den;
	n
}

/// `EncodingParams::is_valid` — the C++ `src/codec/src/encoder.h` checks
/// only that at least one track is enabled.
fn is_valid(p: &EncodingParams) -> bool {
	p.video_enabled != 0 || p.audio_enabled != 0 || p.subtitles_enabled != 0
}

/// `oakcodec_encoder_init`: create an encoder for `params` (count 1);
/// empty handle when the configuration is invalid.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_init(params: *const oakcodec_encoding_params) -> CHandle {
	handle::guard_handle(|| {
		if params.is_null() {
			return Ok(CHandle::null());
		}
		let native = to_native(unsafe { &*params });
		if !is_valid(&native) {
			return Ok(CHandle::null());
		}
		Ok(handle::make_owned(Mutex::new(EncoderBox {
			encoder: None,
			params: native,
			video_opts: Vec::new(),
			open: false,
			flushed: false,
			last_error: String::new(),
		})))
	})
}

/// `oakcodec_encoder_free`: NULL/empty no-op; nulls `ctx` afterwards.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_free(encoder: *mut CHandle) {
	handle::guard_void(|| super::free_handle(encoder));
}

/// `oakcodec_encoder_set_video_option`: set a per-codec video option
/// (e.g. "crf"); only valid between init and open.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_set_video_option(
	encoder: CHandle,
	key: *const c_char,
	value: *const c_char,
) -> c_int {
	handle::guard(|| {
		let b = super::get_box::<Mutex<EncoderBox>>(&encoder).ok_or(crate::error::Error::Invalid)?;
		let key = match crate::ffi::c_str(key) {
			Some(k) => k,
			None => return Err(crate::error::Error::Invalid),
		};
		let mut b = b.lock().unwrap();
		if b.open {
			return Err(crate::error::Error::State);
		}
		let value = crate::ffi::c_str(value).unwrap_or_default();
		// `EncodingParams::set_video_option` replaces an existing key.
		b.video_opts.retain(|(k, _)| k != &key);
		b.video_opts.push((key, value));
		Ok(())
	})
}

/// `oakcodec_encoder_open`: create the encoder for the configured params,
/// apply the video options and open the output.
///
/// # CPP-PARITY
/// The C++ `open()` calls `create_from_params` (which applies the options
/// internally) then `open()`. The Rust trait separates `configure`, so it
/// is invoked between the two; the box's `video_opts` (set between init
/// and open) are stored for future wiring but not yet passed to
/// `configure` (no trait channel carries them in the interim).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_open(encoder: CHandle) -> c_int {
	handle::guard(|| {
		let b = super::get_box::<Mutex<EncoderBox>>(&encoder).ok_or(crate::error::Error::Invalid)?;
		let mut b = b.lock().unwrap();
		if b.open {
			return Err(crate::error::Error::State);
		}
		let e = match crate::encoder::create_from_params(&b.params) {
			Some(e) => e,
			None => {
				b.last_error = "failed to create encoder".to_string();
				return Err(crate::error::Error::Failed("failed to create encoder".to_string()));
			}
		};
		if e.configure(&b.params).is_err() {
			b.last_error = "failed to configure encoder".to_string();
			return Err(crate::error::Error::Failed("failed to configure encoder".to_string()));
		}
		if e.open().is_err() {
			b.last_error = "failed to open stream".to_string();
			return Err(crate::error::Error::Failed("failed to open stream".to_string()));
		}
		b.encoder = Some(e);
		b.open = true;
		Ok(())
	})
}

/// `oakcodec_encoder_write_video`: encode one video frame.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_write_video(encoder: CHandle, frame: CHandle) -> c_int {
	handle::guard(|| {
		let b = super::get_box::<Mutex<EncoderBox>>(&encoder).ok_or(crate::error::Error::Invalid)?;
		let f = super::get_box::<Mutex<crate::frame::Frame>>(&frame)
			.ok_or(crate::error::Error::Invalid)?;
		let e = {
			let b = b.lock().unwrap();
			if !b.open || b.flushed || b.encoder.is_none() {
				return Err(crate::error::Error::State);
			}
			b.encoder.as_ref().unwrap().clone()
		};
		let f = f.lock().unwrap();
		e.write_video(&f)
			.map_err(|_| crate::error::Error::Failed("write_video failed".to_string()))
	})
}

/// `oakcodec_encoder_write_audio`: encode interleaved float audio samples.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_write_audio(
	encoder: CHandle,
	samples: *const f32,
	frame_count: c_int,
) -> c_int {
	handle::guard(|| {
		let b = super::get_box::<Mutex<EncoderBox>>(&encoder).ok_or(crate::error::Error::Invalid)?;
		if (samples.is_null() && frame_count > 0) || frame_count < 0 {
			return Err(crate::error::Error::Invalid);
		}
		let (slice, enc) = {
			let b = b.lock().unwrap();
			if !b.open || b.flushed || b.encoder.is_none() {
				return Err(crate::error::Error::State);
			}
			let channels = b.params.audio_channel_layout.count_ones();
			if channels == 0 {
				return Err(crate::error::Error::State);
			}
			let sample_count = (frame_count as usize).wrapping_mul(channels as usize);
			let slice: &[f32] = if samples.is_null() {
				&[]
			} else {
				// SAFETY: the caller guarantees `samples` holds
				// `frame_count * channels` floats.
				unsafe { std::slice::from_raw_parts(samples, sample_count) }
			};
			let e = b.encoder.as_ref().unwrap().clone();
			(slice, e)
		};
		enc.write_audio(slice, frame_count)
			.map_err(|_| crate::error::Error::Failed("write_audio failed".to_string()))
	})
}

/// `oakcodec_encoder_write_subtitle`: encode one subtitle entry.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_write_subtitle(
	encoder: CHandle,
	text: *const c_char,
	in_seconds: f64,
	out_seconds: f64,
) -> c_int {
	handle::guard(|| {
		let b = super::get_box::<Mutex<EncoderBox>>(&encoder).ok_or(crate::error::Error::Invalid)?;
		let text = match crate::ffi::c_str(text) {
			Some(t) => t,
			None => return Err(crate::error::Error::Invalid),
		};
		let e = {
			let b = b.lock().unwrap();
			if !b.open || b.flushed || b.encoder.is_none() {
				return Err(crate::error::Error::State);
			}
			b.encoder.as_ref().unwrap().clone()
		};
		e.write_subtitle(&text, in_seconds, out_seconds)
			.map_err(|_| crate::error::Error::Failed("write_subtitle failed".to_string()))
	})
}

/// `oakcodec_encoder_flush`: flush the encoders, write the trailer and
/// close the file. Idempotent.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_flush(encoder: CHandle) -> c_int {
	handle::guard(|| {
		let b = super::get_box::<Mutex<EncoderBox>>(&encoder).ok_or(crate::error::Error::Invalid)?;
		let mut b = b.lock().unwrap();
		if !b.open {
			return Err(crate::error::Error::State);
		}
		if b.flushed {
			return Ok(());
		}
		let e = b.encoder.as_ref().unwrap().clone();
		// The C++ ignores the close() result; the Rust interim surfaces it.
		e.close()
			.map_err(|_| crate::error::Error::Failed("close failed".to_string()))?;
		b.flushed = true;
		Ok(())
	})
}

/// `oakcodec_encoder_last_error` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_last_error(
	encoder: CHandle,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| match super::get_box::<Mutex<EncoderBox>>(&encoder) {
		Some(b) => {
			let b = b.lock().unwrap();
			super::string_out(&b.last_error, buf, buf_size)
		}
		None => super::string_out("", buf, buf_size),
	})
}

/// `oakcodec_encoder_get_desired_pixel_format`: the pixel format the
/// encoder wants frames in, or -1 when unknown; `OAKCODEC_E_INVALID` for
/// an empty/invalid encoder.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoder_get_desired_pixel_format(encoder: CHandle) -> c_int {
	handle::guard_raw(|| {
		let b = match super::get_box::<Mutex<EncoderBox>>(&encoder) {
			Some(b) => b,
			None => return crate::error::OAKCODEC_E_INVALID,
		};
		let e = match &b.lock().unwrap().encoder {
			Some(e) => e.clone(),
			None => return crate::error::OAKCODEC_E_INVALID,
		};
		match e.desired_pixel_format() {
			Some(p) => p as c_int,
			None => -1,
		}
	})
}

/// `oakcodec_export_format_get_extension` (two-stage); unknown formats
/// yield the empty string.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_export_format_get_extension(
	format: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let ext = match crate::exportformat::Format::from_i32(format) {
			Some(f) => crate::exportformat::Format::get_extension(f),
			None => String::new(),
		};
		super::string_out(&ext, buf, buf_size)
	})
}

/// `oakcodec_encoding_generate_matrix`: scaling matrix for a scaling
/// method, row-major 4x4 `double` into `out_matrix[16]`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_generate_matrix(
	method: c_int,
	src_width: c_int,
	src_height: c_int,
	dst_width: c_int,
	dst_height: c_int,
	out_matrix: *mut f64,
) -> c_int {
	handle::guard(|| {
		if out_matrix.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let mut m = [0.0f64; 16];
		crate::encodingparams::EncodingParams::generate_matrix(
			crate::encodingparams::scaling_from_i32(method),
			src_width,
			src_height,
			dst_width,
			dst_height,
			&mut m,
		);
		// SAFETY: the caller guarantees `out_matrix` holds 16 doubles.
		unsafe { std::ptr::copy_nonoverlapping(m.as_ptr(), out_matrix, 16) };
		Ok(())
	})
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::bridge::common::oakcommon_videoparams_init_basic;
	use crate::encoder::set_test_encoders;
	use crate::error::{OAKCODEC_E_INVALID, OAKCODEC_E_STATE};
	use crate::ffi::frame::{oakcodec_frame_allocate, oakcodec_frame_free, oakcodec_frame_init_with_params};

	fn cstr(s: &str) -> std::ffi::CString {
		std::ffi::CString::new(s).unwrap()
	}

	fn zeroed_params() -> oakcodec_encoding_params {
		unsafe { std::mem::zeroed() }
	}

	fn valid_params() -> oakcodec_encoding_params {
		let mut p = zeroed_params();
		p.filename = {
			let mut f = [0u8; 1024];
			let name = b"out.mp4";
			f[..name.len()].copy_from_slice(name);
			f
		};
		p.format = 2; // MPEG-4
		p.video_enabled = 1;
		p.video_codec = 3;
		p.video_width = 1920;
		p.video_height = 1080;
		p.video_time_base_num = 1;
		p.video_time_base_den = 30;
		p.video_pixel_format = 0; // U8
		p.audio_enabled = 1;
		p.audio_codec = 4;
		p.audio_sample_rate = 48000;
		p.audio_channel_layout = 0x3;
		p.audio_sample_format = 10; // f32 packed
		p
	}

	/// Fake encoder that accepts every operation.
	struct FakeEncoder {
		id: &'static str,
	}

	impl Encoder for FakeEncoder {
		fn id(&self) -> String {
			self.id.to_string()
		}
		fn supports_video(&self) -> bool {
			true
		}
		fn supports_audio(&self) -> bool {
			true
		}
		fn supports_subtitles(&self) -> bool {
			true
		}
		fn configure(&self, _p: &EncodingParams) -> crate::error::Result<()> {
			Ok(())
		}
		fn open(&self) -> crate::error::Result<()> {
			Ok(())
		}
		fn close(&self) -> crate::error::Result<()> {
			Ok(())
		}
		fn write_video(&self, _frame: &crate::frame::Frame) -> crate::error::Result<()> {
			Ok(())
		}
		fn write_audio(&self, _samples: &[f32], _frame_count: i32) -> crate::error::Result<()> {
			Ok(())
		}
		fn write_subtitle(
			&self,
			_text: &str,
			_in_seconds: f64,
			_out_seconds: f64,
		) -> crate::error::Result<()> {
			Ok(())
		}
		fn flush(&self) -> crate::error::Result<()> {
			Ok(())
		}
		fn desired_pixel_format(&self) -> Option<PixelFormat> {
			Some(PixelFormat::U8)
		}
		fn desired_sample_format(&self) -> Option<oakcore_rs::SampleFormat> {
			None
		}
		fn filename(&self) -> String {
			"out.mp4".to_string()
		}
	}

	#[test]
	fn init_open_write_flush_golden() {
		let _g = crate::ffi::lock_tests();
		set_test_encoders(vec![std::sync::Arc::new(FakeEncoder { id: "fake" })]);

		let p = valid_params();
		let before = handle::alive_count();
		let mut h = unsafe { oakcodec_encoder_init(&p) };
		assert!(!h.is_null());
		assert_eq!(handle::alive_count(), before + 1);

		// set_video_option between init and open.
		let key = cstr("crf");
		let val = cstr("18");
		let rc = unsafe { oakcodec_encoder_set_video_option(h, key.as_ptr(), val.as_ptr()) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		let rc = unsafe { oakcodec_encoder_open(h) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		// write_video with a real frame handle.
		let params = unsafe { oakcommon_videoparams_init_basic(16, 16) };
		let mut fh = unsafe { oakcodec_frame_init_with_params(params) };
		assert_eq!(unsafe { oakcodec_frame_allocate(fh) }, crate::error::OAKCODEC_OK);
		let rc = unsafe { oakcodec_encoder_write_video(h, fh) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		// write_audio: stereo interleaved floats.
		let mut samples = [0f32; 64];
		let rc = unsafe { oakcodec_encoder_write_audio(h, samples.as_ptr(), 32) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		// write_subtitle.
		let text = cstr("hello");
		let rc = unsafe { oakcodec_encoder_write_subtitle(h, text.as_ptr(), 0.0, 2.5) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		// desired pixel format from the fake.
		assert_eq!(unsafe { oakcodec_encoder_get_desired_pixel_format(h) }, 0); // U8

		// flush is idempotent.
		let rc = unsafe { oakcodec_encoder_flush(h) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		let rc = unsafe { oakcodec_encoder_flush(h) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);

		// Writes after flush -> E_STATE.
		let rc = unsafe { oakcodec_encoder_write_video(h, fh) };
		assert_eq!(rc, OAKCODEC_E_STATE);
		let rc = unsafe { oakcodec_encoder_write_audio(h, samples.as_ptr(), 32) };
		assert_eq!(rc, OAKCODEC_E_STATE);
		let rc = unsafe { oakcodec_encoder_write_subtitle(h, text.as_ptr(), 0.0, 1.0) };
		assert_eq!(rc, OAKCODEC_E_STATE);

		unsafe { oakcodec_frame_free(&mut fh) };
		unsafe { oakcodec_encoder_free(&mut h) };
		assert!(h.is_null());
		assert_eq!(handle::alive_count(), before);
		set_test_encoders(Vec::new());
	}

	#[test]
	fn init_invalid_config_and_null_params() {
		let _g = crate::ffi::lock_tests();
		set_test_encoders(Vec::new());

		// NULL params -> empty handle.
		let mut h = unsafe { oakcodec_encoder_init(std::ptr::null()) };
		assert!(h.is_null());

		// All tracks disabled -> empty handle (is_valid).
		let p = zeroed_params();
		let mut h = unsafe { oakcodec_encoder_init(&p) };
		assert!(h.is_null());

		// Audio-only config is valid.
		let mut p = zeroed_params();
		p.format = 7; // WAV
		p.audio_enabled = 1;
		p.audio_sample_rate = 44100;
		p.audio_channel_layout = 0x4;
		p.audio_sample_format = 10;
		let before = handle::alive_count();
		let mut h = unsafe { oakcodec_encoder_init(&p) };
		assert!(!h.is_null());
		assert_eq!(handle::alive_count(), before + 1);
		unsafe { oakcodec_encoder_free(&mut h) };
		assert_eq!(handle::alive_count(), before);
	}

	#[test]
	fn open_errors_and_state_machine() {
		let _g = crate::ffi::lock_tests();
		// Production path: an unknown export format cannot create an encoder.
		set_test_encoders(Vec::new());

		let mut p = valid_params();
		p.format = 99; // unknown format -> create_from_params returns None
		let mut h = unsafe { oakcodec_encoder_init(&p) };
		assert!(!h.is_null());

		let rc = unsafe { oakcodec_encoder_open(h) };
		assert_eq!(rc, crate::error::OAKCODEC_E_FAILED);
		let mut err = [0i8; 128];
		unsafe { oakcodec_encoder_last_error(h, err.as_mut_ptr(), 128) };
		assert_eq!(crate::ffi::c_str(err.as_ptr()).as_deref(), Some("failed to create encoder"));

		// Empty handle -> E_INVALID; last_error empty.
		let empty = CHandle::null();
		assert_eq!(unsafe { oakcodec_encoder_open(empty) }, OAKCODEC_E_INVALID);
		assert_eq!(unsafe { oakcodec_encoder_set_video_option(empty, cstr("crf").as_ptr(), cstr("18").as_ptr()) }, OAKCODEC_E_INVALID);
		assert_eq!(unsafe { oakcodec_encoder_get_desired_pixel_format(empty) }, OAKCODEC_E_INVALID);
		let rc = unsafe { oakcodec_encoder_last_error(empty, err.as_mut_ptr(), 128) };
		assert_eq!(rc, 1);
		assert_eq!(crate::ffi::c_str(err.as_ptr()).as_deref(), Some(""));

		unsafe { oakcodec_encoder_free(&mut h) };
	}

	#[test]
	fn state_and_argument_errors_with_fake() {
		let _g = crate::ffi::lock_tests();
		set_test_encoders(vec![std::sync::Arc::new(FakeEncoder { id: "fake" })]);

		let p = valid_params();
		let mut h = unsafe { oakcodec_encoder_init(&p) };
		assert!(!h.is_null());

		// set_video_option with a NULL key -> E_INVALID.
		let rc = unsafe { oakcodec_encoder_set_video_option(h, std::ptr::null(), std::ptr::null()) };
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// Writes before open -> E_STATE.
		let params = unsafe { oakcommon_videoparams_init_basic(4, 4) };
		let mut fh = unsafe { oakcodec_frame_init_with_params(params) };
		let rc = unsafe { oakcodec_encoder_write_video(h, fh) };
		assert_eq!(rc, OAKCODEC_E_STATE);
		let rc = unsafe { oakcodec_encoder_flush(h) };
		assert_eq!(rc, OAKCODEC_E_STATE);
		// Null samples with a positive count is an argument error (checked
		// before the state, matching the C++ validation order).
		let rc = unsafe { oakcodec_encoder_write_audio(h, std::ptr::null(), 8) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
		// Valid args before open -> E_STATE.
		let mut pre = [0f32; 8];
		let rc = unsafe { oakcodec_encoder_write_audio(h, pre.as_ptr(), 4) };
		assert_eq!(rc, OAKCODEC_E_STATE);

		unsafe { oakcodec_frame_free(&mut fh) };
		unsafe { oakcodec_encoder_free(&mut h) };
		set_test_encoders(Vec::new());
	}

	#[test]
	fn write_audio_argument_validation() {
		let _g = crate::ffi::lock_tests();
		set_test_encoders(vec![std::sync::Arc::new(FakeEncoder { id: "fake" })]);

		let p = valid_params();
		let mut h = unsafe { oakcodec_encoder_init(&p) };
		assert_eq!(unsafe { oakcodec_encoder_open(h) }, crate::error::OAKCODEC_OK);

		// NULL samples with a positive frame count -> E_INVALID.
		let rc = unsafe { oakcodec_encoder_write_audio(h, std::ptr::null(), 8) };
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// Negative frame count -> E_INVALID.
		let samples = [0f32; 8];
		let rc = unsafe { oakcodec_encoder_write_audio(h, samples.as_ptr(), -1) };
		assert_eq!(rc, OAKCODEC_E_INVALID);

		unsafe { oakcodec_encoder_free(&mut h) };
		set_test_encoders(Vec::new());
	}

	#[test]
	fn write_audio_zero_channels_is_state() {
		let _g = crate::ffi::lock_tests();
		set_test_encoders(vec![std::sync::Arc::new(FakeEncoder { id: "fake" })]);

		// Audio enabled but empty channel layout -> E_STATE at write time.
		let mut p = zeroed_params();
		p.format = 7;
		p.audio_enabled = 1;
		p.audio_sample_rate = 44100;
		p.audio_channel_layout = 0;
		p.audio_sample_format = 10;
		let mut h = unsafe { oakcodec_encoder_init(&p) };
		assert!(!h.is_null());
		assert_eq!(unsafe { oakcodec_encoder_open(h) }, crate::error::OAKCODEC_OK);
		let mut samples = [0f32; 8];
		let rc = unsafe { oakcodec_encoder_write_audio(h, samples.as_ptr(), 4) };
		assert_eq!(rc, OAKCODEC_E_STATE);

		unsafe { oakcodec_encoder_free(&mut h) };
		set_test_encoders(Vec::new());
	}

	#[test]
	fn export_format_extension_and_generate_matrix() {
		let mut buf = [0i8; 64];
		let rc = unsafe { oakcodec_export_format_get_extension(2, buf.as_mut_ptr(), 64) };
		assert_eq!(rc, 4); // "mp4" + NUL
		assert_eq!(crate::ffi::c_str(buf.as_ptr()).as_deref(), Some("mp4"));

		// Unknown format -> empty string (size 1 for the NUL).
		let rc = unsafe { oakcodec_export_format_get_extension(99, buf.as_mut_ptr(), 64) };
		assert_eq!(rc, 1);
		assert_eq!(crate::ffi::c_str(buf.as_ptr()).as_deref(), Some(""));

		// Truncation rule: small buffer writes buf_size-1 chars + NUL.
		let rc = unsafe { oakcodec_export_format_get_extension(2, buf.as_mut_ptr(), 3) };
		assert_eq!(rc, 4); // required size unchanged
		assert_eq!(crate::ffi::c_str(buf.as_ptr()).as_deref(), Some("mp"));

		// generate_matrix: Stretch (1) is the identity.
		let mut m = [9.0f64; 16];
		let rc = unsafe { oakcodec_encoding_generate_matrix(1, 1920, 1080, 1280, 720, m.as_mut_ptr()) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(m[0], 1.0);
		assert_eq!(m[5], 1.0);
		assert_eq!(m[10], 1.0);
		assert_eq!(m[15], 1.0);

		// Fit (0) with a square source into a 2:1 destination scales x.
		let mut m = [0.0f64; 16];
		let rc = unsafe { oakcodec_encoding_generate_matrix(0, 1000, 1000, 2000, 1000, m.as_mut_ptr()) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert!((m[0] - 0.5).abs() < 1e-9);
		assert_eq!(m[5], 1.0);

		// NULL out_matrix -> E_INVALID.
		let rc = unsafe { oakcodec_encoding_generate_matrix(0, 1, 1, 2, 2, std::ptr::null_mut()) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
	}
}
