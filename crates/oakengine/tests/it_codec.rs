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

//! Integration tests for the **codec family** — the facade module
//! `src/codec.rs` (contract: `engine/include/oakengine/encoding.h`, backed
//! by the oakcodec module headers `include/codec/{format,encoder}.h`).
//!
//! Every one of the 81 `oakengine_encoding_*` / `oakengine_export_*`
//! exports is exercised through real module behavior — no mocks, no
//! injected backends. Two error-code namespaces are in play:
//!
//! - The facade's own codes (`error.rs`, `-1..-6`): used when the facade
//!   itself rejects the call (NULL handle, out-of-range `set_format`, ...).
//! - The wrapped module codes, which the facade passes through **untranslated**
//!   (`error.rs`): `-50001`/`-50004` for the oakcodec metadata family
//!   (`include/codec/error.h`) and `-60001`/`-60003` for the oakaudio
//!   recording path (`include/audio/error.h`).
//!
//! String getters follow the engine's buf/size two-stage convention: the
//! return value is the string length **excluding** the NUL (negative =
//! error), and a NULL `buf` / `buf_size <= 0` only reports the length.
//!
//! Destroy contracts: `oakengine_encoding_params_destroy` is a facade-owned
//! raw box (`Box<ParamsBox>`) with **no** refcount and **no** debug alive
//! counter (unlike the oakcodec `CHandle` objects, which this family never
//! creates). NULL is a no-op; freeing the same live pointer twice is
//! use-after-free by design (the engine header transfers ownership), so the
//! tests assert NULL-idempotence rather than double-free of a live handle.

#[path = "common/mod.rs"]
mod common;

use std::ffi::{c_char, c_int};

use oakengine::codec::{
	oakengine_encoding_codec_is_lossless, oakengine_encoding_codec_is_still_image,
	oakengine_encoding_codec_name, oakengine_encoding_filename_contains_digit_placeholder,
	oakengine_encoding_filename_remove_digit_placeholder, oakengine_encoding_format_audio_codec_at,
	oakengine_encoding_format_audio_codec_count, oakengine_encoding_format_count,
	oakengine_encoding_format_extension, oakengine_encoding_format_name,
	oakengine_encoding_format_subtitle_codec_at, oakengine_encoding_format_subtitle_codec_count,
	oakengine_encoding_format_video_codec_at, oakengine_encoding_format_video_codec_count,
	oakengine_encoding_generate_matrix, oakengine_encoding_image_sequence_digit_count,
	oakengine_encoding_params_audio_bit_rate, oakengine_encoding_params_audio_codec,
	oakengine_encoding_params_audio_enabled, oakengine_encoding_params_color_transform_output,
	oakengine_encoding_params_create, oakengine_encoding_params_destroy,
	oakengine_encoding_params_disable_audio, oakengine_encoding_params_disable_subtitles,
	oakengine_encoding_params_disable_video, oakengine_encoding_params_enable_audio,
	oakengine_encoding_params_enable_sidecar_subtitles, oakengine_encoding_params_enable_subtitles,
	oakengine_encoding_params_enable_video, oakengine_encoding_params_filename,
	oakengine_encoding_params_format, oakengine_encoding_params_get_audio_params,
	oakengine_encoding_params_get_custom_range, oakengine_encoding_params_get_export_length,
	oakengine_encoding_params_get_last_used, oakengine_encoding_params_get_video_params,
	oakengine_encoding_params_has_custom_range, oakengine_encoding_params_is_valid,
	oakengine_encoding_params_load_file, oakengine_encoding_params_save_file,
	oakengine_encoding_params_set_audio_bit_rate, oakengine_encoding_params_set_color_transform,
	oakengine_encoding_params_set_custom_range, oakengine_encoding_params_set_export_length,
	oakengine_encoding_params_set_filename, oakengine_encoding_params_set_format,
	oakengine_encoding_params_set_last_used, oakengine_encoding_params_set_video_bit_rate,
	oakengine_encoding_params_set_video_buffer_size,
	oakengine_encoding_params_set_video_is_image_sequence,
	oakengine_encoding_params_set_video_max_bit_rate,
	oakengine_encoding_params_set_video_min_bit_rate, oakengine_encoding_params_set_video_option,
	oakengine_encoding_params_set_video_pix_fmt,
	oakengine_encoding_params_set_video_scaling_method,
	oakengine_encoding_params_set_video_threads, oakengine_encoding_params_subtitles_are_sidecar,
	oakengine_encoding_params_subtitles_codec, oakengine_encoding_params_subtitles_enabled,
	oakengine_encoding_params_subtitles_sidecar_format, oakengine_encoding_params_video_bit_rate,
	oakengine_encoding_params_video_buffer_size, oakengine_encoding_params_video_codec,
	oakengine_encoding_params_video_enabled, oakengine_encoding_params_video_is_image_sequence,
	oakengine_encoding_params_video_max_bit_rate, oakengine_encoding_params_video_min_bit_rate,
	oakengine_encoding_params_video_option, oakengine_encoding_params_video_pix_fmt,
	oakengine_encoding_params_video_scaling_method, oakengine_encoding_params_video_threads,
	oakengine_encoding_pix_fmt_at, oakengine_encoding_pix_fmt_count,
	oakengine_encoding_pix_fmt_index, oakengine_encoding_preset_count,
	oakengine_encoding_preset_name, oakengine_encoding_preset_path,
	oakengine_encoding_sample_format_at, oakengine_encoding_sample_format_count,
	oakengine_encoding_start_audio_recording, oakengine_export_render_with_params,
};
use oakengine::common::OakVideoParamsPod;

