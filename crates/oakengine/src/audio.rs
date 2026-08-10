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

//! `engine/include/oakengine/audio.h` over the oakaudio module.
//!
//! The engine API is static (the singleton is implicit); the oakaudio C
//! ABI passes the manager handle explicitly, so every family call goes
//! through [`manager()`] (a borrowed handle; empty when no instance
//! exists — engine semantics then report `paNoDevice`/error as
//! documented). The borrowed `OakAudioParams*` handles are read through
//! the liboakcore `oakcore_audioparams_*` accessors.

use std::ffi::{c_char, c_double, c_int, c_void};

use crate::bridge::audio as a;
use crate::error::Error;
use crate::handle::{
	box_handle, free_box, guard, guard_i64, guard_void, unbox, CHandle, OakEngineAudioProcessor,
};

/// paNoDevice — no audio device selected.
const PA_NO_DEVICE: i64 = -1;

/// Borrowed handle of the AudioManager singleton (empty when none).
fn manager() -> CHandle {
	unsafe { a::oakaudio_manager_instance() }
}

/// Borrowed handle of the AudioManager singleton for other facade
/// families (empty ctx == NULL when none).
pub(crate) fn audio_manager_handle_raw() -> CHandle {
	manager()
}

/// `oakengine_audio_create_instance` — create the singleton (no-op when
/// it already exists).
#[no_mangle]
pub extern "C" fn oakengine_audio_create_instance() -> c_int {
	guard(|| Error::from_module(unsafe { a::oakaudio_manager_create_instance() }))
}

/// `oakengine_audio_destroy_instance` — destroy the singleton (no-op when
/// none exists).
#[no_mangle]
pub extern "C" fn oakengine_audio_destroy_instance() -> c_int {
	guard_void(|| unsafe {
		a::oakaudio_manager_destroy_instance();
	});
	crate::error::OAKENGINE_OK
}
/// `oakengine_audio_manager_handle` — borrowed token of the singleton
/// (NULL when none); only for event-subscription use, never freed.
#[no_mangle]
pub extern "C" fn oakengine_audio_manager_handle() -> *mut c_void {
	let m = manager();
	if m.is_null() {
		std::ptr::null_mut()
	} else {
		m.ctx
	}
}

/// `oakengine_audio_get_output_device` — paNoDevice when none/no instance.
#[no_mangle]
pub extern "C" fn oakengine_audio_get_output_device() -> i64 {
	guard_i64(|| {
		let m = manager();
		if m.is_null() {
			return Ok(PA_NO_DEVICE);
		}
		Ok(i64::from(unsafe { a::oakaudio_manager_get_output_device(m) }))
	})
}

/// `oakengine_audio_set_output_device`.
#[no_mangle]
pub extern "C" fn oakengine_audio_set_output_device(device: i64) -> c_int {
	guard(|| {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		Error::from_module(unsafe { a::oakaudio_manager_set_output_device(m, device as c_int) })
	})
}

/// `oakengine_audio_get_input_device` — paNoDevice when none/no instance.
#[no_mangle]
pub extern "C" fn oakengine_audio_get_input_device() -> i64 {
	guard_i64(|| {
		let m = manager();
		if m.is_null() {
			return Ok(PA_NO_DEVICE);
		}
		Ok(i64::from(unsafe { a::oakaudio_manager_get_input_device(m) }))
	})
}

/// `oakengine_audio_set_input_device`.
#[no_mangle]
pub extern "C" fn oakengine_audio_set_input_device(device: i64) -> c_int {
	guard(|| {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		Error::from_module(unsafe { a::oakaudio_manager_set_input_device(m, device as c_int) })
	})
}

/// `oakengine_audio_hard_reset` — re-initialize PortAudio and refresh the
/// device lists.
#[no_mangle]
pub extern "C" fn oakengine_audio_hard_reset() -> c_int {
	guard(|| {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		Error::from_module(unsafe { a::oakaudio_manager_hard_reset(m) })
	})
}

