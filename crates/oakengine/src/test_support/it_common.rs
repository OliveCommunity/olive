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

//! Integration tests for the common family (`src/common.rs` over
//! `engine/include/oakengine/{config,videoparams}.h`).
//!
//! Every exported function is exercised on a legal path with the result
//! asserted, plus the illegal-input matrix the engine must survive (NULL
//! pointers, empty handles, out-of-range indexes, zero/negative sizes,
//! garbage enums). All behavior is real: the facade calls into the real
//! oakcommon store and videoparams domain.
//!
//! The oakcommon config store is a process-wide singleton backed by
//! `config.ini` (honoring the `OAK_CONFIG_DIR` override), so every test
//! that touches config is serialized under [`CONFIG_LOCK`] and redirects
//! the file into a fresh temp dir. The videoparams tables are immutable
//! statics and the params handles are per-test objects, so those tests
//! run in parallel.

// The whole family is called through uniform `unsafe {}` blocks (matching
// the other test binaries), so extern functions that happen to be safe
// (e.g. `oakengine_config_load`) otherwise trip `unused_unsafe`.
#![allow(unused_unsafe)]

use super::common;

use std::ffi::{c_char, c_int};
use std::path::Path;
use std::sync::atomic::{AtomicI32, Ordering};

use crate::common::{
	oakengine_config_get_int, oakengine_config_get_string, oakengine_config_load,
	oakengine_config_report_error, oakengine_config_save, oakengine_config_set_error_handler,
	oakengine_config_set_int, oakengine_config_set_string, oakengine_video_params_bytes_per_pixel,
	oakengine_video_params_create, oakengine_video_params_divider_name,
	oakengine_video_params_effective_size, oakengine_video_params_equal,
	oakengine_video_params_format_is_float,
	oakengine_video_params_format_pixel_aspect_ratio_string,
	oakengine_video_params_frame_rate_to_string, oakengine_video_params_free,
	oakengine_video_params_internal_channel_count, oakengine_video_params_is_valid,
	oakengine_video_params_make, oakengine_video_params_pixel_format_name,
	oakengine_video_params_standard_pixel_aspect_at,
	oakengine_video_params_standard_pixel_aspect_count,
	oakengine_video_params_standard_pixel_aspect_name, oakengine_video_params_supported_divider_at,
	oakengine_video_params_supported_divider_count, oakengine_video_params_supported_frame_rate_at,
	oakengine_video_params_supported_frame_rate_count, OakVideoParamsPod,
};

/// Read a two-stage facade string into a Rust String.
unsafe fn read_buf(buf: &mut [c_char]) -> String {
	unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }
		.to_string_lossy()
		.into_owned()
}

/// Serializes every test that touches the process-wide config store and
/// redirects `OAK_CONFIG_DIR` to a fresh temp dir for the duration of `f`
/// (same pattern as the oakcommon crate's own test support). The only
/// readers of `OAK_CONFIG_DIR` in this binary are these serialized tests.
/// The serialization uses the SHARED storage-config lock (common), so the
/// config tests never race the write-through tests on the singleton store
/// or the env override.
fn with_temp_config_dir<T>(f: impl FnOnce(&Path) -> T) -> T {
	let _guard = common::STORAGE_CONFIG_LOCK.lock().unwrap_or_else(|e| e.into_inner());
	let dir =
		std::env::temp_dir().join(format!("oakengine_it_common_config_{}", std::process::id()));
	let _ = std::fs::create_dir_all(&dir);
	std::env::set_var("OAK_CONFIG_DIR", &dir);
	let result = f(&dir);
	std::env::remove_var("OAK_CONFIG_DIR");
	let _ = std::fs::remove_dir_all(&dir);
	result
}

/// The process-wide config store is a singleton; see module doc.
// (Serialization now uses the shared `common::STORAGE_CONFIG_LOCK`.)

// ---------------------------------------------------------------------------
// config.h
// ---------------------------------------------------------------------------

