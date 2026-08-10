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

//! FFI-level integration tests for the C-ABI `videoparams` exports in
//! `oakcommon::ffi::videoparams`, asserted against the C++ oracle
//! `src/common/c_api/videoparams.cpp` and the Rust domain
//! `src/common/rust/src/videoparams.rs`.
//!
//! The contract under test (each point matches the C++ oracle):
//! - exports take a `CHandle` by value and never release it, so callers
//!   keep the handle alive and `free` it exactly once;
//! - `free` nullifies the handle, is idempotent, and tolerates a null
//!   pointer;
//! - two-stage string getters return the required size (NUL included) and
//!   only copy when the buffer is large enough — they never truncate;
//! - getters reject a null handle or null out-param with `E_INVALID`,
//!   setters reject a null handle, and string inputs reject null pointers;
//! - out-of-range enum-like setter codes clamp to a documented fallback
//!   (CPP-PARITY with the C++ `static_cast` semantics).

use std::ffi::{c_char, CStr, CString};

use oakcommon::error::{OAKCOMMON_E_FAILED, OAKCOMMON_E_INVALID, OAKCOMMON_OK};
use oakcommon::ffi::videoparams::*;
use oakcommon::handle::{CHandle, OAKCOMMON_ABI_VERSION};

/// `init_basic` arguments: 1920x1080 U8 RGBA, square pixels, progressive,
/// divider 1.
fn make() -> CHandle {
	oakcommon_videoparams_init_basic(1920, 1080, 0, 4, 1, 1, 0, 1)
}

/// `init_with_time_base` arguments: 1920x1080 U8 RGBA, time base 1001/30000,
/// square pixels, progressive, divider 1.
fn make_tb() -> CHandle {
	oakcommon_videoparams_init_with_time_base(1920, 1080, 1001, 30000, 0, 4, 1, 1, 0, 1)
}

/// Convert a string slice to a NUL-terminated C string for FFI inputs.
fn to_cstring(s: &str) -> CString {
	CString::new(s).expect("test string must not contain NUL")
}

/// Cheap struct copy: `CHandle` is neither `Clone` nor `Copy`, but every
/// getter takes it by value. Rebuilding from the same fields duplicates
/// only the handle value — the box stays alive as long as the original
/// handle lives, and the getters never release.
fn dup(h: &CHandle) -> CHandle {
	CHandle {
		ctx: h.ctx,
		addref: h.addref,
		release: h.release,
		abi_version: h.abi_version,
	}
}

/// Drive a two-stage string getter against the C++ `copy_string`
/// convention: a null-buffer size query, a short buffer that must stay
/// untouched (no truncation), an exact-fit copy with its NUL, and an
/// oversized copy with the tail untouched.
fn assert_two_stage_getter(getter: impl Fn(*mut c_char, i32) -> i32, expected: &str) {
	let required = (expected.len() + 1) as i32;

	// Size query: a null buffer returns the required size, NUL included.
	assert_eq!(getter(std::ptr::null_mut(), 0), required);

	// Short buffer: too small, so nothing is written to it.
	let short_size = (required - 1).max(0);
	let mut short = vec![0xABu8; short_size as usize];
	assert_eq!(getter(short.as_mut_ptr() as *mut c_char, short_size), required);
	assert!(short.iter().all(|&b| b == 0xAB), "short buffer must stay untouched");

	// Exact fit: payload followed by a NUL.
	let mut exact = vec![0xCDu8; required as usize];
	assert_eq!(getter(exact.as_mut_ptr() as *mut c_char, required), required);
	assert_eq!(&exact[..expected.len()], expected.as_bytes());
	assert_eq!(exact[expected.len()], 0);

	// Oversized: payload and NUL written, tail left as initialized.
	let mut big = vec![0u8; (required + 8) as usize];
	assert_eq!(getter(big.as_mut_ptr() as *mut c_char, required + 8), required);
	assert_eq!(&big[..expected.len()], expected.as_bytes());
	assert_eq!(big[expected.len()], 0);
	assert!(big[(required + 1) as usize..].iter().all(|&b| b == 0));
}

// ---- Handle lifecycle ----

