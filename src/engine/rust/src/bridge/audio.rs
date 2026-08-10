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

//! oakaudio C ABI bridge: direct Rust calls into the `oakaudio` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oakaudio`'s `ffi` (the
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

//! oakaudio C ABI imports, mirroring the oakaudio crate's exports
//! (`src/audio/rust/src/ffi.rs`; headers `include/audio/*.h`), plus the
//! liboakcore `oakcore_audioparams_*` readers used to convert the
//! engine's borrowed `OakAudioParams*` handles.

use std::ffi::{c_char, c_double, c_int, c_void};

use crate::handle::CHandle;

/// `oakaudio_offsetresult` C ABI POD. Single-lib unification: aliases
/// the oakaudio crate's struct (identical layout).
pub type OffsetResult = oakaudio::ffi::sync::OffsetResult;


/// `oakaudio_stretchoffsetresult` C ABI POD. Single-lib unification: aliases
/// the oakaudio crate's struct (identical layout).
pub type StretchOffsetResult = oakaudio::ffi::sync::StretchOffsetResult;


/// `oakaudio_sourceclip` C ABI POD. Single-lib unification: aliases
/// the oakaudio crate's struct (identical layout).
pub type SourceClip = oakaudio::ffi::sync::SourceClip;



/// Opaque mirror of the oakaudio recording-params POD
/// (`include/audio/manager.h`). The facade passes the engine's
/// `OakEngineEncodingParams*` through untouched — both are the same C ABI
/// mirror of `olive::EncodingParams`.
#[repr(C)]
pub struct EncodingParams {
	_opaque: [u8; 0],
}

extern "C" {
	pub fn oakcore_audioparams_create(sample_rate: c_int, channel_layout: u64, format: c_int) -> *mut c_void;
	pub fn oakcore_audioparams_free(params: *mut c_void);
	pub fn oakcore_audioparams_sample_rate(params: *const c_void) -> c_int;
	pub fn oakcore_audioparams_channel_layout(params: *const c_void) -> u64;
	pub fn oakcore_audioparams_format(params: *const c_void) -> c_int;
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_create_instance() -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_create_instance() }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_destroy_instance() {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_destroy_instance() }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_instance() -> CHandle {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_instance() }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_free(_self: *mut CHandle) {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_free(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_set_output_notify_interval(_self: CHandle, bytes: i64) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_set_output_notify_interval(_self, bytes) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_push_to_output(
		_self: CHandle,
		rate: c_int,
		layout: u64,
		format: c_int,
		samples: *const c_char,
		samples_size: i64,
		error_buf: *mut c_char,
		error_buf_size: c_int,
	) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_push_to_output(_self, rate, layout, format, samples, samples_size, error_buf, error_buf_size) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_clear_buffered_output(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_clear_buffered_output(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_stop_output(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_stop_output(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_reset_output_clock(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_reset_output_clock(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_get_output_device(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_get_output_device(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_set_output_device(_self: CHandle, device: c_int) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_set_output_device(_self, device) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_get_input_device(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_get_input_device(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_set_input_device(_self: CHandle, device: c_int) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_set_input_device(_self, device) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_hard_reset(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_hard_reset(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
/// `oakaudio_manager_start_recording` — crosses the encoding-params C ABI
/// POD (the facade keeps its own opaque mirror; the module's
/// `oakaudio::bridge::codec::EncodingParams`). Kept as a link-time
/// `extern "C"` declaration against the frozen module C ABI.
pub fn oakaudio_manager_start_recording(
	_self: CHandle,
	params: *const EncodingParams,
	error_buf: *mut c_char,
	error_buf_size: c_int,
) -> c_int {
	unsafe { oakaudio_start_recording_extern(_self, params, error_buf, error_buf_size) }
}

extern "C" {
	#[link_name = "oakaudio_manager_start_recording"]
	pub fn oakaudio_start_recording_extern(
		_self: CHandle,
		params: *const EncodingParams,
		error_buf: *mut c_char,
		error_buf_size: c_int,
	) -> c_int;
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_stop_recording(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_stop_recording(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_manager_seconds(_self: CHandle, out: *mut c_double) -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_manager_seconds(_self, out) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_processor_init() -> CHandle {
	unsafe { oakaudio::ffi::processor::oakaudio_processor_init() }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_processor_free(_self: *mut CHandle) {
	unsafe { oakaudio::ffi::processor::oakaudio_processor_free(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_processor_open(
		_self: CHandle,
		in_rate: c_int,
		in_layout: u64,
		in_format: c_int,
		out_rate: c_int,
		out_layout: u64,
		out_format: c_int,
		speed: c_double,
	) -> c_int {
	unsafe { oakaudio::ffi::processor::oakaudio_processor_open(_self, in_rate, in_layout, in_format, out_rate, out_layout, out_format, speed) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_processor_close(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::processor::oakaudio_processor_close(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_processor_is_open(_self: CHandle) -> c_int {
	unsafe { oakaudio::ffi::processor::oakaudio_processor_is_open(_self) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
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
	) -> c_int {
	unsafe { oakaudio::ffi::sync::oakaudio_sync_estimate_envelope_offset(reference, reference_len, candidate, candidate_len, reference_valid, candidate_valid, window_samples, max_offset_windows, out) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_sync_estimate_stretch_and_offset(
		reference: *const c_double,
		reference_len: c_int,
		candidate: *const c_double,
		candidate_len: c_int,
		reference_valid: *const u8,
		candidate_valid: *const u8,
		window_samples: u64,
		max_offset_windows: i64,
		min_rate: c_double,
		max_rate: c_double,
		rate_step: c_double,
		out: *mut StretchOffsetResult,
	) -> c_int {
	unsafe { oakaudio::ffi::sync::oakaudio_sync_estimate_stretch_and_offset(reference, reference_len, candidate, candidate_len, reference_valid, candidate_valid, window_samples, max_offset_windows, min_rate, max_rate, rate_step, out) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_sync_place_by_source_time(
		reference: *const SourceClip,
		candidate: *const SourceClip,
		reference_timeline_in_num: i64,
		reference_timeline_in_den: i64,
		out_num: *mut i64,
		out_den: *mut i64,
		out_valid: *mut c_int,
	) -> c_int {
	unsafe { oakaudio::ffi::sync::oakaudio_sync_place_by_source_time(reference, candidate, reference_timeline_in_num, reference_timeline_in_den, out_num, out_den, out_valid) }
}

/// Direct call into the `oakaudio` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakaudio_sync_place_by_waveform_offset(
		reference_timeline_in_num: i64,
		reference_timeline_in_den: i64,
		candidate_offset_samples: i64,
		sample_rate: c_int,
		out_num: *mut i64,
		out_den: *mut i64,
		out_valid: *mut c_int,
	) -> c_int {
	unsafe { oakaudio::ffi::sync::oakaudio_sync_place_by_waveform_offset(reference_timeline_in_num, reference_timeline_in_den, candidate_offset_samples, sample_rate, out_num, out_den, out_valid) }
}