/// Load/save round-trip, defaults, typed entries and the two-stage string
/// convention (all serialized: the store is process-wide).
#[test]
fn config_roundtrip_persistence() {
	common::force_link();
	with_temp_config_dir(|dir| {
		// A missing config.ini is not an error; defaults are loaded.
		assert_eq!(unsafe { oakengine_config_load() }, 0);

		// Missing keys read as empty / fallback.
		let mut buf = [0 as c_char; 64];
		assert_eq!(
			unsafe { oakengine_config_get_string(c"no/such/key".as_ptr(), buf.as_mut_ptr(), 64) },
			0
		);
		assert_eq!(unsafe { read_buf(&mut buf) }, "");
		assert_eq!(
			unsafe { oakengine_config_get_int(c"no/such/key".as_ptr(), 7) },
			7
		);

		// Compiled-in defaults are readable through the engine getters.
		let len = unsafe {
			oakengine_config_get_string(c"DefaultSequenceFrameRate".as_ptr(), buf.as_mut_ptr(), 64)
		};
		assert_eq!(len, 10);
		assert_eq!(unsafe { read_buf(&mut buf) }, "1001/30000");
		assert_eq!(
			unsafe { oakengine_config_get_int(c"DefaultSequenceWidth".as_ptr(), 0) },
			1920
		);

		// String round-trip.
		assert_eq!(
			unsafe { oakengine_config_set_string(c"it/key".as_ptr(), c"hello".as_ptr()) },
			0
		);
		let len = unsafe { oakengine_config_get_string(c"it/key".as_ptr(), buf.as_mut_ptr(), 64) };
		assert_eq!(len, 5);
		assert_eq!(unsafe { read_buf(&mut buf) }, "hello");

		// Too-small buffer: the full length is reported and the buffer is
		// left untouched (query size, then allocate, then copy).
		let mut small = [0 as c_char; 3];
		let len = unsafe { oakengine_config_get_string(c"it/key".as_ptr(), small.as_mut_ptr(), 3) };
		assert_eq!(len, 5);
		assert_eq!(unsafe { read_buf(&mut small) }, "");

		// A NULL value stores an empty string (engine treats NULL as "").
		assert_eq!(
			unsafe { oakengine_config_set_string(c"it/key".as_ptr(), std::ptr::null()) },
			0
		);
		let len = unsafe { oakengine_config_get_string(c"it/key".as_ptr(), buf.as_mut_ptr(), 64) };
		assert_eq!(len, 0);
		assert_eq!(unsafe { read_buf(&mut buf) }, "");
		assert_eq!(
			unsafe { oakengine_config_set_string(c"it/key".as_ptr(), c"hello".as_ptr()) },
			0
		);

		// A string entry read through the int getter falls back.
		assert_eq!(
			unsafe { oakengine_config_get_int(c"it/key".as_ptr(), 9) },
			9
		);

		// Int round-trip; a known typed key keeps its type across reload.
		assert_eq!(
			unsafe { oakengine_config_set_int(c"it/num".as_ptr(), 1234) },
			0
		);
		assert_eq!(
			unsafe { oakengine_config_get_int(c"it/num".as_ptr(), 0) },
			1234
		);
		assert_eq!(
			unsafe { oakengine_config_set_int(c"DefaultSequenceWidth".as_ptr(), 640) },
			0
		);
		assert_eq!(
			unsafe { oakengine_config_get_int(c"DefaultSequenceWidth".as_ptr(), 0) },
			640
		);

		// Persist, then reload from the file.
		assert_eq!(unsafe { oakengine_config_save() }, 0);
		assert!(dir.join("config.ini").exists());

		assert_eq!(unsafe { oakengine_config_load() }, 0);
		let len = unsafe { oakengine_config_get_string(c"it/key".as_ptr(), buf.as_mut_ptr(), 64) };
		assert_eq!(len, 5);
		assert_eq!(unsafe { read_buf(&mut buf) }, "hello");
		assert_eq!(
			unsafe { oakengine_config_get_int(c"DefaultSequenceWidth".as_ptr(), 0) },
			640
		);
		// A custom typed key loses its type on reload and reads as a string
		// (module C++ parity: only known keys keep their declared type).
		let len = unsafe { oakengine_config_get_string(c"it/num".as_ptr(), buf.as_mut_ptr(), 64) };
		assert_eq!(len, 4);
		assert_eq!(unsafe { read_buf(&mut buf) }, "1234");
		assert_eq!(
			unsafe { oakengine_config_get_int(c"it/num".as_ptr(), 9) },
			9
		);
	});
}

