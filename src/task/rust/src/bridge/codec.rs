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
#[repr(C)]
pub struct OakCodecTaskRequest {
	/// `OakCodecTaskKind` as int.
	pub kind: c_int,
	/// Source media filename.
	pub input_filename: *const c_char,
	/// Final destination path.
	pub output_filename: *const c_char,
	/// Stream inside the source media.
	pub stream_index: c_int,
	/// conform: target sample rate.
	pub sample_rate: c_int,
	/// conform: target channel layout mask.
	pub channel_layout: u64,
	/// conform: target sample format (enum as int).
	pub sample_format: c_int,
	/// proxy: target width, 0 = unspecified/divider.
	pub proxy_width: c_int,
	/// proxy: target height, 0 = unspecified/divider.
	pub proxy_height: c_int,
}

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
#[repr(C)]
pub struct OakCodecProxyParams {
	/// Target width.
	pub width: c_int,
	/// Target height.
	pub height: c_int,
	/// Resolution divider.
	pub divider: c_int,
	/// Proxy format version.
	pub version: c_int,
	/// CRF for the proxy encode.
	pub crf: c_int,
	/// Whether audio is included.
	pub include_audio: c_int,
	/// Output container extension.
	pub extension: [c_char; 32],
	/// ffmpeg preset name.
	pub preset: [c_char; 32],
}

/// Mirror of `oakcodec_encoding_params` (`include/codec/encoder.h`) — the
/// export task fills this to open the encoder. Layout mirrors the header
/// verbatim (a zeroed struct = all tracks disabled).
#[repr(C)]
pub struct OakCodecEncodingParams {
	/// Output filename.
	pub filename: [c_char; 1024],
	/// Export format id (`olive::ExportFormat::Format`).
	pub format: c_int,
	/// Whether video is exported.
	pub video_enabled: c_int,
	/// Video codec id.
	pub video_codec: c_int,
	/// Output width.
	pub video_width: c_int,
	/// Output height.
	pub video_height: c_int,
	/// Frame duration rational numerator.
	pub video_time_base_num: c_int,
	/// Frame duration rational denominator.
	pub video_time_base_den: c_int,
	/// Delivery pixel format (`OakPixelFormat` as int).
	pub video_pixel_format: c_int,
	/// Interlacing (`OAKCODEC_INTERLACE_*`).
	pub video_interlacing: c_int,
	/// Pixel aspect ratio numerator.
	pub video_pixel_aspect_num: c_int,
	/// Pixel aspect ratio denominator.
	pub video_pixel_aspect_den: c_int,
	/// Video bit rate (bit/s, 0 = default).
	pub video_bit_rate: i64,
	/// Minimum video bit rate.
	pub video_min_bit_rate: i64,
	/// Maximum video bit rate.
	pub video_max_bit_rate: i64,
	/// Video buffer size (bytes).
	pub video_buffer_size: i64,
	/// Video threads (0 = auto).
	pub video_threads: c_int,
	/// Encoded pixel format name ("yuv420p").
	pub video_pix_fmt: [c_char; 64],
	/// Whether the output is an image sequence.
	pub video_is_image_sequence: c_int,
	/// Scaling method (`OAKCODEC_ENCODING_SCALING_*`).
	pub video_scaling_method: c_int,
	/// Whether audio is exported.
	pub audio_enabled: c_int,
	/// Audio codec id.
	pub audio_codec: c_int,
	/// Audio sample rate.
	pub audio_sample_rate: c_int,
	/// Audio channel layout mask.
	pub audio_channel_layout: u64,
	/// Audio sample format (`olive::core::SampleFormat::Format` as int).
	pub audio_sample_format: c_int,
	/// Audio bit rate.
	pub audio_bit_rate: i64,
	/// Whether subtitles are exported.
	pub subtitles_enabled: c_int,
	/// Subtitle codec id.
	pub subtitles_codec: c_int,
	/// Whether subtitles go to a sidecar file.
	pub subtitles_are_sidecar: c_int,
	/// Sidecar subtitle format.
	pub subtitles_sidecar_format: c_int,
	/// Output OCIO colorspace name (empty = reference space).
	pub color_transform_output: [c_char; 256],
	/// Export length in seconds (rational).
	pub export_length_num: c_int,
	/// Export length denominator.
	pub export_length_den: c_int,
	/// Whether a custom export range is in effect.
	pub has_custom_range: c_int,
	/// Custom range in (seconds, rational).
	pub custom_range_in_num: i64,
	/// Custom range in denominator.
	pub custom_range_in_den: i64,
	/// Custom range out numerator.
	pub custom_range_out_num: i64,
	/// Custom range out denominator.
	pub custom_range_out_den: i64,
}

