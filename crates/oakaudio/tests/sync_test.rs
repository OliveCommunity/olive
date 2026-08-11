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

//! AudioSynchronizer + AudioWaveformSync contract tests
//! (synchronizer.rs, waveformsync.rs), through the C ABI.

mod common;

use oakaudio::error::OAKAUDIO_E_INVALID;
use oakaudio::ffi::sync::{
	oakaudio_sync_estimate_envelope_offset, oakaudio_sync_estimate_stretch_and_offset,
	oakaudio_sync_extract_rms_envelope, oakaudio_sync_place_by_source_time,
	oakaudio_sync_place_by_waveform_offset, OffsetResult, SourceClip, StretchOffsetResult,
};

fn clip(source: i64, media_in: i64, has_source: bool) -> SourceClip {
	SourceClip {
		source_start_time_num: source,
		source_start_time_den: 1,
		media_in_num: media_in,
		media_in_den: 1,
		has_source_start_time: has_source as i32,
	}
}

/// place_by_source_time: a candidate with matching source time lands at the
/// reference's timeline in point; a source-less candidate (has_source_
/// start_time false) is invalid (no media_in fallback in the C++ logic).
#[test]
fn place_by_source_time_matching() {
	let reference = clip(0, 0, true);
	let candidate = clip(0, 0, true);
	let (mut num, mut den, mut valid) = (0i64, 0i64, 0i32);
	let r = unsafe {
		oakaudio_sync_place_by_source_time(
			&reference, &candidate, 5, 1, &mut num, &mut den, &mut valid,
		)
	};
	assert_eq!(r, 0);
	assert_eq!(num, 5);
	assert_eq!(den, 1);
	assert_eq!(valid, 1);

	// A source-less candidate is invalid: valid=0, null rational.
	let candidate = clip(0, 0, false);
	let r = unsafe {
		oakaudio_sync_place_by_source_time(
			&reference, &candidate, 5, 1, &mut num, &mut den, &mut valid,
		)
	};
	assert_eq!(r, 0);
	assert_eq!(valid, 0);
	assert_eq!(num, 0);
	assert_eq!(den, 0);
}

/// place_by_source_time: when source times disagree by a known delta, the
/// candidate's timeline in point shifts by that delta (in seconds).
#[test]
fn place_by_source_time_delta() {
	let reference = clip(5, 0, true);
	let candidate = clip(12, 0, true);
	let (mut num, mut den, mut valid) = (0i64, 0i64, 0i32);
	let r = unsafe {
		oakaudio_sync_place_by_source_time(
			&reference, &candidate, 0, 1, &mut num, &mut den, &mut valid,
		)
	};
	assert_eq!(r, 0);
	// 0 + (12 + 0) - (5 + 0) = 7
	assert_eq!(num, 7);
	assert_eq!(den, 1);
	assert_eq!(valid, 1);

	// A zero denominator is rejected up front.
	let r = unsafe {
		oakaudio_sync_place_by_source_time(
			&reference, &candidate, 0, 0, &mut num, &mut den, &mut valid,
		)
	};
	assert_eq!(r, OAKAUDIO_E_INVALID);
}

/// place_by_waveform_offset converts a sample offset at a sample rate into
/// a timeline-in shift; out_valid is 1 on success and 0 for a null rate.
#[test]
fn place_by_waveform_offset_conversion() {
	let (mut num, mut den, mut valid) = (0i64, 0i64, 0i32);
	let r = unsafe {
		oakaudio_sync_place_by_waveform_offset(0, 1, 48000, 48000, &mut num, &mut den, &mut valid)
	};
	assert_eq!(r, 0);
	assert_eq!(num, 1);
	assert_eq!(den, 1);
	assert_eq!(valid, 1);

	let r = unsafe {
		oakaudio_sync_place_by_waveform_offset(1, 2, 48000, 48000, &mut num, &mut den, &mut valid)
	};
	assert_eq!(r, 0);
	assert_eq!(num, 3);
	assert_eq!(den, 2);
	assert_eq!(valid, 1);

	// A null rate is invalid (valid=0, null rational), and the FFI rejects
	// a zero timeline denominator.
	let r = unsafe {
		oakaudio_sync_place_by_waveform_offset(1, 2, 48000, 0, &mut num, &mut den, &mut valid)
	};
	assert_eq!(r, 0);
	assert_eq!(valid, 0);
	assert_eq!(num, 0);
	assert_eq!(den, 0);

	let r = unsafe {
		oakaudio_sync_place_by_waveform_offset(1, 0, 48000, 48000, &mut num, &mut den, &mut valid)
	};
	assert_eq!(r, OAKAUDIO_E_INVALID);
}