/// Illegal inputs on the config getters/setters: NULL keys and buffers,
/// empty keys, zero/negative sizes — all must fail cleanly, never crash.
#[test]
fn config_illegal_inputs() {
	common::force_link();
	with_temp_config_dir(|_dir| {
		assert_eq!(unsafe { oakengine_config_load() }, 0);
		assert_eq!(
			unsafe { oakengine_config_set_string(c"it/k".as_ptr(), c"abc".as_ptr()) },
			0
		);

		let mut buf = [0 as c_char; 64];

		// NULL key → OAKENGINE_E_INVALID (-1).
		assert_eq!(
			unsafe { oakengine_config_get_string(std::ptr::null(), buf.as_mut_ptr(), 64) },
			-1
		);
		assert_eq!(
			unsafe { oakengine_config_set_string(std::ptr::null(), c"v".as_ptr()) },
			-1
		);
		assert_eq!(unsafe { oakengine_config_set_int(std::ptr::null(), 5) }, -1);
		// NULL key on the int getter returns the fallback (engine contract).
		assert_eq!(
			unsafe { oakengine_config_get_int(std::ptr::null(), 42) },
			42
		);

		// Empty key → the module's INVALID, passed through untranslated.
		assert_eq!(
			unsafe { oakengine_config_get_string(c"".as_ptr(), buf.as_mut_ptr(), 64) },
			-10001
		);
		assert_eq!(unsafe { oakengine_config_get_int(c"".as_ptr(), 42) }, 42);

		// NULL output buffer with a positive size → module INVALID (-10001).
		assert_eq!(
			unsafe { oakengine_config_get_string(c"it/k".as_ptr(), std::ptr::null_mut(), 64) },
			-10001
		);
		// Negative size → module INVALID.
		assert_eq!(
			unsafe { oakengine_config_get_string(c"it/k".as_ptr(), buf.as_mut_ptr(), -1) },
			-10001
		);
		// NULL buffer with size 0 is the two-stage size query: reports the
		// required length without writing.
		assert_eq!(
			unsafe { oakengine_config_get_string(c"it/k".as_ptr(), std::ptr::null_mut(), 0) },
			3
		);
	});
}

/// Error handler: registered, invoked via report_error and on a load
/// failure, NULL args are safe, NULL handler clears.
#[test]
fn config_error_handler_and_load_failure() {
	common::force_link();
	static CALLED: AtomicI32 = AtomicI32::new(0);
	unsafe extern "C" fn handler(
		_title: *const c_char,
		_message: *const c_char,
		_userdata: *mut std::ffi::c_void,
	) {
		CALLED.fetch_add(1, Ordering::SeqCst);
	}

	with_temp_config_dir(|dir| {
		CALLED.store(0, Ordering::SeqCst);

		// Register and report through the handler.
		assert_eq!(
			unsafe { oakengine_config_set_error_handler(Some(handler), std::ptr::null_mut()) },
			0
		);
		assert_eq!(
			unsafe { oakengine_config_report_error(c"title".as_ptr(), c"message".as_ptr()) },
			0
		);
		assert_eq!(CALLED.load(Ordering::SeqCst), 1);
		// NULL title/message are mapped to empty strings, still invoked.
		assert_eq!(
			unsafe { oakengine_config_report_error(std::ptr::null(), std::ptr::null()) },
			0
		);
		assert_eq!(CALLED.load(Ordering::SeqCst), 2);

		// NULL handler clears; reporting then does not invoke.
		assert_eq!(
			unsafe { oakengine_config_set_error_handler(None, std::ptr::null_mut()) },
			0
		);
		unsafe { oakengine_config_report_error(c"t".as_ptr(), c"m".as_ptr()) };
		assert_eq!(CALLED.load(Ordering::SeqCst), 2);

		// A real load failure (config.ini is a directory) reports through
		// the module's registered handler and returns the module FAILED
		// code (-10003) untranslated.
		assert_eq!(
			unsafe { oakengine_config_set_error_handler(Some(handler), std::ptr::null_mut()) },
			0
		);
		std::fs::create_dir(dir.join("config.ini")).unwrap();
		assert_eq!(unsafe { oakengine_config_load() }, -10003);
		assert_eq!(CALLED.load(Ordering::SeqCst), 3);

		// Cleanup: drop the directory and clear the handler.
		std::fs::remove_dir(dir.join("config.ini")).unwrap();
		unsafe { oakengine_config_set_error_handler(None, std::ptr::null_mut()) };
		assert_eq!(unsafe { oakengine_config_load() }, 0);
	});
}

