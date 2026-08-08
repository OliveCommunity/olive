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

//! AudioVisualWaveform contract tests (waveform.rs), through the C ABI.

mod common;

use std::ffi::CString;

use common::{pair, write_wav};
use oakaudio::error::{OAKAUDIO_E_INVALID, OAKAUDIO_E_STATE};
use oakaudio::ffi::waveform::{
	oakaudio_waveform_extract, oakaudio_waveform_free, oakaudio_waveform_get_channel_count,
	oakaudio_waveform_get_summary, oakaudio_waveform_init, oakaudio_waveform_length,
	oakaudio_waveform_overwrite_samples, oakaudio_waveform_overwrite_silence,
	oakaudio_waveform_overwrite_sums, oakaudio_waveform_re_sum_s, oakaudio_waveform_resize,
	oakaudio_waveform_set_channel_count, oakaudio_waveform_sum_samples_s,
	oakaudio_waveform_trim_in, oakaudio_waveform_trim_range, MinMax,
};

/// Fill `w` with 100 samples/channel of a 0..0.99 ramp at 100 Hz (1 s).
fn fill_ramp(w: oakaudio::handle::CHandle) {
	let ch0: Vec<f32> = (0..100).map(|i| i as f32 * 0.01).collect();
	let ch1: Vec<f32> = (0..100).map(|i| -(i as f32) * 0.01).collect();
	let planes = [ch0.as_ptr(), ch1.as_ptr()];
	assert_eq!(unsafe { oakaudio_waveform_set_channel_count(w, 2) }, 0);
	assert_eq!(
		unsafe { oakaudio_waveform_overwrite_samples(w, planes.as_ptr(), 100, 100, 0, 1) },
		0
	);
}

fn summary(w: oakaudio::handle::CHandle, start: (i64, i64), length: (i64, i64), cap: i32) -> Vec<MinMax> {
	let mut out = vec![MinMax { min: 0.0, max: 0.0 }; cap as usize * 2];
	let n = unsafe {
		oakaudio_waveform_get_summary(
			w,
			start.0,
			start.1,
			length.0,
			length.1,
			out.as_mut_ptr(),
			cap,
		)
	};
	assert_eq!(n, cap);
	out.truncate(cap as usize * 2);
	out
}

/// set_channel_count then overwrite_samples writes planar data at the given
/// start; length() reflects the covered span and get_summary returns
/// channel-interleaved min/max pairs.
#[test]
fn overwrite_samples_and_length() {
	let mut w = unsafe { oakaudio_waveform_init() };
	fill_ramp(w);

	let (mut num, mut den) = (0i64, 0i64);
	assert_eq!(unsafe { oakaudio_waveform_length(w, &mut num, &mut den) }, 0);
	assert_eq!(num, 1);
	assert_eq!(den, 1);

	let out = summary(w, (0, 1), (1, 1), 1);
	assert_eq!(out[0].min, 0.0);
	assert!((out[0].max - 0.99).abs() < 1e-5, "max = {}", out[0].max);
	assert!((out[1].min + 0.99).abs() < 1e-5, "min = {}", out[1].min);
	assert_eq!(out[1].max, 0.0);
	unsafe { oakaudio_waveform_free(&mut w) };
}

/// get_summary with out_pairs NULL returns the required point count without
/// writing; a too-small capacity returns the same count and leaves the
/// buffer untouched (two-stage contract).
#[test]
fn summary_two_stage_query() {
	let mut w = unsafe { oakaudio_waveform_init() };
	fill_ramp(w);

	// NULL out: required count only.
	let n = unsafe { oakaudio_waveform_get_summary(w, 0, 1, 1, 1, std::ptr::null_mut(), 0) };
	assert_eq!(n, 1);

	// Too-small capacity: same count, buffer untouched.
	let mut out = [MinMax { min: -1.0, max: -1.0 }; 2];
	let n = unsafe { oakaudio_waveform_get_summary(w, 0, 1, 1, 1, out.as_mut_ptr(), 0) };
	assert_eq!(n, 1);
	assert_eq!(out[0].min, -1.0);
	assert_eq!(out[0].max, -1.0);

	// A zero/negative length is invalid.
	assert_eq!(
		unsafe { oakaudio_waveform_get_summary(w, 0, 1, 0, 1, std::ptr::null_mut(), 0) },
		OAKAUDIO_E_INVALID
	);
	assert_eq!(
		unsafe { oakaudio_waveform_get_summary(w, 0, 1, 1, 0, std::ptr::null_mut(), 0) },
		OAKAUDIO_E_INVALID
	);
	unsafe { oakaudio_waveform_free(&mut w) };
}

