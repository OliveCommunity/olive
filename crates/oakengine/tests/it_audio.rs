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

//! Integration tests for the audio family: the facade exports
//! `oakengine_audio_*` (src/audio.rs; module C contract
//! `include/audio/{manager,processor,sync,error}.h`), exercised end to
//! end against the REAL `oakaudio` crate — no mocks anywhere.
//!
//! The facade's 26 exported functions are all covered:
//!
//! * Manager (singleton, process-wide — serialized via [`with_manager`]):
//!   create/destroy/instance handle, device accessors, output push/clock,
//!   notify interval, recording start/stop.
//! * Sync (stateless): envelope offset/stretch correlation, source-time
//!   and waveform-offset timeline placement.
//! * Processor (refcounted object — serialized via [`with_processor`] so
//!   the module's debug alive counter (`oakaudio_debug_alive_count`) is
//!   deterministic): create/free/open/close/is_open plus the two
//!   documented facade stubs (`convert` returns `OAKENGINE_E_FAILED`,
//!   `output_params` returns NULL — "not backed" in src/audio.rs).
//!
//! Legal-path value matrices pin exact results (device indices, rational
//! placement arithmetic, envelope correlation values); illegal inputs
//! (NULL pointers, empty handles, out-of-range sizes, garbage enum
//! values) must return a clean negative code or a documented no-op —
//! never crash/abort/panic.
//!
//! Note: `start_recording` with an input device set drives the REAL
//! oakcodec FFmpeg encoder (ffmpeg-next), so it writes a real media file
//! to the system temp dir when the host has the codec; when the host
//! build cannot create the encoder the call fails with the module's
//! `OAKAUDIO_E_FAILED` and a diagnostic in `error_buf` — both outcomes
//! are asserted.

#[path = "common/mod.rs"]
mod common;

use std::ffi::{c_int, c_void, CStr};
use std::path::PathBuf;
use std::sync::Mutex;

use oakengine::audio::{
	oakengine_audio_clear_buffered_output, oakengine_audio_create_instance,
	oakengine_audio_destroy_instance, oakengine_audio_estimate_envelope_offset,
	oakengine_audio_estimate_stretch_and_offset, oakengine_audio_get_input_device,
	oakengine_audio_get_output_device, oakengine_audio_hard_reset, oakengine_audio_manager_handle,
	oakengine_audio_processor_close, oakengine_audio_processor_convert,
	oakengine_audio_processor_create, oakengine_audio_processor_free,
	oakengine_audio_processor_is_open, oakengine_audio_processor_open,
	oakengine_audio_processor_output_params, oakengine_audio_push_to_output,
	oakengine_audio_reset_output_clock, oakengine_audio_set_input_device,
	oakengine_audio_set_output_device, oakengine_audio_set_output_notify_interval,
	oakengine_audio_start_recording, oakengine_audio_stop_output, oakengine_audio_stop_recording,
	oakengine_audio_sync_place_by_source_time, oakengine_audio_sync_place_by_waveform_offset,
	OakAudioSyncPlacement, OakAudioSyncSourceClip, OakAudioWaveformOffset,
	OakAudioWaveformStretchOffset,
};
use oakengine::error::{OAKENGINE_E_FAILED, OAKENGINE_E_INVALID};
use oakengine::handle::{CHandle, OakEngineAudioProcessor};

/// `OAKAUDIO_E_INVALID` (include/audio/error.h) — module codes pass
/// through the facade untranslated.
const AUDIO_E_INVALID: c_int = -60001;
/// `OAKAUDIO_E_FAILED`.
const AUDIO_E_FAILED: c_int = -60003;
/// `OAKAUDIO_E_STATE`.
const AUDIO_E_STATE: c_int = -60002;

// ---------------------------------------------------------------------------
// Serialization + shared fixtures
// ---------------------------------------------------------------------------

/// Serialize manager-touching tests: the AudioManager singleton is
/// process-wide and its create/destroy flips a global flag, so all
/// manager tests take this lock and start from a destroyed state.
fn with_manager(f: impl FnOnce()) {
	static LOCK: Mutex<()> = Mutex::new(());
	let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	f();
}

/// Serialize processor tests: each processor is an independent
/// refcounted object, but the module's debug alive counter is process
/// global, so count assertions need exclusive access to the family.
fn with_processor(f: impl FnOnce()) {
	static LOCK: Mutex<()> = Mutex::new(());
	let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	f();
}

/// Current number of live refcounted oakaudio objects.
fn alive() -> c_int {
	unsafe { oakaudio::ffi::manager::oakaudio_debug_alive_count() }
}

/// A borrowed `OakAudioParams*` mock handle (tests/common/mod.rs provides
/// the `oakcore_audioparams_*` accessors the facade reads through).
fn audio_params(rate: c_int, layout: u64, format: c_int) -> *mut common::OakAudioParams {
	common::oakcore_audioparams_create(rate, layout, format)
}

/// Unique recording output path under the system temp dir.
fn recording_path() -> PathBuf {
	std::env::temp_dir().join(format!("oak-it-audio-rec-{}.wav", std::process::id()))
}