// ---------------------------------------------------------------------------
// videoparams.h — static tables
// ---------------------------------------------------------------------------

/// Static tables: counts, every legal index, specific values, and the
/// out-of-range / NULL failure paths.
#[test]
fn videoparams_static_tables_full() {
	common::force_link();

	// ---- frame rates ------------------------------------------------------
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_count() },
		12
	);
	let mut num: c_int = 0;
	let mut den: c_int = 0;
	for i in 0..12 {
		assert_eq!(
			unsafe { oakengine_video_params_supported_frame_rate_at(i, &mut num, &mut den) },
			0
		);
		assert!(
			num > 0 && den > 0,
			"frame rate {i} must be a positive rational"
		);
	}
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(0, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (10, 1));
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(2, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (24000, 1001)); // 23.976
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(5, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (30000, 1001)); // 29.97
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(6, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (30, 1));
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(11, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (60, 1));

	// Out-of-range / negative / huge indexes → E_INVALID (-1), no panic.
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(12, &mut num, &mut den) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(99, &mut num, &mut den) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(-1, &mut num, &mut den) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_supported_frame_rate_at(c_int::MAX, &mut num, &mut den) },
		-1
	);
	// NULL outputs → E_INVALID.
	assert_eq!(
		unsafe {
			oakengine_video_params_supported_frame_rate_at(0, std::ptr::null_mut(), &mut den)
		},
		-1
	);
	assert_eq!(
		unsafe {
			oakengine_video_params_supported_frame_rate_at(0, &mut num, std::ptr::null_mut())
		},
		-1
	);

	// ---- pixel aspects ----------------------------------------------------
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_count() },
		6
	);
	for i in 0..6 {
		assert_eq!(
			unsafe { oakengine_video_params_standard_pixel_aspect_at(i, &mut num, &mut den) },
			0
		);
		assert!(
			num > 0 && den > 0,
			"pixel aspect {i} must be a positive rational"
		);
	}
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_at(0, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (1, 1));
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_at(4, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (64, 45));
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_at(5, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (4, 3));
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_at(6, &mut num, &mut den) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_at(-1, &mut num, &mut den) },
		-1
	);
	assert_eq!(
		unsafe {
			oakengine_video_params_standard_pixel_aspect_at(0, std::ptr::null_mut(), &mut den)
		},
		-1
	);

	// ---- dividers ---------------------------------------------------------
	assert_eq!(
		unsafe { oakengine_video_params_supported_divider_count() },
		8
	);
	let expected: [c_int; 8] = [1, 2, 3, 4, 6, 8, 12, 16];
	for (i, want) in expected.iter().enumerate() {
		assert_eq!(
			unsafe { oakengine_video_params_supported_divider_at(i as c_int) },
			*want
		);
	}
	assert_eq!(
		unsafe { oakengine_video_params_supported_divider_at(8) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_supported_divider_at(-1) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_supported_divider_at(c_int::MAX) },
		-1
	);
}