/// `oakengine_audio_clear_buffered_output`.
#[no_mangle]
pub extern "C" fn oakengine_audio_clear_buffered_output() -> c_int {
	guard(|| {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		Error::from_module(unsafe { a::oakaudio_manager_clear_buffered_output(m) })
	})
}

/// `oakengine_audio_push_to_output` — queue interleaved samples described
/// by the borrowed `OakAudioParams*` handle.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_push_to_output(
	params: *const c_void,
	samples: *const c_char,
	samples_size: i64,
	error_buf: *mut c_char,
	error_buf_size: c_int,
) -> c_int {
	guard(|| unsafe {
		let m = manager();
		if m.is_null() || params.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		let rate = a::oakcore_audioparams_sample_rate(params);
		let layout = a::oakcore_audioparams_channel_layout(params);
		let format = a::oakcore_audioparams_format(params);
		Error::from_module(a::oakaudio_manager_push_to_output(
			m,
			rate,
			layout,
			format,
			samples,
			samples_size,
			error_buf,
			error_buf_size,
		))
	})
}

/// `oakengine_audio_stop_recording`.
#[no_mangle]
pub extern "C" fn oakengine_audio_stop_recording() -> c_int {
	guard(|| {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		Error::from_module(unsafe { a::oakaudio_manager_stop_recording(m) })
	})
}

/// `oakengine_audio_stop_output`.
#[no_mangle]
pub extern "C" fn oakengine_audio_stop_output() -> c_int {
	guard(|| {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		Error::from_module(unsafe { a::oakaudio_manager_stop_output(m) })
	})
}

/// `oakengine_audio_reset_output_clock`.
#[no_mangle]
pub extern "C" fn oakengine_audio_reset_output_clock() -> c_int {
	guard(|| {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		Error::from_module(unsafe { a::oakaudio_manager_reset_output_clock(m) })
	})
}

/// `oakengine_audio_set_output_notify_interval`.
#[no_mangle]
pub extern "C" fn oakengine_audio_set_output_notify_interval(bytes: i64) -> c_int {
	guard(|| {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		Error::from_module(unsafe { a::oakaudio_manager_set_output_notify_interval(m, bytes) })
	})
}

/// `oakengine_audio_start_recording` — takes ownership of `params`
/// (the handle is destroyed when the recording ends).
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_start_recording(
	params: *mut c_void,
	error_buf: *mut c_char,
	error_buf_size: c_int,
) -> c_int {
	guard(|| unsafe {
		let m = manager();
		if m.is_null() {
			return Err(Error::Failed("no AudioManager instance".into()));
		}
		if params.is_null() {
			return Err(Error::Invalid);
		}
		let rc = a::oakaudio_manager_start_recording(
			m,
			params.cast::<a::EncodingParams>(),
			error_buf,
			error_buf_size,
		);
		Error::from_module(rc)
	})
}

// ---------------------------------------------------------------------------
// Audio synchronization
// ---------------------------------------------------------------------------

/// `oakengine_audio_estimate_envelope_offset` — estimate the sample offset
/// between two RMS envelopes.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_estimate_envelope_offset(
	reference: *const c_double,
	reference_len: c_int,
	candidate: *const c_double,
	candidate_len: c_int,
	reference_valid: *const u8,
	_reference_valid_len: c_int,
	candidate_valid: *const u8,
	_candidate_valid_len: c_int,
	window_samples: u64,
	max_offset_windows: i64,
	out: *mut OakAudioWaveformOffset,
) -> c_int {
	guard(|| unsafe {
		if reference.is_null() || candidate.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let mut result = a::OffsetResult {
			offset_samples: 0,
			confidence: 0.0,
			valid: 0,
		};
		Error::from_module(a::oakaudio_sync_estimate_envelope_offset(
			reference,
			reference_len,
			candidate,
			candidate_len,
			reference_valid,
			candidate_valid,
			window_samples,
			max_offset_windows,
			&mut result,
		))?;
		(*out).offset_samples = result.offset_samples;
		(*out).confidence = result.confidence;
		(*out).valid = result.valid;
		Ok(())
	})
}

