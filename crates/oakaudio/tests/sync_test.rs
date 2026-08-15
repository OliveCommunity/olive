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
//! (synchronizer.rs, waveformsync.rs), calling the public Rust API.

mod common;

use oakcore_rs::Rational;
use oakaudio::synchronizer::{place_by_source_time, place_by_waveform_offset, SourceClip};
use oakaudio::waveformsync::{
	estimate_envelope_offset, estimate_envelope_offset_valid, estimate_stretch_and_offset,
	extract_rms_envelope,
};

fn clip(source: i64, media_in: i64, has_source: bool) -> SourceClip {
	SourceClip {
		source_start_time: Rational::new(source, 1),
		media_in: Rational::new(media_in, 1),
		has_source_start_time: has_source,
	}
}

/// place_by_source_time: a candidate with matching source time lands at the
/// reference's timeline in point; a source-less candidate (has_source_
/// start_time false) is invalid (no media_in fallback in the C++ logic).
#[test]
fn place_by_source_time_matching() {
	let reference = clip(0, 0, true);
	let candidate = clip(0, 0, true);
	let placement = place_by_source_time(&reference, &candidate, Rational::new(5, 1));
	assert!(placement.valid);
	assert_eq!(placement.timeline_in, Rational::new(5, 1));

	// A source-less candidate is invalid: valid=false, null rational.
	let candidate = clip(0, 0, false);
	let placement = place_by_source_time(&reference, &candidate, Rational::new(5, 1));
	assert!(!placement.valid);
	assert!(placement.timeline_in.is_null());
}

/// place_by_source_time: when source times disagree by a known delta, the
/// candidate's timeline in point shifts by that delta (in seconds).
#[test]
fn place_by_source_time_delta() {
	let reference = clip(5, 0, true);
	let candidate = clip(12, 0, true);
	let placement = place_by_source_time(&reference, &candidate, Rational::new(0, 1));
	// 0 + (12 + 0) - (5 + 0) = 7
	assert!(placement.valid);
	assert_eq!(placement.timeline_in, Rational::new(7, 1));
}

/// place_by_waveform_offset converts a sample offset at a sample rate into
/// a timeline-in shift; out_valid is false for a null rate.
#[test]
fn place_by_waveform_offset_conversion() {
	let placement = place_by_waveform_offset(Rational::new(0, 1), 48000, 48000);
	assert!(placement.valid);
	assert_eq!(placement.timeline_in, Rational::new(1, 1));

	let placement = place_by_waveform_offset(Rational::new(1, 2), 48000, 48000);
	assert!(placement.valid);
	assert_eq!(placement.timeline_in, Rational::new(3, 2));

	// A null rate is invalid (valid=false, null rational).
	let placement = place_by_waveform_offset(Rational::new(1, 2), 48000, 0);
	assert!(!placement.valid);
	assert!(placement.timeline_in.is_null());
}

/// extract_rms_envelope produces one value per window; a window larger than
/// the input yields a single envelope point.
#[test]
fn extract_rms_envelope_shape() {
	let data: Vec<f32> = (0..100).map(|i| i as f32).collect();
	let planes = common::planar_from(&data, 2);
	let refs: Vec<&[f32]> = planes.iter().map(Vec::as_slice).collect();

	let env = extract_rms_envelope(&refs, 10);
	assert_eq!(env.len(), 10);
	assert!(env.iter().all(|&v| v > 0.0));

	let env = extract_rms_envelope(&refs, 200);
	assert_eq!(env.len(), 1);
}

/// estimate_envelope_offset: for a candidate delayed by N windows relative
/// to the reference, the returned offset is +N windows and valid=true.
#[test]
fn envelope_offset_recovers_delay() {
	let reference: Vec<f64> = (0..10).map(|i| i as f64).collect();
	let mut candidate = vec![0.0f64; 10];
	candidate[2..].copy_from_slice(&reference[..8]);
	let out = estimate_envelope_offset(&reference, &candidate, 100, 10);
	assert!(out.valid);
	assert_eq!(out.offset_samples, 200);
	assert!((out.confidence - 1.0).abs() < 1e-9);
}

/// estimate_envelope_offset_valid: windows masked invalid on either side
/// are excluded from correlation; empty masks are treated as all-valid.
#[test]
fn envelope_offset_respects_valid_masks() {
	let reference: Vec<f64> = (0..10).map(|i| i as f64).collect();
	let mut candidate = vec![0.0f64; 10];
	candidate[2..].copy_from_slice(&reference[..8]);

	// Only the last reference window is valid -> no lag has >= 2 valid
	// overlap windows, so the estimate is invalid.
	let mut ref_valid = vec![false; 10];
	ref_valid[9] = true;
	let out = estimate_envelope_offset_valid(
		&reference,
		&candidate,
		&ref_valid,
		&[],
		100,
		10,
	);
	assert!(!out.valid);
	assert_eq!(out.confidence, 0.0);

	// Fully-valid masks behave like the unmasked call.
	let valid = vec![true; 10];
	let out = estimate_envelope_offset_valid(
		&reference,
		&candidate,
		&valid,
		&valid,
		100,
		10,
	);
	assert!(out.valid);
	assert_eq!(out.offset_samples, 200);
}

/// estimate_stretch_and_offset: a candidate sampled at 2x the reference
/// rate reports rate ~2.0 (>1 = speed up) with a valid=true result. A
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
	let out = estimate_stretch_and_offset(
		&reference,
		&candidate,
		&[],
		&[],
		100,
		10,
		0.5,
		3.0,
		0.1,
	);
	assert!(out.valid);
	assert!((out.rate - 2.0).abs() < 0.15, "rate = {}", out.rate);
	assert!(out.confidence > 0.99, "confidence = {}", out.confidence);

	// Invalid rate parameters are rejected (invalid result, defaults).
	let out = estimate_stretch_and_offset(
		&reference,
		&candidate,
		&[],
		&[],
		100,
		10,
		0.0,
		3.0,
		0.1,
	);
	assert!(!out.valid);
}

/// estimate_* on identical silent envelopes yields low/no confidence and
/// valid=false (no correlation peak).
#[test]
fn silent_inputs_invalid() {
	let silence = vec![0.0f64; 10];
	let out = estimate_envelope_offset(&silence, &silence, 100, 10);
	assert!(!out.valid);
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