/// Display names and string formatters (pixel aspect names, divider names,
/// frame-rate strings, PAR template formatting).
#[test]
fn videoparams_names_and_formatters() {
	common::force_link();

	let mut buf = [0 as c_char; 64];

	// ---- standard pixel aspect names --------------------------------------
	let len = unsafe { oakengine_video_params_standard_pixel_aspect_name(0, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 6);
	assert_eq!(unsafe { read_buf(&mut buf) }, "Square");
	let len = unsafe { oakengine_video_params_standard_pixel_aspect_name(1, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 3);
	assert_eq!(unsafe { read_buf(&mut buf) }, "8:9");
	let len = unsafe { oakengine_video_params_standard_pixel_aspect_name(4, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 5);
	assert_eq!(unsafe { read_buf(&mut buf) }, "64:45");
	// Out of range → E_INVALID; negative index → E_INVALID.
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_name(6, buf.as_mut_ptr(), 64) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_name(-1, buf.as_mut_ptr(), 64) },
		-1
	);
	// NULL buffer reports the length only (two-stage size query).
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_name(0, std::ptr::null_mut(), 64) },
		6
	);
	// Too-small buffer truncates but reports the full length.
	let mut small = [0 as c_char; 2];
	assert_eq!(
		unsafe { oakengine_video_params_standard_pixel_aspect_name(0, small.as_mut_ptr(), 2) },
		6
	);
	assert_eq!(unsafe { read_buf(&mut small) }, "S");

	// ---- divider names ------------------------------------------------------
	let len = unsafe { oakengine_video_params_divider_name(1, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 4);
	assert_eq!(unsafe { read_buf(&mut buf) }, "Full");
	let len = unsafe { oakengine_video_params_divider_name(2, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 3);
	assert_eq!(unsafe { read_buf(&mut buf) }, "1/2");
	let len = unsafe { oakengine_video_params_divider_name(8, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 3);
	assert_eq!(unsafe { read_buf(&mut buf) }, "1/8");
	// Zero / negative divider → E_INVALID (facade rejects before the module).
	assert_eq!(
		unsafe { oakengine_video_params_divider_name(0, buf.as_mut_ptr(), 64) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_divider_name(-3, buf.as_mut_ptr(), 64) },
		-1
	);
	// NULL buffer with a positive size → module INVALID, passed through.
	assert_eq!(
		unsafe { oakengine_video_params_divider_name(2, std::ptr::null_mut(), 64) },
		-10001
	);

	// ---- frame rate strings -------------------------------------------------
	let len = unsafe { oakengine_video_params_frame_rate_to_string(25, 1, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 6);
	assert_eq!(unsafe { read_buf(&mut buf) }, "25 FPS");
	let len =
		unsafe { oakengine_video_params_frame_rate_to_string(24000, 1001, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 10);
	assert_eq!(unsafe { read_buf(&mut buf) }, "23.976 FPS");
	let len = unsafe { oakengine_video_params_frame_rate_to_string(10, 1, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 6);
	assert_eq!(unsafe { read_buf(&mut buf) }, "10 FPS");

	// Zero denominator: C++-parity float division (1/0 → +inf), rendered as
	// "inf FPS" — a legal return, never a crash/panic.
	let len = unsafe { oakengine_video_params_frame_rate_to_string(1, 0, buf.as_mut_ptr(), 64) };
	assert!(len >= 0, "den=0 must not error ({len})");
	assert_eq!(unsafe { read_buf(&mut buf) }, "inf FPS");
	// 0/0 → NaN → "nan FPS".
	let len = unsafe { oakengine_video_params_frame_rate_to_string(0, 0, buf.as_mut_ptr(), 64) };
	assert!(len >= 0);
	assert_eq!(unsafe { read_buf(&mut buf) }, "nan FPS");
	// NULL buffer with a positive size → module INVALID.
	assert_eq!(
		unsafe { oakengine_video_params_frame_rate_to_string(25, 1, std::ptr::null_mut(), 64) },
		-10001
	);
	// NULL buffer with size 0 is the two-stage size query.
	assert_eq!(
		unsafe { oakengine_video_params_frame_rate_to_string(25, 1, std::ptr::null_mut(), 0) },
		6
	);

	// ---- PAR template formatting (facade-local) -----------------------------
	let len = unsafe {
		oakengine_video_params_format_pixel_aspect_ratio_string(
			c"%1".as_ptr(),
			16,
			15,
			buf.as_mut_ptr(),
			64,
		)
	};
	assert_eq!(len, 5);
	assert_eq!(unsafe { read_buf(&mut buf) }, "16:15");
	let len = unsafe {
		oakengine_video_params_format_pixel_aspect_ratio_string(
			c"par=%1".as_ptr(),
			4,
			3,
			buf.as_mut_ptr(),
			64,
		)
	};
	assert_eq!(len, 7);
	assert_eq!(unsafe { read_buf(&mut buf) }, "par=4:3");
	// No placeholder: the template passes through unchanged.
	let len = unsafe {
		oakengine_video_params_format_pixel_aspect_ratio_string(
			c"raw".as_ptr(),
			16,
			15,
			buf.as_mut_ptr(),
			64,
		)
	};
	assert_eq!(len, 3);
	assert_eq!(unsafe { read_buf(&mut buf) }, "raw");
	// NULL format → E_INVALID; NULL buffer reports the length only.
	assert_eq!(
		unsafe {
			oakengine_video_params_format_pixel_aspect_ratio_string(
				std::ptr::null(),
				16,
				15,
				buf.as_mut_ptr(),
				64,
			)
		},
		-1
	);
	assert_eq!(
		unsafe {
			oakengine_video_params_format_pixel_aspect_ratio_string(
				c"%1".as_ptr(),
				16,
				15,
				std::ptr::null_mut(),
				64,
			)
		},
		5
	);
}

/// Format helpers: float/name queries and bytes-per-pixel across the
/// format matrix (valid, boundary and garbage codes).
#[test]
fn videoparams_format_helpers() {
	common::force_link();

	// format_is_float: F16 = 3, F32 = 4 float; everything else 0, garbage
	// codes map to the Invalid format and report 0 (never crash).
	assert_eq!(unsafe { oakengine_video_params_format_is_float(0) }, 0); // U8
	assert_eq!(unsafe { oakengine_video_params_format_is_float(1) }, 0); // U10
	assert_eq!(unsafe { oakengine_video_params_format_is_float(2) }, 0); // U16
	assert_eq!(unsafe { oakengine_video_params_format_is_float(3) }, 1); // F16
	assert_eq!(unsafe { oakengine_video_params_format_is_float(4) }, 1); // F32
	assert_eq!(unsafe { oakengine_video_params_format_is_float(5) }, 0); // Count
	assert_eq!(unsafe { oakengine_video_params_format_is_float(99) }, 0);
	assert_eq!(unsafe { oakengine_video_params_format_is_float(-1) }, 0);
	assert_eq!(
		unsafe { oakengine_video_params_format_is_float(c_int::MIN) },
		0
	);

	// pixel_format_name for every real format.
	let mut buf = [0 as c_char; 64];
	let len = unsafe { oakengine_video_params_pixel_format_name(0, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 5);
	assert_eq!(unsafe { read_buf(&mut buf) }, "8-bit");
	let len = unsafe { oakengine_video_params_pixel_format_name(1, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 13);
	assert_eq!(unsafe { read_buf(&mut buf) }, "10-bit Packed");
	let len = unsafe { oakengine_video_params_pixel_format_name(4, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 19);
	assert_eq!(unsafe { read_buf(&mut buf) }, "Full-Float (32-bit)");
	// Garbage format → "Unknown (0xFFFFFFFF)" (Invalid renders %X of -1).
	let len = unsafe { oakengine_video_params_pixel_format_name(99, buf.as_mut_ptr(), 64) };
	assert_eq!(len, 20);
	assert_eq!(unsafe { read_buf(&mut buf) }, "Unknown (0xFFFFFFFF)");
	// NULL buffer / negative size → module INVALID; size-0 query → length.
	assert_eq!(
		unsafe { oakengine_video_params_pixel_format_name(0, std::ptr::null_mut(), 64) },
		-10001
	);
	assert_eq!(
		unsafe { oakengine_video_params_pixel_format_name(0, buf.as_mut_ptr(), -1) },
		-10001
	);
	assert_eq!(
		unsafe { oakengine_video_params_pixel_format_name(0, std::ptr::null_mut(), 0) },
		5
	);

	// bytes_per_pixel across the format × channels matrix.
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(0, 4) }, 4); // U8
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(1, 4) }, 4); // U10 packed
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(2, 4) }, 8); // U16
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(3, 4) }, 8); // F16
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(4, 4) }, 16); // F32
																		  // Garbage formats have no channels-per-format entry → 0 bytes.
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(99, 4) }, 0);
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(-1, 4) }, 0);
	// Zero channels → 0 bytes.
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(0, 0) }, 0);
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(4, 0) }, 0);
	// Negative channels: the module does not validate (C++ parity), so the
	// result is the plain signed product — a value, not a crash.
	assert_eq!(unsafe { oakengine_video_params_bytes_per_pixel(4, -1) }, -4);

	assert_eq!(
		unsafe { oakengine_video_params_internal_channel_count() },
		4
	);
}