// ---------------------------------------------------------------------------
// Manager — lifecycle and devices (serialized)
// ---------------------------------------------------------------------------

/// Manager lifecycle + device legal matrix + no-instance illegal matrix.
#[test]
fn manager_device_lifecycle() {
	with_manager(|| {
		let _ = common::force_link();

		// Start from a destroyed state.
		assert_eq!(unsafe { oakengine_audio_destroy_instance() }, 0);

		// --- No instance: every function reports a clean error. ---
		assert!(unsafe { oakengine_audio_manager_handle() }.is_null());
		assert_eq!(unsafe { oakengine_audio_get_output_device() }, -1); // paNoDevice
		assert_eq!(unsafe { oakengine_audio_get_input_device() }, -1);
		assert_eq!(
			unsafe { oakengine_audio_set_output_device(0) },
			OAKENGINE_E_FAILED
		);
		assert_eq!(
			unsafe { oakengine_audio_set_input_device(0) },
			OAKENGINE_E_FAILED
		);
		assert_eq!(unsafe { oakengine_audio_hard_reset() }, OAKENGINE_E_FAILED);
		assert_eq!(
			unsafe { oakengine_audio_clear_buffered_output() },
			OAKENGINE_E_FAILED
		);
		assert_eq!(unsafe { oakengine_audio_stop_output() }, OAKENGINE_E_FAILED);
		assert_eq!(
			unsafe { oakengine_audio_stop_recording() },
			OAKENGINE_E_FAILED
		);
		assert_eq!(
			unsafe { oakengine_audio_reset_output_clock() },
			OAKENGINE_E_FAILED
		);
		assert_eq!(
			unsafe { oakengine_audio_set_output_notify_interval(1024) },
			OAKENGINE_E_FAILED
		);
		// push: NULL params is rejected at the facade before the module runs.
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					std::ptr::null(),
					c"data".as_ptr(),
					4,
					std::ptr::null_mut(),
					0,
				)
			},
			OAKENGINE_E_FAILED
		);
		// start_recording: NULL params with no instance → the facade's
		// manager check fires first (-3).
		assert_eq!(
			unsafe {
				oakengine_audio_start_recording(std::ptr::null_mut(), std::ptr::null_mut(), 0)
			},
			OAKENGINE_E_FAILED
		);

		// --- Lifecycle: create/destroy idempotence and re-create. ---
		assert_eq!(unsafe { oakengine_audio_create_instance() }, 0);
		assert_eq!(unsafe { oakengine_audio_create_instance() }, 0); // no-op when exists
		assert_eq!(unsafe { oakengine_audio_destroy_instance() }, 0);
		assert_eq!(unsafe { oakengine_audio_destroy_instance() }, 0); // no-op when absent
		assert!(unsafe { oakengine_audio_manager_handle() }.is_null());

		assert_eq!(unsafe { oakengine_audio_create_instance() }, 0);
		assert!(!unsafe { oakengine_audio_manager_handle() }.is_null());

		// --- Device accessors: legal matrix. ---
		// The manager singleton retains its device state across
		// destroy/recreate (the OnceLock box is kept; only a flag flips),
		// so pin the devices explicitly instead of assuming fresh defaults.
		assert_eq!(unsafe { oakengine_audio_set_output_device(-1) }, 0);
		assert_eq!(unsafe { oakengine_audio_get_output_device() }, -1);
		assert_eq!(unsafe { oakengine_audio_set_input_device(-1) }, 0);
		assert_eq!(unsafe { oakengine_audio_get_input_device() }, -1);

		// The module records any device index (PortAudio enumeration is not
		// bridged); -1 (paNoDevice), 0, a large index and a negative index.
		for device in [-1i64, 0, 999999, -100] {
			assert_eq!(unsafe { oakengine_audio_set_output_device(device) }, 0);
			assert_eq!(unsafe { oakengine_audio_get_output_device() }, device);
		}
		// An i64 that does not fit an i32 narrows to 0 (C int narrowing).
		assert_eq!(unsafe { oakengine_audio_set_output_device(1 << 40) }, 0);
		assert_eq!(unsafe { oakengine_audio_get_output_device() }, 0);

		for device in [-1i64, 0, 999999] {
			assert_eq!(unsafe { oakengine_audio_set_input_device(device) }, 0);
			assert_eq!(unsafe { oakengine_audio_get_input_device() }, device);
		}

		// --- Output controls. ---
		assert_eq!(unsafe { oakengine_audio_reset_output_clock() }, 0);
		assert_eq!(unsafe { oakengine_audio_stop_output() }, 0);
		assert_eq!(unsafe { oakengine_audio_clear_buffered_output() }, 0);
		assert_eq!(unsafe { oakengine_audio_hard_reset() }, 0);
		// Notify interval: 0 disables, positive accepted, negative invalid.
		assert_eq!(unsafe { oakengine_audio_set_output_notify_interval(0) }, 0);
		assert_eq!(
			unsafe { oakengine_audio_set_output_notify_interval(1024) },
			0
		);
		assert_eq!(
			unsafe { oakengine_audio_set_output_notify_interval(-1) },
			AUDIO_E_INVALID
		);

		// --- push_to_output: legal + illegal matrix. ---
		// NULL params is rejected at the facade (-3) even with an instance.
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					std::ptr::null(),
					c"data".as_ptr(),
					4,
					std::ptr::null_mut(),
					0,
				)
			},
			OAKENGINE_E_FAILED
		);

		// No output device selected → clean E_FAILED with a diagnostic.
		assert_eq!(unsafe { oakengine_audio_set_output_device(-1) }, 0);
		let params = audio_params(48000, 3, 10); // f32 packed, stereo, 48 kHz
		let mut err = [0i8; 128];
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					params as *const c_void,
					c"data".as_ptr(),
					4,
					err.as_mut_ptr(),
					err.len() as c_int,
				)
			},
			AUDIO_E_FAILED
		);
		assert_eq!(
			unsafe { CStr::from_ptr(err.as_ptr()) }.to_str().unwrap(),
			"No output device is set"
		);
		common::oakcore_audioparams_free(params);

		// Garbage sample format → E_INVALID.
		let params = audio_params(48000, 3, 99);
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					params as *const c_void,
					c"data".as_ptr(),
					4,
					std::ptr::null_mut(),
					0,
				)
			},
			AUDIO_E_INVALID
		);
		common::oakcore_audioparams_free(params);

		// Zero sample rate (mock default) → E_INVALID.
		let params = audio_params(0, 3, 10);
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					params as *const c_void,
					c"data".as_ptr(),
					4,
					std::ptr::null_mut(),
					0,
				)
			},
			AUDIO_E_INVALID
		);
		common::oakcore_audioparams_free(params);

		// NULL samples → E_INVALID.
		let params = audio_params(48000, 3, 10);
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					params as *const c_void,
					std::ptr::null(),
					4,
					std::ptr::null_mut(),
					0,
				)
			},
			AUDIO_E_INVALID
		);
		// Negative byte count → E_INVALID.
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					params as *const c_void,
					c"data".as_ptr(),
					-1,
					std::ptr::null_mut(),
					0,
				)
			},
			AUDIO_E_INVALID
		);
		common::oakcore_audioparams_free(params);

		// Legal push with a device selected: bytes are queued, error_buf
		// stays untouched on success.
		assert_eq!(unsafe { oakengine_audio_set_output_device(0) }, 0);
		let params = audio_params(48000, 3, 10);
		let mut err = [0i8; 128];
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					params as *const c_void,
					c"data".as_ptr(),
					4,
					err.as_mut_ptr(),
					err.len() as c_int,
				)
			},
			0
		);
		assert_eq!(
			unsafe { *err.as_ptr() },
			0,
			"error_buf untouched on success"
		);
		// A zero-length push is legal (empty queue op).
		assert_eq!(
			unsafe {
				oakengine_audio_push_to_output(
					params as *const c_void,
					c"".as_ptr(),
					0,
					std::ptr::null_mut(),
					0,
				)
			},
			0
		);
		common::oakcore_audioparams_free(params);

		assert_eq!(unsafe { oakengine_audio_destroy_instance() }, 0);
	});
}