/// Facade error codes (`src/error.rs`).
const E_INVALID: c_int = -1;
const E_STATE: c_int = -2;
const E_FAILED: c_int = -3;
const E_NOT_FOUND: c_int = -4;

/// oakcodec module codes, passed through untranslated (`include/codec/error.h`).
const K_INVALID: c_int = -50001;
const K_NOT_FOUND: c_int = -50004;

/// oakaudio module codes (`include/audio/error.h`).
const A_INVALID: c_int = -60001;
const A_FAILED: c_int = -60003;

/// Read a NUL-terminated buffer written by a buf/size getter as a `String`.
fn read_buf(buf: &[c_char]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	let bytes: Vec<u8> = buf[..len].iter().map(|&c| c as u8).collect();
	String::from_utf8_lossy(&bytes).into_owned()
}

/// A 16-element f32 transform matrix plus a close-enough comparator.
type Matrix16 = [f32; 16];
fn assert_matrix(actual: &Matrix16, expected: &[f64; 16]) {
	for (i, (a, e)) in actual.iter().zip(expected.iter()).enumerate() {
		assert!(
			(*a as f64 - e).abs() < 1e-6,
			"matrix[{i}] = {a} (expected {e})"
		);
	}
}
fn identity16() -> [f64; 16] {
	[
		1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
	]
}

// ---------------------------------------------------------------------------
// 1. Container format / codec metadata — legal input matrix
// ---------------------------------------------------------------------------

/// Legal-path matrix over every metadata export: format enumeration,
/// per-format codec lists, codec flags, pixel/sample format lists, and the
/// image-sequence filename helpers.
#[test]
fn format_and_codec_metadata_legal() {
	common::force_link();
	let mut buf = [0 as c_char; 128];

	// Format enumeration: the table has 15 entries (0..=14; Count = 15).
	let count = unsafe { oakengine_encoding_format_count() };
	assert_eq!(count, 15);

	// Every format must have a non-empty name and extension, and its
	// per-format codec lists must resolve to codecs with names.
	for f in 0..count {
		let n = unsafe { oakengine_encoding_format_name(f, buf.as_mut_ptr(), 128) };
		assert!(n > 0, "format {f} name length");
		assert!(!read_buf(&buf).is_empty(), "format {f} name");

		let e = unsafe { oakengine_encoding_format_extension(f, buf.as_mut_ptr(), 128) };
		assert!(e > 0, "format {f} extension length");
		assert!(!read_buf(&buf).is_empty(), "format {f} extension");

		let vc = unsafe { oakengine_encoding_format_video_codec_count(f) };
		assert!(vc >= 0);
		for i in 0..vc {
			let codec = unsafe { oakengine_encoding_format_video_codec_at(f, i) };
			assert!(codec >= 0, "format {f} video codec at {i}");
			let cn = unsafe { oakengine_encoding_codec_name(codec, buf.as_mut_ptr(), 128) };
			assert!(cn > 0, "codec {codec} name length");
			assert!(!read_buf(&buf).is_empty());
		}
		let ac = unsafe { oakengine_encoding_format_audio_codec_count(f) };
		assert!(ac >= 0);
		for i in 0..ac {
			let codec = unsafe { oakengine_encoding_format_audio_codec_at(f, i) };
			assert!(codec >= 0, "format {f} audio codec at {i}");
			let cn = unsafe { oakengine_encoding_codec_name(codec, buf.as_mut_ptr(), 128) };
			assert!(cn > 0, "codec {codec} name length");
			assert!(!read_buf(&buf).is_empty());
		}
		let sc = unsafe { oakengine_encoding_format_subtitle_codec_count(f) };
		assert!(sc >= 0);
		for i in 0..sc {
			let codec = unsafe { oakengine_encoding_format_subtitle_codec_at(f, i) };
			assert!(codec >= 0, "format {f} subtitle codec at {i}");
			let cn = unsafe { oakengine_encoding_codec_name(codec, buf.as_mut_ptr(), 128) };
			assert!(cn > 0, "codec {codec} name length");
			assert!(!read_buf(&buf).is_empty());
		}
	}

	// Exact values for the named formats (exportformat.rs / exportcodec.rs).
	// Matroska (1).
	assert_eq!(
		unsafe { oakengine_encoding_format_name(1, buf.as_mut_ptr(), 128) },
		14
	);
	assert_eq!(read_buf(&buf), "Matroska Video");
	assert_eq!(
		unsafe { oakengine_encoding_format_extension(1, buf.as_mut_ptr(), 128) },
		3
	);
	assert_eq!(read_buf(&buf), "mkv");
	// MPEG-4 video (2): H.264 / H.264RGB / H.265.
	assert_eq!(unsafe { oakengine_encoding_format_video_codec_count(2) }, 3);
	assert_eq!(unsafe { oakengine_encoding_format_video_codec_at(2, 0) }, 1); // H.264
																		   // WAV (7): no video codecs, PCM (13) audio.
	assert_eq!(unsafe { oakengine_encoding_format_video_codec_count(7) }, 0);
	assert_eq!(unsafe { oakengine_encoding_format_audio_codec_count(7) }, 1);
	assert_eq!(
		unsafe { oakengine_encoding_format_audio_codec_at(7, 0) },
		13
	); // PCM
	// SRT (13): subtitle-only, SRT (17) codec.
	assert_eq!(
		unsafe { oakengine_encoding_format_audio_codec_count(13) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_subtitle_codec_count(13) },
		1
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_subtitle_codec_at(13, 0) },
		17
	); // SRT

	// Codec metadata: names, still-image, lossless.
	assert_eq!(
		unsafe { oakengine_encoding_codec_name(1, buf.as_mut_ptr(), 128) },
		5
	);
	assert_eq!(read_buf(&buf), "H.264");
	assert_eq!(unsafe { oakengine_encoding_codec_is_still_image(5) }, 1); // PNG
	assert_eq!(unsafe { oakengine_encoding_codec_is_still_image(1) }, 0); // H.264
	assert_eq!(unsafe { oakengine_encoding_codec_is_lossless(13) }, 1); // PCM
	assert_eq!(unsafe { oakengine_encoding_codec_is_lossless(12) }, 0); // AAC

	// Pixel formats: the Rust table is empty (CPP-PARITY interim, the list
	// is queried from the format's FFmpeg/OIIO encoder), so the count is 0
	// and every index is E_NOT_FOUND; the index helper falls back to 0.
	assert_eq!(unsafe { oakengine_encoding_pix_fmt_count(2, 1) }, 0);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_at(2, 1, 0, buf.as_mut_ptr(), 128) },
		K_NOT_FOUND
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_index(1, c"yuv420p".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_index(1, std::ptr::null()) },
		0
	);

	// Sample formats: PCM (13) inside WAV (7) exposes its native list.
	assert_eq!(unsafe { oakengine_encoding_sample_format_count(7, 13) }, 6);
	assert_eq!(unsafe { oakengine_encoding_sample_format_at(7, 13, 4) }, 10); // f32 packed
	for i in 0..6 {
		assert!(unsafe { oakengine_encoding_sample_format_at(7, 13, i) } >= 0);
	}
	// AAC (12) has no Rust sample-format table yet -> 0.
	assert_eq!(unsafe { oakengine_encoding_sample_format_count(2, 12) }, 0);

	// Image-sequence filename helpers.
	assert_eq!(
		unsafe {
			oakengine_encoding_filename_contains_digit_placeholder(c"/tmp/out_[#####].png".as_ptr())
		},
		1
	);
	assert_eq!(
		unsafe { oakengine_encoding_filename_contains_digit_placeholder(c"/tmp/out.png".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_image_sequence_digit_count(c"/tmp/out_[#####].png".as_ptr()) },
		5
	);
	assert_eq!(
		unsafe { oakengine_encoding_image_sequence_digit_count(c"/tmp/out.png".as_ptr()) },
		0
	);
	// Placeholder + preceding separator are removed together.
	let len = unsafe {
		oakengine_encoding_filename_remove_digit_placeholder(
			c"/tmp/out_[#####].png".as_ptr(),
			buf.as_mut_ptr(),
			128,
		)
	};
	assert_eq!(len, 12); // "/tmp/out.png"
	assert_eq!(read_buf(&buf), "/tmp/out.png");
	let len = unsafe {
		oakengine_encoding_filename_remove_digit_placeholder(
			c"img_[#####].png".as_ptr(),
			buf.as_mut_ptr(),
			128,
		)
	};
	assert_eq!(len, 7); // "img.png" (the "_" separator goes with the placeholder)
	assert_eq!(read_buf(&buf), "img.png");
}