/// Effective size: divider scaling on the legal matrix plus zero/negative
/// dimensions and dividers → E_INVALID.
#[test]
fn videoparams_effective_size_matrix() {
	common::force_link();

	let mut w: c_int = 0;
	let mut h: c_int = 0;
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(1920, 1080, 1, &mut w, &mut h) },
		0
	);
	assert_eq!((w, h), (1920, 1080));
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(1920, 1080, 2, &mut w, &mut h) },
		0
	);
	assert_eq!((w, h), (960, 540));
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(1920, 1080, 4, &mut w, &mut h) },
		0
	);
	assert_eq!((w, h), (480, 270));
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(100, 50, 3, &mut w, &mut h) },
		0
	);
	assert_eq!((w, h), (33, 16));
	// Divider 16 truncates the odd dimension (integer division).
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(1920, 1080, 16, &mut w, &mut h) },
		0
	);
	assert_eq!((w, h), (120, 67));

	// Both output pointers may be NULL (size computed, nothing written).
	assert_eq!(
		unsafe {
			oakengine_video_params_effective_size(
				1920,
				1080,
				2,
				std::ptr::null_mut(),
				std::ptr::null_mut(),
			)
		},
		0
	);

	// Zero / negative dimensions and dividers → E_INVALID.
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(0, 1080, 2, &mut w, &mut h) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(1920, 0, 2, &mut w, &mut h) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(-1, 1080, 2, &mut w, &mut h) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(1920, 1080, 0, &mut w, &mut h) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_video_params_effective_size(1920, 1080, -2, &mut w, &mut h) },
		-1
	);
}

