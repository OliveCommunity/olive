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

//! `include/codec/format.h` exports.
//!
//! Complete inventory: format_count / format_name / format_extension /
//! format_{video,audio,subtitle}_codec_{count,at} / codec_name /
//! codec_is_still_image / codec_is_lossless / pix_fmt_{count,at,index} /
//! sample_format_{count,at} / filename_contains_digit_placeholder /
//! image_sequence_digit_count / filename_remove_digit_placeholder.
//!
//! # CPP-PARITY
//! The C++ `c_api/format.cpp` mirrors the facade (oakengine/encoding.h)
//! against the `olive::ExportFormat` / `olive::ExportCodec` / `olive::Encoder`
//! statics. The Rust tables live in [`crate::exportformat`] /
//! [`crate::exportcodec`]; the encoder pixel-format query is bridge-dependent
//! on the C++ side and returns empty here (see
//! `Format::get_pixel_formats_for_codec`), so `oakcodec_encoding_pix_fmt_*`
//! report 0/`E_NOT_FOUND`/0 (the preferred-format fallback) like the C++
//! base `Encoder` default. The filename helpers mirror the `Encoder`
//! statics in [`crate::encoder`].

use std::ffi::{c_char, c_int};

use crate::error::{OAKCODEC_E_INVALID, OAKCODEC_E_NOT_FOUND};
use crate::exportcodec::Codec;
use crate::exportformat::Format;
use crate::handle;

/// `oakcodec_encoding_format_count` — `ExportFormat::k_format_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_count() -> c_int {
	handle::guard_raw(|| Format::Count as c_int)
}

/// `oakcodec_encoding_format_name` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_name(
	format: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| match Format::from_i32(format) {
		Some(f) => super::string_out(&Format::get_name(f), buf, buf_size),
		None => OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_encoding_format_extension` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_extension(
	format: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| match Format::from_i32(format) {
		Some(f) => super::string_out(&Format::get_extension(f), buf, buf_size),
		None => OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_encoding_format_video_codec_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_video_codec_count(format: c_int) -> c_int {
	handle::guard_raw(|| match Format::from_i32(format) {
		Some(f) => Format::get_video_codecs(f).len() as c_int,
		None => OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_encoding_format_video_codec_at`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_video_codec_at(
	format: c_int,
	index: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let f = match Format::from_i32(format) {
			Some(f) => f,
			None => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_video_codecs(f);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		list[index as usize] as c_int
	})
}

