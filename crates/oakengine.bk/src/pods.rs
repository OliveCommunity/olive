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

//! POD mirrors for the deleted `src/bridge/` (single-lib unification).
//!
//! The facade still exchanges plain-`repr(C)` PODs with the module crates
//! (and, upward, with the host app through the frozen `oakengine_*` C
//! ABI). The deleted bridge aliased these types to the module crates'
//! `ffi` declarations; those ffi modules are gone, so the engine keeps
//! its own mirrors here. Where a module crate still owns the canonical
//! POD (oakcodec's [`EncodingParamsPOD`]) the facade aliases it directly.

use std::ffi::c_int;

/// `oakcodec_encoding_params` (`include/codec/encoder.h`) — single-lib
/// unification: aliases the oakcodec crate's POD
/// ([`oakcodec::encodingparams::EncodingParams`], identical `#[repr(C)]`
/// layout), so the facade's encoding-params handle reads/writes fields of
/// exactly the struct the oakcodec/oakaudio creators consume.
pub type EncodingParamsPOD = oakcodec::encodingparams::EncodingParams;

/// `oak_export_options` (`engine/include/oakengine/exporter.h`) — POD
/// export parameters for [`crate::codec::oakengine_export_render`]. 0 (or
/// negative) fields select the documented per-field default; the codec
/// fields carry the exporter.h `OAKENGINE_EXPORT_VIDEO_*` /
/// `OAKENGINE_EXPORT_AUDIO_*` values (NOT the engine's `ExportCodec`
/// ids — see the mapping notes on `oakengine_export_render`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakExportOptions {
	/// `OAKENGINE_EXPORT_VIDEO_*` value; default H264.
	pub video_codec: c_int,
	/// `OAKENGINE_EXPORT_AUDIO_*` value; default AAC;
	/// [`OAKENGINE_EXPORT_AUDIO_NONE`] disables the audio track.
	pub audio_codec: c_int,
	/// Video bit rate in bit/s; <= 0 lets the encoder choose (FFmpeg
	/// defaults).
	pub video_bit_rate: i64,
	/// Audio sample rate in Hz; <= 0 uses the engine's default (48 kHz).
	pub audio_sample_rate: c_int,
	/// Audio channel count (1 = mono, 2 = stereo); <= 0 uses the engine's
	/// default (stereo).
	pub audio_channel_count: c_int,
}

/// `OAKENGINE_EXPORT_VIDEO_*` — video codecs for
/// [`OakExportOptions::video_codec`].
pub const OAKENGINE_EXPORT_VIDEO_H264: c_int = 0;
/// H.265/HEVC in an MP4 container.
pub const OAKENGINE_EXPORT_VIDEO_H265: c_int = 1;
/// PNG still-image sequence.
pub const OAKENGINE_EXPORT_VIDEO_PNG_SEQUENCE: c_int = 2;

/// `OAKENGINE_EXPORT_AUDIO_*` — audio codecs for
/// [`OakExportOptions::audio_codec`].
pub const OAKENGINE_EXPORT_AUDIO_AAC: c_int = 0;
/// Uncompressed PCM.
pub const OAKENGINE_EXPORT_AUDIO_PCM: c_int = 1;
/// Disable the audio track entirely (not a codec).
pub const OAKENGINE_EXPORT_AUDIO_NONE: c_int = -1;

/// The oakaudio recording-params POD is the same shared codec POD.
pub type AudioEncodingParams = EncodingParamsPOD;

/// Zeroed encoding-params POD (all fields 0 / NUL). The codec crate's
/// struct has no zeroed constructor; this facade helper provides it.
pub fn zeroed_encoding_params() -> EncodingParamsPOD {
	// All-field zero is a valid value (enums carry their 0 variants).
	unsafe { std::mem::zeroed() }
}

/// `oakrender_video_params` (`include/render/renderer.h`) — identical
/// layout to the engine's own video-params POD.
pub type OakRenderVideoParams = crate::common::OakVideoParamsPod;

/// `oakrender_video_ticket_params` (`include/render/ticket.h`), the POD
/// the deleted bridge aliased from `oakrender::ffi`. The oakrender crate
/// now exposes value-typed `ticket::VideoTicketParams`; the facade keeps
/// this mirror for its synchronous render path (see [`crate::render`]).
/// All handle fields are the shared [`crate::handle::CHandle`].
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakVideoTicketParams {
	/// Connected texture output node (borrowed).
	pub output_node: crate::handle::CHandle,
	/// By-value oakcommon handle.
	pub video_params: crate::handle::CHandle,
	/// Borrowed oakcore audio-params handle, may be null.
	pub audio_params: *const std::ffi::c_void,
	/// Frame timestamp as rational.
	pub time_num: i64,
	/// Frame timestamp as rational.
	pub time_den: i64,
	/// Borrowed, empty ctx = null.
	pub color_manager: crate::handle::CHandle,
	/// RenderMode::Mode as int.
	pub mode: c_int,
	/// 0/0 = off.
	pub force_width: c_int,
	/// 0/0 = off.
	pub force_height: c_int,
	/// Used when has_force_matrix != 0.
	pub force_matrix: [f64; 16],
	/// 0/1.
	pub has_force_matrix: c_int,
	/// PixelFormat as int, -1 = off.
	pub force_format: c_int,
	/// 0 = off.
	pub force_channel_count: c_int,
	/// Borrowed; empty ctx = none.
	pub force_color_output: crate::handle::CHandle,
	/// By value; empty ctx = default.
	pub force_color_transform: crate::handle::CHandle,
	/// Borrowed frame cache; empty ctx = none.
	pub cache: crate::handle::CHandle,
	/// Single-footage decode filename (null = off; M12 P0).
	pub footage_filename: *const std::ffi::c_char,
	/// Media stream index for `footage_filename`.
	pub footage_stream: c_int,
	/// Sequence montage clip array (null = off; M12 P0). Clips are
	/// ordered bottom-to-top; the last element is the topmost.
	pub montage: *const MontagePod,
	/// `montage` element count.
	pub montage_count: c_int,
}

