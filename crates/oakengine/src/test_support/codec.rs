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

//! Smoke tests for the encoding family (`engine/include/oakengine/encoding.h`):
//! container/codec metadata queries and the encoding-params handle.

use super::common;

use std::ffi::{c_char, c_int};

use crate::codec::{
	oakengine_encoding_codec_is_lossless, oakengine_encoding_codec_is_still_image,
	oakengine_encoding_codec_name, oakengine_encoding_filename_contains_digit_placeholder,
	oakengine_encoding_filename_remove_digit_placeholder,
	oakengine_encoding_format_audio_codec_count, oakengine_encoding_format_count,
	oakengine_encoding_format_extension, oakengine_encoding_format_name,
	oakengine_encoding_format_video_codec_at, oakengine_encoding_format_video_codec_count,
	oakengine_encoding_generate_matrix, oakengine_encoding_image_sequence_digit_count,
	oakengine_encoding_params_audio_enabled, oakengine_encoding_params_color_transform_output,
	oakengine_encoding_params_create, oakengine_encoding_params_destroy,
	oakengine_encoding_params_enable_audio, oakengine_encoding_params_enable_video,
	oakengine_encoding_params_filename, oakengine_encoding_params_format,
	oakengine_encoding_params_get_audio_params, oakengine_encoding_params_get_custom_range,
	oakengine_encoding_params_get_video_params, oakengine_encoding_params_has_custom_range,
	oakengine_encoding_params_is_valid, oakengine_encoding_params_set_color_transform,
	oakengine_encoding_params_set_custom_range, oakengine_encoding_params_set_filename,
	oakengine_encoding_params_set_format, oakengine_encoding_params_set_video_bit_rate,
	oakengine_encoding_params_set_video_option, oakengine_encoding_params_set_video_pix_fmt,
	oakengine_encoding_params_video_bit_rate, oakengine_encoding_params_video_codec,
	oakengine_encoding_params_video_enabled, oakengine_encoding_params_video_option,
	oakengine_encoding_params_video_pix_fmt, oakengine_encoding_pix_fmt_index,
	oakengine_encoding_start_audio_recording,
};
use crate::common::OakVideoParamsPod;

/// Container format / codec metadata queries.
#[test]
fn encoding_metadata() {
	// Format enumeration: at least the six named formats exist.
	let count = unsafe { oakengine_encoding_format_count() };
	assert!(count >= 6);

	// Matroska (1): name + extension via two-stage getters.
	let mut buf = [0 as c_char; 64];
	let len = unsafe { oakengine_encoding_format_name(1, buf.as_mut_ptr(), 64) };
	assert!(len > 0);
	assert!(unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
		.to_str()
		.unwrap()
		.contains("Matroska"));
	let len = unsafe { oakengine_encoding_format_extension(1, buf.as_mut_ptr(), 64) };
	assert!(len > 0);
	assert_eq!(
		unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_str()
			.unwrap(),
		"mkv"
	);

	// Per-format codec lists.
	assert!(unsafe { oakengine_encoding_format_video_codec_count(1) } >= 1);
	let codec = unsafe { oakengine_encoding_format_video_codec_at(1, 0) };
	assert!(codec >= 1);
	assert!(unsafe { oakengine_encoding_format_audio_codec_count(1) } >= 1);

	// Codec metadata: name, still-image (PNG = 5), lossless.
	let len = unsafe { oakengine_encoding_codec_name(1, buf.as_mut_ptr(), 64) };
	assert!(len > 0);
	assert_eq!(unsafe { oakengine_encoding_codec_is_still_image(5) }, 1); // PNG
	assert_eq!(unsafe { oakengine_encoding_codec_is_still_image(1) }, 0); // H264
	assert!(unsafe { oakengine_encoding_codec_is_lossless(13) } == 1); // PCM

	// pix_fmt_index: preferred format index when absent.
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_index(1, c"yuv420p".as_ptr()) },
		0
	);
}