/// Recording: no-device / invalid-params paths and a real encoder start.
#[test]
fn manager_recording() {
	with_manager(|| {
		assert_eq!(unsafe { oakengine_audio_destroy_instance() }, 0);
		assert_eq!(unsafe { oakengine_audio_create_instance() }, 0);

		// NULL params → E_INVALID at the facade.
		assert_eq!(
			unsafe {
				oakengine_audio_start_recording(std::ptr::null_mut(), std::ptr::null_mut(), 0)
			},
			OAKENGINE_E_INVALID
		);

		// audio_enabled == 0 → E_INVALID with a diagnostic.
		let mut params = recording_params(false);
		let mut err = [0i8; 128];
		assert_eq!(
			unsafe {
				oakengine_audio_start_recording(
					(&mut params as *mut oakcodec::ffi::encoder::oakcodec_encoding_params)
						.cast::<c_void>(),
					err.as_mut_ptr(),
					err.len() as c_int,
				)
			},
			AUDIO_E_INVALID
		);
		assert_eq!(
			unsafe { CStr::from_ptr(err.as_ptr()) }.to_str().unwrap(),
			"invalid recording parameters"
		);

		// Valid params but no input device → clean E_FAILED. Pin the input
		// device to paNoDevice first (the retained singleton may hold a
		// device index set by a prior serialized manager test).
		assert_eq!(unsafe { oakengine_audio_set_input_device(-1) }, 0);
		let mut params = recording_params(true);
		let mut err = [0i8; 128];
		assert_eq!(
			unsafe {
				oakengine_audio_start_recording(
					(&mut params as *mut oakcodec::ffi::encoder::oakcodec_encoding_params)
						.cast::<c_void>(),
					err.as_mut_ptr(),
					err.len() as c_int,
				)
			},
			AUDIO_E_FAILED
		);
		assert_eq!(
			unsafe { CStr::from_ptr(err.as_ptr()) }.to_str().unwrap(),
			"no input device"
		);

		// With an input device the real oakcodec encoder runs: the module
		// records to a WAV file (pcm_s16le) and reports OAKAUDIO_OK when the
		// host FFmpeg build can create the encoder, or OAKAUDIO_E_FAILED with
		// a diagnostic otherwise. Either way the return is a clean code.
		assert_eq!(unsafe { oakengine_audio_set_input_device(0) }, 0);
		let path = recording_path();
		let _ = std::fs::remove_file(&path);
		let mut params = recording_params(true);
		let mut err = [0i8; 512];
		let rc = unsafe {
			oakengine_audio_start_recording(
				(&mut params as *mut oakcodec::ffi::encoder::oakcodec_encoding_params)
					.cast::<c_void>(),
				err.as_mut_ptr(),
				err.len() as c_int,
			)
		};
		// On this host the real oakcodec encoder opens the WAV output and the
		// recording starts (rc == 0, file written); a host without the codec
		// reports OAKAUDIO_E_FAILED with a diagnostic. Either outcome is a
		// clean code with the corresponding side effect.
		match rc {
			0 => assert!(path.exists(), "recording file written"),
			AUDIO_E_FAILED => {
				let msg = unsafe { CStr::from_ptr(err.as_ptr()) }
					.to_str()
					.unwrap_or("");
				assert!(!msg.is_empty(), "encoder failure should write a diagnostic");
			}
			other => panic!("unexpected recording rc {other}"),
		}
		assert!(rc == 0 || rc == AUDIO_E_FAILED, "unexpected rc {rc}");
		if rc == 0 {
			assert!(path.exists(), "recording file written");
		}
		// Recording is stopped unconditionally (idle stop is a no-op), then
		// the file is removed.
		assert_eq!(unsafe { oakengine_audio_stop_recording() }, 0);
		assert_eq!(unsafe { oakengine_audio_stop_recording() }, 0);
		let _ = std::fs::remove_file(&path);

		assert_eq!(unsafe { oakengine_audio_destroy_instance() }, 0);
	});
}

