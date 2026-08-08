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

//! ffmpeg_bridge C ABI imports (audio filter graph, frames, decoder). The
//! graph converts/resamples/time-stretches planar audio; used by the
//! [`crate::processor`] resampler and the [`crate::waveform`] extractor.

use std::ffi::{c_char, c_int, c_void};

/// `FBSampleFormat` — mirrors `AVSampleFormat` (values cross the C ABI as
/// `int`). `fltp` (planar f32) is the natural exchange format for oakaudio.
///
/// `// CPP-PARITY: ffmpeg_bridge/include/ffmpeg_bridge/ffmpeg_bridge.h:117`.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SampleFormat {
	/// No format.
	None = -1,
	/// Unsigned 8-bit, packed.
	U8 = 0,
	/// Signed 16-bit, packed.
	S16 = 1,
	/// Signed 32-bit, packed.
	S32 = 2,
	/// 32-bit float, packed.
	Flt = 3,
	/// 64-bit float, packed.
	Dbl = 4,
	/// Unsigned 8-bit, planar.
	U8Planar = 5,
	/// Signed 16-bit, planar.
	S16Planar = 6,
	/// Signed 32-bit, planar.
	S32Planar = 7,
	/// 32-bit float, planar.
	Fltp = 8,
	/// 64-bit float, planar.
	Dblp = 9,
	/// Signed 64-bit, packed.
	S64 = 10,
	/// Signed 64-bit, planar.
	S64Planar = 11,
}

/// `FBAudioGraphConfig` — source graph input/output spec.
///
/// `// CPP-PARITY: ffmpeg_bridge/include/ffmpeg_bridge/ffmpeg_bridge.h:510`.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct AudioGraphConfig {
	/// Input sample rate in Hz.
	pub in_sample_rate: c_int,
	/// Input channel layout mask (`0` = derive from `in_channels`).
	pub in_channel_layout_mask: u64,
	/// Input sample format (`FBSampleFormat`; planar float in).
	pub in_sample_format: c_int,
	/// Input channel count.
	pub in_channels: c_int,
	/// Output sample rate in Hz.
	pub out_sample_rate: c_int,
	/// Output channel layout mask (`0` = derive from `out_channels`).
	pub out_channel_layout_mask: u64,
	/// Output sample format (`FBSampleFormat`).
	pub out_sample_format: c_int,
	/// Output channel count.
	pub out_channels: c_int,
	/// Whether the output is planar.
	pub out_is_planar: c_int,
	/// Time-stretch tempo multiplier.
	pub tempo: f64,
}

/// Opaque audio filter graph.
pub type AudioGraph = c_void;
/// Opaque frame.
pub type Frame = c_void;
/// Opaque packet.
pub type Packet = c_void;
/// Opaque decoder.
pub type Decoder = c_void;

/// `FBStreamInfo` — decoded stream metadata, mirroring the same-named
/// struct in ffmpeg_bridge/include/ffmpeg_bridge/ffmpeg_bridge.h. Only the
/// audio fields are consumed by oakaudio; the video/container fields are
/// kept to preserve layout.
///
/// `// CPP-PARITY: ffmpeg_bridge/include/ffmpeg_bridge/ffmpeg_bridge.h:335`.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FBStreamInfo {
	/// Stream index.
	pub index: c_int,
	/// Media type (`FBMediaType`).
	pub codec_type: c_int,
	/// Opaque FFmpeg codec id.
	pub codec_id: c_int,
	/// Non-zero if a decoder exists for this stream.
	pub has_decoder: c_int,
	/// Video width.
	pub width: c_int,
	/// Video height.
	pub height: c_int,
	/// Video pixel format (`FBPixelFormat`).
	pub pixel_format: c_int,
	/// Video field order (`FBFieldOrder`).
	pub field_order: c_int,
	/// Video color range (`FBColorRange`).
	pub color_range: c_int,
	/// Raw `AVColorPrimaries` value.
	pub color_primaries: c_int,
	/// Raw `AVColorTransferCharacteristic` value.
	pub color_trc: c_int,
	/// Sample rate in Hz.
	pub sample_rate: c_int,
	/// Sample format (`FBSampleFormat`).
	pub sample_format: c_int,
	/// Channel layout mask (never zero for valid audio).
	pub channel_layout_mask: u64,
	/// Stream start time.
	pub start_time: i64,
	/// Stream duration.
	pub duration: i64,
	/// Stream time base numerator.
	pub time_base_num: c_int,
	/// Stream time base denominator.
	pub time_base_den: c_int,
	/// Average frame rate numerator.
	pub avg_frame_rate_num: c_int,
	/// Average frame rate denominator.
	pub avg_frame_rate_den: c_int,
}