// ---------------------------------------------------------------------------
// 2. Metadata — illegal-input robustness
// ---------------------------------------------------------------------------

/// Plugins may pass anything: out-of-range formats/codecs/indices, garbage
/// enums, NULL strings and degenerate buffer sizes. Every case must yield a
/// clean negative code (or the documented 0/fallback), never a crash.
#[test]
fn metadata_illegal_inputs() {
	let mut buf = [0 as c_char; 128];

	// Out-of-range / garbage format -> K_INVALID on the two-stage getters.
	assert_eq!(
		unsafe { oakengine_encoding_format_name(-1, buf.as_mut_ptr(), 128) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_name(15, buf.as_mut_ptr(), 128) },
		K_INVALID
	); // Count is not a format
	assert_eq!(
		unsafe { oakengine_encoding_format_name(99, buf.as_mut_ptr(), 128) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_extension(-5, buf.as_mut_ptr(), 128) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_video_codec_count(-1) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_video_codec_count(99) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_audio_codec_count(-2) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_subtitle_codec_count(42) },
		K_INVALID
	);

	// Out-of-range indices -> K_NOT_FOUND (valid format checked first).
	assert_eq!(
		unsafe { oakengine_encoding_format_video_codec_at(2, -1) },
		K_NOT_FOUND
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_video_codec_at(2, 3) },
		K_NOT_FOUND
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_audio_codec_at(7, 1) },
		K_NOT_FOUND
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_subtitle_codec_at(13, 1) },
		K_NOT_FOUND
	);
	// Invalid format wins over the index check.
	assert_eq!(
		unsafe { oakengine_encoding_format_video_codec_at(99, 0) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_audio_codec_at(-1, 0) },
		K_INVALID
	);
	// WAV (7) is a valid format with an empty subtitle-codec list: the
	// out-of-range index yields K_NOT_FOUND; an invalid format wins over
	// the index check.
	assert_eq!(
		unsafe { oakengine_encoding_format_subtitle_codec_at(7, 0) },
		K_NOT_FOUND
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_subtitle_codec_at(99, 0) },
		K_INVALID
	);

	// Garbage codec values: name -> K_INVALID, flags -> documented 0.
	assert_eq!(
		unsafe { oakengine_encoding_codec_name(-1, buf.as_mut_ptr(), 128) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_codec_name(99, buf.as_mut_ptr(), 128) },
		K_INVALID
	);
	assert_eq!(unsafe { oakengine_encoding_codec_is_still_image(99) }, 0);
	assert_eq!(unsafe { oakengine_encoding_codec_is_still_image(-3) }, 0);
	assert_eq!(unsafe { oakengine_encoding_codec_is_lossless(99) }, 0);
	assert_eq!(unsafe { oakengine_encoding_codec_is_lossless(-3) }, 0);

	// Pixel/sample format queries: garbage format or codec -> K_INVALID.
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_count(-1, 1) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_count(2, 99) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_count(15, 1) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_at(-1, 1, 0, buf.as_mut_ptr(), 128) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_at(2, 99, 0, buf.as_mut_ptr(), 128) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_at(2, 1, -1, buf.as_mut_ptr(), 128) },
		K_NOT_FOUND
	);
	// pix_fmt_index: NULL / empty / unknown / garbage codec -> 0.
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_index(1, std::ptr::null()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_index(99, c"yuv420p".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_pix_fmt_index(1, c"not-a-real-fmt".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_sample_format_count(-1, 13) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_sample_format_count(7, 99) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_sample_format_at(-1, 13, 0) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_sample_format_at(7, 99, 0) },
		K_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_sample_format_at(7, 13, -1) },
		K_NOT_FOUND
	);
	assert_eq!(
		unsafe { oakengine_encoding_sample_format_at(7, 13, 6) },
		K_NOT_FOUND
	); // count is 6

	// NULL / degenerate buffers on two-stage getters: length-only, never a
	// crash. The return value stays the string length.
	assert_eq!(
		unsafe { oakengine_encoding_format_name(1, std::ptr::null_mut(), 0) },
		14
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_name(1, std::ptr::null_mut(), -1) },
		14
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_extension(1, buf.as_mut_ptr(), 0) },
		3
	);
	assert_eq!(
		unsafe { oakengine_encoding_format_extension(1, buf.as_mut_ptr(), -4) },
		3
	);
	// Truncation: buf_size = 4 writes 3 chars + NUL, required size unchanged.
	assert_eq!(
		unsafe { oakengine_encoding_format_name(1, buf.as_mut_ptr(), 4) },
		14
	);
	assert_eq!(read_buf(&buf), "Mat");
	assert_eq!(
		unsafe { oakengine_encoding_codec_name(1, std::ptr::null_mut(), 0) },
		5
	);

	// NULL filename helpers: documented fallbacks, no crash.
	assert_eq!(
		unsafe { oakengine_encoding_filename_contains_digit_placeholder(std::ptr::null()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_image_sequence_digit_count(std::ptr::null()) },
		0
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_filename_remove_digit_placeholder(
				std::ptr::null(),
				buf.as_mut_ptr(),
				128,
			)
		},
		K_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_filename_remove_digit_placeholder(
				c"img.png".as_ptr(),
				std::ptr::null_mut(),
				0,
			)
		},
		7
	); // "img.png" unchanged, length-only
}