/// One sequence-montage clip (`oakrender::ffi::OakMontageClip`, M12 P0):
/// the facade resolves the timeline into this POD list; the render
/// producer decodes and composites.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct MontagePod {
	/// Footage filename (borrowed; alive for the render call).
	pub filename: *const std::ffi::c_char,
	/// Media stream index.
	pub stream_index: c_int,
	/// Clip in point (sequence time), rational.
	pub in_num: i64,
	/// Clip in point denominator.
	pub in_den: i64,
	/// Clip out point (sequence time), rational.
	pub out_num: i64,
	/// Clip out point denominator.
	pub out_den: i64,
	/// Media in point, rational.
	pub media_in_num: i64,
	/// Media in point denominator.
	pub media_in_den: i64,
	/// Playback gain (1.0 = unity).
	pub gain: f32,
}

/// The samples block handed out by `oakrender_ticket_get_samples`
/// (caller-owned; release with `oakrender_audio_samples_free`). Read by
/// the facade through the Rust type.
pub struct OakAudioSamplesOut {
	/// Interleaved f32 samples.
	pub data: Box<[f32]>,
	/// Frame count.
	pub frame_count: c_int,
	/// Sample rate (Hz).
	pub sample_rate: c_int,
	/// Channel layout mask.
	pub channel_layout: u64,
	/// Channel count.
	pub channel_count: c_int,
}

/// `oakaudio_min_max` (`include/audio/waveform.h`) — one summarized
/// waveform point of one channel.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct MinMax {
	/// Minimum of the summarized samples.
	pub min: f32,
	/// Maximum of the summarized samples.
	pub max: f32,
}

/// `oakaudio_offset_result` (`include/audio/sync.h`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OffsetResult {
	/// Offset in samples.
	pub offset_samples: i64,
	/// Correlation confidence 0..1.
	pub confidence: f64,
	/// 1 when an estimate was found.
	pub valid: c_int,
}

/// `oakaudio_stretch_offset_result` (`include/audio/sync.h`).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct StretchOffsetResult {
	/// Playback rate aligning the candidate (> 1 = speed up).
	pub rate: f64,
	/// Offset in samples.
	pub offset_samples: i64,
	/// Correlation confidence 0..1.
	pub confidence: f64,
	/// 1 when an estimate was found.
	pub valid: c_int,
}

/// `oakaudio_source_clip` (`include/audio/sync.h`) — one clip's
/// source-time metadata (rational seconds).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct SourceClip {
	/// Source start time numerator.
	pub source_start_time_num: i64,
	/// Source start time denominator.
	pub source_start_time_den: i64,
	/// Media in numerator.
	pub media_in_num: i64,
	/// Media in denominator.
	pub media_in_den: i64,
	/// 1 when the source start time is meaningful.
	pub has_source_start_time: c_int,
}

/// `i32` code -> `PixelFormat` (`repr(i32)` enum; unknown -> `Invalid`).
pub fn pixel_format_from_code(v: c_int) -> oakcore_rs::PixelFormat {
	match v {
		0 => oakcore_rs::PixelFormat::U8,
		1 => oakcore_rs::PixelFormat::U10,
		2 => oakcore_rs::PixelFormat::U16,
		3 => oakcore_rs::PixelFormat::F16,
		4 => oakcore_rs::PixelFormat::F32,
		_ => oakcore_rs::PixelFormat::Invalid,
	}
}

/// `i32` code -> `SampleFormat` (`repr(i32)` enum; unknown -> `Invalid`).
pub fn sample_format_from_code(v: c_int) -> oakcore_rs::SampleFormat {
	match v {
		0 => oakcore_rs::SampleFormat::U8Planar,
		1 => oakcore_rs::SampleFormat::S16Planar,
		2 => oakcore_rs::SampleFormat::S32Planar,
		3 => oakcore_rs::SampleFormat::S64Planar,
		4 => oakcore_rs::SampleFormat::F32Planar,
		5 => oakcore_rs::SampleFormat::F64Planar,
		6 => oakcore_rs::SampleFormat::U8,
		7 => oakcore_rs::SampleFormat::S16,
		8 => oakcore_rs::SampleFormat::S32,
		9 => oakcore_rs::SampleFormat::S64,
		10 => oakcore_rs::SampleFormat::F32,
		11 => oakcore_rs::SampleFormat::F64,
		_ => oakcore_rs::SampleFormat::Invalid,
	}
}

/// `i32` code -> `VideoScalingMethod` (unknown -> `Stretch`, the C++
/// default).
pub fn scaling_from_code(v: c_int) -> oakcodec::encodingparams::VideoScalingMethod {
	match v {
		0 => oakcodec::encodingparams::VideoScalingMethod::Fit,
		2 => oakcodec::encodingparams::VideoScalingMethod::Crop,
		_ => oakcodec::encodingparams::VideoScalingMethod::Stretch,
	}
}