/// `oakengine_audio_estimate_stretch_and_offset` — rate + offset
/// correlation.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_estimate_stretch_and_offset(
	reference: *const c_double,
	reference_len: c_int,
	candidate: *const c_double,
	candidate_len: c_int,
	reference_valid: *const u8,
	_reference_valid_len: c_int,
	candidate_valid: *const u8,
	_candidate_valid_len: c_int,
	window_samples: u64,
	max_offset_windows: i64,
	min_rate: c_double,
	max_rate: c_double,
	rate_step: c_double,
	out: *mut OakAudioWaveformStretchOffset,
) -> c_int {
	guard(|| unsafe {
		if reference.is_null() || candidate.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let mut result = a::StretchOffsetResult {
			rate: 0.0,
			offset_samples: 0,
			confidence: 0.0,
			valid: 0,
		};
		Error::from_module(a::oakaudio_sync_estimate_stretch_and_offset(
			reference,
			reference_len,
			candidate,
			candidate_len,
			reference_valid,
			candidate_valid,
			window_samples,
			max_offset_windows,
			min_rate,
			max_rate,
			rate_step,
			&mut result,
		))?;
		(*out).rate = result.rate;
		(*out).offset_samples = result.offset_samples;
		(*out).confidence = result.confidence;
		(*out).valid = result.valid;
		Ok(())
	})
}

/// `oakengine_audio_sync_place_by_source_time` — timeline placement from
/// source timecodes.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_sync_place_by_source_time(
	reference: *const OakAudioSyncSourceClip,
	candidate: *const OakAudioSyncSourceClip,
	reference_timeline_in_num: i64,
	reference_timeline_in_den: i64,
	out: *mut OakAudioSyncPlacement,
) -> c_int {
	guard(|| unsafe {
		if reference.is_null() || candidate.is_null() || out.is_null() {
			return Err(Error::Invalid);
		}
		let mut num: i64 = 0;
		let mut den: i64 = 0;
		let mut valid: c_int = 0;
		Error::from_module(a::oakaudio_sync_place_by_source_time(
			reference.cast::<a::SourceClip>(),
			candidate.cast::<a::SourceClip>(),
			reference_timeline_in_num,
			reference_timeline_in_den,
			&mut num,
			&mut den,
			&mut valid,
		))?;
		(*out).timeline_in_num = num;
		(*out).timeline_in_den = den;
		(*out).valid = valid;
		Ok(())
	})
}

/// `oakengine_audio_sync_place_by_waveform_offset` — timeline placement
/// from a waveform offset.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_sync_place_by_waveform_offset(
	reference_timeline_in_num: i64,
	reference_timeline_in_den: i64,
	candidate_offset_samples: i64,
	sample_rate: c_int,
	out: *mut OakAudioSyncPlacement,
) -> c_int {
	guard(|| unsafe {
		if out.is_null() {
			return Err(Error::Invalid);
		}
		let mut num: i64 = 0;
		let mut den: i64 = 0;
		let mut valid: c_int = 0;
		Error::from_module(a::oakaudio_sync_place_by_waveform_offset(
			reference_timeline_in_num,
			reference_timeline_in_den,
			candidate_offset_samples,
			sample_rate,
			&mut num,
			&mut den,
			&mut valid,
		))?;
		(*out).timeline_in_num = num;
		(*out).timeline_in_den = den;
		(*out).valid = valid;
		Ok(())
	})
}

// ---------------------------------------------------------------------------
// Audio processor
// ---------------------------------------------------------------------------

/// `oakengine_audio_processor_create`.
#[no_mangle]
pub extern "C" fn oakengine_audio_processor_create() -> *mut OakEngineAudioProcessor {
	crate::handle::guard_ptr(|| {
		let p = unsafe { a::oakaudio_processor_init() };
		if p.is_null() {
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineAudioProcessor>(p))
	})
}

/// `oakengine_audio_processor_free` — NULL no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_processor_free(p: *mut OakEngineAudioProcessor) {
	guard_void(|| unsafe {
		free_box(p);
	})
}

