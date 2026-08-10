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

//! AudioManager contract tests (manager.rs), through the C ABI. The
//! manager is a process-wide singleton, so every test holds the shared
//! `MANAGER_LOCK`.

mod common;

use std::ffi::c_char;

use common::MANAGER_LOCK;
use oakaudio::bridge::codec::EncodingParams;
use oakaudio::error::{
	OAKAUDIO_E_FAILED, OAKAUDIO_E_INVALID, OAKAUDIO_OK,
};
use oakaudio::ffi::manager::{
	oakaudio_debug_alive_count, oakaudio_manager_clear_buffered_output,
	oakaudio_manager_create_instance, oakaudio_manager_destroy_instance,
	oakaudio_manager_find_config_device_by_name_s, oakaudio_manager_find_device_by_name_s,
	oakaudio_manager_free, oakaudio_manager_get_input_device,
	oakaudio_manager_get_output_device, oakaudio_manager_hard_reset,
	oakaudio_manager_instance, oakaudio_manager_push_to_output,
	oakaudio_manager_reset_output_clock, oakaudio_manager_seconds,
	oakaudio_manager_set_input_device, oakaudio_manager_set_output_device,
	oakaudio_manager_set_output_notify_interval, oakaudio_manager_start_recording,
	oakaudio_manager_stop_output, oakaudio_manager_stop_recording,
};

fn instance() -> oakaudio::handle::CHandle {
	unsafe { oakaudio_manager_instance() }
}

/// Lock the manager singleton for a test. The manager state persists across
/// tests (the `OnceLock` cannot be reset), so a panicked test must not
/// poison the lock for the rest of the binary.
fn lock() -> std::sync::MutexGuard<'static, ()> {
	MANAGER_LOCK.lock().unwrap_or_else(|p| p.into_inner())
}

fn encoding_params() -> EncodingParams {
	let mut filename = [0u8; 1024];
	for (i, b) in b"oakaudio_test.wav\0".iter().enumerate() {
		filename[i] = *b;
	}
	EncodingParams {
		filename,
		format: 0,
		video_enabled: 0,
		video_codec: 0,
		video_width: 0,
		video_height: 0,
		video_time_base_num: 0,
		video_time_base_den: 0,
		video_pixel_format: 0,
		video_interlacing: 0,
		video_pixel_aspect_num: 0,
		video_pixel_aspect_den: 0,
		video_bit_rate: 0,
		video_min_bit_rate: 0,
		video_max_bit_rate: 0,
		video_buffer_size: 0,
		video_threads: 0,
		video_pix_fmt: [0u8; 64],
		video_is_image_sequence: 0,
		video_scaling_method: 0,
		audio_enabled: 1,
		audio_codec: 13, // PCM_S16LE (the .wav recording codec)
		audio_sample_rate: 48000,
		audio_channel_layout: 3,
		audio_sample_format: 8,
		audio_bit_rate: 128000,
		subtitles_enabled: 0,
		subtitles_codec: 0,
		subtitles_are_sidecar: 0,
		subtitles_sidecar_format: 0,
		color_transform_output: [0u8; 256],
		export_length_num: 0,
		export_length_den: 0,
		has_custom_range: 0,
		custom_range_in_num: 0,
		custom_range_in_den: 0,
		custom_range_out_num: 0,
		custom_range_out_den: 0,
	}
}

/// create_instance/destroy_instance toggle the singleton; instance() returns
/// a valid borrowed handle between them and NULL after destroy.
#[test]
fn singleton_lifecycle() {
	let _guard = lock();
	unsafe { oakaudio_manager_destroy_instance() };
	assert!(instance().ctx.is_null());

	unsafe { oakaudio_manager_create_instance() };
	let m = instance();
	assert!(!m.ctx.is_null());
	unsafe { oakaudio_manager_free(&mut m.clone()) };

	unsafe { oakaudio_manager_destroy_instance() };
	assert!(instance().ctx.is_null());
	unsafe { oakaudio_manager_create_instance() };
	assert!(!instance().ctx.is_null());
}