// ---------------------------------------------------------------------------
// 3. Transform matrix (`oakengine_encoding_generate_matrix`)
// ---------------------------------------------------------------------------

/// Legal methods, garbage method and degenerate sizes; NULL output.
#[test]
fn generate_matrix_matrix() {
	let mut m: Matrix16 = [0.0; 16];

	// Stretch (1) is the identity.
	assert_eq!(
		unsafe { oakengine_encoding_generate_matrix(1, 1920, 1080, 1280, 720, m.as_mut_ptr()) },
		0
	);
	assert_matrix(&m, &identity16());

	// Fit (0): square source into a 2:1 destination scales x by 0.5.
	assert_eq!(
		unsafe { oakengine_encoding_generate_matrix(0, 1000, 1000, 2000, 1000, m.as_mut_ptr()) },
		0
	);
	assert_matrix(
		&m,
		&[
			0.5, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
		],
	);

	// Crop (2) on the same geometry scales y by 2.0.
	assert_eq!(
		unsafe { oakengine_encoding_generate_matrix(2, 1000, 1000, 2000, 1000, m.as_mut_ptr()) },
		0
	);
	assert_matrix(
		&m,
		&[
			1.0, 0.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
		],
	);

	// Same aspect ratio -> identity for every method.
	for method in 0..3 {
		assert_eq!(
			unsafe {
				oakengine_encoding_generate_matrix(method, 1920, 1080, 960, 540, m.as_mut_ptr())
			},
			0
		);
		assert_matrix(&m, &identity16());
	}

	// Garbage method -> mapped to Stretch -> identity, still OK.
	assert_eq!(
		unsafe { oakengine_encoding_generate_matrix(99, 1000, 1000, 2000, 1000, m.as_mut_ptr()) },
		0
	);
	assert_matrix(&m, &identity16());
	assert_eq!(
		unsafe { oakengine_encoding_generate_matrix(-7, 1000, 1000, 2000, 1000, m.as_mut_ptr()) },
		0
	);
	assert_matrix(&m, &identity16());

	// Degenerate sizes (zero / negative) -> identity, still OK.
	for (sw, sh, dw, dh) in [(0, 0, 100, 100), (100, 100, 0, 0), (-4, 10, 100, 100)] {
		assert_eq!(
			unsafe { oakengine_encoding_generate_matrix(0, sw, sh, dw, dh, m.as_mut_ptr()) },
			0
		);
		assert_matrix(&m, &identity16());
	}

	// NULL output -> facade E_INVALID.
	assert_eq!(
		unsafe { oakengine_encoding_generate_matrix(0, 1, 1, 1, 1, std::ptr::null_mut()) },
		E_INVALID
	);
}

