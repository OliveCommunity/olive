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

//! oakcodec C ABI bridge: direct Rust calls into the `oakcodec` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oakcodec`'s `ffi` (the
//! `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them). Handles cross as the shared
//! [`crate::handle::CHandle`]. Exceptions that keep an `extern "C"`
//! declaration (resolved at link time against the sibling crate in the
//! same dylib) are the host `oakcore_*` symbols and the encoding-params
//! C ABI POD crossings (the facade keeps its own POD mirrors there).

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
/// `olive::EncodingParams` — single-lib unification: aliases the oakcodec
/// crate's POD (`oakcodec_encoding_params`, identical `#[repr(C)]` layout;
/// the engine's `OakEngineEncodingParams` handle is a heap box over exactly
/// this struct, so every engine getter/setter reads/writes a field and
/// `encoder_init`/recording can consume the pointer directly).
pub type EncodingParamsPOD = oakcodec::ffi::encoder::oakcodec_encoding_params;


/// Zeroed encoding-params POD (all fields 0 / NUL). The codec crate's
/// struct has no zeroed constructor; this facade helper provides it.
pub fn zeroed_encoding_params() -> EncodingParamsPOD {
	// All-C fields: all-zero is a valid value.
	unsafe { std::mem::zeroed() }
}


/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_count() -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_count() }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_name(format: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_name(format, buf, buf_size) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_extension(
		format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_extension(format, buf, buf_size) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_video_codec_count(format: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_video_codec_count(format) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_video_codec_at(format: c_int, index: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_video_codec_at(format, index) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_audio_codec_count(format: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_audio_codec_count(format) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_audio_codec_at(format: c_int, index: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_audio_codec_at(format, index) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_subtitle_codec_count(format: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_subtitle_codec_count(format) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_format_subtitle_codec_at(format: c_int, index: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_format_subtitle_codec_at(format, index) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_codec_name(codec: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_codec_name(codec, buf, buf_size) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_codec_is_still_image(codec: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_codec_is_still_image(codec) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_codec_is_lossless(codec: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_codec_is_lossless(codec) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_pix_fmt_count(format: c_int, codec: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_pix_fmt_count(format, codec) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_pix_fmt_at(
		format: c_int,
		codec: c_int,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_pix_fmt_at(format, codec, index, buf, buf_size) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_pix_fmt_index(codec: c_int, pix_fmt: *const c_char) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_pix_fmt_index(codec, pix_fmt) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_sample_format_count(format: c_int, codec: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_sample_format_count(format, codec) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_sample_format_at(format: c_int, codec: c_int, index: c_int) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_sample_format_at(format, codec, index) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_filename_contains_digit_placeholder(filename: *const c_char) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_filename_contains_digit_placeholder(filename) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_image_sequence_digit_count(filename: *const c_char) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_image_sequence_digit_count(filename) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoding_filename_remove_digit_placeholder(
		filename: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcodec::ffi::format::oakcodec_encoding_filename_remove_digit_placeholder(filename, buf, buf_size) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
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

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
/// `oakcodec_encoder_init` — crosses the encoding-params C ABI POD
/// (`oakcodec_encoding_params`; the facade keeps its own POD mirror).
/// Kept as a link-time `extern "C"` declaration against the frozen module
/// C ABI.
pub fn oakcodec_encoder_init(params: *const EncodingParamsPOD) -> CHandle {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_init(params) }
}

/// Direct call into the `oakcodec` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcodec_encoder_free(encoder: *mut CHandle) {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_free(encoder) }
}