extern "C" {
	/// `fb_audio_graph_create` — build a graph from `config`.
	pub fn fb_audio_graph_create(config: *const AudioGraphConfig) -> *mut AudioGraph;
	/// `fb_audio_graph_free`.
	pub fn fb_audio_graph_free(graph: *mut *mut AudioGraph);
	/// `fb_audio_graph_push` — push planar samples; `channel_data == NULL`
	/// flushes the graph.
	pub fn fb_audio_graph_push(
		graph: *mut AudioGraph,
		channel_data: *const *const u8,
		nb_samples: c_int,
	) -> c_int;
	/// `fb_audio_graph_pull` — pull converted samples. 1 = frame produced,
	/// 0 = need more input, negative = error.
	pub fn fb_audio_graph_pull(graph: *mut AudioGraph, out_frame: *mut Frame) -> c_int;

	/// `fb_channel_layout_get_channels` — channel count of a mask.
	pub fn fb_channel_layout_get_channels(mask: u64) -> c_int;
	/// `fb_channel_layout_default` — default layout mask for `nb_channels`.
	pub fn fb_channel_layout_default(nb_channels: c_int) -> u64;

	/// `fb_frame_alloc`.
	pub fn fb_frame_alloc() -> *mut Frame;
	/// `fb_frame_free`.
	pub fn fb_frame_free(frame: *mut *mut Frame);
	/// `fb_frame_unref`.
	pub fn fb_frame_unref(frame: *mut Frame);
	/// `fb_frame_get_nb_samples`.
	pub fn fb_frame_get_nb_samples(frame: *const Frame) -> c_int;
	/// `fb_frame_set_nb_samples`.
	pub fn fb_frame_set_nb_samples(frame: *mut Frame, nb_samples: c_int);
	/// `fb_frame_get_sample_rate`.
	pub fn fb_frame_get_sample_rate(frame: *const Frame) -> c_int;
	/// `fb_frame_get_format`.
	pub fn fb_frame_get_format(frame: *const Frame) -> c_int;
	/// `fb_frame_get_channel_layout_mask`.
	pub fn fb_frame_get_channel_layout_mask(frame: *const Frame) -> u64;
	/// `fb_frame_get_data` — writable plane data.
	pub fn fb_frame_get_data(frame: *mut Frame, plane: c_int) -> *mut u8;
	/// `fb_frame_get_data_const` — read-only plane data.
	pub fn fb_frame_get_data_const(frame: *const Frame, plane: c_int) -> *const u8;
	/// `fb_frame_get_linesize`.
	pub fn fb_frame_get_linesize(frame: *const Frame, plane: c_int) -> c_int;

	/// `fb_packet_alloc`.
	pub fn fb_packet_alloc() -> *mut Packet;
	/// `fb_packet_free`.
	pub fn fb_packet_free(packet: *mut *mut Packet);
	/// `fb_packet_unref`.
	pub fn fb_packet_unref(packet: *mut Packet);

	/// `fb_decoder_create`.
	pub fn fb_decoder_create() -> *mut Decoder;
	/// `fb_decoder_free`.
	pub fn fb_decoder_free(decoder: *mut *mut Decoder);
	/// `fb_decoder_open` — open stream `stream_index` of `filename`.
	pub fn fb_decoder_open(decoder: *mut Decoder, filename: *const c_char, stream_index: c_int)
		-> c_int;
	/// `fb_decoder_close`.
	pub fn fb_decoder_close(decoder: *mut Decoder);
	/// `fb_decoder_get_frame` — decode one frame from `packet`.
	pub fn fb_decoder_get_frame(decoder: *mut Decoder, packet: *mut Packet, frame: *mut Frame) -> c_int;
	/// `fb_decoder_get_packet` — read one packet.
	pub fn fb_decoder_get_packet(decoder: *mut Decoder, packet: *mut Packet) -> c_int;
	/// `fb_decoder_get_stream_info` — copy stream info into `out`.
	pub fn fb_decoder_get_stream_info(decoder: *const Decoder, out: *mut FBStreamInfo) -> c_int;
	/// `fb_decoder_get_format_start_time`.
	pub fn fb_decoder_get_format_start_time(decoder: *const Decoder) -> i64;
	/// `fb_decoder_get_format_duration`.
	pub fn fb_decoder_get_format_duration(decoder: *const Decoder) -> i64;
}
