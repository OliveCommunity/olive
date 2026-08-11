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

//! C ABI export layer: implements `include/audio/*.h` verbatim.
//!
//! Organization: one submodule per public header. The authoritative
//! function list is the header itself; each submodule below carries a
//! complete inventory comment plus the export stubs. Bodies only
//! unwrap handles, call safe Rust, and map results through
//! [`crate::handle::guard*`].

use std::ffi::{c_char, c_double, c_int, CStr};

use oakcore_rs::Rational;

use crate::bridge::codec::EncodingParams;
use crate::error::{Error, OAKAUDIO_E_INVALID};
use crate::handle::{guard, guard_handle, guard_int, guard_void, invalid_if, write_error, CHandle};
use crate::params::{AudioParams, SampleFormat};
use crate::waveform::{AudioVisualWaveform, SamplePerChannel};

/// Map a C sample-format int to the native [`SampleFormat`]; out-of-range
/// values map to `Invalid` (which callers reject with `OAKAUDIO_E_INVALID`).
fn sample_format_from_c(format: c_int) -> SampleFormat {
	match format {
		0 => SampleFormat::U8Planar,
		1 => SampleFormat::S16Planar,
		2 => SampleFormat::S32Planar,
		3 => SampleFormat::S64Planar,
		4 => SampleFormat::F32Planar,
		5 => SampleFormat::F64Planar,
		6 => SampleFormat::U8,
		7 => SampleFormat::S16,
		8 => SampleFormat::S32,
		9 => SampleFormat::S64,
		10 => SampleFormat::F32,
		11 => SampleFormat::F64,
		_ => SampleFormat::Invalid,
	}
}

/// Convert a C bool mask into a `Vec<bool>`; NULL means "all windows valid"
/// (an empty vec, which the correlation treats as all-valid).
///
/// `// CPP-PARITY: src/audio/c_api/sync.cpp:40` (`to_mask`).
fn to_bool_mask(mask: *const u8, len: usize) -> Vec<bool> {
	if mask.is_null() {
		return Vec::new();
	}
	// SAFETY: the caller guarantees `len` valid bytes at `mask` when
	// non-NULL (the header requires masks to match the envelope length).
	unsafe { std::slice::from_raw_parts(mask, len) }
		.iter()
		.map(|&b| b != 0)
		.collect()
}

/// Build a `Vec<&[f32]>` of planar views from a C plane array. NULL planes
/// become empty slices; the caller guarantees `planar` points to at least
/// `channel_count` entries and each plane to `frame_count` floats for the
/// duration of the call.
///
/// # Safety
/// `planar` must be NULL (yields an empty vec) or valid for
/// `channel_count` plane pointers, each valid for `frame_count` floats.
unsafe fn planar_views<'a>(
	planar: *const *const f32,
	channel_count: i32,
	frame_count: i32,
) -> Vec<&'a [f32]> {
	if planar.is_null() {
		return Vec::new();
	}
	let mut views: Vec<&'a [f32]> = Vec::with_capacity(channel_count.max(0) as usize);
	for ch in 0..channel_count {
		let p = unsafe { *planar.add(ch as usize) };
		if p.is_null() {
			views.push(&[]);
		} else {
			views.push(unsafe { std::slice::from_raw_parts(p, frame_count as usize) });
		}
	}
	views
}

/// View a C double array; NULL or a non-positive length yields an empty
/// slice.
///
/// # Safety
/// `ptr` must be valid for `len` doubles when both are non-zero.
unsafe fn double_slice<'a>(ptr: *const c_double, len: c_int) -> &'a [f64] {
	if ptr.is_null() || len <= 0 {
		&[]
	} else {
		unsafe { std::slice::from_raw_parts(ptr, len as usize) }
	}
}

/// Convert a C (num, den) rational pair; `Error::Invalid` when `den` is
/// zero (waveform.h: "den must be non-zero"; mirrors C++ `make_rational`).
///
/// `// CPP-PARITY: src/audio/c_api/waveform.cpp:56` (`make_rational`).
fn rational_from_parts(num: i64, den: i64) -> crate::error::Result<Rational> {
	invalid_if(den == 0)?;
	Ok(Rational::new(num, den))
}

/// `include/audio/manager.h` exports (complete inventory): the singleton
/// `OakAudioManager` is a borrowed handle (addref/release no-ops);
/// `oakaudio_manager_create_instance` /
/// `oakaudio_manager_destroy_instance` /
/// `oakaudio_manager_instance` manage the singleton;
/// `oakaudio_manager_free` / `set_output_notify_interval` /
/// `push_to_output` / `clear_buffered_output` / `stop_output` /
/// `seconds` / `reset_output_clock` / `get_output_device` /
/// `set_output_device` / `get_input_device` / `set_input_device` /
/// `hard_reset` / `start_recording` / `stop_recording` /
/// `find_config_device_by_name_s` / `find_device_by_name_s` /
/// `oakaudio_debug_alive_count`.
pub mod manager {
	use super::*;

