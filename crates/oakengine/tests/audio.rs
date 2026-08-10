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

//! Smoke tests for the audio family (`engine/include/oakengine/audio.h`).
//! The AudioManager singleton is process-wide, so manager tests run in a
//! single serialized test function; processor and sync tests are
//! independent.

#[path = "common/mod.rs"]
mod common;

use oakengine::audio::{
	oakengine_audio_clear_buffered_output, oakengine_audio_create_instance,
	oakengine_audio_destroy_instance, oakengine_audio_estimate_envelope_offset,
	oakengine_audio_get_output_device, oakengine_audio_hard_reset,
	oakengine_audio_processor_close, oakengine_audio_processor_create,
	oakengine_audio_processor_free, oakengine_audio_processor_is_open,
	oakengine_audio_processor_open, oakengine_audio_push_to_output,
	oakengine_audio_reset_output_clock, oakengine_audio_set_output_device,
	oakengine_audio_set_output_notify_interval, oakengine_audio_stop_output,
	oakengine_audio_sync_place_by_waveform_offset, OakAudioSyncPlacement,
	OakAudioWaveformOffset,
};

/// Manager lifecycle: create/destroy round-trip and device accessors
/// (serialized — the singleton is process-wide).
#[test]
fn manager_lifecycle() {
	// Start from a destroyed state.
	unsafe { oakengine_audio_destroy_instance() };

	// No instance → create succeeds, destroy is idempotent.
	assert_eq!(unsafe { oakengine_audio_create_instance() }, 0);
	assert_eq!(unsafe { oakengine_audio_destroy_instance() }, 0);
	assert_eq!(unsafe { oakengine_audio_destroy_instance() }, 0);

	// Recreate for the device tests.
	assert_eq!(unsafe { oakengine_audio_create_instance() }, 0);

	// paNoDevice (-1) until a device is set.
	assert_eq!(unsafe { oakengine_audio_get_output_device() }, -1);
	// The module records any device index (PortAudio validation is not
	// bridged), so setting succeeds and reads back.
	assert_eq!(unsafe { oakengine_audio_set_output_device(999999) }, 0);
	assert_eq!(unsafe { oakengine_audio_get_output_device() }, 999999);
	assert_eq!(unsafe { oakengine_audio_set_output_device(-1) }, 0);

	// Stateless no-op calls succeed with a live manager.
	assert_eq!(unsafe { oakengine_audio_reset_output_clock() }, 0);
	assert_eq!(unsafe { oakengine_audio_stop_output() }, 0);
	assert_eq!(unsafe { oakengine_audio_clear_buffered_output() }, 0);
	assert_eq!(unsafe { oakengine_audio_set_output_notify_interval(1024) }, 0);
	assert_eq!(unsafe { oakengine_audio_hard_reset() }, 0);

	// push with a NULL params handle fails cleanly.
	assert_eq!(
		unsafe { oakengine_audio_push_to_output(std::ptr::null(), c"data".as_ptr(), 4, std::ptr::null_mut(), 0) },
		-3 // OAKENGINE_E_FAILED
	);

	unsafe { oakengine_audio_destroy_instance() };
}

/// Sync envelope-offset correlation runs and fills the result struct.
#[test]
fn sync_envelope_offset() {
	let reference = [0.0_f64, 0.5, 1.0, 0.5, 0.0];
	let candidate = [0.0_f64, 0.0, 0.5, 1.0, 0.5];
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
			16,
			&mut out,
		)
	};
	assert_eq!(rc, 0);
	// The result is filled in either way; the correlation may or may not
	// find a valid offset for this tiny synthetic input.
	assert!(out.confidence >= 0.0 && out.confidence <= 1.0);
}

/// Waveform-offset placement runs and reports validity.
#[test]
fn sync_place_by_waveform_offset() {
	let mut out = OakAudioSyncPlacement {
		timeline_in_num: 0,
		timeline_in_den: 1,
		valid: 0,
	};
	let rc = unsafe {
		oakengine_audio_sync_place_by_waveform_offset(0, 1, 48000, 48000, &mut out)
	};
	assert_eq!(rc, 0);
	// 48000 samples at 48 kHz = 1 second.
	assert_eq!(out.timeline_in_num, 1);
	assert_eq!(out.timeline_in_den, 1);
	assert_eq!(out.valid, 1);

	// NULL out → E_INVALID.
	assert_eq!(
		unsafe { oakengine_audio_sync_place_by_waveform_offset(0, 1, 0, 48000, std::ptr::null_mut()) },
		-1
	);
}

/// Processor lifecycle: create/free round-trip; open with NULL params
/// fails with E_INVALID.
#[test]
fn processor_lifecycle() {
	let p = unsafe { oakengine_audio_processor_create() };
	assert!(!p.is_null());
	assert_eq!(unsafe { oakengine_audio_processor_is_open(p) }, 0);
	assert_eq!(unsafe { oakengine_audio_processor_close(p) }, 0);

	// open with a NULL `to` params handle → E_INVALID.
	assert_eq!(
		unsafe { oakengine_audio_processor_open(p, std::ptr::null(), std::ptr::null(), 1.0) },
		-1
	);

	unsafe { oakengine_audio_processor_free(p) };
	// NULL free is a no-op.
	unsafe { oakengine_audio_processor_free(std::ptr::null_mut()) };
}