/// extract_rms_envelope produces one value per window; a window larger than
/// the input yields a single envelope point.
#[test]
fn extract_rms_envelope_shape() {
	let data: Vec<f32> = (0..100).map(|i| i as f32).collect();
	let planes = common::planar_from(&data, 2);
	let ptrs: Vec<*const f32> = planes.iter().map(|p| p.as_ptr()).collect();

	let mut out = vec![0.0f64; 16];
	let n = unsafe {
		oakaudio_sync_extract_rms_envelope(ptrs.as_ptr(), 2, 100, 10, out.as_mut_ptr(), 16)
	};
	assert_eq!(n, 10);
	assert!(out.iter().take(10).all(|&v| v > 0.0));

	let n = unsafe {
		oakaudio_sync_extract_rms_envelope(ptrs.as_ptr(), 2, 100, 200, out.as_mut_ptr(), 16)
	};
	assert_eq!(n, 1);

	// Invalid inputs.
	assert_eq!(
		unsafe {
			oakaudio_sync_extract_rms_envelope(std::ptr::null(), 2, 100, 10, out.as_mut_ptr(), 16)
		},
		OAKAUDIO_E_INVALID
	);
	assert_eq!(
		unsafe {
			oakaudio_sync_extract_rms_envelope(ptrs.as_ptr(), 0, 100, 10, out.as_mut_ptr(), 16)
		},
		OAKAUDIO_E_INVALID
	);
	assert_eq!(
		unsafe {
			oakaudio_sync_extract_rms_envelope(ptrs.as_ptr(), 2, 100, 0, out.as_mut_ptr(), 16)
		},
		OAKAUDIO_E_INVALID
	);
	assert_eq!(
		unsafe {
			oakaudio_sync_extract_rms_envelope(ptrs.as_ptr(), 2, 100, 10, out.as_mut_ptr(), -1)
		},
		OAKAUDIO_E_INVALID
	);
	// Two-stage: NULL out returns the required count.
	let n = unsafe {
		oakaudio_sync_extract_rms_envelope(ptrs.as_ptr(), 2, 100, 10, std::ptr::null_mut(), 0)
	};
	assert_eq!(n, 10);
}

/// estimate_envelope_offset: for a candidate delayed by N windows relative
/// to the reference, the returned offset is +N windows and valid=1.
#[test]
fn envelope_offset_recovers_delay() {
	let reference: Vec<f64> = (0..10).map(|i| i as f64).collect();
	let mut candidate = vec![0.0f64; 10];
	candidate[2..].copy_from_slice(&reference[..8]);
	let mut out = OffsetResult {
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};
	let r = unsafe {
		oakaudio_sync_estimate_envelope_offset(
			reference.as_ptr(),
			10,
			candidate.as_ptr(),
			10,
			std::ptr::null(),
			std::ptr::null(),
			100,
			10,
			&mut out,
		)
	};
	assert_eq!(r, 0);
	assert_eq!(out.valid, 1);
	assert_eq!(out.offset_samples, 200);
	assert!((out.confidence - 1.0).abs() < 1e-9);
}

/// estimate_envelope_offset: windows masked invalid on either side are
/// excluded from correlation; empty masks are treated as all-valid.
#[test]
fn envelope_offset_respects_valid_masks() {
	let reference: Vec<f64> = (0..10).map(|i| i as f64).collect();
	let mut candidate = vec![0.0f64; 10];
	candidate[2..].copy_from_slice(&reference[..8]);

	// Only the last reference window is valid -> no lag has >= 2 valid
	// overlap windows, so the estimate is invalid.
	let mut ref_valid = [1u8; 10];
	ref_valid[..9].fill(0);
	let mut out = OffsetResult {
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};
	let r = unsafe {
		oakaudio_sync_estimate_envelope_offset(
			reference.as_ptr(),
			10,
			candidate.as_ptr(),
			10,
			ref_valid.as_ptr(),
			std::ptr::null(),
			100,
			10,
			&mut out,
		)
	};
	assert_eq!(r, 0);
	assert_eq!(out.valid, 0);
	assert_eq!(out.confidence, 0.0);

	// Fully-valid masks behave like the unmasked call.
	let mut valid = [1u8; 10];
	let r = unsafe {
		oakaudio_sync_estimate_envelope_offset(
			reference.as_ptr(),
			10,
			candidate.as_ptr(),
			10,
			valid.as_ptr(),
			valid.as_ptr(),
			100,
			10,
			&mut out,
		)
	};
	assert_eq!(r, 0);
	assert_eq!(out.valid, 1);
	assert_eq!(out.offset_samples, 200);

	// NULL arrays / non-positive lengths are invalid.
	let r = unsafe {
		oakaudio_sync_estimate_envelope_offset(
			std::ptr::null(),
			10,
			candidate.as_ptr(),
			10,
			std::ptr::null(),
			std::ptr::null(),
			100,
			10,
			&mut out,
		)
	};
	assert_eq!(r, OAKAUDIO_E_INVALID);
}