// ---------------------------------------------------------------------------
// 4. Encoding-params handle — legal lifecycle round trip
// ---------------------------------------------------------------------------

/// Create, configure every field, read everything back, destroy. The
/// handle is per-call state, so the whole lifecycle runs in one test.
#[test]
fn params_handle_legal_round_trip() {
	let p = unsafe { oakengine_encoding_params_create() };
	assert!(!p.is_null());
	let mut buf = [0 as c_char; 64];

	// Fresh handle: no tracks enabled, format unset (-1), invalid.
	assert_eq!(unsafe { oakengine_encoding_params_is_valid(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_format(p) }, -1);
	assert_eq!(unsafe { oakengine_encoding_params_video_enabled(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_audio_enabled(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_subtitles_enabled(p) }, 0);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_are_sidecar(p) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_sidecar_format(p) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_subtitles_codec(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_video_threads(p) }, 0);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_is_image_sequence(p) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_scaling_method(p) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_video_bit_rate(p) }, 0);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_min_bit_rate(p) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_max_bit_rate(p) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_video_buffer_size(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_audio_bit_rate(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_has_custom_range(p) }, 0);

	// Format: set + read back; Matroska = 1.
	assert_eq!(unsafe { oakengine_encoding_params_set_format(p, 1) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_format(p) }, 1);

	// Filename round trip.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_filename(p, c"out.mkv".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_filename(p, buf.as_mut_ptr(), 64) },
		7
	);
	assert_eq!(read_buf(&buf), "out.mkv");

	// Video: enable with a real video-params POD, then read everything back.
	let mut vp: OakVideoParamsPod = unsafe { std::mem::zeroed() };
	assert_eq!(
		unsafe {
			oakengine::common::oakengine_video_params_make(
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
	let mut out: OakVideoParamsPod = unsafe { std::mem::zeroed() };
	assert_eq!(
		unsafe { oakengine_encoding_params_get_video_params(p, &mut out) },
		0
	);
	assert_eq!((out.width, out.height), (1920, 1080));
	assert_eq!((out.time_base_num, out.time_base_den), (1001, 30000));
	assert_eq!(out.format, 4);
	assert_eq!(out.interlacing, 0); // interlacing arg 0 in the make() call
	assert_eq!((out.pixel_aspect_num, out.pixel_aspect_den), (1, 1));

	// Video bit-rate family (i64 fields).
	unsafe { oakengine_encoding_params_set_video_bit_rate(p, 8_000_000) };
	unsafe { oakengine_encoding_params_set_video_min_bit_rate(p, 4_000_000) };
	unsafe { oakengine_encoding_params_set_video_max_bit_rate(p, 12_000_000) };
	unsafe { oakengine_encoding_params_set_video_buffer_size(p, 16_000_000) };
	assert_eq!(
		unsafe { oakengine_encoding_params_video_bit_rate(p) },
		8_000_000
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_min_bit_rate(p) },
		4_000_000
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_max_bit_rate(p) },
		12_000_000
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_buffer_size(p) },
		16_000_000
	);

	// Threads, encoded pixel format, image-sequence flag, scaling method.
	unsafe { oakengine_encoding_params_set_video_threads(p, 4) };
	assert_eq!(unsafe { oakengine_encoding_params_video_threads(p) }, 4);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_pix_fmt(p, c"yuv420p".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_pix_fmt(p, buf.as_mut_ptr(), 64) },
		7
	);
	assert_eq!(read_buf(&buf), "yuv420p");
	unsafe { oakengine_encoding_params_set_video_is_image_sequence(p, 1) };
	assert_eq!(
		unsafe { oakengine_encoding_params_video_is_image_sequence(p) },
		1
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_scaling_method(p, 2) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_scaling_method(p) },
		2
	);

	// Audio: enable + read back (get_audio_params before enabling is E_STATE,
	// asserted in the illegal test).
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_audio(p, 48000, 3, 0, 13) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_audio_enabled(p) }, 1);
	assert_eq!(unsafe { oakengine_encoding_params_audio_codec(p) }, 13);
	let (mut sr, mut layout, mut sf) = (0 as c_int, 0u64, 0 as c_int);
	assert_eq!(
		unsafe { oakengine_encoding_params_get_audio_params(p, &mut sr, &mut layout, &mut sf) },
		0
	);
	assert_eq!((sr, layout, sf), (48000, 3, 0));
	unsafe { oakengine_encoding_params_set_audio_bit_rate(p, 320_000) };
	assert_eq!(
		unsafe { oakengine_encoding_params_audio_bit_rate(p) },
		320_000
	);

	// Subtitles: plain and sidecar variants.
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_subtitles(p, 17) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_subtitles_enabled(p) }, 1);
	assert_eq!(unsafe { oakengine_encoding_params_subtitles_codec(p) }, 17);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_are_sidecar(p) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_sidecar_subtitles(p, 13, 17) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_subtitles_enabled(p) }, 1);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_are_sidecar(p) },
		1
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_sidecar_format(p) },
		13
	);
	assert_eq!(unsafe { oakengine_encoding_params_subtitles_codec(p) }, 17);

	// Color transform.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_color_transform(p, c"ACEScg".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_color_transform_output(p, buf.as_mut_ptr(), 64) },
		6
	);
	assert_eq!(read_buf(&buf), "ACEScg");

	// Export length.
	let (mut eln, mut eld) = (0 as c_int, 0 as c_int);
	assert_eq!(
		unsafe { oakengine_encoding_params_get_export_length(p, &mut eln, &mut eld) },
		0
	);
	assert_eq!((eln, eld), (0, 0)); // default
	unsafe { oakengine_encoding_params_set_export_length(p, 10, 1) };
	assert_eq!(
		unsafe { oakengine_encoding_params_get_export_length(p, &mut eln, &mut eld) },
		0
	);
	assert_eq!((eln, eld), (10, 1));

	// Custom range.
	let (mut inn, mut ind, mut outn, mut outd) = (0i64, 0i64, 0i64, 0i64);
	unsafe { oakengine_encoding_params_set_custom_range(p, 0, 1, 100, 1) };
	assert_eq!(unsafe { oakengine_encoding_params_has_custom_range(p) }, 1);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_get_custom_range(p, &mut inn, &mut ind, &mut outn, &mut outd)
		},
		0
	);
	assert_eq!((inn, ind, outn, outd), (0, 1, 100, 1));

	// Encoder-specific video options (facade-side map).
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_option(p, c"crf".as_ptr(), c"18".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_option(p, c"crf".as_ptr(), buf.as_mut_ptr(), 64) },
		2
	);
	assert_eq!(read_buf(&buf), "18");
	// A second value for the same key replaces the first.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_option(p, c"crf".as_ptr(), c"23".as_ptr()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_option(p, c"crf".as_ptr(), buf.as_mut_ptr(), 64) },
		2
	);
	assert_eq!(read_buf(&buf), "23");

	// Disable each track and watch is_valid flip back to 0.
	unsafe { oakengine_encoding_params_disable_video(p) };
	assert_eq!(unsafe { oakengine_encoding_params_video_enabled(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_is_valid(p) }, 1); // audio still on
	unsafe { oakengine_encoding_params_disable_audio(p) };
	assert_eq!(unsafe { oakengine_encoding_params_audio_enabled(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_is_valid(p) }, 1); // subtitles still on
	unsafe { oakengine_encoding_params_disable_subtitles(p) };
	assert_eq!(unsafe { oakengine_encoding_params_subtitles_enabled(p) }, 0);
	assert_eq!(unsafe { oakengine_encoding_params_is_valid(p) }, 0);

	unsafe { oakengine_encoding_params_destroy(p) };
}

// ---------------------------------------------------------------------------
// 5. Encoding-params handle — illegal-input robustness
// ---------------------------------------------------------------------------

/// NULL handles, NULL string arguments, out-of-range values, state errors
/// and garbage enums: clean negative codes or documented no-ops only.
#[test]
fn params_handle_illegal_inputs() {
	let p = unsafe { oakengine_encoding_params_create() };
	assert!(!p.is_null());
	let mut buf = [0 as c_char; 64];

	// NULL handle on every c_int-returning getter/setter -> facade E_INVALID.
	assert_eq!(
		unsafe { oakengine_encoding_params_is_valid(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_format(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_format(std::ptr::null_mut(), 1) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_filename(std::ptr::null(), buf.as_mut_ptr(), 64) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_filename(std::ptr::null_mut(), c"x.mkv".as_ptr()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_video(std::ptr::null_mut(), &vp_uninit(), 1) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_audio(std::ptr::null_mut(), 48000, 3, 0, 13) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_subtitles(std::ptr::null_mut(), 17) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_sidecar_subtitles(std::ptr::null_mut(), 13, 17) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_enabled(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_codec(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_get_video_params(std::ptr::null(), &mut vp_uninit()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_audio_enabled(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_audio_codec(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_get_audio_params(
				std::ptr::null(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
			)
		},
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_enabled(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_are_sidecar(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_sidecar_format(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_subtitles_codec(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_bit_rate(std::ptr::null()) },
		E_INVALID as i64
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_min_bit_rate(std::ptr::null()) },
		E_INVALID as i64
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_max_bit_rate(std::ptr::null()) },
		E_INVALID as i64
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_buffer_size(std::ptr::null()) },
		E_INVALID as i64
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_audio_bit_rate(std::ptr::null()) },
		E_INVALID as i64
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_threads(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_pix_fmt(std::ptr::null(), buf.as_mut_ptr(), 64) },
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_set_video_pix_fmt(std::ptr::null_mut(), c"yuv420p".as_ptr())
		},
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_is_image_sequence(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_color_transform_output(std::ptr::null(), buf.as_mut_ptr(), 64)
		},
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_set_color_transform(std::ptr::null_mut(), c"ACEScg".as_ptr())
		},
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_get_export_length(
				std::ptr::null(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
			)
		},
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_has_custom_range(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_get_custom_range(
				std::ptr::null(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
			)
		},
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_scaling_method(std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_scaling_method(std::ptr::null_mut(), 0) },
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_set_video_option(
				std::ptr::null_mut(),
				c"crf".as_ptr(),
				c"18".as_ptr(),
			)
		},
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_video_option(
				std::ptr::null(),
				c"crf".as_ptr(),
				buf.as_mut_ptr(),
				64,
			)
		},
		E_INVALID
	);

	// NULL string arguments.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_filename(p, std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_pix_fmt(p, std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_option(p, std::ptr::null(), c"18".as_ptr()) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_option(p, c"crf".as_ptr(), std::ptr::null()) },
		E_INVALID
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_video_option(p, std::ptr::null(), buf.as_mut_ptr(), 64)
		},
		E_INVALID
	);
	// NULL video-params POD on enable_video.
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_video(p, std::ptr::null(), 1) },
		E_INVALID
	);
	// set_color_transform tolerates NULL (writes the empty string).
	assert_eq!(
		unsafe { oakengine_encoding_params_set_color_transform(p, std::ptr::null()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_color_transform_output(p, buf.as_mut_ptr(), 64) },
		0
	);
	assert_eq!(read_buf(&buf), "");

	// Out-of-range / garbage values.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_format(p, -1) },
		E_INVALID
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_set_format(p, 15) },
		E_INVALID
	); // count
	assert_eq!(
		unsafe { oakengine_encoding_params_set_format(p, 9999) },
		E_INVALID
	);
	// Missing video option key -> E_NOT_FOUND.
	assert_eq!(
		unsafe {
			oakengine_encoding_params_video_option(p, c"missing".as_ptr(), buf.as_mut_ptr(), 64)
		},
		E_NOT_FOUND
	);

	// State errors: disabled tracks make the getters return E_STATE.
	assert_eq!(
		unsafe { oakengine_encoding_params_get_video_params(p, &mut vp_uninit()) },
		E_STATE
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_get_video_params(p, std::ptr::null_mut()) },
		E_STATE
	); // disabled state checked before NULL out
	assert_eq!(
		unsafe {
			oakengine_encoding_params_get_audio_params(
				p,
				std::ptr::null_mut(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
			)
		},
		E_STATE
	);
	// Unset custom range -> E_NOT_FOUND.
	assert_eq!(
		unsafe {
			oakengine_encoding_params_get_custom_range(
				p,
				std::ptr::null_mut(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
			)
		},
		E_NOT_FOUND
	);

	// Enabled video with a NULL out -> E_INVALID (state now OK).
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_video(p, &vp_uninit(), 1) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_get_video_params(p, std::ptr::null_mut()) },
		E_INVALID
	);
	// Garbage enums are accepted verbatim (no validation in the facade):
	// a codec id of 9999 round-trips.
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_video(p, &vp_uninit(), 9999) },
		0
	);
	assert_eq!(unsafe { oakengine_encoding_params_video_codec(p) }, 9999);
	// Audio with a zero rate / empty layout / garbage format/codec: accepted.
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_audio(p, 0, 0, -1, 9999) },
		0
	);
	let (mut sr, mut layout, mut sf) = (0 as c_int, 0u64, 0 as c_int);
	assert_eq!(
		unsafe { oakengine_encoding_params_get_audio_params(p, &mut sr, &mut layout, &mut sf) },
		0
	);
	assert_eq!((sr, layout, sf), (0, 0, -1));
	// Garbage scaling method round-trips too.
	assert_eq!(
		unsafe { oakengine_encoding_params_set_video_scaling_method(p, 99) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_scaling_method(p) },
		99
	);

	// NULL-handle void setters are no-ops (never crash).
	unsafe { oakengine_encoding_params_disable_video(std::ptr::null_mut()) };
	unsafe { oakengine_encoding_params_disable_audio(std::ptr::null_mut()) };
	unsafe { oakengine_encoding_params_disable_subtitles(std::ptr::null_mut()) };
	unsafe { oakengine_encoding_params_set_video_bit_rate(std::ptr::null_mut(), 1) };
	unsafe { oakengine_encoding_params_set_video_min_bit_rate(std::ptr::null_mut(), 1) };
	unsafe { oakengine_encoding_params_set_video_max_bit_rate(std::ptr::null_mut(), 1) };
	unsafe { oakengine_encoding_params_set_video_buffer_size(std::ptr::null_mut(), 1) };
	unsafe { oakengine_encoding_params_set_audio_bit_rate(std::ptr::null_mut(), 1) };
	unsafe { oakengine_encoding_params_set_video_threads(std::ptr::null_mut(), 4) };
	unsafe { oakengine_encoding_params_set_video_is_image_sequence(std::ptr::null_mut(), 1) };
	unsafe { oakengine_encoding_params_set_export_length(std::ptr::null_mut(), 10, 1) };
	unsafe { oakengine_encoding_params_set_custom_range(std::ptr::null_mut(), 0, 1, 100, 1) };
	// Length-only queries tolerate a NULL / zero-sized buffer.
	assert_eq!(
		unsafe { oakengine_encoding_params_filename(p, std::ptr::null_mut(), 0) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_video_pix_fmt(p, std::ptr::null_mut(), -1) },
		0
	);
	assert_eq!(
		unsafe {
			oakengine_encoding_params_video_option(p, c"crf".as_ptr(), std::ptr::null_mut(), 0)
		},
		E_NOT_FOUND
	); // key unset on this handle

	unsafe { oakengine_encoding_params_destroy(p) };
}

