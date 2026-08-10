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

//! oakaudio C ABI imports, mirroring the oakaudio crate's exports
//! (`src/audio/rust/src/ffi.rs`; headers `include/audio/*.h`), plus the
//! liboakcore `oakcore_audioparams_*` readers used to convert the
//! engine's borrowed `OakAudioParams*` handles.

use std::ffi::{c_char, c_double, c_int, c_void};

use crate::handle::CHandle;

/// `oakaudio_sync_offset_result` mirror — `oak_audio_waveform_offset`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OffsetResult {
	/// Offset of the candidate relative to the reference, in samples.
	pub offset_samples: i64,
	/// Normalized correlation confidence in `[0, 1]`.
	pub confidence: c_double,
	/// Whether an offset could be determined.
	pub valid: c_int,
}

/// `oakaudio_stretch_offset_result` mirror — `oak_audio_waveform_stretch_offset`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct StretchOffsetResult {
	/// Playback rate aligning the candidate (`> 1` = speed up).
	pub rate: c_double,
	/// Offset in samples.
	pub offset_samples: i64,
	/// Normalized correlation confidence in `[0, 1]`.
	pub confidence: c_double,
	/// Whether a rate+offset could be determined.
	pub valid: c_int,
}

/// `oakaudio_sync_source_clip` mirror — `oak_audio_sync_source_clip`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct SourceClip {
	/// Source start time numerator (seconds).
	pub source_start_time_num: i64,
	/// Source start time denominator (seconds).
	pub source_start_time_den: i64,
	/// Media in point numerator (seconds).
	pub media_in_num: i64,
	/// Media in point denominator (seconds).
	pub media_in_den: i64,
	/// Whether `source_start_time` is set.
	pub has_source_start_time: c_int,
}