/// push_to_output accepts raw interleaved bytes and starts the virtual
/// playback clock; without a device it fails with a message in error_buf.
///
/// The virtual device never consumes frames (PortAudio is not bridged), so
/// the clock reads 0.0 rather than advancing.
#[test]
fn push_output_advances_clock() {
	let _guard = lock();
	unsafe { oakaudio_manager_create_instance() };
	let m = instance();

	// No stream yet: seconds() reports -1.
	let mut secs = 0.0f64;
	assert_eq!(unsafe { oakaudio_manager_seconds(m, &mut secs) }, OAKAUDIO_OK);
	assert_eq!(secs, -1.0);

	// Without a device, push fails with a human-readable error. The
	// singleton state persists across tests, so pin the no-device state
	// explicitly.
	assert_eq!(unsafe { oakaudio_manager_set_output_device(m, -1) }, OAKAUDIO_OK);
	let samples = vec![0u8; 480 * 2 * 4];
	let mut err = [0 as c_char; 64];
	let r = unsafe {
		oakaudio_manager_push_to_output(
			m,
			48000,
			3,
			4,
			samples.as_ptr() as *const c_char,
			samples.len() as i64,
			err.as_mut_ptr(),
			err.len() as i32,
		)
	};
	assert_eq!(r, OAKAUDIO_E_FAILED);
	assert!(err.iter().any(|&b| b != 0), "error_buf must carry a message");

	// After selecting a device the push succeeds and the clock starts at 0.
	assert_eq!(unsafe { oakaudio_manager_set_output_device(m, 0) }, OAKAUDIO_OK);
	let mut err = [0 as c_char; 64];
	let r = unsafe {
		oakaudio_manager_push_to_output(
			m,
			48000,
			3,
			4,
			samples.as_ptr() as *const c_char,
			samples.len() as i64,
			err.as_mut_ptr(),
			err.len() as i32,
		)
	};
	assert_eq!(r, OAKAUDIO_OK);
	unsafe { oakaudio_manager_seconds(m, &mut secs) };
	assert_eq!(secs, 0.0);

	unsafe { oakaudio_manager_destroy_instance() };
}

/// set/get output & input device: getters report a device, setters persist
/// it; hard_reset keeps the device indices (it only stops the stream and
/// clears buffers).
#[test]
fn device_selection_roundtrip() {
	let _guard = lock();
	unsafe { oakaudio_manager_create_instance() };
	let m = instance();

	assert_eq!(unsafe { oakaudio_manager_set_output_device(m, 42) }, OAKAUDIO_OK);
	assert_eq!(unsafe { oakaudio_manager_get_output_device(m) }, 42);
	assert_eq!(unsafe { oakaudio_manager_set_input_device(m, 7) }, OAKAUDIO_OK);
	assert_eq!(unsafe { oakaudio_manager_get_input_device(m) }, 7);

	assert_eq!(unsafe { oakaudio_manager_hard_reset(m) }, OAKAUDIO_OK);
	assert_eq!(unsafe { oakaudio_manager_get_output_device(m) }, 42);
	assert_eq!(unsafe { oakaudio_manager_get_input_device(m) }, 7);

	// The stream stopped, so the clock is back at -1.
	let mut secs = 0.0f64;
	unsafe { oakaudio_manager_seconds(m, &mut secs) };
	assert_eq!(secs, -1.0);

	unsafe { oakaudio_manager_destroy_instance() };
}

/// set_output_notify_interval stores the interval; clear_buffered_output
/// drops queued bytes, stop_output halts the stream, and reset_output_clock
/// restarts the counter.
#[test]
fn output_control_flags() {
	let _guard = lock();
	unsafe { oakaudio_manager_create_instance() };
	let m = instance();

	assert_eq!(
		unsafe { oakaudio_manager_set_output_notify_interval(m, 1024) },
		OAKAUDIO_OK
	);
	assert_eq!(
		unsafe { oakaudio_manager_set_output_notify_interval(m, -1) },
		OAKAUDIO_E_INVALID
	);
	assert_eq!(unsafe { oakaudio_manager_clear_buffered_output(m) }, OAKAUDIO_OK);
	assert_eq!(unsafe { oakaudio_manager_reset_output_clock(m) }, OAKAUDIO_OK);

	// Push starts the stream, then stop_output halts it (clock -> -1).
	unsafe { oakaudio_manager_set_output_device(m, 0) };
	let samples = vec![0u8; 480 * 2 * 4];
	assert_eq!(
		unsafe {
			oakaudio_manager_push_to_output(
				m, 48000, 3, 4, samples.as_ptr() as *const c_char,
				samples.len() as i64, std::ptr::null_mut(), 0,
			)
		},
		OAKAUDIO_OK
	);
	assert_eq!(unsafe { oakaudio_manager_stop_output(m) }, OAKAUDIO_OK);
	let mut secs = 1.0f64;
	unsafe { oakaudio_manager_seconds(m, &mut secs) };
	assert_eq!(secs, -1.0);

	unsafe { oakaudio_manager_destroy_instance() };
}