/// Fresh (all-zero) video-params POD used for validation-negative calls.
fn vp_uninit() -> OakVideoParamsPod {
	unsafe { std::mem::zeroed() }
}

// ---------------------------------------------------------------------------
// 6. Encoding-params handle — destroy contracts
// ---------------------------------------------------------------------------

/// `oakengine_encoding_params_destroy`: NULL is a no-op (repeatedly), and a
/// live handle frees cleanly. The family keeps no debug alive counter (the
/// handle is a facade-owned raw box, not a refcounted oakcodec handle), so
/// the baseline is verified behaviorally. Double-freeing a live pointer is
/// use-after-free by design (the engine header transfers ownership) and is
/// deliberately not invoked.
#[test]
fn params_destroy_contracts() {
	unsafe { oakengine_encoding_params_destroy(std::ptr::null_mut()) };
	unsafe { oakengine_encoding_params_destroy(std::ptr::null_mut()) };

	let p = unsafe { oakengine_encoding_params_create() };
	assert!(!p.is_null());
	// The handle still works right up to the destroy.
	assert_eq!(unsafe { oakengine_encoding_params_set_format(p, 1) }, 0);
	unsafe { oakengine_encoding_params_destroy(p) };

	// NULL remains a no-op after a real destroy.
	unsafe { oakengine_encoding_params_destroy(std::ptr::null_mut()) };
}

