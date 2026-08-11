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

//! AudioProcessor contract tests (processor.rs), through the C ABI. The
//! conversion runs a real FFmpeg filter graph (aresample/aformat/atempo),
//! so resampling and time-stretch have filter latency: the exact frame
//! counts are drained after `flush`, while identity conversion is an
//! immediate passthrough.

mod common;

use oakaudio::error::{OAKAUDIO_E_INVALID, OAKAUDIO_E_STATE, OAKAUDIO_OK};
use oakaudio::ffi::processor::{
	oakaudio_processor_close, oakaudio_processor_convert, oakaudio_processor_flush,
	oakaudio_processor_free, oakaudio_processor_init, oakaudio_processor_is_open,
	oakaudio_processor_open,
};

/// Stereo f32_p planes of `frames` ramp samples.
fn ramp_planes(frames: usize) -> Vec<Vec<f32>> {
	vec![
		(0..frames).map(|i| i as f32 * 0.01).collect(),
		(0..frames).map(|i| -(i as f32) * 0.01).collect(),
	]
}

fn open_identity(h: oakaudio::handle::CHandle) -> i32 {
	unsafe { oakaudio_processor_open(h, 48000, 3, 4, 48000, 3, 4, 1.0) }
}

/// init yields a valid handle; is_open is false before open and true after;
/// close returns it to closed without error.
#[test]
fn processor_open_isopen_close() {
	let mut h = unsafe { oakaudio_processor_init() };
	assert!(!h.ctx.is_null());
	assert_eq!(unsafe { oakaudio_processor_is_open(h) }, 0);
	assert_eq!(open_identity(h), OAKAUDIO_OK);
	assert_eq!(unsafe { oakaudio_processor_is_open(h) }, 1);
	assert_eq!(unsafe { oakaudio_processor_close(h) }, OAKAUDIO_OK);
	assert_eq!(unsafe { oakaudio_processor_is_open(h) }, 0);
	unsafe { oakaudio_processor_free(&mut h) };
}

/// open with matching in/out rate and format is an identity passthrough:
/// convert returns the same frame count and samples within 1e-6.
#[test]
fn identity_convert_passthrough() {
	let mut h = unsafe { oakaudio_processor_init() };
	assert_eq!(open_identity(h), OAKAUDIO_OK);

	let planes = ramp_planes(32);
	let in_ptrs: Vec<*const f32> = planes.iter().map(|p| p.as_ptr()).collect();
	let mut out = vec![vec![0f32; 32]; 2];
	let mut out_ptrs: Vec<*mut f32> = out.iter_mut().map(|p| p.as_mut_ptr()).collect();

	let n = unsafe {
		oakaudio_processor_convert(
			h,
			in_ptrs.as_ptr(),
			32,
			out_ptrs.as_ptr(),
			32,
		)
	};
	assert_eq!(n, 32);
	for ch in 0..2 {
		for i in 0..32 {
			assert!(
				(out[ch][i] - planes[ch][i]).abs() < 1e-6,
				"ch{ch}[{i}]: {} vs {}",
				out[ch][i],
				planes[ch][i]
			);
		}
	}
	unsafe { oakaudio_processor_free(&mut h) };
}

/// convert with an output capacity smaller than the produced frames returns
/// the produced count clamped to capacity and fills up to capacity.
#[test]
fn convert_capacity_truncation() {
	let mut h = unsafe { oakaudio_processor_init() };
	assert_eq!(open_identity(h), OAKAUDIO_OK);

	let planes = ramp_planes(32);
	let in_ptrs: Vec<*const f32> = planes.iter().map(|p| p.as_ptr()).collect();
	let mut out = vec![vec![9.9f32; 10]; 2];
	let mut out_ptrs: Vec<*mut f32> = out.iter_mut().map(|p| p.as_mut_ptr()).collect();

	let n = unsafe {
		oakaudio_processor_convert(h, in_ptrs.as_ptr(), 32, out_ptrs.as_ptr(), 10)
	};
	assert_eq!(n, 10);
	for ch in 0..2 {
		for i in 0..10 {
			assert_eq!(out[ch][i], planes[ch][i]);
		}
	}

	// The graph has already drained; nothing further to pull.
	let mut out2 = vec![vec![0f32; 32]; 2];
	let mut out2_ptrs: Vec<*mut f32> = out2.iter_mut().map(|p| p.as_mut_ptr()).collect();
	let n = unsafe {
		oakaudio_processor_convert(h, in_ptrs.as_ptr(), 0, out2_ptrs.as_ptr(), 32)
	};
	assert_eq!(n, 0);

	unsafe { oakaudio_processor_free(&mut h) };
}