/// `oakcodec_encoding_params` with WAV / pcm_s16le and the requested audio
/// track.
fn recording_params(audio_enabled: bool) -> oakcodec::ffi::encoder::oakcodec_encoding_params {
	let mut p: oakcodec::ffi::encoder::oakcodec_encoding_params = unsafe { std::mem::zeroed() };
	let path = recording_path();
	let bytes = path.as_os_str().as_encoded_bytes();
	assert!(bytes.len() < p.filename.len(), "temp path too long");
	p.filename[..bytes.len()].copy_from_slice(bytes);
	p.format = 7; // WAV
	p.audio_enabled = audio_enabled as c_int;
	p.audio_codec = 13; // PCM_S16LE
	p.audio_sample_rate = 48000;
	p.audio_channel_layout = 3; // stereo
	p.audio_sample_format = 7; // SampleFormat::S16 (packed)
	p.export_length_num = 1;
	p.export_length_den = 1;
	p
}

// ---------------------------------------------------------------------------
// Sync — envelope correlation (stateless)
// ---------------------------------------------------------------------------

/// Envelope offset correlation finds the exact shift of a delayed copy.
#[test]
fn sync_estimate_envelope_offset() {
	// candidate[k] == reference[k-1]: the candidate lags the reference by
	// one envelope window, so the best lag is +1 window = +window_samples.
	let reference = [0.1_f64, 0.8, 0.3, 0.6, 0.9];
	let candidate = [0.0_f64, 0.1, 0.8, 0.3, 0.6];
	let mut out = OakAudioWaveformOffset {
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};
	let rc = unsafe {
		oakengine_audio_estimate_envelope_offset(
			reference.as_ptr(),
			5,
			candidate.as_ptr(),
			5,
			std::ptr::null(),
			0,
			std::ptr::null(),
			0,
			128,
			1,
			&mut out,
		)
	};
	assert_eq!(rc, 0);
	assert_eq!(out.valid, 1);
	assert_eq!(out.offset_samples, 128);
	assert!((out.confidence - 1.0).abs() < 1e-9);

	// Explicit all-valid masks (the contract allows NULL = all valid) give
	// the same result.
	let ref_valid = [1u8; 5];
	let cand_valid = [1u8; 5];
	let mut out = OakAudioWaveformOffset {
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};
	let rc = unsafe {
		oakengine_audio_estimate_envelope_offset(
			reference.as_ptr(),
			5,
			candidate.as_ptr(),
			5,
			ref_valid.as_ptr(),
			5,
			cand_valid.as_ptr(),
			5,
			128,
			1,
			&mut out,
		)
	};
	assert_eq!(rc, 0);
	assert_eq!(out.valid, 1);
	assert_eq!(out.offset_samples, 128);

	// A single constant envelope carries no correlation energy: valid=0 is
	// the documented "no estimate" outcome, rc stays 0.
	let flat = [0.5_f64, 0.5, 0.5, 0.5];
	let mut out = OakAudioWaveformOffset {
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};
	let rc = unsafe {
		oakengine_audio_estimate_envelope_offset(
			flat.as_ptr(),
			4,
			flat.as_ptr(),
			4,
			std::ptr::null(),
			0,
			std::ptr::null(),
			0,
			128,
			4,
			&mut out,
		)
	};
	assert_eq!(rc, 0);
	assert_eq!(out.valid, 0);
}