// ---------------------------------------------------------------------------
// 7. Deferred / not-backed entry points
// ---------------------------------------------------------------------------

/// Preset path/count/name, params load/save, export render and the
/// sequence-bound last-used stubs are documented as not backed: they return
/// the fixed contract value (E_FAILED / 0 / NULL / no-op) regardless of the
/// arguments, so every argument combination is safe.
#[test]
fn deferred_stubs_contract() {
	let mut buf = [0 as c_char; 64];

	assert_eq!(
		unsafe { oakengine_encoding_preset_path(buf.as_mut_ptr(), 64) },
		E_FAILED
	);
	assert_eq!(
		unsafe { oakengine_encoding_preset_path(std::ptr::null_mut(), 0) },
		E_FAILED
	);
	assert_eq!(unsafe { oakengine_encoding_preset_count() }, 0);
	assert_eq!(
		unsafe { oakengine_encoding_preset_name(0, buf.as_mut_ptr(), 64) },
		E_FAILED
	);
	assert_eq!(
		unsafe { oakengine_encoding_preset_name(99, std::ptr::null_mut(), 0) },
		E_FAILED
	);

	let p = unsafe { oakengine_encoding_params_create() };
	assert!(!p.is_null());
	assert_eq!(
		unsafe { oakengine_encoding_params_load_file(p, c"preset.oep".as_ptr()) },
		E_FAILED
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_load_file(std::ptr::null_mut(), c"p.oep".as_ptr()) },
		E_FAILED
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_save_file(p, c"preset.oep".as_ptr()) },
		E_FAILED
	);
	assert_eq!(
		unsafe { oakengine_encoding_params_save_file(std::ptr::null_mut(), std::ptr::null()) },
		E_FAILED
	);
	assert_eq!(
		unsafe { oakengine_export_render_with_params(std::ptr::null_mut(), p) },
		E_FAILED
	);
	assert_eq!(
		unsafe { oakengine_export_render_with_params(std::ptr::null_mut(), std::ptr::null()) },
		E_FAILED
	);

	assert!(unsafe { oakengine_encoding_params_get_last_used(std::ptr::null_mut()) }.is_null());
	assert!(unsafe { oakengine_encoding_params_get_last_used(std::ptr::null_mut()) }.is_null());
	unsafe { oakengine_encoding_params_set_last_used(std::ptr::null_mut(), p) };
	unsafe { oakengine_encoding_params_set_last_used(std::ptr::null_mut(), std::ptr::null()) };

	unsafe { oakengine_encoding_params_destroy(p) };
}

