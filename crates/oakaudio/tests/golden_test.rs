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

//! Cross-cutting golden/parity tests: values captured from the C++
//! implementation to pin exact behavior of the Rust rewrite.

mod common;

use std::ffi::CString;

use common::write_wav_header_only;
use oakcore_rs::Rational;
use oakaudio::params::{frames_to_rational, rational_to_samples, AudioParams, SampleFormat};
use oakaudio::waveform::{extract, AudioVisualWaveform};

/// SampleFormat planar-first ordering matches the authoritative C++ enum:
/// f32_p == 4 == OAKAUDIO_PROCESSOR_OUTPUT_FORMAT. This guards the
/// oakcore-rs ordering divergence documented in params.rs.
#[test]
fn sample_format_planar_first_ordering() {
	assert_eq!(SampleFormat::F32Planar as i32, 4);
	assert_eq!(oakaudio::processor::OUTPUT_FORMAT as i32, 4);
	// Invalid is -1 and the packed family follows planar-first.
	assert_eq!(SampleFormat::Invalid as i32, -1);
	assert_eq!(SampleFormat::U8Planar as i32, 0);
	assert_eq!(SampleFormat::F64 as i32, 11);
}

/// Rational time<->sample conversions (frames_to_rational /
/// rational_to_samples) round-trip 48000 Hz sample counts exactly.
#[test]
fn sample_time_conversion_roundtrip() {
	let rate = 48000i32;
	for frames in [0i64, 1, 480, 48000, 48001, 1234567] {
		let t = frames_to_rational(frames, rate);
		assert_eq!(rational_to_samples(t, rate), frames);
	}
	assert_eq!(frames_to_rational(48000, 48000), Rational::new(1, 1));
	assert_eq!(frames_to_rational(1, 48000), Rational::new(1, 48000));
}

/// AudioParams value-type conversions: channel count from the layout mask,
/// bytes-per-sample-per-channel, samples_to_bytes, and the double ->
/// rational conversion edge cases (NaN / out-of-range / tiny -> null).
#[test]
fn params_value_types() {
	use oakaudio::params::rational_from_double;

	let p = AudioParams {
		sample_rate: 48000,
		channel_layout: 3,
		format: SampleFormat::F32,
	};
	assert_eq!(p.channel_count(), 2);
	assert_eq!(p.bytes_per_sample_per_channel(), 4);
	assert_eq!(p.samples_to_bytes(480), 480 * 4 * 2);

	assert_eq!(rational_from_double(0.5), Rational::new(1, 2));
	assert_eq!(rational_from_double(1.0), Rational::new(1, 1));
	assert!(rational_from_double(f64::NAN).is_null());
	assert!(rational_from_double(1e10).is_null());
	assert!(rational_from_double(1e-20).is_null());
	assert!((rational_from_double(0.25).to_f64() - 0.25).abs() < 1e-9);
}

/// AudioVisualWaveform mipmap layout: get_summary at a fine zoom scale
/// covers fewer source samples than at a coarse scale, so the returned
/// min/max pair brackets exactly the mipmapped window. The values are
/// captured from the Rust implementation (which mirrors the C++ mipmap
/// chain); the window coverage itself is load-bearing.
#[test]
fn waveform_mipmap_scale_parity() {
	// 1024 ramp samples @ 48000 Hz, two channels.
	let ch0: Vec<f32> = (0..1024).map(|i| i as f32 * 0.001).collect();
	let ch1: Vec<f32> = (0..1024).map(|i| -(i as f32) * 0.001).collect();
	let planes = [ch0.as_slice(), ch1.as_slice()];

	let mut w = AudioVisualWaveform::new();
	w.set_channel_count(2);
	w.overwrite_samples(&planes, 48000, Rational::new(0, 1));

	// One summary point is produced for any queried window; a 1/1024 s
	// window (one 1024-rate mipmap point ~ 46.875 source samples) must
	// bracket a narrower range than a 1/64 s window (~750 samples).
	let fine = w.get_summary_from_time(Rational::new(0, 1), Rational::new(1, 1024));
	assert_eq!(fine.len(), 2);
	assert_eq!(fine[0].min, 0.0);
	assert!(
		(fine[0].max - 0.046).abs() < 1e-5,
		"fine max = {}",
		fine[0].max
	);
	assert!(
		(fine[1].min + 0.046).abs() < 1e-5,
		"fine min = {}",
		fine[1].min
	);
	assert_eq!(fine[1].max, 0.0);

	let coarse = w.get_summary_from_time(Rational::new(0, 1), Rational::new(1, 64));
	assert_eq!(coarse.len(), 2);
	assert_eq!(coarse[0].min, 0.0);
	assert!(
		(coarse[0].max - 0.749).abs() < 1e-5,
		"coarse max = {}",
		coarse[0].max
	);
	assert!(
		(coarse[1].min + 0.749).abs() < 1e-5,
		"coarse min = {}",
		coarse[1].min
	);
	assert_eq!(coarse[1].max, 0.0);

	// Coarser windows necessarily cover more source samples.
	assert!(coarse[0].max > fine[0].max);
	assert!(coarse[1].min < fine[1].min);
}

/// levelmeter dB conversion: peak_db == 20*log10(peak_linear) and the
/// -200 dB floor match the C++ helpers for the same sample values.
#[test]
fn levelmeter_db_golden() {
	let tone = common::planar_from(&[0.5f32; 64], 1);
	let refs: Vec<&[f32]> = tone.iter().map(|v| v.as_slice()).collect();
	let stats = oakaudio::levelmeter::analyze_sample_buffer(&refs);
	let expected_db = 20.0 * 0.5f64.log10();
	assert!((stats.channels[0].peak_db - expected_db).abs() < 1e-9);
	assert!((stats.channels[0].rms_db - expected_db).abs() < 1e-9);

	let silence = common::silence_planar(1, 64);
	let refs: Vec<&[f32]> = silence.iter().map(|v| v.as_slice()).collect();
	let stats = oakaudio::levelmeter::analyze_sample_buffer(&refs);
	assert_eq!(stats.channels[0].peak_db, -200.0);
	assert_eq!(stats.channels[0].rms_db, -200.0);
	assert_eq!(stats.channels[0].vu_db, -200.0);
}

/// waveformsync envelope offset golden: a reference ramp delayed by two
/// windows in the candidate is recovered as +2 windows with full
/// confidence.
#[test]
fn waveform_sync_offset_golden() {
	let reference: Vec<f64> = (0..10).map(|i| i as f64 * 0.1 + 0.1).collect();
	let mut candidate = vec![0.0f64; 10];
	candidate[2..].copy_from_slice(&reference[..8]);
	let out = oakaudio::waveformsync::estimate_envelope_offset(&reference, &candidate, 100, 10);
	assert!(out.valid);
	assert_eq!(out.offset_samples, 200);
	assert!((out.confidence - 1.0).abs() < 1e-9);
}

/// waveform::extract channel cap: a stream claiming more than
/// EXTRACT_MAX_CHANNELS (64) channels is rejected rather than overflowing
/// the internal plane array.
#[test]
fn extract_channel_cap() {
	let path = std::env::temp_dir().join(format!("oakaudio_cap_{}.wav", std::process::id()));
	write_wav_header_only(&path, 65, 48000).unwrap();

	let cpath = CString::new(path.to_str().unwrap()).unwrap();
	assert!(
		extract(&cpath, 0, 4).is_err(),
		"oversized stream must be rejected"
	);
	std::fs::remove_file(&path).ok();
}