	/// `oakaudio_manager_create_instance`: OAKAUDIO_OK, or OAKAUDIO_E_*.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_create_instance() -> c_int {
		guard(|| crate::manager::create_instance())
	}

	/// `oakaudio_manager_destroy_instance`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_destroy_instance() {
		guard_void(|| crate::manager::destroy_instance());
	}

	/// `oakaudio_manager_instance`: borrowed handle (never owned).
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_instance() -> CHandle {
		guard_handle(|| Ok(crate::manager::instance()))
	}

	/// `oakaudio_manager_free`: NULL/empty no-op.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_free(_self: *mut CHandle) {
		crate::manager::free(_self);
	}

	/// `oakaudio_manager_set_output_notify_interval`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_set_output_notify_interval(
		_self: CHandle,
		bytes: i64,
	) -> c_int {
		guard(|| crate::manager::set_output_notify_interval(&_self, bytes))
	}

	/// `oakaudio_manager_push_to_output`: queue raw interleaved sample bytes.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_push_to_output(
		_self: CHandle,
		rate: c_int,
		layout: u64,
		format: c_int,
		samples: *const c_char,
		samples_size: i64,
		error_buf: *mut c_char,
		error_buf_size: c_int,
	) -> c_int {
		guard(|| {
			// CPP-PARITY: `rate <= 0 || !samples || samples_size < 0`; an
			// unrepresentable sample format is additionally rejected (the
			// C++ casts the raw int into the enum without validating).
			invalid_if(rate <= 0 || samples.is_null() || samples_size < 0)?;
			let params = AudioParams {
				sample_rate: rate,
				channel_layout: layout,
				format: sample_format_from_c(format),
			};
			invalid_if(params.format == SampleFormat::Invalid)?;
			let samples: &[u8] = if samples_size <= 0 {
				&[]
			} else {
				// SAFETY: the caller guarantees `samples_size` bytes.
				unsafe { std::slice::from_raw_parts(samples as *const u8, samples_size as usize) }
			};
			let r = crate::manager::push_to_output(&_self, params, samples, &mut []);
			// CPP-PARITY: the manager's error string is surfaced through
			// error_buf on the failure path only.
			if let Err(Error::Failed(msg)) = &r {
				write_error(msg, error_buf, error_buf_size);
			}
			r
		})
	}

	/// `oakaudio_manager_clear_buffered_output`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_clear_buffered_output(_self: CHandle) -> c_int {
		guard(|| crate::manager::clear_buffered_output(&_self))
	}

	/// `oakaudio_manager_stop_output`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_stop_output(_self: CHandle) -> c_int {
		guard(|| crate::manager::stop_output(&_self))
	}

	/// `oakaudio_manager_seconds`: write elapsed playback seconds into `out`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_seconds(_self: CHandle, out: *mut c_double) -> c_int {
		guard(|| {
			invalid_if(out.is_null())?;
			let mut seconds = 0.0f64;
			crate::manager::seconds(&_self, &mut seconds)?;
			unsafe {
				*out = seconds;
			}
			Ok(())
		})
	}

	/// `oakaudio_manager_reset_output_clock`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_reset_output_clock(_self: CHandle) -> c_int {
		guard(|| crate::manager::reset_output_clock(&_self))
	}

	/// `oakaudio_manager_get_output_device`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_get_output_device(_self: CHandle) -> c_int {
		guard_int(|| crate::manager::get_output_device(&_self))
	}

	/// `oakaudio_manager_set_output_device`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_set_output_device(
		_self: CHandle,
		device: c_int,
	) -> c_int {
		guard(|| crate::manager::set_output_device(&_self, device))
	}

	/// `oakaudio_manager_get_input_device`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_get_input_device(_self: CHandle) -> c_int {
		guard_int(|| crate::manager::get_input_device(&_self))
	}

	/// `oakaudio_manager_set_input_device`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_set_input_device(
		_self: CHandle,
		device: c_int,
	) -> c_int {
		guard(|| crate::manager::set_input_device(&_self, device))
	}

	/// `oakaudio_manager_hard_reset`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_hard_reset(_self: CHandle) -> c_int {
		guard(|| crate::manager::hard_reset(&_self))
	}

	/// `oakaudio_manager_start_recording`: record input to `params` via the
	/// oakcodec encoder.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_start_recording(
		_self: CHandle,
		params: *const EncodingParams,
		error_buf: *mut c_char,
		error_buf_size: c_int,
	) -> c_int {
		guard(|| {
			// SAFETY: the caller guarantees `params` is a valid pointer when
			// non-NULL.
			let Some(params) = (unsafe { params.as_ref() }) else {
				// CPP-PARITY: `!params || !params->audio_enabled`; the error
				// string is written on the invalid path too so callers can
				// always diagnose a failed start.
				write_error("invalid recording parameters", error_buf, error_buf_size);
				return Err(Error::Invalid);
			};
			if params.audio_enabled == 0 {
				write_error("invalid recording parameters", error_buf, error_buf_size);
				return Err(Error::Invalid);
			}
			let r = crate::manager::start_recording(&_self, params, &mut []);
			if let Err(Error::Failed(msg)) = &r {
				write_error(msg, error_buf, error_buf_size);
			}
			r
		})
	}

	/// `oakaudio_manager_stop_recording`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_stop_recording(_self: CHandle) -> c_int {
		guard(|| crate::manager::stop_recording(&_self))
	}

	/// `oakaudio_manager_find_config_device_by_name_s`: static, no handle.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_find_config_device_by_name_s(
		is_output_device: c_int,
	) -> c_int {
		crate::manager::find_config_device_by_name_s(is_output_device != 0)
	}

	/// `oakaudio_manager_find_device_by_name_s`: static, no handle.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_manager_find_device_by_name_s(
		name: *const c_char,
		is_output_device: c_int,
	) -> c_int {
		if name.is_null() {
			return OAKAUDIO_E_INVALID;
		}
		// SAFETY: the caller guarantees a NUL-terminated string.
		let name = unsafe { CStr::from_ptr(name) };
		crate::manager::find_device_by_name_s(name, is_output_device != 0)
	}

	/// `oakaudio_debug_alive_count`: surviving object count.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_debug_alive_count() -> c_int {
		crate::manager::debug_alive_count()
	}
}