// ---------------------------------------------------------------------------
// 8. Audio recording (`oakengine_encoding_start_audio_recording`)
// ---------------------------------------------------------------------------

/// Recording needs the process-wide AudioManager singleton (oakaudio). The
/// whole lifecycle runs in this one test so the singleton is never shared
/// with another test. With no manager the facade reports E_STATE; with the
/// real manager and no selected input device the oakaudio module reports
/// E_FAILED (-60003, "no input device") — the real end-to-end path through
/// the facade, the oakaudio manager, and the error-string plumbing.
#[test]
fn start_audio_recording_manager_paths() {
	let p = unsafe { oakengine_encoding_params_create() };
	assert!(!p.is_null());
	let mut err = [0 as c_char; 128];

	// NULL params -> facade E_INVALID.
	assert_eq!(
		unsafe {
			oakengine_encoding_start_audio_recording(std::ptr::null(), err.as_mut_ptr(), 128)
		},
		E_INVALID
	);

	// No manager singleton -> facade E_STATE.
	assert_eq!(
		unsafe { oakengine_encoding_start_audio_recording(p, err.as_mut_ptr(), 128) },
		E_STATE
	);

	// Create the real singleton through the audio facade.
	assert_eq!(
		unsafe { oakengine::audio::oakengine_audio_create_instance() },
		0
	);

	// Audio disabled -> oakaudio rejects with E_INVALID and writes the
	// diagnostic string.
	assert_eq!(
		unsafe { oakengine_encoding_start_audio_recording(p, err.as_mut_ptr(), 128) },
		A_INVALID
	);
	assert!(!read_buf(&err).is_empty());

	// Audio enabled, but no input device selected -> E_FAILED + message.
	assert_eq!(
		unsafe { oakengine_encoding_params_enable_audio(p, 48000, 3, 0, 13) },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_start_audio_recording(p, err.as_mut_ptr(), 128) },
		A_FAILED
	);
	assert!(!read_buf(&err).is_empty());

	// NULL error buffer is tolerated by the failure paths (length-only
	// reporting is not used here; the buffer is simply optional).
	assert_eq!(
		unsafe { oakengine_encoding_start_audio_recording(p, std::ptr::null_mut(), 0) },
		A_FAILED
	);

	// Tear the singleton down: the E_STATE contract returns.
	assert_eq!(
		unsafe { oakengine::audio::oakengine_audio_destroy_instance() },
		0
	);
	assert_eq!(
		unsafe { oakengine_encoding_start_audio_recording(p, err.as_mut_ptr(), 128) },
		E_STATE
	);

	unsafe { oakengine_encoding_params_destroy(p) };
}