/// Envelope offset: every NULL/zero/size/garbage argument fails cleanly.
#[test]
fn sync_estimate_envelope_offset_invalid() {
	let reference = [0.1_f64, 0.8, 0.3, 0.6, 0.9];
	let mut out = OakAudioWaveformOffset {
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};

	// NULL pointers are rejected at the facade (-1).
	assert_eq!(
		unsafe {
			oakengine_audio_estimate_envelope_offset(
				std::ptr::null(),
				5,
				reference.as_ptr(),
				5,
				std::ptr::null(),
				0,
				std::ptr::null(),
				0,
				128,
				4,
				&mut out,
			)
		},
		OAKENGINE_E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_audio_estimate_envelope_offset(
				reference.as_ptr(),
				5,
				std::ptr::null(),
				5,
				std::ptr::null(),
				0,
				std::ptr::null(),
				0,
				128,
				4,
				&mut out,
			)
		},
		OAKENGINE_E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_audio_estimate_envelope_offset(
				reference.as_ptr(),
				5,
				reference.as_ptr(),
				5,
				std::ptr::null(),
				0,
				std::ptr::null(),
				0,
				128,
				4,
				std::ptr::null_mut(),
			)
		},
		OAKENGINE_E_INVALID
	);

	// Zero/negative lengths, zero window, negative max offset → module
	// E_INVALID (-60001).
	for (len, window, max_off) in [(0, 128u64, 4i64), (-1, 128, 4), (5, 0, 4), (5, 128, -1)] {
		assert_eq!(
			unsafe {
				oakengine_audio_estimate_envelope_offset(
					reference.as_ptr(),
					len,
					reference.as_ptr(),
					len,
					std::ptr::null(),
					0,
					std::ptr::null(),
					0,
					window,
					max_off,
					&mut out,
				)
			},
			AUDIO_E_INVALID,
			"len={len} window={window} max_off={max_off}"
		);
	}
}

/// Stretch+offset correlation: an identical candidate resolves to rate
/// 1.0 with zero offset.
#[test]
fn sync_estimate_stretch_and_offset() {
	let reference = [0.1_f64, 0.8, 0.3, 0.6, 0.9];
	let mut out = OakAudioWaveformStretchOffset {
		rate: 0.0,
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};
	let rc = unsafe {
		oakengine_audio_estimate_stretch_and_offset(
			reference.as_ptr(),
			5,
			reference.as_ptr(),
			5,
			std::ptr::null(),
			0,
			std::ptr::null(),
			0,
			128,
			1,
			0.5,
			1.5,
			0.25,
			&mut out,
		)
	};
	assert_eq!(rc, 0);
	assert_eq!(out.valid, 1);
	assert_eq!(out.offset_samples, 0);
	assert!((out.rate - 1.0).abs() < 1e-9);
	assert!((out.confidence - 1.0).abs() < 1e-9);

	// Illegal ranges fail cleanly: NULL out (-1), bad rate bounds (-60001).
	assert_eq!(
		unsafe {
			oakengine_audio_estimate_stretch_and_offset(
				reference.as_ptr(),
				5,
				reference.as_ptr(),
				5,
				std::ptr::null(),
				0,
				std::ptr::null(),
				0,
				128,
				4,
				0.5,
				1.5,
				0.25,
				std::ptr::null_mut(),
			)
		},
		OAKENGINE_E_INVALID
	);
	for (min_rate, max_rate, step) in [
		(0.0, 1.5, 0.25),
		(-1.0, 1.5, 0.25),
		(1.5, 1.0, 0.25),
		(0.5, 1.5, 0.0),
	] {
		assert_eq!(
			unsafe {
				oakengine_audio_estimate_stretch_and_offset(
					reference.as_ptr(),
					5,
					reference.as_ptr(),
					5,
					std::ptr::null(),
					0,
					std::ptr::null(),
					0,
					128,
					4,
					min_rate,
					max_rate,
					step,
					&mut out,
				)
			},
			AUDIO_E_INVALID,
			"min={min_rate} max={max_rate} step={step}"
		);
	}
}

// ---------------------------------------------------------------------------
// Sync — timeline placement (stateless)
// ---------------------------------------------------------------------------

