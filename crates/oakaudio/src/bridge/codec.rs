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

//! oakcodec C ABI calls (audio encoding + decoding for the manager and
//! waveform extraction) — now direct Rust calls into the oakcodec crate
//! (single-lib unification, see `docs/zh/plans/riir/single-lib.md`).
//!
//! The previous `*mut c_void` handle convention drifted from the real
//! codec C ABI (which uses the shared [`CHandle`]); the wrappers below
//! match the oakcodec ffi signatures exactly.

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

/// `oakcodec_encoding_params` — single-lib unification: aliases the
/// oakcodec crate's POD (identical layout; only the audio fields are
/// consumed by oakaudio).
pub type EncodingParams = oakcodec::ffi::encoder::oakcodec_encoding_params;

/// `oakcodec_audio_stream_info` — audio stream metadata from probing.
/// Single-lib unification: aliases the oakcodec crate's POD.
pub type AudioStreamInfo = oakcodec::decoder::OakCodecAudioStreamInfo;

/// `oakcodec_encoder_init` — create an encoder for `params`.
pub unsafe fn oakcodec_encoder_init(params: *const EncodingParams) -> CHandle {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_init(params) }
}

/// `oakcodec_encoder_free`.
pub unsafe fn oakcodec_encoder_free(encoder: *mut CHandle) {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_free(encoder) }
}

/// `oakcodec_encoder_open`.
pub unsafe fn oakcodec_encoder_open(encoder: CHandle) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_open(encoder) }
}

/// `oakcodec_encoder_write_audio` — feed interleaved `f32` audio.
pub unsafe fn oakcodec_encoder_write_audio(
	encoder: CHandle,
	samples: *const f32,
	frame_count: c_int,
) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_write_audio(encoder, samples, frame_count) }
}

/// `oakcodec_encoder_flush`.
pub unsafe fn oakcodec_encoder_flush(encoder: CHandle) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_flush(encoder) }
}

/// `oakcodec_encoder_last_error` — copy the last error string into `buf`.
pub unsafe fn oakcodec_encoder_last_error(
	encoder: CHandle,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oakcodec::ffi::encoder::oakcodec_encoder_last_error(encoder, buf, buf_size) }
}

/// `oakcodec_decoder_probe` — create a probe handle for a file.
pub unsafe fn oakcodec_decoder_probe(filename: *const c_char) -> CHandle {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_probe(filename) }
}

/// `oakcodec_decoder_free`.
pub unsafe fn oakcodec_decoder_free(decoder: *mut CHandle) {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_free(decoder) }
}

/// `oakcodec_decoder_probe_audio_stream_count`.
pub unsafe fn oakcodec_decoder_probe_audio_stream_count(probe: CHandle) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_probe_audio_stream_count(probe) }
}

/// `oakcodec_decoder_probe_get_audio_stream` — copy audio stream info.
pub unsafe fn oakcodec_decoder_probe_get_audio_stream(
	probe: CHandle,
	index: c_int,
	out: *mut AudioStreamInfo,
) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_probe_get_audio_stream(probe, index, out) }
}

/// `oakcodec_decoder_open` — open stream `stream_index` for decoding.
pub unsafe fn oakcodec_decoder_open(
	decoder: CHandle,
	filename: *const c_char,
	stream_index: c_int,
) -> c_int {
	unsafe { oakcodec::ffi::decoder::oakcodec_decoder_open(decoder, filename, stream_index) }
}

/// `oakcodec_decoder_decode_audio` — decode/convert frames into `buf`.
#[allow(clippy::too_many_arguments)]
pub unsafe fn oakcodec_decoder_decode_audio(
	decoder: CHandle,
	in_num: c_int,
	in_den: c_int,
	out_num: c_int,
	out_den: c_int,
	sample_rate: c_int,
	channel_layout: u64,
	buf: *mut f32,
	buf_frames: c_int,
) -> c_int {
	unsafe {
		oakcodec::ffi::decoder::oakcodec_decoder_decode_audio(
			decoder,
			in_num,
			in_den,
			out_num,
			out_den,
			sample_rate,
			channel_layout,
			buf,
			buf_frames,
		)
	}
}
