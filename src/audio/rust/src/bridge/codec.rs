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

//! oakcodec C ABI imports (audio encoding + decoding for the manager and
//! waveform extraction).

use std::ffi::{c_char, c_int, c_void};

/// `oakcodec_encoding_params` — a `#[repr(C)]` mirror of the same-named
/// struct in include/codec/encoder.h, field-for-field (the offset of every
/// audio field is load-bearing: `audio_enabled` sits at byte 1180 — NOT
/// 1028, which is `video_enabled` — because the 20-field video block
/// precedes it; natural alignment inserts 4 pad bytes before the first
/// `int64_t`, verified against `offsetof` on the C++ header).
///
/// Only the audio fields are consumed by oakaudio; the remaining fields
/// are kept verbatim to preserve layout.
///
/// `// CPP-PARITY: include/codec/encoder.h:79` (`oakcodec_encoding_params`).
#[repr(C)]
pub struct EncodingParams {
	/// Output filename (NUL-terminated).
	pub filename: [c_char; 1024],
	/// Output container format id.
	pub format: c_int,
	/// Whether video is enabled.
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
	/// Whether audio is enabled.
	pub audio_enabled: c_int,
	/// Audio codec id.
	pub audio_codec: c_int,
	/// Audio sample rate in Hz.
	pub audio_sample_rate: c_int,
	/// Audio channel layout mask.
	pub audio_channel_layout: u64,
	/// Audio sample format (`oakcodec` sample format).
	pub audio_sample_format: c_int,
	/// Audio bit rate in bits per second.
	pub audio_bit_rate: i64,
	/// Whether subtitles are enabled.
	pub subtitles_enabled: c_int,
	/// Subtitle codec id.
	pub subtitles_codec: c_int,
	/// Whether subtitles are a sidecar file.
	pub subtitles_are_sidecar: c_int,
	/// Sidecar subtitle format (`ExportFormat::Format`).
	pub subtitles_sidecar_format: c_int,
	/// Output OCIO colorspace name (empty = reference space).
	pub color_transform_output: [c_char; 256],
	/// Export length in seconds (rational), numerator.
	pub export_length_num: c_int,
	/// Export length in seconds (rational), denominator.
	pub export_length_den: c_int,
	/// Whether a custom export range is set.
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

/// `oakcodec_audio_stream_info` — audio stream metadata from probing.
///
/// `// CPP-PARITY: include/codec/decoder.h` (`oakcodec_audio_stream_info`).
#[repr(C)]
pub struct AudioStreamInfo {
	/// Stream index.
	pub stream_index: c_int,
	/// Sample rate in Hz.
	pub sample_rate: c_int,
	/// Channel layout mask.
	pub channel_layout: u64,
	/// Number of channels.
	pub channel_count: c_int,
	/// Duration in stream time base units.
	pub duration_ts: i64,
	/// Stream time base numerator.
	pub time_base_num: c_int,
	/// Stream time base denominator.
	pub time_base_den: c_int,
}

extern "C" {
	/// `oakcodec_encoder_init` — create an encoder for `params`.
	pub fn oakcodec_encoder_init(params: *const EncodingParams) -> *mut c_void;
	/// `oakcodec_encoder_free`.
	pub fn oakcodec_encoder_free(encoder: *mut c_void);
	/// `oakcodec_encoder_open`.
	pub fn oakcodec_encoder_open(encoder: *mut c_void) -> c_int;
	/// `oakcodec_encoder_write_audio` — feed interleaved `f32` audio.
	pub fn oakcodec_encoder_write_audio(
		encoder: *mut c_void,
		samples: *const f32,
		frame_count: c_int,
	) -> c_int;
	/// `oakcodec_encoder_flush`.
	pub fn oakcodec_encoder_flush(encoder: *mut c_void) -> c_int;
	/// `oakcodec_encoder_last_error` — copy the last error string into `buf`.
	pub fn oakcodec_encoder_last_error(
		encoder: *mut c_void,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;

	/// `oakcodec_decoder_probe` — create a probe handle for a file.
	pub fn oakcodec_decoder_probe(filename: *const c_char) -> *mut c_void;
	/// `oakcodec_decoder_free`.
	pub fn oakcodec_decoder_free(decoder: *mut c_void);
	/// `oakcodec_decoder_probe_audio_stream_count`.
	pub fn oakcodec_decoder_probe_audio_stream_count(decoder: *mut c_void) -> c_int;
	/// `oakcodec_decoder_probe_get_audio_stream` — copy audio stream info.
	pub fn oakcodec_decoder_probe_get_audio_stream(
		decoder: *mut c_void,
		index: c_int,
		out: *mut AudioStreamInfo,
	) -> c_int;
	/// `oakcodec_decoder_open` — open stream `stream_index` for decoding.
	pub fn oakcodec_decoder_open(decoder: *mut c_void, filename: *const c_char, stream_index: c_int)
		-> c_int;
	/// `oakcodec_decoder_decode_audio` — decode/convert frames into `buf`.
	pub fn oakcodec_decoder_decode_audio(
		decoder: *mut c_void,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		sample_rate: c_int,
		channel_layout: u64,
		buf: *mut f32,
		buf_frames: c_int,
	) -> c_int;
}