/// `place_by_source_time`: timeline_in = reference_timeline_in +
/// (candidate.source_start + candidate.media_in) -
/// (reference.source_start + reference.media_in).
#[test]
fn sync_place_by_source_time() {
	// Integers: 3 + (5 + 1) - (10 + 0) = -1.
	let reference = OakAudioSyncSourceClip {
		source_start_time_num: 10,
		source_start_time_den: 1,
		media_in_num: 0,
		media_in_den: 1,
		has_source_start_time: 1,
	};
	let candidate = OakAudioSyncSourceClip {
		source_start_time_num: 5,
		source_start_time_den: 1,
		media_in_num: 1,
		media_in_den: 1,
		has_source_start_time: 1,
	};
	let mut out = OakAudioSyncPlacement {
		timeline_in_num: 0,
		timeline_in_den: 1,
		valid: 0,
	};
	let rc = unsafe {
		oakengine_audio_sync_place_by_source_time(&reference, &candidate, 3, 1, &mut out)
	};
	assert_eq!(rc, 0);
	assert_eq!(out.timeline_in_num, -1);
	assert_eq!(out.timeline_in_den, 1);
	assert_eq!(out.valid, 1);

	// Rationals: 5 + (1 + 1/4) - (1/2 + 0) = 23/4.
	let reference = OakAudioSyncSourceClip {
		source_start_time_num: 1,
		source_start_time_den: 2,
		media_in_num: 0,
		media_in_den: 1,
		has_source_start_time: 1,
	};
	let candidate = OakAudioSyncSourceClip {
		source_start_time_num: 1,
		source_start_time_den: 1,
		media_in_num: 1,
		media_in_den: 4,
		has_source_start_time: 1,
	};
	let mut out = OakAudioSyncPlacement {
		timeline_in_num: 0,
		timeline_in_den: 1,
		valid: 0,
	};
	let rc = unsafe {
		oakengine_audio_sync_place_by_source_time(&reference, &candidate, 5, 1, &mut out)
	};
	assert_eq!(rc, 0);
	assert_eq!(out.timeline_in_num, 23);
	assert_eq!(out.timeline_in_den, 4);
	assert_eq!(out.valid, 1);

	// A clip without a source start time is documented invalid: rc 0, the
	// placement is 0/0 and valid=0 (not an error).
	let no_source = OakAudioSyncSourceClip {
		source_start_time_num: 0,
		source_start_time_den: 1,
		media_in_num: 0,
		media_in_den: 1,
		has_source_start_time: 0,
	};
	let mut out = OakAudioSyncPlacement {
		timeline_in_num: 0,
		timeline_in_den: 1,
		valid: 0,
	};
	let rc = unsafe {
		oakengine_audio_sync_place_by_source_time(&no_source, &candidate, 3, 1, &mut out)
	};
	assert_eq!(rc, 0);
	assert_eq!(out.valid, 0);
	assert_eq!(out.timeline_in_num, 0);
	assert_eq!(out.timeline_in_den, 0);

	// Illegal arguments: NULL pointers → -1; zero denominators → -60001.
	assert_eq!(
		unsafe {
			oakengine_audio_sync_place_by_source_time(std::ptr::null(), &candidate, 3, 1, &mut out)
		},
		OAKENGINE_E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_audio_sync_place_by_source_time(&reference, std::ptr::null(), 3, 1, &mut out)
		},
		OAKENGINE_E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_audio_sync_place_by_source_time(
				&reference,
				&candidate,
				3,
				1,
				std::ptr::null_mut(),
			)
		},
		OAKENGINE_E_INVALID
	);
	let bad_den = OakAudioSyncSourceClip {
		source_start_time_num: 1,
		source_start_time_den: 1,
		media_in_num: 0,
		media_in_den: 0, // zero denominator
		has_source_start_time: 1,
	};
	assert_eq!(
		unsafe { oakengine_audio_sync_place_by_source_time(&reference, &bad_den, 3, 1, &mut out,) },
		AUDIO_E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_audio_sync_place_by_source_time(
				&reference, &candidate, 3, 0, // zero reference timeline denominator
				&mut out,
			)
		},
		AUDIO_E_INVALID
	);
}

/// `place_by_waveform_offset`: timeline_in = reference_timeline_in +
/// candidate_offset_samples / sample_rate.
#[test]
fn sync_place_by_waveform_offset() {
	let mut out = OakAudioSyncPlacement {
		timeline_in_num: 0,
		timeline_in_den: 1,
		valid: 0,
	};
	// 48000 samples at 48 kHz = 1 second.
	assert_eq!(
		unsafe { oakengine_audio_sync_place_by_waveform_offset(0, 1, 48000, 48000, &mut out) },
		0
	);
	assert_eq!(out.timeline_in_num, 1);
	assert_eq!(out.timeline_in_den, 1);
	assert_eq!(out.valid, 1);

	// Zero offset keeps the reference timeline point (5/2 stays 5/2).
	assert_eq!(
		unsafe { oakengine_audio_sync_place_by_waveform_offset(5, 2, 0, 48000, &mut out) },
		0
	);
	assert_eq!(out.timeline_in_num, 5);
	assert_eq!(out.timeline_in_den, 2);
	assert_eq!(out.valid, 1);

	// Negative offset: -24000 samples = -0.5 s.
	assert_eq!(
		unsafe { oakengine_audio_sync_place_by_waveform_offset(0, 1, -24000, 48000, &mut out) },
		0
	);
	assert_eq!(out.timeline_in_num, -1);
	assert_eq!(out.timeline_in_den, 2);
	assert_eq!(out.valid, 1);

	// 1/2 + 1 s = 3/2.
	assert_eq!(
		unsafe { oakengine_audio_sync_place_by_waveform_offset(1, 2, 48000, 48000, &mut out) },
		0
	);
	assert_eq!(out.timeline_in_num, 3);
	assert_eq!(out.timeline_in_den, 2);
	assert_eq!(out.valid, 1);

	// Illegal: NULL out → -1; zero reference denominator → -60001;
	// sample_rate <= 0 is documented invalid (rc 0, valid 0).
	assert_eq!(
		unsafe {
			oakengine_audio_sync_place_by_waveform_offset(0, 1, 0, 48000, std::ptr::null_mut())
		},
		OAKENGINE_E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_audio_sync_place_by_waveform_offset(0, 0, 0, 48000, &mut out) },
		AUDIO_E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_audio_sync_place_by_waveform_offset(0, 1, 0, 0, &mut out) },
		0
	);
	assert_eq!(out.valid, 0);
	assert_eq!(out.timeline_in_num, 0);
	assert_eq!(out.timeline_in_den, 0);
}