/// `include/audio/processor.h` exports (complete inventory): the neutral
/// by-value `OakAudioProcessor` handle (refcounted object, no singleton
/// semantics); `oakaudio_processor_init` / `oakaudio_processor_free` /
/// `oakaudio_processor_open` / `oakaudio_processor_close` /
/// `oakaudio_processor_is_open` / `oakaudio_processor_convert` /
/// `oakaudio_processor_flush`. `OAKAUDIO_PROCESSOR_OUTPUT_FORMAT == 4`
/// (`SampleFormat::f32_p`) pins the planar-first sample format ordering.
pub mod processor {
	use super::*;

	/// `oakaudio_processor_init`: new processor, refcount 1.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_processor_init() -> CHandle {
		guard_handle(crate::processor::init)
	}

	/// `oakaudio_processor_free`: NULL/empty no-op.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_processor_free(_self: *mut CHandle) {
		crate::processor::free(_self);
	}

	/// `oakaudio_processor_open`: configure a resampling/conversion graph.
	///
	/// Validation order and codes follow the C++ exactly (empty handle,
	/// already-open state, rates/speed, forced planar-float output);
	/// `in_format` is passed through unvalidated, matching `SampleFormat(
	/// SampleFormat::Format(in_format))` in the C++.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_processor_open(
		_self: CHandle,
		in_rate: c_int,
		in_layout: u64,
		in_format: c_int,
		out_rate: c_int,
		out_layout: u64,
		out_format: c_int,
		speed: c_double,
	) -> c_int {
		guard(|| {
			let from = AudioParams {
				sample_rate: in_rate,
				channel_layout: in_layout,
				format: sample_format_from_c(in_format),
			};
			let to = AudioParams {
				sample_rate: out_rate,
				channel_layout: out_layout,
				format: sample_format_from_c(out_format),
			};
			crate::processor::open(&_self, from, to, speed)
		})
	}

	/// `oakaudio_processor_close`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_processor_close(_self: CHandle) -> c_int {
		guard(|| crate::processor::close(&_self))
	}

	/// `oakaudio_processor_is_open`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_processor_is_open(_self: CHandle) -> c_int {
		guard_int(|| Ok(crate::processor::is_open(&_self)? as i32))
	}

	/// `oakaudio_processor_convert`: resample/convert planar float in-place.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_processor_convert(
		_self: CHandle,
		in_planar: *const *const f32,
		in_frame_count: c_int,
		out_planar: *const *mut f32,
		out_capacity_frames: c_int,
	) -> c_int {
		guard_int(|| {
			crate::processor::convert(
				&_self,
				in_planar,
				in_frame_count,
				out_planar,
				out_capacity_frames,
			)
		})
	}

	/// `oakaudio_processor_flush`: drain buffered frames.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_processor_flush(_self: CHandle) -> c_int {
		guard(|| crate::processor::flush(&_self))
	}
}

/// `include/audio/sync.h` exports (complete inventory): stateless placement
/// and envelope helpers (no handle); `oakaudio_offset_result`,
/// `oakaudio_stretch_offset_result`, `oakaudio_source_clip` value structs;
/// `oakaudio_sync_extract_rms_envelope` /
/// `oakaudio_sync_estimate_envelope_offset` /
/// `oakaudio_sync_estimate_stretch_and_offset` /
/// `oakaudio_sync_place_by_source_time` /
/// `oakaudio_sync_place_by_waveform_offset`.
pub mod sync {
	use super::*;