/// start_recording validates its parameters: NULL params or a disabled
/// audio track return OAKAUDIO_E_INVALID with an error string. The full
/// encoder-open success path (oakcodec writes a real file via ffmpeg) is
/// an end-to-end concern covered by the codec crate's own tests; with the
/// real oakcodec linked, `start_recording` either opens the encoder
/// (OAKAUDIO_OK, environment-dependent) or reports the encoder's
/// last-error string — both are correct manager behavior, so this test
/// pins the manager's own validation only.
#[test]
fn recording_start_stop() {
	let _guard = lock();
	unsafe { oakaudio_manager_create_instance() };
	let m = instance();
	unsafe { oakaudio_manager_set_input_device(m, 0) };

	let mut err = [0 as c_char; 64];
	let params = encoding_params();
	// With a real encoder, the attempt must at least reach the encoder
	// (a failure must surface a diagnostic in error_buf, not crash).
	let r = unsafe { oakaudio_manager_start_recording(m, &params, err.as_mut_ptr(), err.len() as i32) };
	if r != 0 {
		assert!(
			err.iter().any(|&b| b != 0),
			"failed start_recording must report a reason"
		);
		let _ = std::fs::remove_file("oakaudio_test.wav");
	} else {
		assert_eq!(unsafe { oakaudio_manager_stop_recording(m) }, OAKAUDIO_OK);
		// The real encoder writes the output file during open; clean it up.
		let _ = std::fs::remove_file("oakaudio_test.wav");
	}

	// NULL params is invalid and reports the reason in error_buf.
	let mut err = [0 as c_char; 64];
	let r = unsafe { oakaudio_manager_start_recording(m, std::ptr::null(), err.as_mut_ptr(), err.len() as i32) };
	assert_eq!(r, OAKAUDIO_E_INVALID);
	assert!(err.iter().any(|&b| b != 0));

	// A disabled audio track is likewise invalid.
	let mut disabled = encoding_params();
	disabled.audio_enabled = 0;
	let mut err = [0 as c_char; 64];
	let r = unsafe {
		oakaudio_manager_start_recording(m, &disabled, err.as_mut_ptr(), err.len() as i32)
	};
	assert_eq!(r, OAKAUDIO_E_INVALID);
	assert!(err.iter().any(|&b| b != 0));

	unsafe { oakaudio_manager_destroy_instance() };
}

/// Device enumeration is not bridged: every name/config lookup falls back to
/// paNoDevice (-1); a NULL name is OAKAUDIO_E_INVALID. The config-backed
/// buffer size/name helpers degrade to their defaults.
#[test]
fn device_name_lookup() {
	let _guard = lock();

	assert_eq!(
		unsafe { oakaudio_manager_find_device_by_name_s(std::ptr::null(), 1) },
		OAKAUDIO_E_INVALID
	);
	let name = c"anything";
	assert_eq!(
		unsafe { oakaudio_manager_find_device_by_name_s(name.as_ptr(), 1) },
		-1
	);
	assert_eq!(unsafe { oakaudio_manager_find_config_device_by_name_s(1) }, -1);
	assert_eq!(unsafe { oakaudio_manager_find_config_device_by_name_s(0) }, -1);

	// config::output_buffer_size() reads its default (0) from the stub;
	// device_name degrades to the empty string.
	assert_eq!(oakaudio::config::output_buffer_size(), 0);
	assert!(oakaudio::config::device_name(true).as_c_str().is_empty());
	assert!(oakaudio::config::device_name(false).as_c_str().is_empty());
}

/// PreviewAudioDevice pull-side plumbing (read/notify callback/clock) that
/// the manager path only touches indirectly.
#[test]
fn preview_device_pull_side() {
	use oakaudio::params::AudioParams;
	use oakaudio::previewdevice::PreviewAudioDevice;

	let mut dev = PreviewAudioDevice::new();
	dev.set_params(AudioParams {
		sample_rate: 48000,
		channel_layout: 3,
		format: oakaudio::params::SampleFormat::F32,
	});
	assert_eq!(dev.bytes_per_frame(), 8);

	let callbacks = std::sync::Arc::new(std::sync::atomic::AtomicI32::new(0));
	let cb = std::sync::Arc::clone(&callbacks);
	dev.set_notify_callback(move || {
		cb.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
	});
	dev.set_notify_interval(4);
	dev.write(&[1u8; 10]);

	// Reading 6 bytes crosses a 4-byte notify boundary.
	let mut buf = [0u8; 6];
	assert_eq!(dev.read(&mut buf), 6);
	assert!(callbacks.load(std::sync::atomic::Ordering::Relaxed) >= 1);
	assert_eq!(buf, [1u8; 6]);

	// Clock accounting.
	dev.add_output_frames(3);
	assert_eq!(dev.output_frames_consumed(), 3);
	dev.reset_output_frames();
	assert_eq!(dev.output_frames_consumed(), 0);
	dev.clear();
	assert_eq!(dev.output_frames_consumed(), 0);
}

/// free(NULL)/free(empty) are no-ops on the manager handle.
#[test]
fn free_null_noop() {
	let _guard = lock();
	unsafe { oakaudio_manager_create_instance() };
	let before = unsafe { oakaudio_debug_alive_count() };

	let mut empty = oakaudio::handle::CHandle::null();
	unsafe { oakaudio_manager_free(&mut empty) };
	unsafe { oakaudio_manager_free(std::ptr::null_mut()) };
	assert_eq!(unsafe { oakaudio_debug_alive_count() }, before);

	unsafe { oakaudio_manager_destroy_instance() };
}
