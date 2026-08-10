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

//! FFI-level integration tests for the C-ABI `subtitleparams` exports in
//! `oakcommon::ffi::subtitleparams`, asserted against the C++ oracle
//! `src/common/c_api/subtitleparams.cpp` and the Rust domain
//! `src/common/rust/src/subtitleparams.rs`.
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
//! - an out-of-range subtitle index yields `E_NOT_FOUND`; malformed XML
//!   yields `E_FAILED`; the empty set is invalid with `count` 0 and
//!   `duration` 0/1.

use std::ffi::{c_char, CString};

use oakcommon::error::{
	OAKCOMMON_E_FAILED, OAKCOMMON_E_INVALID, OAKCOMMON_E_NOT_FOUND, OAKCOMMON_OK,
};
use oakcommon::ffi::subtitleparams::*;
use oakcommon::handle::{CHandle, OAKCOMMON_ABI_VERSION};

/// Create an empty subtitle parameter set.
fn make() -> CHandle {
	oakcommon_subtitleparams_init()
}

/// A populated set: stream index 2, disabled, two subtitles.
fn make_populated() -> CHandle {
	let h = make();
	assert_eq!(
		oakcommon_subtitleparams_add_subtitle(dup(&h), 0, 1, 25, 1, to_cstring("hello").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_subtitleparams_add_subtitle(dup(&h), 25, 1, 50, 1, to_cstring("world").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(oakcommon_subtitleparams_set_stream_index(dup(&h), 2), OAKCOMMON_OK);
	assert_eq!(oakcommon_subtitleparams_set_enabled(dup(&h), 0), OAKCOMMON_OK);
	h
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

/// `init` yields a stamped, non-empty handle; `free` nullifies it, is
/// idempotent, and tolerates a null pointer.
#[test]
fn init_free_lifecycle() {
	let h = oakcommon_subtitleparams_init();
	assert!(!h.is_null());
	assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	assert!(h.addref.is_some());
	assert!(h.release.is_some());

	let mut hf = make();
	assert!(!hf.is_null());
	oakcommon_subtitleparams_free(&mut hf);
	assert!(hf.is_null());
	// A second free of the now-empty handle is safe.
	oakcommon_subtitleparams_free(&mut hf);
	assert!(hf.is_null());
	// Freeing a null pointer is safe.
	oakcommon_subtitleparams_free(std::ptr::null_mut());
	// Freeing a handle struct that is already null (by value) is safe too.
	let mut empty = CHandle::null();
	oakcommon_subtitleparams_free(&mut empty);
	assert!(empty.is_null());
}

// ---- Core field round-trips ----

/// Stream index defaults to 0 and round-trips; a null handle or null
/// out-param is `E_INVALID`.
#[test]
fn stream_index_roundtrip() {
	let h = make();
	let mut si = -1i32;

	assert_eq!(oakcommon_subtitleparams_get_stream_index(dup(&h), &mut si), OAKCOMMON_OK);
	assert_eq!(si, 0);

	assert_eq!(oakcommon_subtitleparams_set_stream_index(dup(&h), 3), OAKCOMMON_OK);
	assert_eq!(oakcommon_subtitleparams_get_stream_index(dup(&h), &mut si), OAKCOMMON_OK);
	assert_eq!(si, 3);

	assert_eq!(oakcommon_subtitleparams_set_stream_index(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(
		oakcommon_subtitleparams_get_stream_index(CHandle::null(), &mut si),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_get_stream_index(dup(&h), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

/// `enabled` defaults to 1 (CPP-PARITY: the C++ constructor sets
/// `enabled_ = true`) and round-trips; any non-zero setter value enables.
#[test]
fn enabled_roundtrip() {
	let h = make();
	let mut en = -1i32;

	assert_eq!(oakcommon_subtitleparams_get_enabled(dup(&h), &mut en), OAKCOMMON_OK);
	assert_eq!(en, 1);

	assert_eq!(oakcommon_subtitleparams_set_enabled(dup(&h), 0), OAKCOMMON_OK);
	assert_eq!(oakcommon_subtitleparams_get_enabled(dup(&h), &mut en), OAKCOMMON_OK);
	assert_eq!(en, 0);

	// A non-zero code (even 5) enables the stream.
	assert_eq!(oakcommon_subtitleparams_set_enabled(dup(&h), 5), OAKCOMMON_OK);
	assert_eq!(oakcommon_subtitleparams_get_enabled(dup(&h), &mut en), OAKCOMMON_OK);
	assert_eq!(en, 1);

	assert_eq!(oakcommon_subtitleparams_set_enabled(CHandle::null(), 1), OAKCOMMON_E_INVALID);
	assert_eq!(
		oakcommon_subtitleparams_get_enabled(CHandle::null(), &mut en),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_get_enabled(dup(&h), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

// ---- Empty-set behavior ----

/// An empty set is invalid, has count 0, and duration 0/1; null handle or
/// null out-param is `E_INVALID`.
#[test]
fn empty_set_defaults() {
	let h = make();
	let mut v = -1i32;
	let mut c = -1i32;
	let mut n = -1i32;
	let mut d = -1i32;

	assert_eq!(oakcommon_subtitleparams_is_valid(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 0);
	assert_eq!(oakcommon_subtitleparams_count(dup(&h), &mut c), OAKCOMMON_OK);
	assert_eq!(c, 0);
	assert_eq!(oakcommon_subtitleparams_duration(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (0, 1));

	assert_eq!(oakcommon_subtitleparams_is_valid(CHandle::null(), &mut v), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_subtitleparams_count(CHandle::null(), &mut c), OAKCOMMON_E_INVALID);
	assert_eq!(
		oakcommon_subtitleparams_duration(CHandle::null(), &mut n, &mut d),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(oakcommon_subtitleparams_is_valid(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(oakcommon_subtitleparams_count(dup(&h), std::ptr::null_mut()), OAKCOMMON_E_INVALID);
	assert_eq!(
		oakcommon_subtitleparams_duration(dup(&h), std::ptr::null_mut(), &mut d),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_duration(dup(&h), &mut n, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

// ---- Subtitles ----

/// `add_subtitle` appends entries; `is_valid`/`count`/`duration`/
/// `get_subtitle` reflect the populated set. Rationals are reduced
/// (CPP-PARITY with the C++ `Rational` constructor).
#[test]
fn add_subtitle_and_query() {
	let h = make_populated();
	let mut v = -1i32;
	let mut c = -1i32;
	let mut n = -1i32;
	let mut d = -1i32;

	assert_eq!(oakcommon_subtitleparams_is_valid(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 1);
	assert_eq!(oakcommon_subtitleparams_count(dup(&h), &mut c), OAKCOMMON_OK);
	assert_eq!(c, 2);
	assert_eq!(oakcommon_subtitleparams_duration(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (50, 1));

	assert_eq!(oakcommon_subtitleparams_get_subtitle(dup(&h), 0, &mut n, &mut d, &mut v, &mut c), OAKCOMMON_OK);
	assert_eq!((n, d), (0, 1));
	assert_eq!((v, c), (25, 1));
	assert_eq!(oakcommon_subtitleparams_get_subtitle(dup(&h), 1, &mut n, &mut d, &mut v, &mut c), OAKCOMMON_OK);
	assert_eq!((n, d), (25, 1));
	assert_eq!((v, c), (50, 1));

	// Reduction: Rational(2,4) -> 1/2, Rational(9,3) -> 3/1.
	assert_eq!(
		oakcommon_subtitleparams_add_subtitle(dup(&h), 2, 4, 9, 3, to_cstring("t").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(oakcommon_subtitleparams_get_subtitle(dup(&h), 2, &mut n, &mut d, &mut v, &mut c), OAKCOMMON_OK);
	assert_eq!((n, d), (1, 2));
	assert_eq!((v, c), (3, 1));
}

/// `add_subtitle` rejects a null handle or null text; an empty text string
/// is a valid subtitle.
#[test]
fn add_subtitle_failures() {
	let h = make();
	assert_eq!(
		oakcommon_subtitleparams_add_subtitle(CHandle::null(), 0, 1, 1, 1, to_cstring("x").as_ptr()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_add_subtitle(dup(&h), 0, 1, 1, 1, std::ptr::null()),
		OAKCOMMON_E_INVALID
	);

	// Empty text is fine and counts as a subtitle.
	assert_eq!(
		oakcommon_subtitleparams_add_subtitle(dup(&h), 0, 1, 1, 1, to_cstring("").as_ptr()),
		OAKCOMMON_OK
	);
	let mut c = -1i32;
	assert_eq!(oakcommon_subtitleparams_count(dup(&h), &mut c), OAKCOMMON_OK);
	assert_eq!(c, 1);
}

/// `get_subtitle` on an out-of-range index is `E_NOT_FOUND`; a null handle
/// or any null out-param is `E_INVALID`.
#[test]
fn get_subtitle_out_of_range() {
	let h = make_populated();
	let mut n = -1i32;
	let mut d = -1i32;
	let mut v = -1i32;
	let mut c = -1i32;

	assert_eq!(oakcommon_subtitleparams_get_subtitle(dup(&h), 2, &mut n, &mut d, &mut v, &mut c), OAKCOMMON_E_NOT_FOUND);
	assert_eq!(oakcommon_subtitleparams_get_subtitle(dup(&h), -1, &mut n, &mut d, &mut v, &mut c), OAKCOMMON_E_NOT_FOUND);

	// On an empty set even index 0 is out of range.
	let e = make();
	assert_eq!(oakcommon_subtitleparams_get_subtitle(dup(&e), 0, &mut n, &mut d, &mut v, &mut c), OAKCOMMON_E_NOT_FOUND);

	assert_eq!(
		oakcommon_subtitleparams_get_subtitle(CHandle::null(), 0, &mut n, &mut d, &mut v, &mut c),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_get_subtitle(dup(&h), 0, std::ptr::null_mut(), &mut d, &mut v, &mut c),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_get_subtitle(dup(&h), 0, &mut n, std::ptr::null_mut(), &mut v, &mut c),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_get_subtitle(dup(&h), 0, &mut n, &mut d, std::ptr::null_mut(), &mut c),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_get_subtitle(dup(&h), 0, &mut n, &mut d, &mut v, std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

/// `clear` removes every subtitle (count 0, invalid, duration 0/1) and
/// rejects a null handle.
#[test]
fn clear() {
	let h = make_populated();
	assert_eq!(oakcommon_subtitleparams_clear(dup(&h)), OAKCOMMON_OK);

	let mut v = -1i32;
	let mut c = -1i32;
	let mut n = -1i32;
	let mut d = -1i32;
	assert_eq!(oakcommon_subtitleparams_count(dup(&h), &mut c), OAKCOMMON_OK);
	assert_eq!(c, 0);
	assert_eq!(oakcommon_subtitleparams_is_valid(dup(&h), &mut v), OAKCOMMON_OK);
	assert_eq!(v, 0);
	assert_eq!(oakcommon_subtitleparams_duration(dup(&h), &mut n, &mut d), OAKCOMMON_OK);
	assert_eq!((n, d), (0, 1));

	// Clearing again is a no-op success.
	assert_eq!(oakcommon_subtitleparams_clear(dup(&h)), OAKCOMMON_OK);

	assert_eq!(oakcommon_subtitleparams_clear(CHandle::null()), OAKCOMMON_E_INVALID);
}

// ---- String getters ----

/// `get_subtitle_text` is a two-stage string getter; an out-of-range index
/// is `E_NOT_FOUND`, and a null handle or invalid out-buffer is
/// `E_INVALID`.
#[test]
fn get_subtitle_text_two_stage() {
	let h = make_populated();
	assert_two_stage_getter(
		|buf, size| oakcommon_subtitleparams_get_subtitle_text(dup(&h), 0, buf, size),
		"hello",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_subtitleparams_get_subtitle_text(dup(&h), 1, buf, size),
		"world",
	);

	// Out-of-range index is E_NOT_FOUND (even on a size query).
	assert_eq!(
		oakcommon_subtitleparams_get_subtitle_text(dup(&h), 2, std::ptr::null_mut(), 0),
		OAKCOMMON_E_NOT_FOUND
	);
	assert_eq!(
		oakcommon_subtitleparams_get_subtitle_text(dup(&h), -1, std::ptr::null_mut(), 0),
		OAKCOMMON_E_NOT_FOUND
	);

	assert_eq!(
		oakcommon_subtitleparams_get_subtitle_text(CHandle::null(), 0, std::ptr::null_mut(), 0),
		OAKCOMMON_E_INVALID
	);
	// A null buffer with a positive size is an invalid string out-param.
	assert_eq!(
		oakcommon_subtitleparams_get_subtitle_text(dup(&h), 0, std::ptr::null_mut(), 5),
		OAKCOMMON_E_INVALID
	);
	// A negative size is invalid too.
	assert_eq!(
		oakcommon_subtitleparams_get_subtitle_text(dup(&h), 0, std::ptr::null_mut(), -1),
		OAKCOMMON_E_INVALID
	);
}

/// `generate_ass_header` is a static two-stage string getter (no handle).
/// The payload matches the C++ header verbatim (CRLF line endings).
#[test]
fn generate_ass_header_two_stage() {
	let expected = "[Script Info]\r\n\
		; Script generated by Oak\r\n\
		ScriptType: v4.00+\r\n\
		PlayResX: 384\r\n\
		PlayResY: 288\r\n\
		ScaledBorderAndShadow: yes\r\n\
		\r\n\
		[V4+ Styles]\r\n\
		Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, \
		BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, \
		BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n\
		Style: Default,Arial,16,&HFFFFFF,&HFFFFFF,&H000000,&H000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,0\r\n\
		\r\n\
		[Events]\r\n\
		Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
	assert_two_stage_getter(
		|buf, size| oakcommon_subtitleparams_generate_ass_header(buf, size),
		expected,
	);

	// An invalid out-buffer (null with a positive size) is E_INVALID.
	assert_eq!(
		oakcommon_subtitleparams_generate_ass_header(std::ptr::null_mut(), 5),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_generate_ass_header(std::ptr::null_mut(), -1),
		OAKCOMMON_E_INVALID
	);
}

// ---- XML ----

/// `load_xml` parses a fragment and applies stream index, enabled, and
/// subtitles; malformed input or a missing root is `E_FAILED`; a null
/// handle or null xml is `E_INVALID`.
#[test]
fn load_xml() {
	let h = make();

	let xml = "<subtitleparams><streamindex>7</streamindex><enabled>0</enabled>\
		<subtitles><subtitle in=\"0/1\" out=\"25/1\">hello</subtitle>\
		<subtitle in=\"25/1\" out=\"50/1\">world</subtitle></subtitles></subtitleparams>";
	assert_eq!(oakcommon_subtitleparams_load_xml(dup(&h), to_cstring(xml).as_ptr()), OAKCOMMON_OK);

	let mut si = -1i32;
	let mut en = -1i32;
	let mut c = -1i32;
	assert_eq!(oakcommon_subtitleparams_get_stream_index(dup(&h), &mut si), OAKCOMMON_OK);
	assert_eq!(si, 7);
	assert_eq!(oakcommon_subtitleparams_get_enabled(dup(&h), &mut en), OAKCOMMON_OK);
	assert_eq!(en, 0);
	assert_eq!(oakcommon_subtitleparams_count(dup(&h), &mut c), OAKCOMMON_OK);
	assert_eq!(c, 2);
	assert_eq!(oakcommon_subtitleparams_get_subtitle_text(dup(&h), 0, std::ptr::null_mut(), 0), 6);
	assert_two_stage_getter(
		|buf, size| oakcommon_subtitleparams_get_subtitle_text(dup(&h), 1, buf, size),
		"world",
	);

	// Malformed / missing-root fragments fail with E_FAILED.
	assert_eq!(
		oakcommon_subtitleparams_load_xml(dup(&h), to_cstring("<subtitleparams><streamindex>").as_ptr()),
		OAKCOMMON_E_FAILED
	);
	assert_eq!(
		oakcommon_subtitleparams_load_xml(dup(&h), to_cstring("not xml").as_ptr()),
		OAKCOMMON_E_FAILED
	);
	assert_eq!(
		oakcommon_subtitleparams_load_xml(dup(&h), to_cstring("").as_ptr()),
		OAKCOMMON_E_FAILED
	);

	assert_eq!(
		oakcommon_subtitleparams_load_xml(CHandle::null(), to_cstring(xml).as_ptr()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(oakcommon_subtitleparams_load_xml(dup(&h), std::ptr::null()), OAKCOMMON_E_INVALID);
}

/// `save_xml` is a two-stage string getter; the output matches the C++
/// `XmlStreamWriter` format exactly (no indentation, escaped text).
#[test]
fn save_xml_two_stage() {
	// The empty set serializes to streamindex 0 / enabled 1 / no subtitles.
	let e = make();
	assert_two_stage_getter(
		|buf, size| oakcommon_subtitleparams_save_xml(dup(&e), buf, size),
		"<subtitleparams><streamindex>0</streamindex><enabled>1</enabled><subtitles></subtitles></subtitleparams>",
	);

	// The populated set round-trips through load_xml.
	let h = make_populated();
	let expected = "<subtitleparams><streamindex>2</streamindex><enabled>0</enabled>\
		<subtitles><subtitle in=\"0/1\" out=\"25/1\">hello</subtitle>\
		<subtitle in=\"25/1\" out=\"50/1\">world</subtitle></subtitles></subtitleparams>";
	assert_two_stage_getter(
		|buf, size| oakcommon_subtitleparams_save_xml(dup(&h), buf, size),
		expected,
	);

	// A loaded fragment round-trips byte-for-byte.
	let xml = "<subtitleparams><streamindex>9</streamindex><enabled>1</enabled>\
		<subtitles><subtitle in=\"3/2\" out=\"5/1\">hi</subtitle></subtitles></subtitleparams>";
	assert_eq!(oakcommon_subtitleparams_load_xml(dup(&h), to_cstring(xml).as_ptr()), OAKCOMMON_OK);
	assert_two_stage_getter(
		|buf, size| oakcommon_subtitleparams_save_xml(dup(&h), buf, size),
		xml,
	);

	assert_eq!(
		oakcommon_subtitleparams_save_xml(CHandle::null(), std::ptr::null_mut(), 0),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_subtitleparams_save_xml(dup(&h), std::ptr::null_mut(), 5),
		OAKCOMMON_E_INVALID
	);

	// The saved XML parses back to the same data via the C API.
	let mut si = -1i32;
	let mut en = -1i32;
	let mut c = -1i32;
	let mut n = -1i32;
	let mut d = -1i32;
	assert_eq!(oakcommon_subtitleparams_get_stream_index(dup(&h), &mut si), OAKCOMMON_OK);
	assert_eq!(si, 9);
	assert_eq!(oakcommon_subtitleparams_get_enabled(dup(&h), &mut en), OAKCOMMON_OK);
	assert_eq!(en, 1);
	assert_eq!(oakcommon_subtitleparams_count(dup(&h), &mut c), OAKCOMMON_OK);
	assert_eq!(c, 1);
	assert_eq!(oakcommon_subtitleparams_get_subtitle(dup(&h), 0, &mut n, &mut d, &mut si, &mut en), OAKCOMMON_OK);
	assert_eq!((n, d), (3, 2));
	assert_eq!((si, en), (5, 1));
}