	/// `oakaudio_offset_result` — candidate offset + correlation confidence.
	#[repr(C)]
	pub struct OffsetResult {
		/// Offset of the candidate relative to the reference, in samples.
		pub offset_samples: i64,
		/// Normalized correlation confidence in `[0, 1]`.
		pub confidence: c_double,
		/// Whether an offset could be determined.
		pub valid: c_int,
	}

	/// `oakaudio_stretch_offset_result` — rate + offset + confidence.
	#[repr(C)]
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

	/// `oakaudio_source_clip` — one clip's source-time metadata.
	#[repr(C)]
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

	/// `oakaudio_sync_extract_rms_envelope`: windowed RMS envelope.
	///
	/// Two-stage contract: `out == NULL` or `capacity < windows` returns the
	/// required window count without writing.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_sync_extract_rms_envelope(
		planar: *const *const f32,
		channel_count: c_int,
		frame_count: c_int,
		window_samples: u64,
		out: *mut c_double,
		capacity: c_int,
	) -> c_int {
		guard_int(|| {
			// CPP-PARITY: sync.cpp:58 — every plane must be non-NULL.
			invalid_if(
				planar.is_null()
					|| channel_count <= 0
					|| frame_count < 0
					|| window_samples == 0
					|| capacity < 0,
			)?;
			for ch in 0..channel_count {
				// SAFETY: `planar` is non-NULL with `channel_count` entries.
				if unsafe { *planar.add(ch as usize) }.is_null() {
					return Err(Error::Invalid);
				}
			}
			let views = unsafe { planar_views(planar, channel_count, frame_count) };
			let envelope =
				crate::waveformsync::extract_rms_envelope(&views, window_samples as usize);
			let windows = envelope.len();
			if out.is_null() || (capacity as usize) < windows {
				return Ok(windows as i32);
			}
			unsafe {
				std::ptr::copy_nonoverlapping(envelope.as_ptr(), out, windows);
			}
			Ok(windows as i32)
		})
	}

	/// `oakaudio_sync_estimate_envelope_offset`: masked envelope correlation.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_sync_estimate_envelope_offset(
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
		guard(|| {
			// CPP-PARITY: sync.cpp:93 — NULL arrays and non-positive lengths
			// are invalid; masks may be NULL (all windows valid).
			invalid_if(
				out.is_null()
					|| reference.is_null()
					|| candidate.is_null()
					|| reference_len <= 0
					|| candidate_len <= 0
					|| window_samples == 0
					|| max_offset_windows < 0,
			)?;
			let reference = unsafe { double_slice(reference, reference_len) };
			let candidate = unsafe { double_slice(candidate, candidate_len) };
			let ref_valid = to_bool_mask(reference_valid, reference_len as usize);
			let cand_valid = to_bool_mask(candidate_valid, candidate_len as usize);
			let result = crate::waveformsync::estimate_envelope_offset_valid(
				reference,
				candidate,
				&ref_valid,
				&cand_valid,
				window_samples as usize,
				max_offset_windows,
			);
			// SAFETY: `out` is non-NULL (checked above).
			unsafe {
				*out = OffsetResult {
					offset_samples: result.offset_samples,
					confidence: result.confidence,
					valid: result.valid as c_int,
				};
			}
			Ok(())
		})
	}

	/// `oakaudio_sync_estimate_stretch_and_offset`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_sync_estimate_stretch_and_offset(
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
		guard(|| {
			// CPP-PARITY: sync.cpp:123.
			invalid_if(
				out.is_null()
					|| reference.is_null()
					|| candidate.is_null()
					|| reference_len <= 0
					|| candidate_len <= 0
					|| window_samples == 0
					|| max_offset_windows < 0
					|| min_rate <= 0.0
					|| max_rate < min_rate
					|| rate_step <= 0.0,
			)?;
			let reference = unsafe { double_slice(reference, reference_len) };
			let candidate = unsafe { double_slice(candidate, candidate_len) };
			let ref_valid = to_bool_mask(reference_valid, reference_len as usize);
			let cand_valid = to_bool_mask(candidate_valid, candidate_len as usize);
			let result = crate::waveformsync::estimate_stretch_and_offset(
				reference,
				candidate,
				&ref_valid,
				&cand_valid,
				window_samples as usize,
				max_offset_windows,
				min_rate,
				max_rate,
				rate_step,
			);
			// SAFETY: `out` is non-NULL (checked above).
			unsafe {
				*out = StretchOffsetResult {
					rate: result.rate,
					offset_samples: result.offset_samples,
					confidence: result.confidence,
					valid: result.valid as c_int,
				};
			}
			Ok(())
		})
	}

	/// `oakaudio_sync_place_by_source_time`: timeline placement by source time.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_sync_place_by_source_time(
		reference: *const SourceClip,
		candidate: *const SourceClip,
		reference_timeline_in_num: i64,
		reference_timeline_in_den: i64,
		out_num: *mut i64,
		out_den: *mut i64,
		out_valid: *mut c_int,
	) -> c_int {
		guard(|| {
			// CPP-PARITY: sync.cpp:153 — every denominator must be non-zero.
			// SAFETY: non-NULL pointers are valid for one struct each.
			let Some(reference) = (unsafe { reference.as_ref() }) else {
				return Err(Error::Invalid);
			};
			let Some(candidate) = (unsafe { candidate.as_ref() }) else {
				return Err(Error::Invalid);
			};
			invalid_if(
				out_num.is_null()
					|| out_den.is_null()
					|| out_valid.is_null()
					|| reference.source_start_time_den == 0
					|| reference.media_in_den == 0
					|| candidate.source_start_time_den == 0
					|| candidate.media_in_den == 0
					|| reference_timeline_in_den == 0,
			)?;
			let reference_clip = crate::synchronizer::SourceClip {
				source_start_time: Rational::new(
					reference.source_start_time_num,
					reference.source_start_time_den,
				),
				media_in: Rational::new(reference.media_in_num, reference.media_in_den),
				has_source_start_time: reference.has_source_start_time != 0,
			};
			let candidate_clip = crate::synchronizer::SourceClip {
				source_start_time: Rational::new(
					candidate.source_start_time_num,
					candidate.source_start_time_den,
				),
				media_in: Rational::new(candidate.media_in_num, candidate.media_in_den),
				has_source_start_time: candidate.has_source_start_time != 0,
			};
			let placement = crate::synchronizer::place_by_source_time(
				&reference_clip,
				&candidate_clip,
				Rational::new(reference_timeline_in_num, reference_timeline_in_den),
			);
			// SAFETY: out pointers are non-NULL (checked above).
			unsafe {
				*out_num = placement.timeline_in.numerator();
				*out_den = placement.timeline_in.denominator();
				*out_valid = placement.valid as c_int;
			}
			Ok(())
		})
	}

	/// `oakaudio_sync_place_by_waveform_offset`: timeline placement from an
	/// offset in samples.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_sync_place_by_waveform_offset(
		reference_timeline_in_num: i64,
		reference_timeline_in_den: i64,
		candidate_offset_samples: i64,
		sample_rate: c_int,
		out_num: *mut i64,
		out_den: *mut i64,
		out_valid: *mut c_int,
	) -> c_int {
		guard(|| {
			// CPP-PARITY: sync.cpp:191.
			invalid_if(
				out_num.is_null()
					|| out_den.is_null()
					|| out_valid.is_null()
					|| reference_timeline_in_den == 0,
			)?;
			let placement = crate::synchronizer::place_by_waveform_offset(
				Rational::new(reference_timeline_in_num, reference_timeline_in_den),
				candidate_offset_samples,
				sample_rate,
			);
			// SAFETY: out pointers are non-NULL (checked above).
			unsafe {
				*out_num = placement.timeline_in.numerator();
				*out_den = placement.timeline_in.denominator();
				*out_valid = placement.valid as c_int;
			}
			Ok(())
		})
	}
}

