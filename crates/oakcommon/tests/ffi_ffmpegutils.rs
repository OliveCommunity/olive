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

//! Integration tests for the C ABI surface of `oakcommon::ffi::ffmpegutils`,
//! which implements `include/common/ffmpegutils.h` (oracle:
//! `src/common/src/ffmpegutils.cpp`).
//!
//! MUST be run with `--features test-stubs`: unlike every other ffi module,
//! `oakcommon_ffmpegutils_get_compatible_bridge_pixel_format` reaches the
//! real `fb_find_best_pix_fmt_of_list` symbol (ffmpeg_bridge C ABI) in
//! non-test builds, and the integration-test binary cannot link it. The
//! `test-stubs` feature substitutes the in-crate stub (see
//! `src/ffmpegutils.rs::find_best_pix_fmt_of_list`), mirroring the
//! oakplugin/oaktimeline convention. `cargo test --lib` needs no flag: the
//! stub is active under `cfg(test)` there.
//!
//! The pure mappings are already unit-tested in `src/ffmpegutils.rs`; these
//! tests pin the exported wrapper contract: a value written to a non-null
//! out-param with `OAKCOMMON_OK`, or `OAKCOMMON_E_INVALID` for a null
//! out-param, for every export.

#[cfg(feature = "test-stubs")]
mod ffi_ffmpegutils_tests {
	use std::ptr::null_mut;