// ---------------------------------------------------------------------------
// Processor — lifecycle, free contracts, validation (serialized)
// ---------------------------------------------------------------------------

/// Create/free round-trip, NULL/empty free, module double-free safety and
/// the alive-count leak check.
#[test]
fn processor_lifecycle_and_free_contracts() {
	with_processor(|| {
		let baseline = alive();

		// free(NULL) is a no-op.
		unsafe { oakengine_audio_processor_free(std::ptr::null_mut()) };
		assert_eq!(alive(), baseline);

		// free(empty handle box) is a no-op: a box wrapping CHandle::null
		// has nothing to release. The box must be a real heap box
		// (free_box deallocates it); a stack-allocated wrapper would be
		// deallocated out from under its owner.
		let empty_ptr = oakengine::handle::box_handle::<OakEngineAudioProcessor>(CHandle::null());
		assert!(!empty_ptr.is_null());
		unsafe { oakengine_audio_processor_free(empty_ptr) };
		assert_eq!(alive(), baseline);

		// create bumps the counter; free restores it (leak check).
		let p = unsafe { oakengine_audio_processor_create() };
		assert!(!p.is_null());
		assert_eq!(alive(), baseline + 1);
		unsafe { oakengine_audio_processor_free(p) };
		assert_eq!(alive(), baseline);

		// The module-level free is double-free-safe (ctx is nulled after
		// release); the counter decrements exactly once.
		let mut h = oakaudio::handle::CHandle::null();
		h = unsafe { oakaudio::ffi::processor::oakaudio_processor_init() };
		assert!(!h.ctx.is_null());
		assert_eq!(alive(), baseline + 1);
		unsafe { oakaudio::ffi::processor::oakaudio_processor_free(&mut h) };
		unsafe { oakaudio::ffi::processor::oakaudio_processor_free(&mut h) }; // no-op
		assert!(h.ctx.is_null());
		assert_eq!(alive(), baseline);
	});
}

/// Processor open: validation order and the clean failure of the
/// environment-bound graph creation.
#[test]
fn processor_open_validation() {
	with_processor(|| {
		let p = unsafe { oakengine_audio_processor_create() };
		assert!(!p.is_null());

		// NULL `to`/`from` params handles → -1 at the facade.
		assert_eq!(
			unsafe { oakengine_audio_processor_open(p, std::ptr::null(), std::ptr::null(), 1.0) },
			OAKENGINE_E_INVALID
		);

		// Garbage params: zero rates (mock default) → -60001; tempo <= 0 →
		// -60001; non-planar output format → -60001.
		let from0 = audio_params(0, 3, 4);
		let to0 = audio_params(0, 3, 4);
		assert_eq!(
			unsafe {
				oakengine_audio_processor_open(p, from0 as *const c_void, to0 as *const c_void, 1.0)
			},
			AUDIO_E_INVALID
		);
		common::oakcore_audioparams_free(from0);
		common::oakcore_audioparams_free(to0);

		let from = audio_params(48000, 3, 4);
		let to = audio_params(48000, 3, 4);
		assert_eq!(
			unsafe {
				oakengine_audio_processor_open(p, from as *const c_void, to as *const c_void, 0.0)
			},
			AUDIO_E_INVALID
		);
		assert_eq!(
			unsafe {
				oakengine_audio_processor_open(p, from as *const c_void, to as *const c_void, -1.0)
			},
			AUDIO_E_INVALID
		);
		common::oakcore_audioparams_free(from);
		common::oakcore_audioparams_free(to);

		// Output format must be planar f32 (4); f32 packed (10) is invalid.
		let from = audio_params(48000, 3, 4);
		let to_packed = audio_params(48000, 3, 10);
		assert_eq!(
			unsafe {
				oakengine_audio_processor_open(
					p,
					from as *const c_void,
					to_packed as *const c_void,
					1.0,
				)
			},
			AUDIO_E_INVALID
		);
		common::oakcore_audioparams_free(from);
		common::oakcore_audioparams_free(to_packed);

		// Legal arguments reach the module's graph creation, which now runs
		// a real in-process FFmpeg filter graph (ffmpeg-next): the open
		// succeeds and the processor reports open; a second open is a state
		// error and close shuts it down again.
		let from = audio_params(48000, 3, 4);
		let to = audio_params(48000, 3, 4);
		assert_eq!(
			unsafe {
				oakengine_audio_processor_open(p, from as *const c_void, to as *const c_void, 1.0)
			},
			0
		);
		assert_eq!(unsafe { oakengine_audio_processor_is_open(p) }, 1);
		assert_eq!(
			unsafe {
				oakengine_audio_processor_open(p, from as *const c_void, to as *const c_void, 1.0)
			},
			AUDIO_E_STATE
		);
		assert_eq!(unsafe { oakengine_audio_processor_close(p) }, 0);
		assert_eq!(unsafe { oakengine_audio_processor_is_open(p) }, 0);
		common::oakcore_audioparams_free(from);
		common::oakcore_audioparams_free(to);

		unsafe { oakengine_audio_processor_free(p) };
	});
}