/// `oakcodec_encoding_format_audio_codec_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_audio_codec_count(format: c_int) -> c_int {
	handle::guard_raw(|| match Format::from_i32(format) {
		Some(f) => Format::get_audio_codecs(f).len() as c_int,
		None => OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_encoding_format_audio_codec_at`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_audio_codec_at(
	format: c_int,
	index: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let f = match Format::from_i32(format) {
			Some(f) => f,
			None => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_audio_codecs(f);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		list[index as usize] as c_int
	})
}

/// `oakcodec_encoding_format_subtitle_codec_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_subtitle_codec_count(format: c_int) -> c_int {
	handle::guard_raw(|| match Format::from_i32(format) {
		Some(f) => Format::get_subtitle_codecs(f).len() as c_int,
		None => OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_encoding_format_subtitle_codec_at`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_format_subtitle_codec_at(
	format: c_int,
	index: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let f = match Format::from_i32(format) {
			Some(f) => f,
			None => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_subtitle_codecs(f);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		list[index as usize] as c_int
	})
}

/// `oakcodec_encoding_codec_name` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_codec_name(
	codec: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| match Codec::from_i32(codec) {
		Some(c) => super::string_out(&Codec::get_codec_name(c), buf, buf_size),
		None => OAKCODEC_E_INVALID,
	})
}

/// `oakcodec_encoding_codec_is_still_image` (0 for an invalid codec).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_codec_is_still_image(codec: c_int) -> c_int {
	handle::guard_raw(|| match Codec::from_i32(codec) {
		Some(c) => Codec::is_codec_a_still_image(c) as c_int,
		None => 0,
	})
}

/// `oakcodec_encoding_codec_is_lossless` (0 for an invalid codec).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_codec_is_lossless(codec: c_int) -> c_int {
	handle::guard_raw(|| match Codec::from_i32(codec) {
		Some(c) => Codec::is_codec_lossless(c) as c_int,
		None => 0,
	})
}

/// `oakcodec_encoding_pix_fmt_count`.
///
/// # CPP-PARITY
/// The C++ side instantiates the format's encoder and asks it for the codec's
/// pixel formats; the Rust table is empty (see [`Format::get_pixel_formats_for_codec`]),
/// so the count is 0 — the same as the C++ base `Encoder` default and the
/// C++ result for encoder-less codecs.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_pix_fmt_count(format: c_int, codec: c_int) -> c_int {
	handle::guard_raw(|| {
		let (f, c) = match (Format::from_i32(format), Codec::from_i32(codec)) {
			(Some(f), Some(c)) => (f, c),
			_ => return OAKCODEC_E_INVALID,
		};
		Format::get_pixel_formats_for_codec(f, c).len() as c_int
	})
}

/// `oakcodec_encoding_pix_fmt_at` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_pix_fmt_at(
	format: c_int,
	codec: c_int,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let (f, c) = match (Format::from_i32(format), Codec::from_i32(codec)) {
			(Some(f), Some(c)) => (f, c),
			_ => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_pixel_formats_for_codec(f, c);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		// Interim: the Rust list carries no names yet, so this arm is
		// unreachable while the list is empty (C++ queries the FFmpeg bridge).
		super::string_out(&list[index as usize].to_string(), buf, buf_size)
	})
}

/// `oakcodec_encoding_pix_fmt_index` — index of `pix_fmt` in `codec`'s
/// supported pixel formats; 0 (the preferred format) for an invalid codec,
/// a NULL/empty `pix_fmt`, or when not found.
///
/// # CPP-PARITY
/// The C++ side searches the FFmpeg encoder's list. The Rust table is empty,
/// so every lookup falls back to 0 — the documented behavior for "absent".
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_pix_fmt_index(
	codec: c_int,
	pix_fmt: *const c_char,
) -> c_int {
	handle::guard_raw(|| {
		if Codec::from_i32(codec).is_none() {
			return 0;
		}
		match crate::ffi::c_str(pix_fmt) {
			Some(s) if !s.is_empty() => {
				// Interim: empty table (see module doc) -> preferred index 0.
				let _ = s;
				0
			}
			_ => 0,
		}
	})
}

/// `oakcodec_encoding_sample_format_count`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_sample_format_count(
	format: c_int,
	codec: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let (f, c) = match (Format::from_i32(format), Codec::from_i32(codec)) {
			(Some(f), Some(c)) => (f, c),
			_ => return OAKCODEC_E_INVALID,
		};
		Format::get_sample_formats_for_codec(f, c).len() as c_int
	})
}

/// `oakcodec_encoding_sample_format_at` — an
/// `olive::core::SampleFormat::Format` value.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_sample_format_at(
	format: c_int,
	codec: c_int,
	index: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let (f, c) = match (Format::from_i32(format), Codec::from_i32(codec)) {
			(Some(f), Some(c)) => (f, c),
			_ => return OAKCODEC_E_INVALID,
		};
		let list = Format::get_sample_formats_for_codec(f, c);
		if index < 0 || index as usize >= list.len() {
			return OAKCODEC_E_NOT_FOUND;
		}
		list[index as usize] as c_int
	})
}

/// `oakcodec_encoding_filename_contains_digit_placeholder` (0 for NULL).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_filename_contains_digit_placeholder(
	filename: *const c_char,
) -> c_int {
	handle::guard_raw(|| match crate::ffi::c_str(filename) {
		Some(f) => crate::encoder::filename_contains_digit_placeholder(&f) as c_int,
		None => 0,
	})
}

/// `oakcodec_encoding_image_sequence_digit_count` (0 for NULL).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_image_sequence_digit_count(
	filename: *const c_char,
) -> c_int {
	handle::guard_raw(|| match crate::ffi::c_str(filename) {
		Some(f) => crate::encoder::image_sequence_placeholder_digit_count(&f),
		None => 0,
	})
}

/// `oakcodec_encoding_filename_remove_digit_placeholder` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_encoding_filename_remove_digit_placeholder(
	filename: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| match crate::ffi::c_str(filename) {
		Some(f) => super::string_out(
			&crate::encoder::filename_remove_digit_placeholder(&f),
			buf,
			buf_size,
		),
		None => OAKCODEC_E_INVALID,
	})
}

#[cfg(test)]
mod tests {
	use super::*;

	fn cstr(s: &str) -> std::ffi::CString {
		std::ffi::CString::new(s).unwrap()
	}

	#[test]
	fn format_metadata_exports() {
		let _g = crate::ffi::lock_tests();
		let mut buf = [0i8; 64];

		// Count matches the 15-entry table (0..=14, Count = 15).
		assert_eq!(unsafe { oakcodec_encoding_format_count() }, 15);

		// Matroska (1): "Matroska Video" / "mkv".
		let rc = unsafe { oakcodec_encoding_format_name(1, buf.as_mut_ptr(), 64) };
		assert_eq!(rc, 15); // "Matroska Video" (14) + NUL
		assert_eq!(
			crate::ffi::c_str(buf.as_ptr()).as_deref(),
			Some("Matroska Video")
		);
		let rc = unsafe { oakcodec_encoding_format_extension(1, buf.as_mut_ptr(), 64) };
		assert_eq!(rc, 4); // "mkv" + NUL
		assert_eq!(crate::ffi::c_str(buf.as_ptr()).as_deref(), Some("mkv"));

		// Truncation rule on a two-stage getter.
		let rc = unsafe { oakcodec_encoding_format_name(1, buf.as_mut_ptr(), 4) };
		assert_eq!(rc, 15); // required size unchanged
		assert_eq!(crate::ffi::c_str(buf.as_ptr()).as_deref(), Some("Mat"));

		// Invalid format -> E_INVALID.
		assert_eq!(
			unsafe { oakcodec_encoding_format_name(-1, buf.as_mut_ptr(), 64) },
			OAKCODEC_E_INVALID
		);
		assert_eq!(
			unsafe { oakcodec_encoding_format_extension(15, buf.as_mut_ptr(), 64) },
			OAKCODEC_E_INVALID
		); // Count is not a real format
		assert_eq!(
			unsafe { oakcodec_encoding_format_extension(99, buf.as_mut_ptr(), 64) },
			OAKCODEC_E_INVALID
		);
	}

	#[test]
	fn format_codec_lists_exports() {
		let _g = crate::ffi::lock_tests();
		// MPEG-4 video (2) carries H.264/H.264RGB/H.265.
		assert_eq!(unsafe { oakcodec_encoding_format_video_codec_count(2) }, 3);
		assert_eq!(
			unsafe { oakcodec_encoding_format_video_codec_at(2, 0) },
			1 // H.264
		);
		// WAV (7) has no video codecs but PCM (13) audio.
		assert_eq!(unsafe { oakcodec_encoding_format_video_codec_count(7) }, 0);
		assert_eq!(unsafe { oakcodec_encoding_format_audio_codec_count(7) }, 1);
		assert_eq!(
			unsafe { oakcodec_encoding_format_audio_codec_at(7, 0) },
			13 // PCM
		);
		// SRT (13): subtitle-only, with the SRT (17) codec.
		assert_eq!(unsafe { oakcodec_encoding_format_audio_codec_count(13) }, 0);
		assert_eq!(
			unsafe { oakcodec_encoding_format_subtitle_codec_count(13) },
			1
		);
		assert_eq!(
			unsafe { oakcodec_encoding_format_subtitle_codec_at(13, 0) },
			17 // SRT
		);

		// Failure paths.
		assert_eq!(
			unsafe { oakcodec_encoding_format_video_codec_count(-1) },
			OAKCODEC_E_INVALID
		);
		assert_eq!(
			unsafe { oakcodec_encoding_format_video_codec_at(2, -1) },
			OAKCODEC_E_NOT_FOUND
		);
		assert_eq!(
			unsafe { oakcodec_encoding_format_video_codec_at(2, 3) },
			OAKCODEC_E_NOT_FOUND
		);
		assert_eq!(
			unsafe { oakcodec_encoding_format_audio_codec_at(7, 1) },
			OAKCODEC_E_NOT_FOUND
		);
		assert_eq!(
			unsafe { oakcodec_encoding_format_subtitle_codec_at(13, 1) },
			OAKCODEC_E_NOT_FOUND
		);
	}

	#[test]
	fn codec_metadata_exports() {
		let _g = crate::ffi::lock_tests();
		let mut buf = [0i8; 64];

		let rc = unsafe { oakcodec_encoding_codec_name(1, buf.as_mut_ptr(), 64) };
		assert_eq!(rc, 6); // "H.264" (5) + NUL
		assert_eq!(crate::ffi::c_str(buf.as_ptr()).as_deref(), Some("H.264"));
		assert_eq!(
			unsafe { oakcodec_encoding_codec_name(-1, buf.as_mut_ptr(), 64) },
			OAKCODEC_E_INVALID
		);

		// Still images: PNG (5) yes, H.264 (1) no.
		assert_eq!(unsafe { oakcodec_encoding_codec_is_still_image(5) }, 1);
		assert_eq!(unsafe { oakcodec_encoding_codec_is_still_image(1) }, 0);
		// Lossless: PCM (13) yes, AAC (12) no.
		assert_eq!(unsafe { oakcodec_encoding_codec_is_lossless(13) }, 1);
		assert_eq!(unsafe { oakcodec_encoding_codec_is_lossless(12) }, 0);
		// Invalid codec -> 0 (not an error) for both flags.
		assert_eq!(unsafe { oakcodec_encoding_codec_is_still_image(99) }, 0);
		assert_eq!(unsafe { oakcodec_encoding_codec_is_lossless(99) }, 0);
	}

	#[test]
	fn pixel_and_sample_format_exports() {
		let _g = crate::ffi::lock_tests();
		let mut buf = [0i8; 64];

		// Bad arguments -> E_INVALID.
		assert_eq!(
			unsafe { oakcodec_encoding_pix_fmt_count(-1, 1) },
			OAKCODEC_E_INVALID
		);
		assert_eq!(
			unsafe { oakcodec_encoding_pix_fmt_count(2, 99) },
			OAKCODEC_E_INVALID
		);
		// Interim: the Rust pixel-format table is empty, so the count is 0
		// and any index is E_NOT_FOUND (see the module doc).
		assert_eq!(unsafe { oakcodec_encoding_pix_fmt_count(2, 1) }, 0);
		assert_eq!(
			unsafe { oakcodec_encoding_pix_fmt_at(-1, 1, 0, buf.as_mut_ptr(), 64) },
			OAKCODEC_E_INVALID
		);
		assert_eq!(
			unsafe { oakcodec_encoding_pix_fmt_at(2, 1, 0, buf.as_mut_ptr(), 64) },
			OAKCODEC_E_NOT_FOUND
		);
		// pix_fmt_index: absent/empty/NULL/invalid codec all yield 0.
		assert_eq!(
			unsafe { oakcodec_encoding_pix_fmt_index(1, cstr("yuv420p").as_ptr()) },
			0
		);
		assert_eq!(
			unsafe { oakcodec_encoding_pix_fmt_index(1, std::ptr::null()) },
			0
		);
		assert_eq!(
			unsafe { oakcodec_encoding_pix_fmt_index(99, cstr("yuv420p").as_ptr()) },
			0
		);

		// PCM (13) in WAV (7) exposes its native sample formats.
		assert_eq!(unsafe { oakcodec_encoding_sample_format_count(7, 13) }, 6);
		// f32 packed = 10 (oakcore SampleFormat values match the C++).
		assert_eq!(unsafe { oakcodec_encoding_sample_format_at(7, 13, 4) }, 10);
		// Out-of-range index -> E_NOT_FOUND; bad args -> E_INVALID.
		assert_eq!(
			unsafe { oakcodec_encoding_sample_format_at(7, 13, 6) },
			OAKCODEC_E_NOT_FOUND
		);
		assert_eq!(
			unsafe { oakcodec_encoding_sample_format_at(-1, 13, 0) },
			OAKCODEC_E_INVALID
		);
		// Non-PCM codecs query the bridge on the C++ side; empty here.
		assert_eq!(unsafe { oakcodec_encoding_sample_format_count(2, 12) }, 0);
	}

	#[test]
	fn filename_helper_exports() {
		let _g = crate::ffi::lock_tests();
		let mut buf = [0i8; 128];

		assert_eq!(
			unsafe {
				oakcodec_encoding_filename_contains_digit_placeholder(
					cstr("/tmp/out_[#####].png").as_ptr(),
				)
			},
			1
		);
		assert_eq!(
			unsafe {
				oakcodec_encoding_filename_contains_digit_placeholder(cstr("/tmp/out.png").as_ptr())
			},
			0
		);
		assert_eq!(
			unsafe { oakcodec_encoding_filename_contains_digit_placeholder(std::ptr::null()) },
			0
		);

		assert_eq!(
			unsafe {
				oakcodec_encoding_image_sequence_digit_count(cstr("/tmp/out_[#####].png").as_ptr())
			},
			5
		);
		assert_eq!(
			unsafe { oakcodec_encoding_image_sequence_digit_count(cstr("/tmp/out.png").as_ptr()) },
			0
		);
		assert_eq!(
			unsafe { oakcodec_encoding_image_sequence_digit_count(std::ptr::null()) },
			0
		);

		let rc = unsafe {
			oakcodec_encoding_filename_remove_digit_placeholder(
				cstr("/tmp/out_[#####].png").as_ptr(),
				buf.as_mut_ptr(),
				128,
			)
		};
		assert_eq!(rc, 13); // "/tmp/out.png" (12) + NUL
		assert_eq!(
			crate::ffi::c_str(buf.as_ptr()).as_deref(),
			Some("/tmp/out.png")
		);
		assert_eq!(
			unsafe {
				oakcodec_encoding_filename_remove_digit_placeholder(
					std::ptr::null(),
					buf.as_mut_ptr(),
					128,
				)
			},
			OAKCODEC_E_INVALID
		);
	}
}