/// All three constructors yield a stamped, non-empty handle; `free`
/// nullifies it, is idempotent, and tolerates a null pointer.
#[test]
fn init_free_lifecycle() {
	let h = oakcommon_videoparams_init();
	assert!(!h.is_null());
	assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	assert!(h.addref.is_some());
	assert!(h.release.is_some());

	let hb = make();
	assert!(!hb.is_null());
	assert_eq!(hb.abi_version, OAKCOMMON_ABI_VERSION);

	let ht = make_tb();
	assert!(!ht.is_null());
	assert_eq!(ht.abi_version, OAKCOMMON_ABI_VERSION);

	let mut hf = oakcommon_videoparams_init_basic(1, 1, 4, 4, 1, 1, 0, 1);
	assert!(!hf.is_null());
	oakcommon_videoparams_free(&mut hf);
	assert!(hf.is_null());
	// A second free of the now-empty handle is safe.
	oakcommon_videoparams_free(&mut hf);
	assert!(hf.is_null());
	// Freeing a null pointer is safe.
	oakcommon_videoparams_free(std::ptr::null_mut());
}

// ---- Core field round-trips ----

/// Width, height, and depth round-trip; a null handle or null out-param is
/// `E_INVALID`.
#[test]
fn width_height_depth_roundtrip() {
	let h = make();
	let mut w = -1i32;
	let mut ht = -1i32;
	let mut d = -1i32;

	assert_eq!(oakcommon_videoparams_get_width(dup(&h), &mut w), OAKCOMMON_OK);
	assert_eq!(w, 1920);
	assert_eq!(oakcommon_videoparams_get_height(dup(&h), &mut ht), OAKCOMMON_OK);
	assert_eq!(ht, 1080);
	assert_eq!(oakcommon_videoparams_get_depth(dup(&h), &mut d), OAKCOMMON_OK);
	assert_eq!(d, 1);

	assert_eq!(oakcommon_videoparams_set_width(dup(&h), 640), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_height(dup(&h), 480), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_depth(dup(&h), 2), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_width(dup(&h), &mut w), OAKCOMMON_OK);
	assert_eq!(w, 640);
	assert_eq!(oakcommon_videoparams_get_height(dup(&h), &mut ht), OAKCOMMON_OK);
	assert_eq!(ht, 480);
	assert_eq!(oakcommon_videoparams_get_depth(dup(&h), &mut d), OAKCOMMON_OK);
	assert_eq!(d, 2);

	assert_eq!(oakcommon_videoparams_set_width(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_width(CHandle::null(), &mut w), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_width(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_height(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_depth(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
}

/// `is_3d` is derived from depth (CPP-PARITY: C++ has no `set_is_3d`;
/// the getter reads `depth > 1`).
#[test]
fn is_3d_roundtrip() {
	let h = make();
	let mut is3d = -1i32;
	let mut d = -1i32;
	assert_eq!(oakcommon_videoparams_get_is_3d(dup(&h), &mut is3d), OAKCOMMON_OK);
	assert_eq!(is3d, 0);

	assert_eq!(oakcommon_videoparams_set_depth(dup(&h), 2), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_is_3d(dup(&h), &mut is3d), OAKCOMMON_OK);
	assert_eq!(is3d, 1);
	assert_eq!(oakcommon_videoparams_get_depth(dup(&h), &mut d), OAKCOMMON_OK);
	assert_eq!(d, 2);

	assert_eq!(oakcommon_videoparams_set_depth(dup(&h), 1), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_is_3d(dup(&h), &mut is3d), OAKCOMMON_OK);
	assert_eq!(is3d, 0);
	assert_eq!(oakcommon_videoparams_get_depth(dup(&h), &mut d), OAKCOMMON_OK);
	assert_eq!(d, 1);

	assert_eq!(oakcommon_videoparams_set_depth(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_is_3d(CHandle::null(), &mut is3d), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_is_3d(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
}

/// Time base and frame rate round-trip independently: `set_time_base` does
/// not touch the frame rate and `set_frame_rate` does not touch the time
/// base (CPP-PARITY with the C++ setters). The frame rate expressed as a
/// time base is always the flipped frame rate.
#[test]
fn time_base_frame_rate_roundtrip() {
	let h = make_tb();
	let mut n = -1i32;
	let mut d = -1i32;

	// Derived from the constructor: frame rate = flipped time base.
	assert_eq!(oakcommon_videoparams_get_time_base(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (1001, 30000));
	assert_eq!(oakcommon_videoparams_get_frame_rate(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (30000, 1001));
	assert_eq!(oakcommon_videoparams_frame_rate_as_time_base(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (1001, 30000));

	assert_eq!(oakcommon_videoparams_set_time_base(dup(&h), 1, 50), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_time_base(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (1, 50));
	// The frame rate is untouched by `set_time_base`.
	assert_eq!(oakcommon_videoparams_get_frame_rate(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (30000, 1001));

	assert_eq!(oakcommon_videoparams_set_frame_rate(dup(&h), 60, 1), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_frame_rate(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (60, 1));
	// The time base is untouched by `set_frame_rate`.
	assert_eq!(oakcommon_videoparams_get_time_base(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (1, 50));
	assert_eq!(oakcommon_videoparams_frame_rate_as_time_base(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (1, 60));

	assert_eq!(oakcommon_videoparams_set_time_base(CHandle::null(), 1, 50), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_frame_rate(CHandle::null(), 60, 1), OAKCOMMON_E_INVALID);
	for getter in [
		|buf1, buf2| oakcommon_videoparams_get_time_base(CHandle::null(), buf1, buf2),
		|buf1, buf2| oakcommon_videoparams_get_frame_rate(CHandle::null(), buf1, buf2),
		|buf1, buf2| oakcommon_videoparams_frame_rate_as_time_base(CHandle::null(), buf1, buf2),
	] {
		assert_eq!(getter(&mut n, &mut d), OAKCOMMON_E_INVALID);
	}
	// A null out-param is `E_INVALID` even with a valid handle.
	assert_eq!(
		oakcommon_videoparams_get_time_base(dup(&h), std::ptr::null_mut(), &mut d),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_videoparams_get_frame_rate(dup(&h), &mut n, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_videoparams_frame_rate_as_time_base(dup(&h), std::ptr::null_mut(), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

/// Pixel aspect ratio, format, and channel count round-trip. The default
/// handle has no format (code -1) and zero channels; an out-of-range format
/// code maps back to `Invalid` (code -1, CPP-PARITY with the C++ enum
/// `static_cast`).
#[test]
fn par_format_channel_roundtrip() {
	let h = oakcommon_videoparams_init();
	let mut n = -1i32;
	let mut d = -1i32;
	let mut f = -1i32;
	let mut c = -1i32;

	assert_eq!(oakcommon_videoparams_get_pixel_aspect_ratio(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (1, 1));
	assert_eq!(oakcommon_videoparams_get_format(dup(&h), &mut f), OAKCOMMON_OK);
	assert_eq!(f, -1);
	assert_eq!(oakcommon_videoparams_get_channel_count(dup(&h), &mut c), OAKCOMMON_OK);
	assert_eq!(c, 0);

	assert_eq!(oakcommon_videoparams_set_pixel_aspect_ratio(dup(&h), 16, 9), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_format(dup(&h), 0), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_channel_count(dup(&h), 2), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_pixel_aspect_ratio(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (16, 9));
	assert_eq!(oakcommon_videoparams_get_format(dup(&h), &mut f), OAKCOMMON_OK);
	assert_eq!(f, 0);
	assert_eq!(oakcommon_videoparams_get_channel_count(dup(&h), &mut c), OAKCOMMON_OK);
	assert_eq!(c, 2);

	// A null (0/5) pixel aspect ratio falls back to square pixels.
	assert_eq!(oakcommon_videoparams_set_pixel_aspect_ratio(dup(&h), 0, 5), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_pixel_aspect_ratio(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (1, 1));

	// Out-of-range format codes map to `Invalid`, read back as -1.
	assert_eq!(oakcommon_videoparams_set_format(dup(&h), 999), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_format(dup(&h), &mut f), OAKCOMMON_OK);
	assert_eq!(f, -1);

	assert_eq!(oakcommon_videoparams_set_pixel_aspect_ratio(CHandle::null(), 1, 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_format(CHandle::null(), 0), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_channel_count(CHandle::null(), 2), OAKCOMMON_E_INVALID);
	assert_eq!(
		oakcommon_videoparams_get_pixel_aspect_ratio(CHandle::null(), &mut n, &mut d),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(oakcommon_videoparams_get_format(CHandle::null(), &mut f), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_channel_count(CHandle::null(), &mut c), OAKCOMMON_E_INVALID);
	assert_eq!(
		oakcommon_videoparams_get_pixel_aspect_ratio(dup(&h), std::ptr::null_mut(), &mut d),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(oakcommon_videoparams_get_format(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_channel_count(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
}

/// Interlacing, divider, and enabled round-trip. Interlacing codes clamp:
/// 1 -> TopFirst, 2 -> BottomFirst, anything else -> None (0).
#[test]
fn interlacing_divider_enabled_roundtrip() {
	let h = make();
	let mut il = -1i32;
	let mut dv = -1i32;
	let mut en = -1i32;

	assert_eq!(oakcommon_videoparams_get_interlacing(dup(&h), &mut il), OAKCOMMON_OK);
	assert_eq!(il, 0);
	assert_eq!(oakcommon_videoparams_get_divider(dup(&h), &mut dv), OAKCOMMON_OK);
	assert_eq!(dv, 1);
	assert_eq!(oakcommon_videoparams_get_enabled(dup(&h), &mut en), OAKCOMMON_OK);
	assert_eq!(en, 1);

	assert_eq!(oakcommon_videoparams_set_interlacing(dup(&h), 1), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_interlacing(dup(&h), &mut il), OAKCOMMON_OK);
	assert_eq!(il, 1);
	assert_eq!(oakcommon_videoparams_set_interlacing(dup(&h), 2), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_interlacing(dup(&h), &mut il), OAKCOMMON_OK);
	assert_eq!(il, 2);
	assert_eq!(oakcommon_videoparams_set_interlacing(dup(&h), 3), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_interlacing(dup(&h), &mut il), OAKCOMMON_OK);
	assert_eq!(il, 0);

	assert_eq!(oakcommon_videoparams_set_divider(dup(&h), 4), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_divider(dup(&h), &mut dv), OAKCOMMON_OK);
	assert_eq!(dv, 4);

	assert_eq!(oakcommon_videoparams_set_enabled(dup(&h), 0), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_enabled(dup(&h), &mut en), OAKCOMMON_OK);
	assert_eq!(en, 0);

	assert_eq!(oakcommon_videoparams_set_interlacing(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_divider(CHandle::null(), 2), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_enabled(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_interlacing(CHandle::null(), &mut il), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_divider(CHandle::null(), &mut dv), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_enabled(CHandle::null(), &mut en), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_interlacing(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_divider(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_enabled(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
}

/// Video type, x/y offset, stream index, start time, and duration
/// round-trip. Video-type codes clamp: 1 -> Still, 2 -> ImageSequence,
/// anything else -> Video (0).
#[test]
fn video_type_position_stream_roundtrip() {
	let h = make();
	let mut vt = -1i32;
	let mut x = -1f32;
	let mut y = -1f32;
	let mut si = -1i32;
	let mut st = -1i64;
	let mut du = -1i64;

	assert_eq!(oakcommon_videoparams_get_video_type(dup(&h), &mut vt), OAKCOMMON_OK);
	assert_eq!(vt, 0);
	assert_eq!(oakcommon_videoparams_get_x(dup(&h), &mut x), OAKCOMMON_OK);
	assert_eq!(x, 0.0);
	assert_eq!(oakcommon_videoparams_get_y(dup(&h), &mut y), OAKCOMMON_OK);
	assert_eq!(y, 0.0);
	assert_eq!(oakcommon_videoparams_get_stream_index(dup(&h), &mut si), OAKCOMMON_OK);
	assert_eq!(si, 0);
	assert_eq!(oakcommon_videoparams_get_start_time(dup(&h), &mut st), OAKCOMMON_OK);
	assert_eq!(st, 0);
	assert_eq!(oakcommon_videoparams_get_duration(dup(&h), &mut du), OAKCOMMON_OK);
	assert_eq!(du, 0);

	assert_eq!(oakcommon_videoparams_set_video_type(dup(&h), 1), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_video_type(dup(&h), &mut vt), OAKCOMMON_OK);
	assert_eq!(vt, 1);
	assert_eq!(oakcommon_videoparams_set_video_type(dup(&h), 2), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_video_type(dup(&h), &mut vt), OAKCOMMON_OK);
	assert_eq!(vt, 2);
	assert_eq!(oakcommon_videoparams_set_video_type(dup(&h), 9), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_video_type(dup(&h), &mut vt), OAKCOMMON_OK);
	assert_eq!(vt, 0);

	assert_eq!(oakcommon_videoparams_set_x(dup(&h), 1.5), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_y(dup(&h), -2.5), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_stream_index(dup(&h), 3), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_start_time(dup(&h), 12345i64), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_duration(dup(&h), 67890i64), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_x(dup(&h), &mut x), OAKCOMMON_OK);
	assert_eq!(x, 1.5);
	assert_eq!(oakcommon_videoparams_get_y(dup(&h), &mut y), OAKCOMMON_OK);
	assert_eq!(y, -2.5);
	assert_eq!(oakcommon_videoparams_get_stream_index(dup(&h), &mut si), OAKCOMMON_OK);
	assert_eq!(si, 3);
	assert_eq!(oakcommon_videoparams_get_start_time(dup(&h), &mut st), OAKCOMMON_OK);
	assert_eq!(st, 12345);
	assert_eq!(oakcommon_videoparams_get_duration(dup(&h), &mut du), OAKCOMMON_OK);
	assert_eq!(du, 67890);

	assert_eq!(oakcommon_videoparams_set_video_type(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_x(CHandle::null(), 1.0), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_y(CHandle::null(), 1.0), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_stream_index(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_start_time(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_duration(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_video_type(CHandle::null(), &mut vt), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_x(CHandle::null(), &mut x), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_y(CHandle::null(), &mut y), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_stream_index(CHandle::null(), &mut si), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_start_time(CHandle::null(), &mut st), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_duration(CHandle::null(), &mut du), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_video_type(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_x(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_y(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_stream_index(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_start_time(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_duration(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
}

/// Premultiplied alpha, color range, primaries, and transfer round-trip.
/// Color-range codes clamp: 1 -> Full, anything else -> Limited (0).
#[test]
fn color_fields_roundtrip() {
	let h = make();
	let mut pa = -1i32;
	let mut cr = -1i32;
	let mut cp = -1i32;
	let mut ct = -1i32;

	assert_eq!(oakcommon_videoparams_get_premultiplied_alpha(dup(&h), &mut pa), OAKCOMMON_OK);
	assert_eq!(pa, 0);
	assert_eq!(oakcommon_videoparams_get_color_range(dup(&h), &mut cr), OAKCOMMON_OK);
	assert_eq!(cr, 0);
	assert_eq!(oakcommon_videoparams_get_color_primaries(dup(&h), &mut cp), OAKCOMMON_OK);
	assert_eq!(cp, 0);
	assert_eq!(oakcommon_videoparams_get_color_transfer(dup(&h), &mut ct), OAKCOMMON_OK);
	assert_eq!(ct, 0);

	assert_eq!(oakcommon_videoparams_set_premultiplied_alpha(dup(&h), 1), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_color_range(dup(&h), 1), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_color_primaries(dup(&h), 2), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_set_color_transfer(dup(&h), 3), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_premultiplied_alpha(dup(&h), &mut pa), OAKCOMMON_OK);
	assert_eq!(pa, 1);
	assert_eq!(oakcommon_videoparams_get_color_range(dup(&h), &mut cr), OAKCOMMON_OK);
	assert_eq!(cr, 1);
	assert_eq!(oakcommon_videoparams_get_color_primaries(dup(&h), &mut cp), OAKCOMMON_OK);
	assert_eq!(cp, 2);
	assert_eq!(oakcommon_videoparams_get_color_transfer(dup(&h), &mut ct), OAKCOMMON_OK);
	assert_eq!(ct, 3);

	// Out-of-range color range clamps to Limited.
	assert_eq!(oakcommon_videoparams_set_color_range(dup(&h), 5), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_color_range(dup(&h), &mut cr), OAKCOMMON_OK);
	assert_eq!(cr, 0);

	assert_eq!(oakcommon_videoparams_set_premultiplied_alpha(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_color_range(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_color_primaries(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_color_transfer(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_premultiplied_alpha(CHandle::null(), &mut pa), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_color_range(CHandle::null(), &mut cr), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_color_primaries(CHandle::null(), &mut cp), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_color_transfer(CHandle::null(), &mut ct), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_premultiplied_alpha(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_color_range(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_color_primaries(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_color_transfer(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
}

// ---- String fields ----

/// `colorspace` round-trips through a two-stage getter; the empty string is
/// a valid value (required size 1). Null handle or null string is
/// `E_INVALID`.
#[test]
fn colorspace_two_stage() {
	let h = make();
	assert_eq!(oakcommon_videoparams_set_colorspace(dup(&h), to_cstring("sRGB").as_ptr()), OAKCOMMON_OK);
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_colorspace(dup(&h), buf, size), "sRGB");

	// The empty colorspace is a valid value.
	assert_eq!(oakcommon_videoparams_set_colorspace(dup(&h), to_cstring("").as_ptr()), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_colorspace(dup(&h), std::ptr::null_mut(), 0), 1);

	assert_eq!(oakcommon_videoparams_set_colorspace(CHandle::null(), to_cstring("sRGB").as_ptr()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_set_colorspace(dup(&h), std::ptr::null()), OAKCOMMON_E_INVALID);
	assert_eq!(
		oakcommon_videoparams_get_colorspace(CHandle::null(), std::ptr::null_mut(), 0),
		OAKCOMMON_E_INVALID
	);
}

// ---- Derived values ----

/// Square-pixel width, effective dimensions/depth, validity, bytes per
/// channel/pixel, and buffer size, computed from the stored fields.
#[test]
fn derived_dimensions_and_buffer() {
	let h = make();
	let mut v = -1i32;
	let mut w = -1i32;
	let mut ht = -1i32;
	let mut d = -1i32;

	assert_eq!(oakcommon_videoparams_get_square_pixel_width(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 1920);
	assert_eq!(oakcommon_videoparams_get_effective_width(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 1920);
	assert_eq!(oakcommon_videoparams_get_effective_height(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 1080);
	assert_eq!(oakcommon_videoparams_get_effective_depth(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 1);
	assert_eq!(oakcommon_videoparams_get_is_valid(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 1);
	assert_eq!(oakcommon_videoparams_get_bytes_per_channel(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 1);
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 4);
	assert_eq!(oakcommon_videoparams_get_buffer_size(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 1920 * 1080 * 4);

	// Square-pixel width follows the pixel aspect ratio (lround(1920*16/9)
	// == 3413) and the effective width follows the divider.
	assert_eq!(oakcommon_videoparams_set_pixel_aspect_ratio(dup(&h), 16, 9), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_square_pixel_width(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 3413);
	assert_eq!(oakcommon_videoparams_set_divider(dup(&h), 2), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_effective_width(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 960);
	assert_eq!(oakcommon_videoparams_get_effective_height(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 540);

	// An invalid format makes the parameter set invalid.
	assert_eq!(oakcommon_videoparams_set_format(dup(&h), 999), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_is_valid(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 0);

	assert_eq!(oakcommon_videoparams_get_square_pixel_width(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_effective_width(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_effective_height(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_effective_depth(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_is_valid(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_bytes_per_channel(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_buffer_size(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_square_pixel_width(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_effective_width(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_effective_height(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_effective_depth(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_is_valid(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_bytes_per_channel(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_buffer_size(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);

	// Clean up the width/height out-params used above to keep clippy quiet.
	assert_eq!(w, -1);
	assert_eq!(ht, -1);
	assert_eq!(d, -1);
}

/// `get_time_in_timebase_units` converts a time into time-base units plus
/// the start time. With no time base set it writes `i64::MIN`
/// (CPP-PARITY: C++ returns `INT64_MIN` / `AV_NOPTS_VALUE`) but still
/// succeeds.
#[test]
fn time_in_timebase_units() {
	let h = make_tb();
	let mut ts = -1i64;

	// Time base 1001/30000; 1001/30000 seconds == 1 unit.
	assert_eq!(oakcommon_videoparams_get_time_in_timebase_units(dup(&h), 1001, 30000, &mut ts), OAKCOMMON_OK);
	assert_eq!(ts, 1);

	assert_eq!(oakcommon_videoparams_set_start_time(dup(&h), 5i64), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_get_time_in_timebase_units(dup(&h), 1001, 30000, &mut ts), OAKCOMMON_OK);
	assert_eq!(ts, 6);

	// A time base of 1001/30000 (29.97 fps): 1 second == 30 units.
	assert_eq!(oakcommon_videoparams_get_time_in_timebase_units(dup(&h), 1, 1, &mut ts), OAKCOMMON_OK);
	assert_eq!(ts, 35);

	// `init_basic` has no time base: i64::MIN is written, call succeeds.
	let nb = make();
	assert_eq!(oakcommon_videoparams_get_time_in_timebase_units(dup(&nb), 1, 1, &mut ts), OAKCOMMON_OK);
	assert_eq!(ts, i64::MIN);

	assert_eq!(
		oakcommon_videoparams_get_time_in_timebase_units(CHandle::null(), 1, 1, &mut ts),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_videoparams_get_time_in_timebase_units(dup(&h), 1, 1, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

/// `equals` compares the nine parameter-set fields; the same fields yield 1
/// and any difference yields 0. A null handle, null `other`, or null
/// out-param is `E_INVALID`.
#[test]
fn equals() {
	let a = make();
	let b = make();
	let mut eq = -1i32;

	assert_eq!(oakcommon_videoparams_equals(dup(&a), dup(&b), &mut eq), OAKCOMMON_OK);
	assert_eq!(eq, 1);

	assert_eq!(oakcommon_videoparams_set_width(dup(&b), 640), OAKCOMMON_OK);
	assert_eq!(oakcommon_videoparams_equals(dup(&a), dup(&b), &mut eq), OAKCOMMON_OK);
	assert_eq!(eq, 0);

	assert_eq!(
		oakcommon_videoparams_equals(CHandle::null(), dup(&b), &mut eq),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_videoparams_equals(dup(&a), CHandle::null(), &mut eq),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_videoparams_equals(dup(&a), dup(&b), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

// ---- XML ----

/// `load_xml` parses a fragment and applies the fields; malformed input is
/// `E_FAILED`. `save_xml` is a two-stage getter whose output starts with
/// the `<videoparams>` root.
#[test]
fn load_save_xml() {
	let h = make();

	// Round-trip through XML.
	let xml = "<videoparams><width>640</width><height>480</height></videoparams>";
	assert_eq!(oakcommon_videoparams_load_xml(dup(&h), to_cstring(xml).as_ptr()), OAKCOMMON_OK);
	let mut w = -1i32;
	let mut ht = -1i32;
	assert_eq!(oakcommon_videoparams_get_width(dup(&h), &mut w), OAKCOMMON_OK);
	assert_eq!(w, 640);
	assert_eq!(oakcommon_videoparams_get_height(dup(&h), &mut ht), OAKCOMMON_OK);
	assert_eq!(ht, 480);

	// save_xml is a two-stage getter (CPP-PARITY: copy_string semantics).
	let required = oakcommon_videoparams_save_xml(dup(&h), std::ptr::null_mut(), 0);
	assert!(required > 0);
	let mut out = vec![0u8; required as usize];
	assert_eq!(
		oakcommon_videoparams_save_xml(dup(&h), out.as_mut_ptr() as *mut c_char, required),
		required
	);
	let s = unsafe { CStr::from_ptr(out.as_ptr() as *const c_char) }.to_str().unwrap();
	assert!(s.starts_with("<videoparams>"));
	assert!(s.contains("<width>640</width>"));

	// Malformed fragments fail with E_FAILED.
	assert_eq!(oakcommon_videoparams_load_xml(dup(&h), to_cstring("<width>").as_ptr()), OAKCOMMON_E_FAILED);
	assert_eq!(oakcommon_videoparams_load_xml(dup(&h), to_cstring("not xml").as_ptr()), OAKCOMMON_E_FAILED);
	assert_eq!(oakcommon_videoparams_load_xml(dup(&h), to_cstring("").as_ptr()), OAKCOMMON_E_FAILED);

	assert_eq!(
		oakcommon_videoparams_load_xml(CHandle::null(), to_cstring(xml).as_ptr()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(oakcommon_videoparams_load_xml(dup(&h), std::ptr::null()), OAKCOMMON_E_INVALID);
	assert_eq!(
		oakcommon_videoparams_save_xml(CHandle::null(), std::ptr::null_mut(), 0),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_videoparams_save_xml(dup(&h), std::ptr::null_mut(), 5),
		OAKCOMMON_E_INVALID
	);
}

// ---- Static helpers ----

/// Pure static arithmetic helpers. These take no handle and have no failure
/// path by design (out-of-range formats map to `Invalid`, which yields 0
/// bytes).
#[test]
fn static_arithmetic() {
	// bytes per channel: U8=1, U10=0, U16/F16=2, F32=4, Invalid/Count=0.
	assert_eq!(oakcommon_videoparams_get_bytes_per_channel_for_format(0), 1);
	assert_eq!(oakcommon_videoparams_get_bytes_per_channel_for_format(1), 0);
	assert_eq!(oakcommon_videoparams_get_bytes_per_channel_for_format(2), 2);
	assert_eq!(oakcommon_videoparams_get_bytes_per_channel_for_format(4), 4);
	assert_eq!(oakcommon_videoparams_get_bytes_per_channel_for_format(999), 0);
	assert_eq!(oakcommon_videoparams_static_get_bytes_per_channel(0), 1);
	assert_eq!(oakcommon_videoparams_static_get_bytes_per_channel(4), 4);

	// bytes per pixel: U10 is packed 4 bytes only for 4 channels.
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel_for_format(0, 4), 4);
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel_for_format(1, 4), 4);
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel_for_format(1, 3), 0);
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel_for_format(2, 3), 6);
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel_for_format(4, 4), 16);
	assert_eq!(oakcommon_videoparams_get_bytes_per_pixel_for_format(999, 4), 0);
	assert_eq!(oakcommon_videoparams_static_get_bytes_per_pixel(0, 4), 4);
	assert_eq!(oakcommon_videoparams_static_get_bytes_per_pixel(1, 4), 4);

	// buffer size = w * h * bpp (U8 RGBA).
	assert_eq!(oakcommon_videoparams_calculate_buffer_size(1920, 1080, 0, 4), 1920 * 1080 * 4);

	// float formats are F16/F32 only.
	assert_eq!(oakcommon_videoparams_format_is_float(3), 1);
	assert_eq!(oakcommon_videoparams_format_is_float(4), 1);
	assert_eq!(oakcommon_videoparams_format_is_float(0), 0);
	assert_eq!(oakcommon_videoparams_format_is_float(999), 0);

	// auto divider: 1920x1080 -> 2 (of 8, 12, 16 ... capped).
	assert_eq!(oakcommon_videoparams_generate_auto_divider(100, 100), 1);
	assert_eq!(oakcommon_videoparams_generate_auto_divider(1920, 1080), 2);
	assert_eq!(oakcommon_videoparams_generate_auto_divider(3840, 2160), 3);

	// scaled dimension is floor(dim/divider).
	assert_eq!(oakcommon_videoparams_get_scaled_dimension(1920, 2), 960);
	assert_eq!(oakcommon_videoparams_get_scaled_dimension(1080, 2), 540);

	// divider for a target resolution.
	assert_eq!(
		oakcommon_videoparams_get_divider_for_target_resolution(3840, 2160, 1920, 1080),
		2
	);
	assert_eq!(
		oakcommon_videoparams_get_divider_for_target_resolution(1920, 1080, 1920, 1080),
		1
	);
	assert_eq!(
		oakcommon_videoparams_get_divider_for_target_resolution(1921, 1081, 959, 540),
		3
	);
}

/// Static two-stage string getters: divider names, format names, and
/// frame-rate strings. An invalid output buffer is `E_INVALID`.
#[test]
fn static_string_getters() {
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_name_for_divider(1, buf, size), "Full");
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_name_for_divider(2, buf, size), "1/2");
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_name_for_divider(8, buf, size), "1/8");

	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_format_name(0, buf, size), "8-bit");
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_format_name(1, buf, size), "10-bit Packed");
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_format_name(2, buf, size), "16-bit Integer");
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_format_name(3, buf, size), "Half-Float (16-bit)");
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_format_name(4, buf, size), "Full-Float (32-bit)");
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_format_name(5, buf, size), "Unknown (0x5)");
	assert_two_stage_getter(|buf, size| oakcommon_videoparams_get_format_name(-1, buf, size), "Unknown (0xFFFFFFFF)");

	assert_two_stage_getter(
		|buf, size| oakcommon_videoparams_frame_rate_to_string(24000, 1001, buf, size),
		"23.976 FPS",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_videoparams_frame_rate_to_string(24, 1, buf, size),
		"24 FPS",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_videoparams_frame_rate_to_string(0, 0, buf, size),
		"nan FPS",
	);

	// An invalid output buffer (null with a positive size) is E_INVALID.
	assert_eq!(oakcommon_videoparams_get_name_for_divider(1, std::ptr::null_mut(), 5), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_get_format_name(0, std::ptr::null_mut(), 5), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_videoparams_frame_rate_to_string(24, 1, std::ptr::null_mut(), 5), OAKCOMMON_E_INVALID);
}