extern "C" {
	/// `oakcodec_set_task_submit_cb` — install the task submitter (`None`
	/// unregisters, matching the header's `cb == NULL`).
	pub fn oakcodec_set_task_submit_cb(
		cb: Option<OakCodecTaskSubmitFn>,
		userdata: *mut c_void,
	);
	/// `oakcodec_task_submit_is_registered`.
	pub fn oakcodec_task_submit_is_registered() -> c_int;

	/// `oakcodec_decoder_init`.
	pub fn oakcodec_decoder_init() -> OakDecoder;
	/// `oakcodec_decoder_free`.
	pub fn oakcodec_decoder_free(decoder: *mut OakDecoder);
	/// `oakcodec_decoder_open`.
	pub fn oakcodec_decoder_open(
		decoder: OakDecoder,
		filename: *const c_char,
		stream_index: c_int,
	) -> c_int;
	/// `oakcodec_decoder_close`.
	pub fn oakcodec_decoder_close(decoder: OakDecoder) -> c_int;
	/// `oakcodec_decoder_is_open`.
	pub fn oakcodec_decoder_is_open(decoder: OakDecoder) -> c_int;
	/// `oakcodec_decoder_decode_audio`.
	pub fn oakcodec_decoder_decode_audio(
		decoder: OakDecoder,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		sample_rate: c_int,
		channel_layout: c_uint,
		buf: *mut f32,
		buf_frames: c_int,
	) -> c_int;
	/// `oakcodec_decoder_conform_audio` — the whole-stream conform pass.
	pub fn oakcodec_decoder_conform_audio(
		decoder: OakDecoder,
		output_filenames: *const *const c_char,
		filename_count: c_int,
		sample_rate: c_int,
		channel_layout: u64,
		sample_format: c_int,
		cancelled: OakCancelAtom,
	) -> c_int;
	/// `oakcodec_decoder_last_error`.
	pub fn oakcodec_decoder_last_error(decoder: OakDecoder, buf: *mut c_char, buf_size: c_int) -> c_int;

	// --- decoder.h: image-sequence heuristics ---
	/// `oakcodec_decoder_get_image_sequence_digit_count`.
	pub fn oakcodec_decoder_get_image_sequence_digit_count(filename: *const c_char) -> c_int;
	/// `oakcodec_decoder_get_image_sequence_index`.
	pub fn oakcodec_decoder_get_image_sequence_index(filename: *const c_char) -> i64;
	/// `oakcodec_decoder_transform_image_sequence_file_name` (two-stage).
	pub fn oakcodec_decoder_transform_image_sequence_file_name(
		filename: *const c_char,
		number: i64,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;

	// --- encoder.h extras ---
	/// `oakcodec_encoder_get_desired_pixel_format`.
	pub fn oakcodec_encoder_get_desired_pixel_format(encoder: OakEncoder) -> c_int;
	/// `oakcodec_encoding_generate_matrix` (row-major 4x4).
	pub fn oakcodec_encoding_generate_matrix(
		method: c_int,
		src_width: c_int,
		src_height: c_int,
		dst_width: c_int,
		dst_height: c_int,
		out_matrix: *mut f64,
	) -> c_int;

	// --- proxy.h ---
	/// `oakcodec_proxy_find_ffmpeg` (buf/size getter).
	pub fn oakcodec_proxy_find_ffmpeg(configured_path: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakcodec_proxy_params_default`.
	pub fn oakcodec_proxy_params_default(out: *mut OakCodecProxyParams) -> c_int;

	/// `oakcodec_encoder_init`.
	pub fn oakcodec_encoder_init(params: *const OakCodecEncodingParams) -> OakEncoder;
	/// `oakcodec_encoder_free`.
	pub fn oakcodec_encoder_free(encoder: *mut OakEncoder);
	/// `oakcodec_encoder_open`.
	pub fn oakcodec_encoder_open(encoder: OakEncoder) -> c_int;
	/// `oakcodec_encoder_write_video`.
	pub fn oakcodec_encoder_write_video(encoder: OakEncoder, frame: OakFrame) -> c_int;
	/// `oakcodec_encoder_write_audio`.
	pub fn oakcodec_encoder_write_audio(
		encoder: OakEncoder,
		samples: *const f32,
		frame_count: c_int,
	) -> c_int;
	/// `oakcodec_encoder_write_subtitle`.
	pub fn oakcodec_encoder_write_subtitle(
		encoder: OakEncoder,
		text: *const c_char,
		in_seconds: f64,
		out_seconds: f64,
	) -> c_int;
	/// `oakcodec_encoder_flush`.
	pub fn oakcodec_encoder_flush(encoder: OakEncoder) -> c_int;
	/// `oakcodec_encoder_last_error`.
	pub fn oakcodec_encoder_last_error(encoder: OakEncoder, buf: *mut c_char, buf_size: c_int) -> c_int;
}