/// overwrite_sums copies channel-interleaved pairs from another waveform
/// into a dest range; a 0/1 length copies all of src.
#[test]
fn overwrite_sums_range_copy() {
	let mut src = unsafe { oakaudio_waveform_init() };
	fill_ramp(src);

	let mut dst = unsafe { oakaudio_waveform_init() };
	assert_eq!(unsafe { oakaudio_waveform_set_channel_count(dst, 2) }, 0);
	assert_eq!(
		unsafe { oakaudio_waveform_overwrite_sums(dst, src, 0, 1, 0, 1, 0, 1) },
		0
	);
	// A 0/1 length means "copy everything": dst matches src exactly.
	let out = summary(dst, (0, 1), (1, 1), 1);
	assert_eq!(out[0].min, 0.0);
	assert!((out[0].max - 0.99).abs() < 1e-5, "max = {}", out[0].max);
	assert!((out[1].min + 0.99).abs() < 1e-5, "min = {}", out[1].min);
	assert_eq!(out[1].max, 0.0);

	let (mut num, mut den) = (0i64, 0i64);
	unsafe { oakaudio_waveform_length(dst, &mut num, &mut den) };
	assert_eq!(num, 1);

	// Empty src handle is invalid.
	let empty = oakaudio::handle::CHandle::null();
	assert_eq!(
		unsafe { oakaudio_waveform_overwrite_sums(dst, empty, 0, 1, 0, 1, 0, 1) },
		OAKAUDIO_E_INVALID
	);

	unsafe { oakaudio_waveform_free(&mut dst) };
	unsafe { oakaudio_waveform_free(&mut src) };
}

/// overwrite_silence zeroes min/max over a range without changing length.
#[test]
fn overwrite_silence() {
	let mut w = unsafe { oakaudio_waveform_init() };
	fill_ramp(w);
	assert_eq!(
		unsafe { oakaudio_waveform_overwrite_silence(w, 0, 1, 1, 2) },
		0
	);

	// First half is silenced; second half retains the ramp data.
	let out = summary(w, (0, 1), (1, 2), 1);
	assert_eq!(pair(out[0].min, out[0].max), pair(0.0, 0.0));
	assert_eq!(pair(out[1].min, out[1].max), pair(0.0, 0.0));

	let out = summary(w, (1, 2), (1, 2), 1);
	assert!(out[0].max > 0.5, "second half must keep ramp data, got {:?}", out[0]);
	assert!(out[1].min < -0.5);

	let (mut num, mut den) = (0i64, 0i64);
	unsafe { oakaudio_waveform_length(w, &mut num, &mut den) };
	assert_eq!((num, den), (1, 1), "overwrite_silence must not change length");
	unsafe { oakaudio_waveform_free(&mut w) };
}