/// estimate_stretch_and_offset: a candidate sampled at 2x the reference
/// rate reports rate ~2.0 (>1 = speed up) with a valid=1 result. A
/// non-linear (sine) reference is used — normalized correlation of linear
/// ramps is degenerate (any rate correlates 1.0), but only the true rate
/// resamples the sine back onto the reference exactly.
#[test]
fn stretch_offset_recovers_rate() {
	let reference: Vec<f64> = (0..10)
		.map(|k| (2.0 * std::f64::consts::PI * 0.7 * k as f64).sin())
		.collect();
	// Candidate at 2x: even samples are exact, odd samples are midpoints.
	let mut candidate = Vec::with_capacity(20);
	for k in 0..10 {
		candidate.push(reference[k]);
		if k + 1 < 10 {
			candidate.push((reference[k] + reference[k + 1]) / 2.0);
		}
	}
	let mut out = StretchOffsetResult {
		rate: 0.0,
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};
	let r = unsafe {
		oakaudio_sync_estimate_stretch_and_offset(
			reference.as_ptr(),
			10,
			candidate.as_ptr(),
			candidate.len() as i32,
			std::ptr::null(),
			std::ptr::null(),
			100,
			10,
			0.5,
			3.0,
			0.1,
			&mut out,
		)
	};
	assert_eq!(r, 0);
	assert_eq!(out.valid, 1);
	assert!((out.rate - 2.0).abs() < 0.15, "rate = {}", out.rate);
	assert!(out.confidence > 0.99, "confidence = {}", out.confidence);

	// Invalid rate parameters are rejected.
	let r = unsafe {
		oakaudio_sync_estimate_stretch_and_offset(
			reference.as_ptr(),
			10,
			candidate.as_ptr(),
			candidate.len() as i32,
			std::ptr::null(),
			std::ptr::null(),
			100,
			10,
			0.0,
			3.0,
			0.1,
			&mut out,
		)
	};
	assert_eq!(r, OAKAUDIO_E_INVALID);
}

/// estimate_* on identical silent envelopes yields low/no confidence and
/// valid=0 (no correlation peak).
#[test]
fn silent_inputs_invalid() {
	let silence = vec![0.0f64; 10];
	let mut out = OffsetResult {
		offset_samples: 0,
		confidence: 0.0,
		valid: 0,
	};
	let r = unsafe {
		oakaudio_sync_estimate_envelope_offset(
			silence.as_ptr(),
			10,
			silence.as_ptr(),
			10,
			std::ptr::null(),
			std::ptr::null(),
			100,
			10,
			&mut out,
		)
	};
	assert_eq!(r, 0);
	assert_eq!(out.valid, 0);
	assert_eq!(out.confidence, 0.0);
}

/// The crate-level unmasked wrappers (estimate_offset on raw sample
/// buffers, estimate_envelope_offset on envelopes) route to the same
/// correlation core and recover the same delay. A non-monotonic envelope
/// is used — equal-slope linear ramps correlate 1.0 at multiple lags, so
/// only the exact match is unambiguous.
#[test]
fn crate_level_unmasked_wrappers() {
	let reference: Vec<f64> = vec![0.0, 0.1, 0.2, 0.9, 0.8, 0.3, 0.4, 0.5, 0.6, 0.7];
	let mut candidate = vec![0.0f64; 10];
	candidate[2..].copy_from_slice(&reference[..8]);

	let env = oakaudio::waveformsync::estimate_envelope_offset(&reference, &candidate, 100, 10);
	assert!(env.valid);
	assert_eq!(env.offset_samples, 200);
	assert!((env.confidence - 1.0).abs() < 1e-9);

	// Raw sample buffers: 10 windows of 100 constant-amplitude samples,
	// candidate delayed by two windows.
	let ref_samples: Vec<f32> = (0..1000).map(|i| reference[i / 100] as f32).collect();
	let mut cand_samples = vec![0.0f32; 1000];
	cand_samples[200..].copy_from_slice(&ref_samples[..800]);
	let raw = oakaudio::waveformsync::estimate_offset(
		&[ref_samples.as_slice()],
		&[cand_samples.as_slice()],
		100,
		500,
	);
	assert!(raw.valid, "offset should be recovered from raw samples");
	assert_eq!(raw.offset_samples, 200);
}