extern "C" {
	// ---- manager.h --------------------------------------------------------
	/// `oakaudio_manager_create_instance` — create the singleton.
	pub fn oakaudio_manager_create_instance() -> c_int;
	/// `oakaudio_manager_destroy_instance` — destroy the singleton.
	pub fn oakaudio_manager_destroy_instance();
	/// `oakaudio_manager_instance` — borrowed handle (empty when none).
	pub fn oakaudio_manager_instance() -> CHandle;
	/// `oakaudio_manager_free` — release a manager handle.
	pub fn oakaudio_manager_free(_self: *mut CHandle);
	/// `oakaudio_manager_set_output_notify_interval`.
	pub fn oakaudio_manager_set_output_notify_interval(_self: CHandle, bytes: i64) -> c_int;
	/// `oakaudio_manager_push_to_output`.
	pub fn oakaudio_manager_push_to_output(
		_self: CHandle,
		rate: c_int,
		layout: u64,
		format: c_int,
		samples: *const c_char,
		samples_size: i64,
		error_buf: *mut c_char,
		error_buf_size: c_int,
	) -> c_int;
	/// `oakaudio_manager_clear_buffered_output`.
	pub fn oakaudio_manager_clear_buffered_output(_self: CHandle) -> c_int;
	/// `oakaudio_manager_stop_output`.
	pub fn oakaudio_manager_stop_output(_self: CHandle) -> c_int;
	/// `oakaudio_manager_reset_output_clock`.
	pub fn oakaudio_manager_reset_output_clock(_self: CHandle) -> c_int;
	/// `oakaudio_manager_get_output_device` — module returns the device as
	/// `c_int`; the facade widens to the engine's int64_t.
	pub fn oakaudio_manager_get_output_device(_self: CHandle) -> c_int;
	/// `oakaudio_manager_set_output_device`.
	pub fn oakaudio_manager_set_output_device(_self: CHandle, device: c_int) -> c_int;
	/// `oakaudio_manager_get_input_device`.
	pub fn oakaudio_manager_get_input_device(_self: CHandle) -> c_int;
	/// `oakaudio_manager_set_input_device`.
	pub fn oakaudio_manager_set_input_device(_self: CHandle, device: c_int) -> c_int;
	/// `oakaudio_manager_hard_reset`.
	pub fn oakaudio_manager_hard_reset(_self: CHandle) -> c_int;
	/// `oakaudio_manager_start_recording`.
	pub fn oakaudio_manager_start_recording(
		_self: CHandle,
		params: *const EncodingParams,
		error_buf: *mut c_char,
		error_buf_size: c_int,
	) -> c_int;
	/// `oakaudio_manager_stop_recording`.
	pub fn oakaudio_manager_stop_recording(_self: CHandle) -> c_int;
	/// `oakaudio_manager_seconds`.
	pub fn oakaudio_manager_seconds(_self: CHandle) -> i64;

	// ---- processor.h ------------------------------------------------------
	/// `oakaudio_processor_init` — new processor, refcount 1.
	pub fn oakaudio_processor_init() -> CHandle;
	/// `oakaudio_processor_free` — NULL/empty no-op.
	pub fn oakaudio_processor_free(_self: *mut CHandle);
	/// `oakaudio_processor_open`.
	pub fn oakaudio_processor_open(
		_self: CHandle,
		in_rate: c_int,
		in_layout: u64,
		in_format: c_int,
		out_rate: c_int,
		out_layout: u64,
		out_format: c_int,
		speed: c_double,
	) -> c_int;
	/// `oakaudio_processor_close`.
	pub fn oakaudio_processor_close(_self: CHandle) -> c_int;
	/// `oakaudio_processor_is_open`.
	pub fn oakaudio_processor_is_open(_self: CHandle) -> c_int;

	// ---- sync.h -----------------------------------------------------------
	/// `oakaudio_sync_estimate_envelope_offset`.
	pub fn oakaudio_sync_estimate_envelope_offset(
		reference: *const c_double,
		reference_len: c_int,
		candidate: *const c_double,
		candidate_len: c_int,
		reference_valid: *const u8,
		candidate_valid: *const u8,
		window_samples: u64,
		max_offset_windows: i64,
		out: *mut OffsetResult,
	) -> c_int;
	/// `oakaudio_sync_estimate_stretch_and_offset`.
	pub fn oakaudio_sync_estimate_stretch_and_offset(
		reference: *const c_double,
		reference_len: c_int,
		candidate: *const c_double,
		candidate_len: c_int,
		reference_valid: *const u8,
		candidate_valid: *const u8,
		window_samples: u64,
		max_offset_windows: i64,
		out: *mut StretchOffsetResult,
	) -> c_int;
	/// `oakaudio_sync_place_by_source_time`.
	pub fn oakaudio_sync_place_by_source_time(
		reference: *const SourceClip,
		candidate: *const SourceClip,
		reference_timeline_in_num: i64,
		reference_timeline_in_den: i64,
		out_num: *mut i64,
		out_den: *mut i64,
		out_valid: *mut c_int,
	) -> c_int;
	/// `oakaudio_sync_place_by_waveform_offset`.
	pub fn oakaudio_sync_place_by_waveform_offset(
		reference_timeline_in_num: i64,
		reference_timeline_in_den: i64,
		candidate_offset_samples: i64,
		sample_rate: c_int,
		out_num: *mut i64,
		out_den: *mut i64,
		out_valid: *mut c_int,
	) -> c_int;

	// ---- liboakcore (oakcore_audioparams_*) --------------------------------
	/// `oakcore_audioparams_create` — new owned params (release with
	/// [`oakcore_audioparams_free`]).
	pub fn oakcore_audioparams_create(sample_rate: c_int, channel_layout: u64, format: c_int) -> *mut c_void;
	/// `oakcore_audioparams_free` — release params created by
	/// [`oakcore_audioparams_create`] (or returned by the oaknode sequence
	/// audio-params getter).
	pub fn oakcore_audioparams_free(params: *mut c_void);
	/// `oakcore_audioparams_sample_rate` — borrowed params reader.
	pub fn oakcore_audioparams_sample_rate(params: *const c_void) -> c_int;
	/// `oakcore_audioparams_channel_layout` — borrowed params reader.
	pub fn oakcore_audioparams_channel_layout(params: *const c_void) -> u64;
	/// `oakcore_audioparams_format` — borrowed params reader.
	pub fn oakcore_audioparams_format(params: *const c_void) -> c_int;
}

/// Opaque mirror of the oakaudio recording-params POD
/// (`include/audio/manager.h`). The facade passes the engine's
/// `OakEngineEncodingParams*` through untouched — both are the same C ABI
/// mirror of `olive::EncodingParams`.
#[repr(C)]
pub struct EncodingParams {
	_opaque: [u8; 0],
}