/// trim_in/trim_range/resize adjust length and drop or pad data; a negative
/// trim_in prepends silence (C++ semantics).
#[test]
fn trim_and_resize() {
	let mut w = unsafe { oakaudio_waveform_init() };
	fill_ramp(w);

	// Negative trim_in prepends silence: absolute end (length) unchanged.
	assert_eq!(unsafe { oakaudio_waveform_trim_in(w, -1, 2) }, 0);
	let (mut num, mut den) = (0i64, 0i64);
	unsafe { oakaudio_waveform_length(w, &mut num, &mut den) };
	assert_eq!((num, den), (1, 1));

	// Resize extends to 2 s.
	assert_eq!(unsafe { oakaudio_waveform_resize(w, 2, 1) }, 0);
	unsafe { oakaudio_waveform_length(w, &mut num, &mut den) };
	assert_eq!((num, den), (2, 1));

	// trim_range keeps 0.5 s from the (prepended) start.
	assert_eq!(unsafe { oakaudio_waveform_trim_range(w, 0, 1, 1, 2) }, 0);
	unsafe { oakaudio_waveform_length(w, &mut num, &mut den) };
	assert_eq!((num, den), (1, 2));

	// A negative resize target or a zero denominator is invalid.
	assert_eq!(
		unsafe { oakaudio_waveform_resize(w, -1, 2) },
		OAKAUDIO_E_INVALID
	);
	assert_eq!(
		unsafe { oakaudio_waveform_resize(w, 1, 0) },
		OAKAUDIO_E_INVALID
	);
	unsafe { oakaudio_waveform_free(&mut w) };
}

/// sum_samples_s reduces planar samples into one min/max pair per channel;
/// re_sum_s merges channel-interleaved entries into one pair per channel.
/// Both match golden vectors from the C++ implementation.
#[test]
fn sum_and_resum_golden() {
	let ch0 = [1.0f32, -2.0, 3.0];
	let ch1 = [4.0f32, -5.0, 6.0];
	let planes = [ch0.as_ptr(), ch1.as_ptr()];
	let mut out = [MinMax { min: 0.0, max: 0.0 }; 2];
	let r = unsafe {
		oakaudio_waveform_sum_samples_s(planes.as_ptr(), 2, 0, 3, out.as_mut_ptr())
	};
	assert_eq!(r, 0);
	assert_eq!(pair(out[0].min, out[0].max), pair(-2.0, 3.0));
	assert_eq!(pair(out[1].min, out[1].max), pair(-5.0, 6.0));

	// re_sum_s over 4 interleaved entries, 2 channels -> one pair per
	// channel merging both points.
	let input = [
		MinMax { min: 1.0, max: 2.0 },
		MinMax { min: 3.0, max: 4.0 },
		MinMax { min: 5.0, max: 6.0 },
		MinMax { min: 7.0, max: 8.0 },
	];
	let mut out = [MinMax { min: 0.0, max: 0.0 }; 2];
	let r = unsafe { oakaudio_waveform_re_sum_s(input.as_ptr(), 4, 2, out.as_mut_ptr()) };
	assert_eq!(r, 0);
	assert_eq!(pair(out[0].min, out[0].max), pair(1.0, 6.0));
	assert_eq!(pair(out[1].min, out[1].max), pair(3.0, 8.0));

	// Invalid arguments.
	assert_eq!(
		unsafe { oakaudio_waveform_sum_samples_s(planes.as_ptr(), 2, 0, 0, out.as_mut_ptr()) },
		OAKAUDIO_E_INVALID
	);
	assert_eq!(
		unsafe { oakaudio_waveform_re_sum_s(input.as_ptr(), 0, 2, out.as_mut_ptr()) },
		OAKAUDIO_E_INVALID
	);
}