/// open with a zero/negative rate or a wrong output format returns
/// OAKAUDIO_E_INVALID and leaves the processor closed; an empty handle is
/// OAKAUDIO_E_INVALID everywhere.
#[test]
fn open_invalid_params() {
	let mut h = unsafe { oakaudio_processor_init() };
	assert_eq!(
		unsafe { oakaudio_processor_open(h, 0, 3, 4, 48000, 3, 4, 1.0) },
		OAKAUDIO_E_INVALID
	);
	assert_eq!(unsafe { oakaudio_processor_is_open(h) }, 0);
	assert_eq!(
		unsafe { oakaudio_processor_open(h, 48000, 3, 4, 48000, 3, 0, 1.0) },
		OAKAUDIO_E_INVALID
	);
	assert_eq!(unsafe { oakaudio_processor_is_open(h) }, 0);

	let empty = oakaudio::handle::CHandle::null();
	assert_eq!(open_identity(empty), OAKAUDIO_E_INVALID);
	assert_eq!(unsafe { oakaudio_processor_is_open(empty) }, OAKAUDIO_E_INVALID);
	assert_eq!(unsafe { oakaudio_processor_close(empty) }, OAKAUDIO_E_INVALID);
	assert_eq!(unsafe { oakaudio_processor_flush(empty) }, OAKAUDIO_E_INVALID);
	let mut out_ptrs: Vec<*mut f32> = Vec::new();
	assert_eq!(
		unsafe { oakaudio_processor_convert(empty, std::ptr::null(), 0, out_ptrs.as_ptr(), 0) },
		OAKAUDIO_E_INVALID
	);

	// convert before open is a state error.
	assert_eq!(
		unsafe { oakaudio_processor_convert(h, std::ptr::null(), 0, out_ptrs.as_ptr(), 0) },
		OAKAUDIO_E_STATE
	);
	unsafe { oakaudio_processor_free(&mut h) };
}

/// Resampling to half rate halves the frame count (44100 -> 22050). The
/// real resampler holds samples back (filter delay), so the frames are
/// drained after `flush`; flush then keeps the processor open.
#[test]
fn resample_and_flush() {
	let mut h = unsafe { oakaudio_processor_init() };
	assert_eq!(
		unsafe { oakaudio_processor_open(h, 44100, 3, 4, 22050, 3, 4, 1.0) },
		OAKAUDIO_OK
	);

	// 1 second of input keeps the resampler delay well below the signal.
	let frames = 44100;
	let planes = ramp_planes(frames);
	let in_ptrs: Vec<*const f32> = planes.iter().map(|p| p.as_ptr()).collect();
	let mut out = vec![vec![0f32; frames]; 2];
	let mut out_ptrs: Vec<*mut f32> = out.iter_mut().map(|p| p.as_mut_ptr()).collect();

	let mut total = unsafe {
		oakaudio_processor_convert(h, in_ptrs.as_ptr(), frames as i32, out_ptrs.as_ptr(), frames as i32)
	};
	assert_eq!(unsafe { oakaudio_processor_flush(h) }, OAKAUDIO_OK);
	// Drain the resampler delay after end-of-input.
	while total < frames as i32 {
		let n = unsafe {
			oakaudio_processor_convert(
				h,
				std::ptr::null(),
				0,
				out_ptrs.as_ptr(),
				frames as i32,
			)
		};
		if n == 0 {
			break;
		}
		total += n;
	}
	assert!(
		(total - 22050).abs() <= 2,
		"half-rate output must halve the frame count (got {total})"
	);
	assert_eq!(unsafe { oakaudio_processor_is_open(h) }, 1);
	unsafe { oakaudio_processor_free(&mut h) };
}

/// A tempo factor != 1.0 time-stretches: tempo 2.0 halves the frame count
/// (drained after `flush`; atempo needs a full analysis window before it
/// produces output) and the processor stays open.
#[test]
fn tempo_stretch() {
	let mut h = unsafe { oakaudio_processor_init() };
	assert_eq!(
		unsafe { oakaudio_processor_open(h, 48000, 3, 4, 48000, 3, 4, 2.0) },
		OAKAUDIO_OK
	);

	// 1 second of input: many atempo windows (1024 samples at 48 kHz).
	let frames = 48000;
	let planes = ramp_planes(frames);
	let in_ptrs: Vec<*const f32> = planes.iter().map(|p| p.as_ptr()).collect();
	let mut out = vec![vec![0f32; frames]; 2];
	let mut out_ptrs: Vec<*mut f32> = out.iter_mut().map(|p| p.as_mut_ptr()).collect();

	let mut total = unsafe {
		oakaudio_processor_convert(h, in_ptrs.as_ptr(), frames as i32, out_ptrs.as_ptr(), frames as i32)
	};
	assert_eq!(unsafe { oakaudio_processor_flush(h) }, OAKAUDIO_OK);
	while total < frames as i32 {
		let n = unsafe {
			oakaudio_processor_convert(
				h,
				std::ptr::null(),
				0,
				out_ptrs.as_ptr(),
				frames as i32,
			)
		};
		if n == 0 {
			break;
		}
		total += n;
	}
	assert!(
		(total - 24000).abs() <= 2400,
		"tempo 2.0 must halve the frame count (got {total})"
	);
	assert_eq!(unsafe { oakaudio_processor_is_open(h) }, 1);
	unsafe { oakaudio_processor_close(h) };

	// A non-positive speed is rejected (on a closed processor).
	assert_eq!(
		unsafe { oakaudio_processor_open(h, 48000, 3, 4, 48000, 3, 4, 0.0) },
		OAKAUDIO_E_INVALID
	);
	assert_eq!(open_identity(h), OAKAUDIO_OK);
	// Already open -> state error.
	assert_eq!(open_identity(h), OAKAUDIO_E_STATE);
	unsafe { oakaudio_processor_free(&mut h) };
}

/// free(NULL)/free(empty) are no-ops.
#[test]
fn free_null_noop() {
	let mut h = oakaudio::handle::CHandle::null();
	unsafe { oakaudio_processor_free(&mut h) };
	unsafe { oakaudio_processor_free(std::ptr::null_mut()) };
}
