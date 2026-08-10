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

//! Smoke tests for the common family (`engine/include/oakengine/config.h`
//! and `videoparams.h`). The oakcommon config store is a process-wide
//! singleton, so config tests are serialized inside single test
//! functions.

#[path = "common/mod.rs"]
mod common;


use std::ffi::{c_char, c_int};

use oakengine::common::{
	oakengine_config_get_int, oakengine_config_get_string, oakengine_config_load,
	oakengine_config_save, oakengine_config_set_error_handler, oakengine_config_set_int,
	oakengine_config_set_string, oakengine_video_params_bytes_per_pixel,
	oakengine_video_params_effective_size, oakengine_video_params_equal,
	oakengine_video_params_format_is_float, oakengine_video_params_internal_channel_count,
	oakengine_video_params_is_valid, oakengine_video_params_make,
	oakengine_video_params_standard_pixel_aspect_at,
	oakengine_video_params_standard_pixel_aspect_count,
	oakengine_video_params_supported_divider_at, oakengine_video_params_supported_divider_count,
	oakengine_video_params_supported_frame_rate_at,
	oakengine_video_params_supported_frame_rate_count, OakVideoParamsPod,
};

/// Config: load/save, string and int round-trips, missing-key behavior.
#[test]
fn config_round_trip() {
	assert_eq!(unsafe { oakengine_config_load() }, 0);

	// Missing key reads as 0 / empty.
	let mut buf = [0 as c_char; 64];
	assert_eq!(
		unsafe { oakengine_config_get_string(c"no/such/key".as_ptr(), buf.as_mut_ptr(), 64) },
		0
	);
	assert_eq!(unsafe { oakengine_config_get_int(c"no/such/key".as_ptr(), 7) }, 7);

	// String round-trip.
	assert_eq!(unsafe { oakengine_config_set_string(c"facade/test".as_ptr(), c"hello".as_ptr()) }, 0);
	let len = unsafe { oakengine_config_get_string(c"facade/test".as_ptr(), buf.as_mut_ptr(), 64) };
	assert_eq!(len, 5);
	assert_eq!(unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(), "hello");

	// A too-small buffer is not written (the two-stage convention is:
	// query the required size, allocate, copy) — the module reports the
	// full length and leaves the buffer untouched.
	let mut small = [0 as c_char; 3];
	let len = unsafe { oakengine_config_get_string(c"facade/test".as_ptr(), small.as_mut_ptr(), 3) };
	assert_eq!(len, 5); // reported full length
	assert_eq!(unsafe { std::ffi::CStr::from_ptr(small.as_ptr()) }.to_str().unwrap(), "");

	// Int round-trip.
	assert_eq!(unsafe { oakengine_config_set_int(c"facade/n".as_ptr(), 1234) }, 0);
	assert_eq!(unsafe { oakengine_config_get_int(c"facade/n".as_ptr(), 0) }, 1234);

	assert_eq!(unsafe { oakengine_config_save() }, 0);
}

/// Config error handler: registered, then invoked via report_error.
#[test]
fn config_error_handler() {
	static CALLED: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(0);
	unsafe extern "C" fn handler(
		_title: *const c_char,
		_message: *const c_char,
		_userdata: *mut std::ffi::c_void,
	) {
		CALLED.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
	}
	assert_eq!(unsafe { oakengine_config_set_error_handler(Some(handler), std::ptr::null_mut()) }, 0);
	// Report an error through the handler.
	assert_eq!(unsafe {
		oakengine::common::oakengine_config_report_error(c"t".as_ptr(), c"m".as_ptr())
	}, 0);
	assert_eq!(CALLED.load(std::sync::atomic::Ordering::SeqCst), 1);
	// NULL handler clears; reporting then does not invoke.
	assert_eq!(unsafe { oakengine_config_set_error_handler(None, std::ptr::null_mut()) }, 0);
	unsafe { oakengine::common::oakengine_config_report_error(c"t".as_ptr(), c"m".as_ptr()) };
	assert_eq!(CALLED.load(std::sync::atomic::Ordering::SeqCst), 1);
}

/// Videoparams static tables.
#[test]
fn videoparams_static_tables() {
	// 12 standard frame rates; the 23.976 entry is 24000/1001.
	assert_eq!(unsafe { oakengine_video_params_supported_frame_rate_count() }, 12);
	let (mut num, mut den) = (0, 0);
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(2, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (24000, 1001));
	// Out of range → E_INVALID.
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(99, &mut num, &mut den) },
		-1
	);

	// 6 standard pixel aspects; index 4 is PAL widescreen 64/45.
	assert_eq!(unsafe { oakengine_video_params_standard_pixel_aspect_count() }, 6);
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_at(4, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (64, 45));

	// Dividers 1..=8; out of range → -1.
	assert_eq!(unsafe { oakengine_video_params_supported_divider_count() }, 8);
	assert_eq!(unsafe { oakengine_video_params_supported_divider_at(5) }, 8);
	assert_eq!(unsafe { oakengine_video_params_supported_divider_at(99) }, -1);

	// Format helpers (PixelFormat codes: F16 = 3, F32 = 4).
	assert_eq!(unsafe { oakengine_video_params_format_is_float(4) }, 1); // F32
	assert_eq!(unsafe { oakengine_video_params_format_is_float(3) }, 1); // F16
	assert_eq!(unsafe { oakengine_video_params_format_is_float(0) }, 0); // U8
	assert_eq!(unsafe { oakengine_video_params_internal_channel_count() }, 4);
	assert!(unsafe { oakengine_video_params_bytes_per_pixel(1, 4) } > 0);
}

/// Videoparams POD: make/equal/valid + effective size.
#[test]
fn videoparams_pod() {
	let mut a: OakVideoParamsPod = unsafe { std::mem::zeroed() };
	assert_eq!(
		unsafe {
			oakengine_video_params_make(
				&mut a,
				1920,
				1080,
				1001,
				30000,
				16,
				1,
				1,
				0,
				1,
				1,
			)
		},
		0
	);
	assert_eq!(a.width, 1920);
	assert_eq!(a.height, 1080);
	assert_eq!(a.time_base_num, 1001);

	// A valid POD is valid.
	assert_eq!(unsafe { oakengine_video_params_is_valid(&a) }, 1);
	// Zero dimensions are not.
	let mut bad = a;
	bad.width = 0;
	assert_eq!(unsafe { oakengine_video_params_is_valid(&bad) }, 0);
	// NULL is invalid.
	assert_eq!(unsafe { oakengine_video_params_is_valid(std::ptr::null()) }, 0);

	// Equality: identical PODs equal; differing field not.
	let mut b = a;
	assert_eq!(unsafe { oakengine_video_params_equal(&a, &b) }, 1);
	b.divider = 2;
	assert_eq!(unsafe { oakengine_video_params_equal(&a, &b) }, 0);
	assert_eq!(unsafe { oakengine_video_params_equal(std::ptr::null(), &a) }, 0);

	// Effective size halves at divider 2.
	let (mut w, mut h) = (0, 0);
	assert_eq!(unsafe { oakengine_video_params_effective_size(1920, 1080, 2, &mut w, &mut h) }, 0);
	assert_eq!((w, h), (960, 540));
	// Invalid divider.
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(1920, 1080, 0, &mut w, &mut h) },
		-1
	);
}