	use oakcommon::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};
	use oakcommon::ffi::ffmpegutils::*;

	/// Every out-param wrapper must reject a null pointer without writing.
	#[test]
	fn null_out_param_returns_invalid() {
		let mut out: i32 = -999;
		assert_eq!(
			oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(26, -1, null_mut()),
			OAKCOMMON_E_INVALID
		);
		assert_eq!(
			oakcommon_ffmpegutils_get_compatible_pixel_format(0, null_mut()),
			OAKCOMMON_E_INVALID
		);
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_pixel_format(0, 4, null_mut()),
			OAKCOMMON_E_INVALID
		);
		assert_eq!(
			oakcommon_ffmpegutils_get_native_sample_format(0, null_mut()),
			OAKCOMMON_E_INVALID
		);
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_sample_format(6, null_mut()),
			OAKCOMMON_E_INVALID
		);
		assert_eq!(
			oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(12, null_mut()),
			OAKCOMMON_E_INVALID
		);
		// The out-param is untouched on the failure path.
		assert_eq!(out, -999);
	}

	/// `get_compatible_bridge_pixel_format` picks an exact candidate from the
	/// clamped list (values match `FB_PIX_FMT_*` / `PixelFormat`).
	#[test]
	fn compatible_bridge_pixel_format_exact_candidate() {
		let mut out: i32 = -999;
		// pix_fmt = RGBA (26), maximum = invalid (-1): no limit, RGBA is
		// first in the candidate list.
		assert_eq!(
			oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(26, -1, &mut out),
			OAKCOMMON_OK
		);
		assert_eq!(out, 26);
		// pix_fmt = RGBA_F32_LE (220), maximum = F32 (4): the f32 clamp adds
		// RGBA_F32_LE, so it matches exactly.
		assert_eq!(
			oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(220, 4, &mut out),
			OAKCOMMON_OK
		);
		assert_eq!(out, 220);
	}

	/// `get_compatible_bridge_pixel_format` falls back to the first candidate
	/// for an unknown source format (bridge loss-metric behaviour).
	#[test]
	fn compatible_bridge_pixel_format_unknown_falls_back() {
		let mut out: i32 = -999;
		// YUV420P (0) is not in the RGBA-oriented candidate list -> first
		// candidate RGBA (26).
		assert_eq!(
			oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(0, -1, &mut out),
			OAKCOMMON_OK
		);
		assert_eq!(out, 26);
	}

	/// `get_compatible_pixel_format` maps native formats to the least-lossy
	/// native format.
	#[test]
	fn compatible_pixel_format_maps_native() {
		let mut out: i32 = -999;
		assert_eq!(
			oakcommon_ffmpegutils_get_compatible_pixel_format(0, &mut out),
			OAKCOMMON_OK
		); // U8
		assert_eq!(out, 0);
		assert_eq!(
			oakcommon_ffmpegutils_get_compatible_pixel_format(3, &mut out),
			OAKCOMMON_OK
		); // F16
		assert_eq!(out, 2); // -> U16
		assert_eq!(
			oakcommon_ffmpegutils_get_compatible_pixel_format(-1, &mut out),
			OAKCOMMON_OK
		); // invalid
		assert_eq!(out, -1);
	}

	/// `get_ffmpeg_pixel_format` maps native format + channel count to a
	/// bridge pixel format.
	#[test]
	fn ffmpeg_pixel_format_maps_native_to_bridge() {
		let mut out: i32 = -999;
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_pixel_format(0, 3, &mut out),
			OAKCOMMON_OK
		); // U8 RGB
		assert_eq!(out, 2); // RGB24
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_pixel_format(0, 4, &mut out),
			OAKCOMMON_OK
		); // U8 RGBA
		assert_eq!(out, 26); // RGBA
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_pixel_format(2, 4, &mut out),
			OAKCOMMON_OK
		); // U16 RGBA
		assert_eq!(out, 105); // RGBA64LE
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_pixel_format(1, 3, &mut out),
			OAKCOMMON_OK
		); // U10 RGB
		assert_eq!(out, -1); // no bridge format
	}

	/// `get_native_sample_format` maps bridge sample formats to native.
	#[test]
	fn native_sample_format_maps_bridge_to_native() {
		let mut out: i32 = -999;
		assert_eq!(
			oakcommon_ffmpegutils_get_native_sample_format(0, &mut out),
			OAKCOMMON_OK
		); // U8
		assert_eq!(out, 6); // SMP_FMT_U8
		assert_eq!(
			oakcommon_ffmpegutils_get_native_sample_format(1, &mut out),
			OAKCOMMON_OK
		); // S16
		assert_eq!(out, 7);
		assert_eq!(
			oakcommon_ffmpegutils_get_native_sample_format(8, &mut out),
			OAKCOMMON_OK
		); // FLTP
		assert_eq!(out, 4); // SMP_FMT_F32_P
		assert_eq!(
			oakcommon_ffmpegutils_get_native_sample_format(999, &mut out),
			OAKCOMMON_OK
		);
		assert_eq!(out, -1); // unknown -> invalid
	}

	/// `get_ffmpeg_sample_format` maps native sample formats to bridge.
	#[test]
	fn ffmpeg_sample_format_maps_native_to_bridge() {
		let mut out: i32 = -999;
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_sample_format(6, &mut out),
			OAKCOMMON_OK
		); // SMP_FMT_U8
		assert_eq!(out, 0); // U8
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_sample_format(10, &mut out),
			OAKCOMMON_OK
		); // SMP_FMT_F32
		assert_eq!(out, 3); // FLT
		assert_eq!(
			oakcommon_ffmpegutils_get_ffmpeg_sample_format(-1, &mut out),
			OAKCOMMON_OK
		); // invalid
		assert_eq!(out, -1);
	}

	/// JPEG-range bridge formats convert to their regular counterparts;
	/// everything else passes through unchanged.
	#[test]
	fn jpeg_space_converts_to_regular_space() {
		let mut out: i32 = -999;
		assert_eq!(
			oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(12, &mut out),
			OAKCOMMON_OK
		); // YUVJ420P
		assert_eq!(out, 0); // YUV420P
		assert_eq!(
			oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(13, &mut out),
			OAKCOMMON_OK
		); // YUVJ422P
		assert_eq!(out, 4); // YUV422P
		assert_eq!(
			oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(14, &mut out),
			OAKCOMMON_OK
		); // YUVJ444P
		assert_eq!(out, 5); // YUV444P
		assert_eq!(
			oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(32, &mut out),
			OAKCOMMON_OK
		); // YUVJ440P
		assert_eq!(out, 31); // YUV440P
		assert_eq!(
			oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(138, &mut out),
			OAKCOMMON_OK
		); // YUVJ411P
		assert_eq!(out, 7); // YUV411P
					  // Non-JPEG formats pass through unchanged.
		assert_eq!(
			oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(26, &mut out),
			OAKCOMMON_OK
		); // RGBA
		assert_eq!(out, 26);
		assert_eq!(
			oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(-1, &mut out),
			OAKCOMMON_OK
		); // none
		assert_eq!(out, -1);
	}
}