// ---------------------------------------------------------------------------
// videoparams.h — POD make/equal/valid
// ---------------------------------------------------------------------------

/// A valid POD used across the POD tests.
fn valid_pod() -> OakVideoParamsPod {
	let mut p: OakVideoParamsPod = unsafe { std::mem::zeroed() };
	assert_eq!(
		unsafe { oakengine_video_params_make(&mut p, 1920, 1080, 1001, 30000, 4, 1, 1, 0, 1, 2,) },
		0
	);
	p
}

/// make fills every field; equal compares all of them; is_valid implements
/// the engine's POD validity rules.
#[test]
fn videoparams_pod_make_equal_valid() {
	common::force_link();

	// make: every field lands in the POD.
	let mut p: OakVideoParamsPod = unsafe { std::mem::zeroed() };
	assert_eq!(
		unsafe { oakengine_video_params_make(&mut p, 1920, 1080, 1001, 30000, 4, 1, 1, 0, 1, 2) },
		0
	);
	assert_eq!(p.width, 1920);
	assert_eq!(p.height, 1080);
	assert_eq!(p.time_base_num, 1001);
	assert_eq!(p.time_base_den, 30000);
	assert_eq!(p.format, 4);
	assert_eq!(p.pixel_aspect_num, 1);
	assert_eq!(p.pixel_aspect_den, 1);
	assert_eq!(p.interlacing, 0);
	assert_eq!(p.color_range, 1);
	assert_eq!(p.divider, 2);
	assert_eq!(p.video_type, 0);
	assert_eq!(p.premultiplied_alpha, 0);
	// NULL POD → E_INVALID.
	assert_eq!(
		unsafe {
			oakengine_video_params_make(
				std::ptr::null_mut(),
				1920,
				1080,
				1001,
				30000,
				4,
				1,
				1,
				0,
				1,
				2,
			)
		},
		-1
	);

	// equal: identical PODs → 1; any differing field → 0; NULL → 0.
	let a = valid_pod();
	let mut b = a;
	assert_eq!(unsafe { oakengine_video_params_equal(&a, &b) }, 1);
	for (field, val) in [
		("width", 640),
		("height", 720),
		("time_base_num", 25),
		("time_base_den", 1),
		("format", 0),
		("pixel_aspect_num", 4),
		("pixel_aspect_den", 3),
		("interlacing", 1),
		("color_range", 0),
		("divider", 1),
		("video_type", 1),
		("premultiplied_alpha", 1),
	] {
		let mut c = a;
		match field {
			"width" => c.width = val,
			"height" => c.height = val,
			"time_base_num" => c.time_base_num = val,
			"time_base_den" => c.time_base_den = val,
			"format" => c.format = val,
			"pixel_aspect_num" => c.pixel_aspect_num = val,
			"pixel_aspect_den" => c.pixel_aspect_den = val,
			"interlacing" => c.interlacing = val,
			"color_range" => c.color_range = val,
			"divider" => c.divider = val,
			"video_type" => c.video_type = val,
			"premultiplied_alpha" => c.premultiplied_alpha = val,
			_ => unreachable!(),
		}
		assert_eq!(
			unsafe { oakengine_video_params_equal(&a, &c) },
			0,
			"equal must be 0 when {field} differs"
		);
	}
	assert_eq!(
		unsafe { oakengine_video_params_equal(std::ptr::null(), &a) },
		0
	);
	assert_eq!(
		unsafe { oakengine_video_params_equal(&a, std::ptr::null()) },
		0
	);

	// is_valid: the valid POD → 1.
	assert_eq!(unsafe { oakengine_video_params_is_valid(&a) }, 1);
	// NULL → 0.
	assert_eq!(
		unsafe { oakengine_video_params_is_valid(std::ptr::null()) },
		0
	);
	// Each invalidating field → 0.
	let cases: [(&str, fn(&mut OakVideoParamsPod)); 6] = [
		("width", |p| p.width = 0),
		("height", |p| p.height = 0),
		("pixel_aspect_num", |p| p.pixel_aspect_num = 0),
		("pixel_aspect_den", |p| p.pixel_aspect_den = 0),
		("format", |p| p.format = -1),
		("time_base_den", |p| p.time_base_den = 0),
	];
	for (name, mutate) in cases {
		let mut c = a;
		mutate(&mut c);
		assert_eq!(
			unsafe { oakengine_video_params_is_valid(&c) },
			0,
			"is_valid must be 0 when {name} is invalid"
		);
	}
	// NOTE (observed divergence): the facade's POD check uses `format >= 0`,
	// so out-of-range-but-non-negative formats (e.g. 99, or the Count code 5)
	// read as "valid" here, while the module/C++ `VideoParams::is_valid`
	// additionally requires `format < Count`. The facade check is a
	// simplified local rule (the POD has no channel_count), not a crash.
	let mut c = a;
	c.format = 99;
	assert_eq!(unsafe { oakengine_video_params_is_valid(&c) }, 1);
}

