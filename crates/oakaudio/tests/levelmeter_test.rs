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

//! AudioLevelMeter contract tests (levelmeter.rs), calling the public
//! `analyze_sample_buffer` API.

mod common;

use oakaudio::levelmeter::{analyze_sample_buffer, Stats};

fn analyze(planes: &[Vec<f32>]) -> Stats {
	let refs: Vec<&[f32]> = planes.iter().map(Vec::as_slice).collect();
	analyze_sample_buffer(&refs)
}

/// A silence buffer reports silence=true, all-zero linear fields, and dB
/// fields floored at -200.
#[test]
fn silence_analysis() {
	let planes = common::silence_planar(2, 64);
	let stats = analyze(&planes);
	assert!(stats.silence);
	assert_eq!(stats.max_peak_linear, 0.0);
	assert_eq!(stats.integrated_lufs, -200.0);
	for ch in &stats.channels {
		assert_eq!(ch.peak_linear, 0.0);
		assert_eq!(ch.rms_linear, 0.0);
		assert_eq!(ch.peak_db, -200.0);
		assert_eq!(ch.rms_db, -200.0);
		assert_eq!(ch.vu_db, -200.0);
	}
}

/// A constant-amplitude tone reports peak_linear == rms_linear == that
/// amplitude (power terms), peak_db matches 20*log10(amp), and silence is
/// false.
#[test]
fn constant_tone_stats() {
	let planes = common::planar_from(&[0.5f32; 64], 1);
	let stats = analyze(&planes);
	assert!(!stats.silence);
	assert!((stats.channels[0].peak_linear - 0.5).abs() < 1e-9);
	assert!((stats.channels[0].rms_linear - 0.5).abs() < 1e-9);
	let expected_db = 20.0 * 0.5f64.log10();
	assert!((stats.channels[0].peak_db - expected_db).abs() < 1e-9);
	assert!((stats.channels[0].rms_db - expected_db).abs() < 1e-9);
}

/// A full-scale square wave yields max_peak_linear == 1.0 and a peak_db
/// near 0 dB; per-channel stats are filled for each channel.
#[test]
fn full_scale_peak() {
	let ch0: Vec<f32> = (0..64)
		.map(|i| if i % 2 == 0 { 1.0 } else { -1.0 })
		.collect();
	let ch1: Vec<f32> = (0..64)
		.map(|i| if i % 2 == 0 { -1.0 } else { 1.0 })
		.collect();
	let stats = analyze(&[ch0, ch1]);
	assert_eq!(stats.max_peak_linear, 1.0);
	assert!(!stats.silence);
	assert!((stats.channels[0].peak_db - 0.0).abs() < 1e-9);
	assert!((stats.channels[1].peak_db - 0.0).abs() < 1e-9);
	assert!((stats.channels[0].rms_linear - 1.0).abs() < 1e-9);
	assert!((stats.channels[1].rms_linear - 1.0).abs() < 1e-9);
}

/// integrated_lufs stays -200 for silence and matches the BS.1770
/// mean-square formula (no K-weighting) for a tone.
#[test]
fn integrated_lufs_silence_vs_tone() {
	let silence = common::silence_planar(2, 64);
	let stats = analyze(&silence);
	assert_eq!(stats.integrated_lufs, -200.0);

	let tone = common::planar_from(&[0.5f32; 64], 2);
	let stats = analyze(&tone);
	// mean square over all channels = 0.25; -0.691 + 10*log10(0.25)
	let expected = -0.691 + 10.0 * 0.25f64.log10();
	assert!((stats.integrated_lufs - expected).abs() < 1e-9);
}

/// Empty or zero-length inputs yield a default stats struct (no channels,
/// silence).
#[test]
fn empty_input() {
	let stats = analyze_sample_buffer(&[]);
	assert!(stats.channels.is_empty());
	assert!(stats.silence);

	let silence = common::silence_planar(1, 0);
	let refs: Vec<&[f32]> = silence.iter().map(Vec::as_slice).collect();
	let stats = analyze_sample_buffer(&refs);
	assert!(stats.silence);
	assert_eq!(stats.max_peak_linear, 0.0);
}