/// extract decodes a file through the oakcodec decoder C ABI into
/// channel-interleaved pairs; a missing file returns OAKAUDIO_E_NOT_FOUND.
#[test]
fn extract_file_and_notfound() {
	let path = std::env::temp_dir().join(format!(
		"oakaudio_extract_{}.wav",
		std::process::id()
	));
	// 8 frames of stereo ramp, 48000 Hz.
	let mut samples = Vec::with_capacity(16);
	for i in 0..8i16 {
		samples.push(i * 1000);
		samples.push(-(i * 1000));
	}
	write_wav(&path, 2, 48000, &samples).unwrap();
	let cpath = CString::new(path.to_str().unwrap()).unwrap();

	// Size query first: NULL out_pairs, the count and channel count still
	// come back.
	let mut channels = 0i32;
	let n = unsafe {
		oakaudio_waveform_extract(
			cpath.as_ptr(),
			0,
			4,
			std::ptr::null_mut(),
			0,
			&mut channels,
		)
	};
	assert_eq!(n, 2);
	assert_eq!(channels, 2);

	// Full extraction: 8 frames / 4 per point = 2 points, 2 pairs each.
	let mut pairs = [MinMax { min: 0.0, max: 0.0 }; 4];
	let mut channels = 0i32;
	let n = unsafe {
		oakaudio_waveform_extract(
			cpath.as_ptr(),
			0,
			4,
			pairs.as_mut_ptr(),
			2,
			&mut channels,
		)
	};
	assert_eq!(n, 2);
	assert_eq!(channels, 2);
	let scale = 32768.0f32;
	assert_eq!(pair(pairs[0].min, pairs[0].max), pair(0.0, 3000.0 / scale));
	assert_eq!(pair(pairs[1].min, pairs[1].max), pair(-3000.0 / scale, 0.0));
	assert_eq!(pair(pairs[2].min, pairs[2].max), pair(4000.0 / scale, 7000.0 / scale));
	assert_eq!(pair(pairs[3].min, pairs[3].max), pair(-7000.0 / scale, -4000.0 / scale));

	// Missing file -> NOT_FOUND.
	let missing = CString::new(
		std::env::temp_dir()
			.join(format!("oakaudio_missing_{}.wav", std::process::id()))
			.to_str()
			.unwrap(),
	)
	.unwrap();
	let _ = std::fs::remove_file(std::path::Path::new(missing.to_str().unwrap()));
	let r = unsafe {
		oakaudio_waveform_extract(
			missing.as_ptr(),
			0,
			4,
			std::ptr::null_mut(),
			0,
			std::ptr::null_mut(),
		)
	};
	assert_eq!(r, -60004);

	std::fs::remove_file(&path).ok();
}

/// FFI validation and empty-handle error paths.
#[test]
fn ffi_error_paths() {
	let empty = oakaudio::handle::CHandle::null();
	assert_eq!(
		unsafe { oakaudio_waveform_get_channel_count(empty) },
		OAKAUDIO_E_INVALID
	);
	assert_eq!(
		unsafe { oakaudio_waveform_set_channel_count(empty, 2) },
		OAKAUDIO_E_INVALID
	);
	let (mut num, mut den) = (0i64, 0i64);
	assert_eq!(
		unsafe { oakaudio_waveform_length(empty, &mut num, &mut den) },
		OAKAUDIO_E_INVALID
	);

	// Negative channel count is invalid.
	let mut w = unsafe { oakaudio_waveform_init() };
	assert_eq!(
		unsafe { oakaudio_waveform_set_channel_count(w, -1) },
		OAKAUDIO_E_INVALID
	);

	// overwrite_samples before set_channel_count is a state error.
	let data = [0.5f32; 8];
	let planes = [data.as_ptr()];
	assert_eq!(
		unsafe { oakaudio_waveform_overwrite_samples(w, planes.as_ptr(), 8, 48000, 0, 1) },
		OAKAUDIO_E_STATE
	);
	// A zero denominator is rejected.
	assert_eq!(
		unsafe { oakaudio_waveform_overwrite_samples(w, planes.as_ptr(), 8, 48000, 0, 0) },
		OAKAUDIO_E_INVALID
	);
	// A non-positive frame count is invalid.
	assert_eq!(
		unsafe { oakaudio_waveform_overwrite_samples(w, planes.as_ptr(), 0, 48000, 0, 1) },
		OAKAUDIO_E_INVALID
	);
	// NULL planes are invalid.
	assert_eq!(
		unsafe { oakaudio_waveform_overwrite_samples(w, std::ptr::null(), 8, 48000, 0, 1) },
		OAKAUDIO_E_INVALID
	);
	unsafe { oakaudio_waveform_free(&mut w) };
}

/// free(NULL)/free(empty) are no-ops.
#[test]
fn free_null_noop() {
	let mut w = oakaudio::handle::CHandle::null();
	unsafe { oakaudio_waveform_free(&mut w) };
	unsafe { oakaudio_waveform_free(std::ptr::null_mut()) };
}