/// Image-sequence filename helpers.
#[test]
fn filename_helpers() {
	assert_eq!(
		unsafe {
			oakengine_encoding_filename_contains_digit_placeholder(c"img[#####].png".as_ptr())
		},
		1
	);
	assert_eq!(
		unsafe { oakengine_encoding_filename_contains_digit_placeholder(c"img.png".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_image_sequence_digit_count(c"img[#####].png".as_ptr()) },
		5
	);

	let mut buf = [0 as c_char; 64];
	let len = unsafe {
		oakengine_encoding_filename_remove_digit_placeholder(
			c"img[#####].png".as_ptr(),
			buf.as_mut_ptr(),
			64,
		)
	};
	assert!(len > 0);
	assert_eq!(
		unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_str()
			.unwrap(),
		"img.png"
	);
}

/// Transform matrix: fit produces a valid 16-float matrix.
#[test]
fn generate_matrix() {
	let mut m = [0.0_f32; 16];
	assert_eq!(
		unsafe { oakengine_encoding_generate_matrix(0, 1920, 1080, 960, 540, m.as_mut_ptr()) },
		0
	);
	// The 4x4 identity-ish matrix has a non-zero top-left.
	assert!(m[0] > 0.0 || m[5] > 0.0);
	// NULL output → E_INVALID.
	assert_eq!(
		unsafe { oakengine_encoding_generate_matrix(0, 1, 1, 1, 1, std::ptr::null_mut()) },
		-1
	);
}

/// Encoding-params handle lifecycle: create → configure → read back →
/// destroy (serialized inside one test; the handle is per-call state).
#[test]
fn params_handle_round_trip() {
	let p = unsafe { oakengine_encoding_params_create() };
	assert!(!p.is_null());

	// Fresh: nothing enabled, format unset (-1).
	assert_eq!(unsafe { oakengine_encoding_params_is_valid(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_format(p) }, -1);

	// Format: set + get, and reject out-of-range.
	assert_eq!(unsafe { oakengine_encoding_params_set_format(p, 1) }, 0); // Matroska
	assert_eq!(unsafe { oakengine_encoding_params_format(p) }, 1);
	assert_eq!(unsafe { oakengine_encoding_params_set_format(p, 9999) }, -1);

	// Filename round-trip.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_filename(p, c"out.mkv".as_ptr()) },
		0
	);
	let mut buf = [0 as c_char; 64];
	let len = unsafe { oakengine_encoding_params_filename(p, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 7);
	assert_eq!(
		unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_str()
			.unwrap(),
		"out.mkv"
	);

	// Enable video: valid, get_video_params reads back.
	let mut vp: OakVideoParamsPod = unsafe { std::mem::zeroed() };
	assert_eq!(
		unsafe {
			crate::common::oakengine_video_params_make(
				&mut vp, 1920, 1080, 1001, 30000, 4, 1, 1, 0, 1, 1,
			)
		},
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_video(p, &vp, 1) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_is_valid(p) }, 1);
	assert_eq!(unsafe { oakengine_encoding_params_video_enabled(p) }, 1);
	assert_eq!(unsafe { oakengine_encoding_params_video_codec(p) }, 1);
	let mut out_vp: OakVideoParamsPod = unsafe { std::mem::zeroed() };
	assert_eq!(
		unsafe { oakengine_encoding_params_get_video_params(p, &mut out_vp) },
		0
	);
	assert_eq!(out_vp.width, 1920);
	assert_eq!(out_vp.height, 1080);
	assert_eq!(out_vp.time_base_num, 1001);

	// Video bit rate round-trip.
	unsafe { oakengine_encoding_params_set_video_bit_rate(p, 8_000_000) };
	assert_eq!(
		unsafe { oakengine_encoding_params_video_bit_rate(p) },
		8_000_000
	);

	// Encoded pixel format round-trip.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_pix_fmt(p, c"yuv420p".as_ptr()) },
		0
	);
	let len = unsafe { oakengine_encoding_params_video_pix_fmt(p, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 7);
	assert_eq!(
		unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_str()
			.unwrap(),
		"yuv420p"
	);

	// Audio: disabled get_video/audio → E_STATE; enable then read back.
	let mut sr: c_int = 0;
	let mut layout: u64 = 0;
	let mut sf: c_int = 0;
	assert_eq!(
		unsafe { oakengine_encoding_params_get_audio_params(p, &mut sr, &mut layout, &mut sf) },
		-2
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_audio(p, 48000, 3, 0, 13) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_audio_enabled(p) }, 1);
	assert_eq!(
		unsafe { oakengine_encoding_params_get_audio_params(p, &mut sr, &mut layout, &mut sf) },
		0
	);
	assert_eq!(sr, 48000);
	assert_eq!(layout, 3);

	// Custom range: not set → E_NOT_FOUND; set → reads back.
	let (mut inn, mut ind, mut outn, mut outd) = (0i64, 0i64, 0i64, 0i64);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_get_custom_range(p, &mut inn, &mut ind, &mut outn, &mut outd)
		},
		-4
	);
	unsafe { oakengine_encoding_params_set_custom_range(p, 0, 1, 100, 1) };
	assert_eq!(unsafe { oakengine_encoding_params_has_custom_range(p) }, 1);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_get_custom_range(p, &mut inn, &mut ind, &mut outn, &mut outd)
		},
		0
	);
	assert_eq!((inn, ind, outn, outd), (0, 1, 100, 1));

	// Color transform + video option round-trips.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_color_transform(p, c"ACEScg".as_ptr()) },
		0
	);
	let len = unsafe { oakengine_encoding_params_color_transform_output(p, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 6);
	assert_eq!(
		unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_str()
			.unwrap(),
		"ACEScg"
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_option(p, c"crf".as_ptr(), c"18".as_ptr()) },
		0
	);
	let len =
		unsafe { oakengine_encoding_params_video_option(p, c"crf".as_ptr(), buf.as_mut_ptr(), 64) };
	assert_eq!(len, 2);
	assert_eq!(
		unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
			.to_str()
			.unwrap(),
		"18"
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_video_option(p, c"missing".as_ptr(), buf.as_mut_ptr(), 64)
		},
		-4
	);

	unsafe { oakengine_encoding_params_destroy(p) };
	// NULL destroy is a no-op.
	unsafe { oakengine_encoding_params_destroy(std::ptr::null_mut()) };
}

/// Audio recording without a running audio manager fails with E_STATE.
#[test]
fn start_audio_recording_no_manager() {
	let p = unsafe { oakengine_encoding_params_create() };
	assert!(!p.is_null());
	let rc = unsafe { oakengine_encoding_start_audio_recording(p, std::ptr::null_mut(), 0) };
	assert_eq!(rc, -2); // OAKENGINE_E_STATE
	unsafe { oakengine_encoding_params_destroy(p) };
}
