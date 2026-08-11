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

//! AudioLevelMeter contract tests (levelmeter.rs), through the C ABI.

mod common;

use oakaudio::error::{OAKAUDIO_E_INVALID, OAKAUDIO_OK};
use oakaudio::ffi::levelmeter::{oakaudio_levelmeter_analyze, ChannelStats, MeterStats};

fn analyze(planes: &[Vec<f32>]) -> (Vec<ChannelStats>, MeterStats) {
	let ptrs: Vec<*const f32> = planes.iter().map(|p| p.as_ptr()).collect();
	let mut channels: Vec<ChannelStats> = (0..planes.len())
		.map(|_| ChannelStats {
			peak_linear: 0.0,
			peak_db: 0.0,
			rms_linear: 0.0,
			rms_db: 0.0,
			vu_db: 0.0,
		})
		.collect();
	let mut summary = MeterStats {
		max_peak_linear: 0.0,
		integrated_lufs: 0.0,
		silence: 0,
	};
	let r = unsafe {
		oakaudio_levelmeter_analyze(
			ptrs.as_ptr(),
			planes.len() as i32,
			planes.first().map_or(0, |p| p.len()) as i32,
			channels.as_mut_ptr(),
			channels.len() as i32,
			&mut summary,
		)
	};
	assert_eq!(r, OAKAUDIO_OK);
	(channels, summary)
}

/// A silence buffer reports silence=1, all-zero linear fields, and dB
/// fields floored at -200.
#[test]
fn silence_analysis() {
	let planes = common::silence_planar(2, 64);
	let (channels, summary) = analyze(&planes);
	assert_eq!(summary.silence, 1);
	assert_eq!(summary.max_peak_linear, 0.0);
	assert_eq!(summary.integrated_lufs, -200.0);
	for ch in &channels {
		assert_eq!(ch.peak_linear, 0.0);
		assert_eq!(ch.rms_linear, 0.0);
		assert_eq!(ch.peak_db, -200.0);
		assert_eq!(ch.rms_db, -200.0);
		assert_eq!(ch.vu_db, -200.0);
	}
}

/// A constant-amplitude tone reports peak_linear == rms_linear == that
/// amplitude (power terms), peak_db matches 20*log10(amp), and silence=0.
#[test]
fn constant_tone_stats() {
	let planes = common::planar_from(&[0.5f32; 64], 1);
	let (channels, summary) = analyze(&planes);
	assert_eq!(summary.silence, 0);
	assert!((channels[0].peak_linear - 0.5).abs() < 1e-9);
	assert!((channels[0].rms_linear - 0.5).abs() < 1e-9);
	let expected_db = 20.0 * 0.5f64.log10();
	assert!((channels[0].peak_db - expected_db).abs() < 1e-9);
	assert!((channels[0].rms_db - expected_db).abs() < 1e-9);
}

/// A full-scale square wave yields max_peak_linear == 1.0 and a peak_db
/// near 0 dB; per-channel channels array is filled for each channel.
#[test]
fn full_scale_peak() {
	let ch0: Vec<f32> = (0..64)
		.map(|i| if i % 2 == 0 { 1.0 } else { -1.0 })
		.collect();
	let ch1: Vec<f32> = (0..64)
		.map(|i| if i % 2 == 0 { -1.0 } else { 1.0 })
		.collect();
	let (channels, summary) = analyze(&[ch0, ch1]);
	assert_eq!(summary.max_peak_linear, 1.0);
	assert_eq!(summary.silence, 0);
	assert!((channels[0].peak_db - 0.0).abs() < 1e-9);
	assert!((channels[1].peak_db - 0.0).abs() < 1e-9);
	assert!((channels[0].rms_linear - 1.0).abs() < 1e-9);
	assert!((channels[1].rms_linear - 1.0).abs() < 1e-9);
}

/// integrated_lufs stays -200 for silence and matches the BS.1770
/// mean-square formula (no K-weighting) for a tone.
#[test]
fn integrated_lufs_silence_vs_tone() {
	let silence = common::silence_planar(2, 64);
	let (_, summary) = analyze(&silence);
	assert_eq!(summary.integrated_lufs, -200.0);

	let tone = common::planar_from(&[0.5f32; 64], 2);
	let (_, summary) = analyze(&tone);
	// mean square over all channels = 0.25; -0.691 + 10*log10(0.25)
	let expected = -0.691 + 10.0 * 0.25f64.log10();
	assert!((summary.integrated_lufs - expected).abs() < 1e-9);
}

/// channel_count of 0, a NULL planar pointer, or NULL for both outputs
/// returns OAKAUDIO_E_INVALID.
#[test]
fn invalid_input() {
	let planes = common::planar_from(&[0.5f32; 8], 1);
	let ptr = planes[0].as_ptr();
	let mut summary = MeterStats {
		max_peak_linear: 0.0,
		integrated_lufs: 0.0,
		silence: 0,
	};

	// channel_count 0.
	assert_eq!(
		unsafe { oakaudio_levelmeter_analyze(&ptr, 0, 8, std::ptr::null_mut(), 0, &mut summary) },
		OAKAUDIO_E_INVALID
	);
	// NULL planar.
	assert_eq!(
		unsafe {
			oakaudio_levelmeter_analyze(
				std::ptr::null(),
				1,
				8,
				std::ptr::null_mut(),
				0,
				&mut summary,
			)
		},
		OAKAUDIO_E_INVALID
	);
	// Both outputs NULL.
	assert_eq!(
		unsafe {
			oakaudio_levelmeter_analyze(&ptr, 1, 8, std::ptr::null_mut(), 0, std::ptr::null_mut())
		},
		OAKAUDIO_E_INVALID
	);
	// Negative frame count.
	assert_eq!(
		unsafe { oakaudio_levelmeter_analyze(&ptr, 1, -1, std::ptr::null_mut(), 0, &mut summary) },
		OAKAUDIO_E_INVALID
	);
}