// ---------------------------------------------------------------------------
// videoparams.h — opaque handle lifecycle
// ---------------------------------------------------------------------------

/// create/free lifecycle: NULL rejection, real-handle creation, NULL free.
#[test]
fn videoparams_create_free_lifecycle() {
	common::force_link();

	// NULL POD → NULL handle.
	assert!(unsafe { oakengine_video_params_create(std::ptr::null()) }.is_null());

	// Valid POD → non-NULL handle; freed cleanly.
	let pod = valid_pod();
	let h = unsafe { oakengine_video_params_create(&pod) };
	assert!(!h.is_null());
	unsafe { oakengine_video_params_free(h) };

	// A zeroed POD still yields a handle (the module initializes a default
	// set and the setters accept any values); the handle frees cleanly.
	let zeroed: OakVideoParamsPod = unsafe { std::mem::zeroed() };
	let h = unsafe { oakengine_video_params_create(&zeroed) };
	assert!(!h.is_null());
	unsafe { oakengine_video_params_free(h) };

	// free(NULL) is a documented no-op.
	unsafe { oakengine_video_params_free(std::ptr::null_mut()) };

	// NOTE (contract): the facade's free deallocates the handle box
	// (`Box::from_raw`), so a second free of the same pointer is a
	// use-after-free and is NOT part of the family's contract — unlike the
	// module-level `oakcommon_videoparams_free`, which nulls the handle out
	// before returning. This family exposes no debug alive counter to
	// verify a return to baseline; leak-free operation is implied by the
	// create/free round-trips above.
}