/// `include/audio/waveform.h` exports (complete inventory): neutral by-value
/// `OakAudioWaveform` handle (refcounted); `oakaudio_min_max` value struct;
/// `oakaudio_waveform_init` / `oakaudio_waveform_free` /
/// `oakaudio_waveform_get_channel_count` / `oakaudio_waveform_set_channel_count`
/// / `oakaudio_waveform_length` / `oakaudio_waveform_overwrite_samples` /
/// `oakaudio_waveform_overwrite_sums` / `oakaudio_waveform_overwrite_silence`
/// / `oakaudio_waveform_trim_in` / `oakaudio_waveform_resize` /
/// `oakaudio_waveform_trim_range` / `oakaudio_waveform_get_summary` /
/// `oakaudio_waveform_sum_samples_s` / `oakaudio_waveform_re_sum_s` /
/// `oakaudio_waveform_extract`.
pub mod waveform {
	use super::*;

	/// `oakaudio_min_max` — one min/max pair.
	#[repr(C)]
	#[derive(Clone, Copy, Debug, PartialEq)]
	pub struct MinMax {
		/// Minimum amplitude in the window.
		pub min: f32,
		/// Maximum amplitude in the window.
		pub max: f32,
	}

	/// `oakaudio_waveform_init`: new waveform, refcount 1.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_init() -> CHandle {
		guard_handle(crate::waveform::init)
	}

	/// `oakaudio_waveform_free`: NULL/empty no-op.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_free(_self: *mut CHandle) {
		crate::waveform::free(_self);
	}

	/// `oakaudio_waveform_get_channel_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_get_channel_count(_self: CHandle) -> c_int {
		guard_int(|| Ok(crate::waveform::get(&_self)?.channel_count()))
	}

	/// `oakaudio_waveform_set_channel_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_set_channel_count(
		_self: CHandle,
		channels: c_int,
	) -> c_int {
		guard(|| {
			invalid_if(channels < 0)?;
			crate::waveform::get_mut(&_self)?.set_channel_count(channels);
			Ok(())
		})
	}

	/// `oakaudio_waveform_length`: length as a rational pair.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_length(
		_self: CHandle,
		num: *mut i64,
		den: *mut i64,
	) -> c_int {
		guard(|| {
			invalid_if(num.is_null() || den.is_null())?;
			let length = crate::waveform::get(&_self)?.length();
			unsafe {
				*num = length.numerator();
				*den = length.denominator();
			}
			Ok(())
		})
	}

	/// `oakaudio_waveform_overwrite_samples`.
	///
	/// Validation order follows the C++: invalid args (incl. `frame_count
	/// <= 0`) first, then the channel-count state check (`E_STATE`).
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_overwrite_samples(
		_self: CHandle,
		planar: *const *const f32,
		frame_count: c_int,
		sample_rate: c_int,
		start_num: i64,
		start_den: i64,
	) -> c_int {
		guard(|| {
			// CPP-PARITY: waveform.cpp:208.
			let start = rational_from_parts(start_num, start_den)?;
			invalid_if(planar.is_null() || frame_count <= 0 || sample_rate <= 0)?;
			let channels = crate::waveform::get(&_self)?.channel_count();
			if channels <= 0 {
				return Err(Error::State);
			}
			for ch in 0..channels {
				// SAFETY: `planar` is non-NULL with `channels` entries.
				if unsafe { *planar.add(ch as usize) }.is_null() {
					return Err(Error::Invalid);
				}
			}
			let views = unsafe { planar_views(planar, channels, frame_count) };
			crate::waveform::get_mut(&_self)?.overwrite_samples(&views, sample_rate, start);
			Ok(())
		})
	}

	/// `oakaudio_waveform_overwrite_sums`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_overwrite_sums(
		_self: CHandle,
		src: CHandle,
		dest_num: i64,
		dest_den: i64,
		offset_num: i64,
		offset_den: i64,
		length_num: i64,
		length_den: i64,
	) -> c_int {
		guard(|| {
			let dest = rational_from_parts(dest_num, dest_den)?;
			let offset = rational_from_parts(offset_num, offset_den)?;
			let length = rational_from_parts(length_num, length_den)?;
			let src_waveform = crate::waveform::get(&src)?;
			// SAFETY: `self` and `src` are distinct handles (the FFI
			// contract forbids aliasing them).
			crate::waveform::get_mut(&_self)?.overwrite_sums(src_waveform, dest, offset, length);
			Ok(())
		})
	}

	/// `oakaudio_waveform_overwrite_silence`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_overwrite_silence(
		_self: CHandle,
		start_num: i64,
		start_den: i64,
		length_num: i64,
		length_den: i64,
	) -> c_int {
		guard(|| {
			let start = rational_from_parts(start_num, start_den)?;
			let length = rational_from_parts(length_num, length_den)?;
			crate::waveform::get_mut(&_self)?.overwrite_silence(start, length);
			Ok(())
		})
	}

	/// `oakaudio_waveform_trim_in`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_trim_in(
		_self: CHandle,
		length_num: i64,
		length_den: i64,
	) -> c_int {
		guard(|| {
			let length = rational_from_parts(length_num, length_den)?;
			crate::waveform::get_mut(&_self)?.trim_in(length);
			Ok(())
		})
	}

	/// `oakaudio_waveform_resize`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_resize(
		_self: CHandle,
		length_num: i64,
		length_den: i64,
	) -> c_int {
		guard(|| {
			let length = rational_from_parts(length_num, length_den)?;
			invalid_if(length < Rational::new(0, 1))?;
			crate::waveform::get_mut(&_self)?.resize(length);
			Ok(())
		})
	}

	/// `oakaudio_waveform_trim_range`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_trim_range(
		_self: CHandle,
		in_num: i64,
		in_den: i64,
		length_num: i64,
		length_den: i64,
	) -> c_int {
		guard(|| {
			let r#in = rational_from_parts(in_num, in_den)?;
			let length = rational_from_parts(length_num, length_den)?;
			crate::waveform::get_mut(&_self)?.trim_range(r#in, length);
			Ok(())
		})
	}

	/// `oakaudio_waveform_get_summary`: channel-interleaved min/max pairs.
	///
	/// Two-stage contract: `out_pairs == NULL` or `capacity_points` smaller
	/// than the required count returns the count without writing.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_get_summary(
		_self: CHandle,
		start_num: i64,
		start_den: i64,
		length_num: i64,
		length_den: i64,
		out_pairs: *mut MinMax,
		capacity_points: c_int,
	) -> c_int {
		guard_int(|| {
			// CPP-PARITY: waveform.cpp:337 — `length <= 0` is invalid.
			let start = rational_from_parts(start_num, start_den)?;
			let length = rational_from_parts(length_num, length_den)?;
			invalid_if(length <= Rational::new(0, 1) || capacity_points < 0)?;
			let sample = crate::waveform::get(&_self)?.get_summary_from_time(start, length);
			let channels = crate::waveform::get(&_self)?.channel_count().max(1) as usize;
			let points = sample.len() / channels;
			if out_pairs.is_null() || (capacity_points as usize) < points {
				return Ok(points as i32);
			}
			// SAFETY: `out_pairs` holds at least `points * channels`
			// entries (capacity is in points, checked above); `Sample` is
			// channel-interleaved `SamplePerChannel`.
			for (i, spc) in sample.iter().take(points * channels).enumerate() {
				unsafe {
					*out_pairs.add(i) = MinMax {
						min: spc.min,
						max: spc.max,
					};
				}
			}
			Ok(points as i32)
		})
	}

	/// `oakaudio_waveform_sum_samples_s`: static, no handle.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_sum_samples_s(
		planar: *const *const f32,
		channel_count: c_int,
		start_index: c_int,
		length: c_int,
		out: *mut MinMax,
	) -> c_int {
		guard_int(|| {
			// CPP-PARITY: waveform.cpp:364 — `out` is required (no
			// two-stage query) and `length <= 0` is invalid.
			invalid_if(
				planar.is_null()
					|| out.is_null()
					|| channel_count <= 0
					|| start_index < 0
					|| length <= 0,
			)?;
			for ch in 0..channel_count {
				// SAFETY: `planar` is non-NULL with `channel_count` entries.
				if unsafe { *planar.add(ch as usize) }.is_null() {
					return Err(Error::Invalid);
				}
			}
			// Each plane must hold at least `start_index + length` floats;
			// the C++ SampleBuffer has exactly that span.
			let mut views: Vec<&[f32]> = Vec::with_capacity(channel_count as usize);
			for ch in 0..channel_count {
				// SAFETY: non-NULL planes of `start_index + length` floats.
				let p = unsafe { *planar.add(ch as usize) };
				views.push(unsafe {
					std::slice::from_raw_parts(p, (start_index + length) as usize)
				});
			}
			let sample =
				AudioVisualWaveform::sum_samples(&views, start_index as usize, length as usize);
			// CPP-PARITY: a short summary is an internal failure.
			if sample.len() < channel_count as usize {
				return Err(Error::Failed("sum_samples underflow".to_string()));
			}
			for (i, spc) in sample.iter().enumerate() {
				// SAFETY: `out` holds at least `channel_count` entries.
				unsafe {
					*out.add(i) = MinMax {
						min: spc.min,
						max: spc.max,
					};
				}
			}
			Ok(crate::error::OAKAUDIO_OK)
		})
	}

	/// `oakaudio_waveform_re_sum_s`: static, no handle.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_re_sum_s(
		r#in: *const MinMax,
		nb_entries: c_int,
		nb_channels: c_int,
		out: *mut MinMax,
	) -> c_int {
		guard_int(|| {
			// CPP-PARITY: waveform.cpp:393 — both buffers are required.
			invalid_if(r#in.is_null() || out.is_null() || nb_entries <= 0 || nb_channels <= 0)?;
			// SAFETY: `in` holds `nb_entries` entries.
			let entries = unsafe { std::slice::from_raw_parts(r#in, nb_entries as usize) };
			let samples: Vec<SamplePerChannel> = entries
				.iter()
				.map(|m| SamplePerChannel {
					min: m.min,
					max: m.max,
				})
				.collect();
			let sample =
				AudioVisualWaveform::re_sum_samples(&samples, nb_entries as usize, nb_channels);
			for (i, spc) in sample.iter().enumerate() {
				// SAFETY: `out` holds at least `nb_channels` entries.
				unsafe {
					*out.add(i) = MinMax {
						min: spc.min,
						max: spc.max,
					};
				}
			}
			Ok(crate::error::OAKAUDIO_OK)
		})
	}

	/// `oakaudio_waveform_extract`: whole-file waveform via oakcodec decoder.
	///
	/// Two-stage contract: `out_pairs == NULL` or `capacity_points` smaller
	/// than the required point count returns the count without writing; the
	/// channel count is reported whenever `out_channel_count` is non-NULL.
	/// Decoding/probing lives in [`crate::waveform::extract`].
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_waveform_extract(
		filename: *const c_char,
		stream_index: c_int,
		samples_per_point: c_int,
		out_pairs: *mut MinMax,
		capacity_points: c_int,
		out_channel_count: *mut c_int,
	) -> c_int {
		guard_int(|| {
			// CPP-PARITY: waveform.cpp:409.
			invalid_if(
				filename.is_null()
					|| stream_index < 0
					|| samples_per_point <= 0
					|| capacity_points < 0,
			)?;
			// SAFETY: the caller guarantees a NUL-terminated string.
			let filename = unsafe { CStr::from_ptr(filename) };
			let outcome = crate::waveform::extract(filename, stream_index, samples_per_point)?;
			let channels = outcome.channels.max(1);
			let point_count = outcome.points.len() / channels as usize;
			// CPP-PARITY: the channel count is reported even for a size-only
			// query.
			if !out_channel_count.is_null() {
				unsafe {
					*out_channel_count = outcome.channels;
				}
			}
			if out_pairs.is_null() || (capacity_points as usize) < point_count {
				return Ok(point_count as i32);
			}
			for (i, spc) in outcome.points.iter().enumerate() {
				// SAFETY: `out_pairs` holds `point_count * channels` entries
				// (capacity is in points, checked above).
				unsafe {
					*out_pairs.add(i) = MinMax {
						min: spc.min,
						max: spc.max,
					};
				}
			}
			Ok(point_count as i32)
		})
	}
}

/// `include/audio/levelmeter.h` exports (complete inventory): stateless
/// peak/RMS/VU/LUFS analysis of planar float audio (no handle);
/// `oakaudio_channel_stats`, `oakaudio_meter_stats` value structs;
/// `oakaudio_levelmeter_analyze`.
pub mod levelmeter {
	use super::*;

	/// `oakaudio_channel_stats` — per-channel analysis results (dB floor -200).
	#[repr(C)]
	pub struct ChannelStats {
		/// Peak amplitude, linear scale.
		pub peak_linear: c_double,
		/// Peak amplitude, decibel scale.
		pub peak_db: c_double,
		/// Root-mean-square level, linear scale.
		pub rms_linear: c_double,
		/// Root-mean-square level, decibel scale.
		pub rms_db: c_double,
		/// VU-meter ballistics reading, decibel scale.
		pub vu_db: c_double,
	}

	/// `oakaudio_meter_stats` — buffer-wide summary.
	#[repr(C)]
	pub struct MeterStats {
		/// Maximum peak across all channels, linear scale.
		pub max_peak_linear: c_double,
		/// Integrated loudness (EBU R128 LUFS).
		pub integrated_lufs: c_double,
		/// Whether every channel was silent below the noise gate.
		pub silence: c_int,
	}

	/// `oakaudio_levelmeter_analyze`.
	///
	/// Validation follows the C++: null planar / non-positive channel count
	/// / negative frame count / undersized `channels` buffer, and the
	/// no-output double-null case, are all `OAKAUDIO_E_INVALID`.
	#[no_mangle]
	pub unsafe extern "C" fn oakaudio_levelmeter_analyze(
		planar: *const *const f32,
		channel_count: c_int,
		frame_count: c_int,
		channels: *mut ChannelStats,
		channels_capacity: c_int,
		summary: *mut MeterStats,
	) -> c_int {
		guard_int(|| {
			// CPP-PARITY: levelmeter.cpp:39.
			invalid_if(
				planar.is_null()
					|| channel_count <= 0
					|| frame_count < 0
					|| (!channels.is_null() && channels_capacity < channel_count),
			)?;
			invalid_if(channels.is_null() && summary.is_null())?;
			for ch in 0..channel_count {
				// SAFETY: `planar` is non-NULL with `channel_count` entries.
				if unsafe { *planar.add(ch as usize) }.is_null() {
					return Err(Error::Invalid);
				}
			}
			let views = unsafe { planar_views(planar, channel_count, frame_count) };
			let stats = crate::levelmeter::analyze_sample_buffer(&views);

			if !channels.is_null() {
				for (i, ch) in stats.channels.iter().enumerate() {
					// SAFETY: capacity >= channel_count (checked above).
					unsafe {
						*channels.add(i) = ChannelStats {
							peak_linear: ch.peak_linear,
							peak_db: ch.peak_db,
							rms_linear: ch.rms_linear,
							rms_db: ch.rms_db,
							vu_db: ch.vu_db,
						};
					}
				}
			}
			if !summary.is_null() {
				unsafe {
					*summary = MeterStats {
						max_peak_linear: stats.max_peak_linear,
						integrated_lufs: stats.integrated_lufs,
						silence: stats.silence as c_int,
					};
				}
			}
			Ok(crate::error::OAKAUDIO_OK)
		})
	}
}