/// `oakengine_audio_processor_open` — open the conversion graph; `from`/
/// `to` are borrowed `OakAudioParams*` handles (read via oakcore).
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_processor_open(
	p: *mut OakEngineAudioProcessor,
	from: *const c_void,
	to: *const c_void,
	tempo: c_double,
) -> c_int {
	guard(|| unsafe {
		let handle = unbox(p)?;
		if from.is_null() || to.is_null() {
			return Err(Error::Invalid);
		}
		let in_rate = a::oakcore_audioparams_sample_rate(from);
		let in_layout = a::oakcore_audioparams_channel_layout(from);
		let in_format = a::oakcore_audioparams_format(from);
		let out_rate = a::oakcore_audioparams_sample_rate(to);
		let out_layout = a::oakcore_audioparams_channel_layout(to);
		let out_format = a::oakcore_audioparams_format(to);
		Error::from_module(a::oakaudio_processor_open(
			handle,
			in_rate,
			in_layout,
			in_format,
			out_rate,
			out_layout,
			out_format,
			tempo,
		))
	})
}

/// `oakengine_audio_processor_close` — NULL/not-open no-op.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_processor_close(p: *mut OakEngineAudioProcessor) -> c_int {
	guard(|| unsafe {
		if p.is_null() {
			return Ok(());
		}
		let handle = unbox(p)?;
		Error::from_module(a::oakaudio_processor_close(handle))
	})
}

/// `oakengine_audio_processor_is_open` — 1 when open, 0 when NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_processor_is_open(p: *mut OakEngineAudioProcessor) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if p.is_null() {
			return Ok(0);
		}
		let handle = unbox(p)?;
		Ok(a::oakaudio_processor_is_open(handle))
	})
}

/// `oakengine_audio_processor_convert` — **not backed**: the oakaudio
/// module's processor converts planar→planar, while the engine contract
/// is planar→packed with an owned output buffer. Returns
/// `OAKENGINE_E_FAILED` until the module exposes a packed-output
/// converter.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_processor_convert(
	_p: *mut OakEngineAudioProcessor,
	_in: *mut *mut f32,
	_nb_in_samples: c_int,
	_out_data: *mut *const c_void,
	_out_size: *mut c_int,
) -> c_int {
	crate::error::OAKENGINE_E_FAILED
}

/// `oakengine_audio_processor_output_params` — **not backed**: the
/// oakaudio module has no output-params getter. Returns NULL.
#[no_mangle]
pub unsafe extern "C" fn oakengine_audio_processor_output_params(
	_p: *mut OakEngineAudioProcessor,
) -> *mut c_void {
	std::ptr::null_mut()
}

/// `engine/include/oakengine/audio.h` — envelope-offset result.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakAudioWaveformOffset {
	/// Offset in samples.
	pub offset_samples: i64,
	/// Correlation confidence.
	pub confidence: c_double,
	/// 1 when usable.
	pub valid: c_int,
}

/// `engine/include/oakengine/audio.h` — rate+offset result.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakAudioWaveformStretchOffset {
	/// Playback rate.
	pub rate: c_double,
	/// Offset in samples.
	pub offset_samples: i64,
	/// Correlation confidence.
	pub confidence: c_double,
	/// 1 when usable.
	pub valid: c_int,
}

/// `engine/include/oakengine/audio.h` — source-clip description.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakAudioSyncSourceClip {
	/// Source start time num.
	pub source_start_time_num: i64,
	/// Source start time den.
	pub source_start_time_den: i64,
	/// Media in num.
	pub media_in_num: i64,
	/// Media in den.
	pub media_in_den: i64,
	/// 1 when source start time is meaningful.
	pub has_source_start_time: c_int,
}

/// `engine/include/oakengine/audio.h` — timeline placement result.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakAudioSyncPlacement {
	/// Timeline in-point num.
	pub timeline_in_num: i64,
	/// Timeline in-point den.
	pub timeline_in_den: i64,
	/// 1 when usable.
	pub valid: c_int,
}