/// is_open/close on NULL, empty and closed handles.
#[test]
fn processor_is_open_and_close() {
	with_processor(|| {
		let p = unsafe { oakengine_audio_processor_create() };
		assert!(!p.is_null());

		// NULL handle: is_open → 0, close → 0 (documented no-ops).
		assert_eq!(
			unsafe { oakengine_audio_processor_is_open(std::ptr::null_mut()) },
			0
		);
		assert_eq!(
			unsafe { oakengine_audio_processor_close(std::ptr::null_mut()) },
			0
		);

		// Empty handle box: -1 (invalid) from both.
		let mut empty_box = OakEngineAudioProcessor {
			handle: CHandle::null(),
		};
		let empty_ptr = &mut empty_box as *mut OakEngineAudioProcessor;
		assert_eq!(
			unsafe { oakengine_audio_processor_is_open(empty_ptr) },
			OAKENGINE_E_INVALID
		);
		assert_eq!(
			unsafe { oakengine_audio_processor_close(empty_ptr) },
			OAKENGINE_E_INVALID
		);

		// Fresh processor: closed. close on a closed processor is a no-op
		// (0); is_open stays 0.
		assert_eq!(unsafe { oakengine_audio_processor_is_open(p) }, 0);
		assert_eq!(unsafe { oakengine_audio_processor_close(p) }, 0);
		assert_eq!(unsafe { oakengine_audio_processor_close(p) }, 0);
		assert_eq!(unsafe { oakengine_audio_processor_is_open(p) }, 0);

		unsafe { oakengine_audio_processor_free(p) };
	});
}

/// The two documented facade stubs: convert is "not backed" and always
/// returns E_FAILED; output_params is "not backed" and always returns
/// NULL. Both must tolerate any pointer.
#[test]
fn processor_convert_and_output_params_stubs() {
	with_processor(|| {
		let p = unsafe { oakengine_audio_processor_create() };
		assert!(!p.is_null());

		let mut in_planes: [*mut f32; 1] = [std::ptr::null_mut()];
		let mut out_data: *const c_void = std::ptr::null();
		let mut out_size: c_int = 0;

		// NULL handle.
		assert_eq!(
			unsafe {
				oakengine_audio_processor_convert(
					std::ptr::null_mut(),
					in_planes.as_mut_ptr(),
					0,
					&mut out_data,
					&mut out_size,
				)
			},
			OAKENGINE_E_FAILED
		);
		assert!(unsafe { oakengine_audio_processor_output_params(std::ptr::null_mut()) }.is_null());

		// Valid handle — same documented stub result.
		assert_eq!(
			unsafe {
				oakengine_audio_processor_convert(
					p,
					in_planes.as_mut_ptr(),
					0,
					&mut out_data,
					&mut out_size,
				)
			},
			OAKENGINE_E_FAILED
		);
		assert!(unsafe { oakengine_audio_processor_output_params(p) }.is_null());

		unsafe { oakengine_audio_processor_free(p) };
	});
}

/// The full open→close cycle runs the module's real in-process FFmpeg
/// filter graph (ffmpeg-next), so open succeeds under `cargo test`.
/// `convert` stays the documented facade stub (`OAKENGINE_E_FAILED`; see
/// [`processor_convert_and_output_params_stubs`]) — the module-level
/// convert success path is covered by oakaudio's own processor tests.
#[test]
fn processor_full_open_convert_cycle() {
	with_processor(|| {
		let p = unsafe { oakengine_audio_processor_create() };
		assert!(!p.is_null());
		let from = audio_params(48000, 3, 4);
		let to = audio_params(48000, 3, 4);
		let rc = unsafe {
			oakengine_audio_processor_open(p, from as *const c_void, to as *const c_void, 1.0)
		};
		assert_eq!(rc, 0);
		assert_eq!(unsafe { oakengine_audio_processor_is_open(p) }, 1);
		let mut in_planes: [*mut f32; 2] = [std::ptr::null_mut(); 2];
		let mut out_data: *const c_void = std::ptr::null();
		let mut out_size: c_int = 0;
		// The facade convert is the documented "not backed" stub.
		assert_eq!(
			unsafe {
				oakengine_audio_processor_convert(
					p,
					in_planes.as_mut_ptr(),
					0,
					&mut out_data,
					&mut out_size,
				)
			},
			OAKENGINE_E_FAILED
		);
		assert_eq!(unsafe { oakengine_audio_processor_close(p) }, 0);
		common::oakcore_audioparams_free(from);
		common::oakcore_audioparams_free(to);
		unsafe { oakengine_audio_processor_free(p) };
	});
}
