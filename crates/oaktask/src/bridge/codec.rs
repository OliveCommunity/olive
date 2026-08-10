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

//! oakcodec C ABI imports. The task module reaches the codec side through
//! `include/codec/task.h` (the task submitter), `include/codec/decoder.h`
//! (conform/proxy work) and `include/codec/encoder.h` (export). Signatures
//! mirror the headers verbatim.

use std::ffi::{c_char, c_int, c_uint, c_void};

use crate::bridge::render::OakCancelAtom;
use crate::handle::CHandle;

/// `OakCodecTaskKind` enum values (`include/codec/task.h`).
pub const OAKCODEC_TASK_CONFORM: c_int = 0;
/// `OakCodecTaskKind::OAKCODEC_TASK_PROXY`.
pub const OAKCODEC_TASK_PROXY: c_int = 1;
/// `OAKCODEC_E_CANCELLED` (`include/codec/error.h`).
pub const OAKCODEC_E_CANCELLED: c_int = -50006;
/// `OAKCODEC_E_INVALID` (`include/codec/error.h`).
pub const OAKCODEC_E_INVALID: c_int = -50001;
/// `OAKCODEC_E_FAILED` (`include/codec/error.h`).
pub const OAKCODEC_E_FAILED: c_int = -50003;

/// Mirror of `OakCodecTaskRequest` (`include/codec/task.h`).
pub type OakCodecTaskRequest = oakcodec::task::OakCodecTaskRequest;


/// `oakcodec_task_submit_fn` callback typedef.
pub type OakCodecTaskSubmitFn =
	unsafe extern "C" fn(req: *const OakCodecTaskRequest, userdata: *mut c_void) -> c_int;

/// Mirror of `OakCodecFrame`/`OakFrame` handle type (`include/codec/frame.h`).
pub type OakFrame = CHandle;

/// Mirror of `OakEncoder` handle type (`include/codec/encoder.h`).
pub type OakEncoder = CHandle;

/// Mirror of `OakDecoder` handle type (`include/codec/decoder.h`).
pub type OakDecoder = CHandle;

/// Mirror of `OakCodecProxyParams` (`include/codec/proxy.h`).
pub type OakCodecProxyParams = oakcodec::ffi::proxy::oakcodec_proxy_params;


/// Mirror of `oakcodec_encoding_params` (`include/codec/encoder.h`) — the
/// export task fills this to open the encoder. Layout mirrors the header
/// verbatim (a zeroed struct = all tracks disabled).
pub type OakCodecEncodingParams = oakcodec::ffi::encoder::oakcodec_encoding_params;


/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_set_task_submit_cb(
		cb: Option<OakCodecTaskSubmitFn>,
		userdata: *mut c_void,
	) {
	unsafe { oakcodec::ffi::task::oakcodec_set_task_submit_cb(cb, userdata) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_task_submit_is_registered() -> c_int {
	unsafe { oakcodec::ffi::task::oakcodec_task_submit_is_registered() }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_init() -> OakDecoder {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_init() }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_free(decoder: *mut OakDecoder) {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_free(decoder) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_open(
		decoder: OakDecoder,
		filename: *const c_char,
		stream_index: c_int,
	) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_open(decoder, filename, stream_index) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_close(decoder: OakDecoder) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_close(decoder) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_is_open(decoder: OakDecoder) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_is_open(decoder) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_decode_audio(
		decoder: OakDecoder,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		sample_rate: c_int,
		channel_layout: u64,
		buf: *mut f32,
		buf_frames: c_int,
	) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_decode_audio(decoder, in_num, in_den, out_num, out_den, sample_rate, channel_layout, buf, buf_frames) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_conform_audio(
		decoder: OakDecoder,
		output_filenames: *const *const c_char,
		filename_count: c_int,
		sample_rate: c_int,
		channel_layout: u64,
		sample_format: c_int,
		cancelled: OakCancelAtom,
	) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_conform_audio(decoder, output_filenames, filename_count, sample_rate, channel_layout, sample_format, cancelled) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_last_error(decoder: OakDecoder, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_last_error(decoder, buf, buf_size) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_get_image_sequence_digit_count(filename: *const c_char) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_get_image_sequence_digit_count(filename) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_get_image_sequence_index(filename: *const c_char) -> i64 {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_get_image_sequence_index(filename) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_decoder_transform_image_sequence_file_name(
		filename: *const c_char,
		number: i64,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_transform_image_sequence_file_name(filename, number, buf, buf_size) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_get_desired_pixel_format(encoder: OakEncoder) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_get_desired_pixel_format(encoder) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoding_generate_matrix(
		method: c_int,
		src_width: c_int,
		src_height: c_int,
		dst_width: c_int,
		dst_height: c_int,
		out_matrix: *mut f64,
	) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoding_generate_matrix(method, src_width, src_height, dst_width, dst_height, out_matrix) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_proxy_find_ffmpeg(configured_path: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oakcodec::ffi::proxy::oakcodec_proxy_find_ffmpeg(configured_path, buf, buf_size) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_proxy_params_default(out: *mut OakCodecProxyParams) -> c_int {
	unsafe { oakcodec::ffi::proxy::oakcodec_proxy_params_default(out) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_init(params: *const OakCodecEncodingParams) -> OakEncoder {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_init(params) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_free(encoder: *mut OakEncoder) {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_free(encoder) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_open(encoder: OakEncoder) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_open(encoder) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_write_video(encoder: OakEncoder, frame: OakFrame) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_write_video(encoder, frame) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_write_audio(
		encoder: OakEncoder,
		samples: *const f32,
		frame_count: c_int,
	) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_write_audio(encoder, samples, frame_count) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_write_subtitle(
		encoder: OakEncoder,
		text: *const c_char,
		in_seconds: f64,
		out_seconds: f64,
	) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_write_subtitle(encoder, text, in_seconds, out_seconds) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_flush(encoder: OakEncoder) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_flush(encoder) }
}

/// Direct call into the `oakcodec` crate (single-lib unification).
pub fn oakcodec_encoder_last_error(encoder: OakEncoder, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_last_error(encoder, buf, buf_size) }
}

