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

//! Integration tests for the C ABI surface of
//! `oakcommon::ffi::commandlineparser`, which implements
//! `include/common/commandlineparser.h` (oracle: `src/common/c_api/commandlineparser.cpp`).
//!
//! Every exported function gets a success-path test and a failure-path test.
//! The failure paths cover the empty (NULL-ctx) handle plus the documented
//! invalid-argument codes; the string getters are TRUNCATING (see the C++
//! `copy_setting`), so all three two-stage buffer cases are exercised.
//!
//! These tests are pure: they create their own parser/option/argument
//! handles and touch no config files, so they need no `OAK_CONFIG_DIR`
//! locking and run independently of the config tests.

use std::ffi::{c_char, CStr, CString};
use std::ptr::null_mut;

use oakcommon::error::{OAKCOMMON_E_INVALID, OAKCOMMON_OK};
use oakcommon::ffi::commandlineparser::*;
use oakcommon::handle::{CHandle, OAKCOMMON_ABI_VERSION};

/// A fresh parser handle (reference count 1, non-null ctx).
fn new_parser() -> CHandle {
	let h = oakcommon_commandlineparser_init();
	assert!(!h.ctx.is_null());
	h
}

/// Shallow-copy a handle, mirroring C's pass-by-value semantics. The FFI
/// exports take handles by value (a `CHandle` is a plain
/// `{ctx, addref, release, abi_version}` record), but the Rust type
/// deliberately does not implement `Copy`, so each call consumes its
/// argument. `dup` copies the four scalar fields; the exports never
/// release the handles they receive, so the reference count is untouched
/// and no `addref` is needed.
fn dup(h: &CHandle) -> CHandle {
	CHandle {
		ctx: h.ctx,
		addref: h.addref,
		release: h.release,
		abi_version: h.abi_version,
	}
}

/// Build a NUL-terminated C string.
fn c_str(s: &str) -> CString {
	CString::new(s).unwrap()
}

/// Build a `char *const *`-style array: returns the CStrings (kept alive
/// for the caller) plus their pointer array.
fn c_strings(values: &[&str]) -> (Vec<CString>, Vec<*const c_char>) {
	let cs: Vec<CString> = values.iter().map(|s| c_str(s)).collect();
	let ptrs = cs.iter().map(|s| s.as_ptr()).collect();
	(cs, ptrs)
}

/// Register a single `takes_arg` option and return its borrowed handle.
fn register_option(parser: CHandle, name: &str) -> CHandle {
	let mut out = CHandle::null();
	let (_names, ptrs) = c_strings(&[name]);
	let r = oakcommon_commandlineparser_add_option(
		parser,
		ptrs.as_ptr(),
		1,
		c_str("desc").as_ptr(),
		1,
		c_str("ARG").as_ptr(),
		0,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_OK);
	assert!(!out.ctx.is_null());
	out
}

/// Register a single positional argument and return its borrowed handle.
fn register_positional(parser: CHandle, name: &str) -> CHandle {
	let mut out = CHandle::null();
	let r = oakcommon_commandlineparser_add_positional_argument(
		parser,
		c_str(name).as_ptr(),
		c_str("desc").as_ptr(),
		1,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_OK);
	assert!(!out.ctx.is_null());
	out
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineparser_init
// ---------------------------------------------------------------------------

/// init returns a live handle with the standard vtable and ABI stamp, and
/// free() releases it cleanly. There is no testable failure path: the only
/// failure mode is allocation failure / panic inside `guard_handle`, both
/// of which return a NULL-ctx handle and cannot be triggered deterministically.
#[test]
fn init_returns_live_handle() {
	let mut h = oakcommon_commandlineparser_init();
	assert!(!h.ctx.is_null());
	assert!(h.addref.is_some());
	assert!(h.release.is_some());
	assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	oakcommon_commandlineparser_free(&mut h);
	assert!(h.ctx.is_null());
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineparser_free
// ---------------------------------------------------------------------------

#[test]
fn parser_free_null_pointer_is_noop() {
	oakcommon_commandlineparser_free(null_mut());
}

#[test]
fn parser_free_twice_is_safe_and_nulls_ctx() {
	let mut h = oakcommon_commandlineparser_init();
	assert!(!h.ctx.is_null());
	oakcommon_commandlineparser_free(&mut h);
	assert!(h.ctx.is_null());
	// Second free on the same (now empty) handle must be a no-op.
	oakcommon_commandlineparser_free(&mut h);
	assert!(h.ctx.is_null());
}

#[test]
fn parser_free_null_handle_struct_is_noop() {
	let mut h = CHandle::null();
	oakcommon_commandlineparser_free(&mut h);
	assert!(h.ctx.is_null());
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineparser_set_app_info
// ---------------------------------------------------------------------------

#[test]
fn set_app_info_success() {
	let mut p = new_parser();
	let r = oakcommon_commandlineparser_set_app_info(
		dup(&p),
		c_str("myapp").as_ptr(),
		c_str("1.0").as_ptr(),
	);
	assert_eq!(r, OAKCOMMON_OK);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn set_app_info_empty_handle_is_invalid() {
	let e = CHandle::null();
	let r = oakcommon_commandlineparser_set_app_info(e, c_str("n").as_ptr(), c_str("v").as_ptr());
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn set_app_info_null_name_is_invalid() {
	let mut p = new_parser();
	let r = oakcommon_commandlineparser_set_app_info(dup(&p), null_mut(), c_str("v").as_ptr());
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn set_app_info_null_version_is_invalid() {
	let mut p = new_parser();
	// CPP-PARITY: the C++ oracle tolerates a NULL version (treated as ""),
	// but the Rust export rejects it with E_INVALID.
	let r = oakcommon_commandlineparser_set_app_info(dup(&p), c_str("n").as_ptr(), null_mut());
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineparser_add_option
// ---------------------------------------------------------------------------

#[test]
fn add_option_success() {
	let mut p = new_parser();
	let mut out = CHandle::null();
	let (_names, ptrs) = c_strings(&["o", "output"]);
	let r = oakcommon_commandlineparser_add_option(
		dup(&p),
		ptrs.as_ptr(),
		2,
		c_str("Output file.").as_ptr(),
		1,
		c_str("FILE").as_ptr(),
		0,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_OK);
	assert!(!out.ctx.is_null());
	assert!(out.addref.is_some());
	assert!(out.release.is_some());
	oakcommon_commandlineoption_free(&mut out);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_option_empty_parser_is_invalid() {
	let e = CHandle::null();
	let mut out = CHandle::null();
	let (_names, ptrs) = c_strings(&["o"]);
	let r = oakcommon_commandlineparser_add_option(
		e,
		ptrs.as_ptr(),
		1,
		c_str("d").as_ptr(),
		0,
		c_str("A").as_ptr(),
		0,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn add_option_null_names_is_invalid() {
	let mut p = new_parser();
	let mut out = CHandle::null();
	let r = oakcommon_commandlineparser_add_option(
		dup(&p),
		null_mut(),
		1,
		c_str("d").as_ptr(),
		0,
		c_str("A").as_ptr(),
		0,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_option_non_positive_name_count_is_invalid() {
	let mut p = new_parser();
	let mut out = CHandle::null();
	let (_names, ptrs) = c_strings(&["o"]);
	let r0 = oakcommon_commandlineparser_add_option(
		dup(&p),
		ptrs.as_ptr(),
		0,
		c_str("d").as_ptr(),
		0,
		c_str("A").as_ptr(),
		0,
		&mut out,
	);
	assert_eq!(r0, OAKCOMMON_E_INVALID);
	let rn = oakcommon_commandlineparser_add_option(
		dup(&p),
		ptrs.as_ptr(),
		-1,
		c_str("d").as_ptr(),
		0,
		c_str("A").as_ptr(),
		0,
		&mut out,
	);
	assert_eq!(rn, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_option_null_description_is_invalid() {
	// CPP-PARITY: the header documents description as "may be NULL" (C++ maps
	// it to ""), but the Rust export rejects a NULL description with E_INVALID.
	let mut p = new_parser();
	let mut out = CHandle::null();
	let (_names, ptrs) = c_strings(&["o"]);
	let r = oakcommon_commandlineparser_add_option(
		dup(&p),
		ptrs.as_ptr(),
		1,
		null_mut(),
		0,
		c_str("A").as_ptr(),
		0,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_option_null_arg_placeholder_is_invalid() {
	// CPP-PARITY: the header documents arg_placeholder as "may be NULL" (C++
	// maps it to ""), but the Rust export rejects it with E_INVALID.
	let mut p = new_parser();
	let mut out = CHandle::null();
	let (_names, ptrs) = c_strings(&["o"]);
	let r = oakcommon_commandlineparser_add_option(
		dup(&p),
		ptrs.as_ptr(),
		1,
		c_str("d").as_ptr(),
		0,
		null_mut(),
		0,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_option_null_out_option_is_invalid() {
	// CPP-PARITY: the header says out_option "may be NULL if unused"; the Rust
	// export requires it.
	let mut p = new_parser();
	let (_names, ptrs) = c_strings(&["o"]);
	let r = oakcommon_commandlineparser_add_option(
		dup(&p),
		ptrs.as_ptr(),
		1,
		c_str("d").as_ptr(),
		0,
		c_str("A").as_ptr(),
		0,
		null_mut(),
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_option_non_utf8_name_is_invalid() {
	let mut p = new_parser();
	let mut out = CHandle::null();
	// 0xFF is not valid UTF-8; the export rejects it with E_INVALID before
	// the option is registered.
	let bad = CString::new(vec![b'x', 0xFF]).unwrap();
	let ptrs = [bad.as_ptr()];
	let r = oakcommon_commandlineparser_add_option(
		dup(&p),
		ptrs.as_ptr(),
		1,
		c_str("d").as_ptr(),
		0,
		c_str("A").as_ptr(),
		0,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	// The failed registration must not leave a partial option behind: a
	// subsequent valid registration still works and lands at index 0.
	let mut out2 = CHandle::null();
	let (_names, ptrs2) = c_strings(&["ok"]);
	let r2 = oakcommon_commandlineparser_add_option(
		dup(&p),
		ptrs2.as_ptr(),
		1,
		c_str("d").as_ptr(),
		0,
		c_str("A").as_ptr(),
		0,
		&mut out2,
	);
	assert_eq!(r2, OAKCOMMON_OK);
	assert!(!out2.ctx.is_null());
	oakcommon_commandlineoption_free(&mut out2);
	oakcommon_commandlineparser_free(&mut p);
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineparser_add_positional_argument
// ---------------------------------------------------------------------------

#[test]
fn add_positional_argument_success() {
	let mut p = new_parser();
	let mut out = CHandle::null();
	let r = oakcommon_commandlineparser_add_positional_argument(
		dup(&p),
		c_str("input").as_ptr(),
		c_str("Input file").as_ptr(),
		1,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_OK);
	assert!(!out.ctx.is_null());
	assert!(out.release.is_some());
	oakcommon_commandlinepositionalargument_free(&mut out);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_positional_argument_empty_parser_is_invalid() {
	let e = CHandle::null();
	let mut out = CHandle::null();
	let r = oakcommon_commandlineparser_add_positional_argument(
		e,
		c_str("in").as_ptr(),
		c_str("d").as_ptr(),
		1,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn add_positional_argument_null_name_is_invalid() {
	let mut p = new_parser();
	let mut out = CHandle::null();
	let r = oakcommon_commandlineparser_add_positional_argument(
		dup(&p),
		null_mut(),
		c_str("d").as_ptr(),
		1,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_positional_argument_null_description_is_invalid() {
	// CPP-PARITY: C++ maps a NULL description to ""; the Rust export rejects
	// it with E_INVALID.
	let mut p = new_parser();
	let mut out = CHandle::null();
	let r = oakcommon_commandlineparser_add_positional_argument(
		dup(&p),
		c_str("in").as_ptr(),
		null_mut(),
		1,
		&mut out,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn add_positional_argument_null_out_is_invalid() {
	// CPP-PARITY: C++ allows a NULL out_argument; the Rust export requires it.
	let mut p = new_parser();
	let r = oakcommon_commandlineparser_add_positional_argument(
		dup(&p),
		c_str("in").as_ptr(),
		c_str("d").as_ptr(),
		1,
		null_mut(),
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineparser_process
// ---------------------------------------------------------------------------

#[test]
fn process_success() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	let mut pos = register_positional(dup(&p), "input");
	let (_argv, argv_ptrs) = c_strings(&["prog", "-o", "out.mov", "in.mp4"]);
	let r = oakcommon_commandlineparser_process(dup(&p), argv_ptrs.as_ptr(), 4);
	assert_eq!(r, OAKCOMMON_OK);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn process_argc_zero_success() {
	let mut p = new_parser();
	// Non-null argv with argc == 0: the loop never runs, returns OK.
	let (_argv, argv_ptrs) = c_strings(&["prog"]);
	let r = oakcommon_commandlineparser_process(dup(&p), argv_ptrs.as_ptr(), 0);
	assert_eq!(r, OAKCOMMON_OK);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn process_empty_parser_is_invalid() {
	let e = CHandle::null();
	let (_argv, argv_ptrs) = c_strings(&["prog"]);
	let r = oakcommon_commandlineparser_process(e, argv_ptrs.as_ptr(), 1);
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn process_null_argv_is_invalid() {
	let mut p = new_parser();
	let r = oakcommon_commandlineparser_process(dup(&p), null_mut(), 1);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn process_negative_argc_is_invalid() {
	let mut p = new_parser();
	let (_argv, argv_ptrs) = c_strings(&["prog"]);
	let r = oakcommon_commandlineparser_process(dup(&p), argv_ptrs.as_ptr(), -1);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineparser_print_help
// ---------------------------------------------------------------------------

#[test]
fn print_help_success() {
	let mut p = new_parser();
	let r = oakcommon_commandlineparser_print_help(dup(&p), c_str("oak").as_ptr());
	assert_eq!(r, OAKCOMMON_OK);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn print_help_empty_parser_is_invalid() {
	let e = CHandle::null();
	let r = oakcommon_commandlineparser_print_help(e, c_str("oak").as_ptr());
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn print_help_null_filename_is_invalid() {
	let mut p = new_parser();
	let r = oakcommon_commandlineparser_print_help(dup(&p), null_mut());
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineparser_free(&mut p);
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineoption_is_set / _free
// ---------------------------------------------------------------------------

#[test]
fn option_is_set_success() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	let mut is_set = true;
	let r = oakcommon_commandlineoption_is_set(dup(&opt), &mut is_set);
	assert_eq!(r, OAKCOMMON_OK);
	// CPP-PARITY: the Rust borrowed handle is a registration-time snapshot
	// (make_borrowed bit-copies), so is_set is always false on the handle;
	// the C++ oracle stores a pointer to the live option and would report the
	// parser's real state.
	assert!(!is_set);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_is_set_empty_handle_is_invalid() {
	let e = CHandle::null();
	let mut is_set = true;
	let r = oakcommon_commandlineoption_is_set(e, &mut is_set);
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn option_is_set_null_out_is_invalid() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	let r = oakcommon_commandlineoption_is_set(dup(&opt), null_mut());
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_free_null_pointer_is_noop() {
	oakcommon_commandlineoption_free(null_mut());
}

#[test]
fn option_free_twice_is_safe_and_nulls_ctx() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	assert!(!opt.ctx.is_null());
	oakcommon_commandlineoption_free(&mut opt);
	assert!(opt.ctx.is_null());
	// Second free on the same (now empty) handle must be a no-op.
	oakcommon_commandlineoption_free(&mut opt);
	assert!(opt.ctx.is_null());
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_free_null_handle_struct_is_noop() {
	let mut h = CHandle::null();
	oakcommon_commandlineoption_free(&mut h);
	assert!(h.ctx.is_null());
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineoption_get_setting (TRUNCATING two-stage getter)
// ---------------------------------------------------------------------------

#[test]
fn option_get_setting_size_query() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	assert_eq!(
		oakcommon_commandlineoption_set_setting(dup(&opt), c_str("abcdef").as_ptr()),
		OAKCOMMON_OK
	);
	// Stage 1: null buffer, size 0 -> required size len+1, nothing written.
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), null_mut(), 0);
	assert_eq!(r, 7); // len("abcdef") + NUL
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_get_setting_short_buffer_truncates_and_returns_required() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	assert_eq!(
		oakcommon_commandlineoption_set_setting(dup(&opt), c_str("abcdef").as_ptr()),
		OAKCOMMON_OK
	);
	// Stage 2: buf_size 4 < 7 -> writes "abc" + NUL, still returns 7.
	let mut buf = [0xFFu8; 4];
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), buf.as_mut_ptr() as *mut c_char, 4);
	assert_eq!(r, 7);
	assert_eq!(&buf[..3], b"abc");
	assert_eq!(buf[3], 0);
	// buf_size 1 -> writes only the NUL terminator.
	let mut tiny = [0xFFu8; 1];
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), tiny.as_mut_ptr() as *mut c_char, 1);
	assert_eq!(r, 7);
	assert_eq!(tiny[0], 0);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_get_setting_exact_fit_writes_full_string() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	assert_eq!(
		oakcommon_commandlineoption_set_setting(dup(&opt), c_str("abcdef").as_ptr()),
		OAKCOMMON_OK
	);
	// Stage 3: buf_size == len+1 -> full string plus NUL, returns len+1.
	let mut buf = [0xFFu8; 7];
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), buf.as_mut_ptr() as *mut c_char, 7);
	assert_eq!(r, 7);
	assert_eq!(buf[6], 0);
	let s = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) };
	assert_eq!(s.to_str().unwrap(), "abcdef");
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_get_setting_non_null_buf_size_zero_writes_nothing() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	assert_eq!(
		oakcommon_commandlineoption_set_setting(dup(&opt), c_str("abcdef").as_ptr()),
		OAKCOMMON_OK
	);
	// A non-null buffer with size 0 is a valid size query: required returned,
	// buffer untouched.
	let mut buf = [0xFFu8; 8];
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), buf.as_mut_ptr() as *mut c_char, 0);
	assert_eq!(r, 7);
	assert_eq!(buf, [0xFFu8; 8]);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_get_setting_unset_returns_empty_string() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	// Never set: get_setting returns the empty string (required size 1).
	let mut buf = [0xFFu8; 1];
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), buf.as_mut_ptr() as *mut c_char, 1);
	assert_eq!(r, 1);
	assert_eq!(buf[0], 0);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_get_setting_empty_handle_is_invalid() {
	let e = CHandle::null();
	let r = oakcommon_commandlineoption_get_setting(e, null_mut(), 0);
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn option_get_setting_negative_buf_size_is_invalid() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	let mut buf = [0u8; 4];
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), buf.as_mut_ptr() as *mut c_char, -1);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	let r2 = oakcommon_commandlineoption_get_setting(dup(&opt), null_mut(), -1);
	assert_eq!(r2, OAKCOMMON_E_INVALID);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_get_setting_null_buf_with_size_is_invalid() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), null_mut(), 1);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

// ---------------------------------------------------------------------------
// oakcommon_commandlineoption_set_setting
// ---------------------------------------------------------------------------

#[test]
fn option_set_setting_success() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	let r = oakcommon_commandlineoption_set_setting(dup(&opt), c_str("value").as_ptr());
	assert_eq!(r, OAKCOMMON_OK);
	// Round-trip through the two-stage getter.
	let mut buf = vec![0u8; 6];
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), buf.as_mut_ptr() as *mut c_char, 6);
	assert_eq!(r, 6);
	let s = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) };
	assert_eq!(s.to_str().unwrap(), "value");
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn option_set_setting_empty_handle_is_invalid() {
	let e = CHandle::null();
	let r = oakcommon_commandlineoption_set_setting(e, c_str("v").as_ptr());
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn option_set_setting_null_value_is_invalid() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	let r = oakcommon_commandlineoption_set_setting(dup(&opt), null_mut());
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

/// CPP-PARITY: `process()` mutates the parser-owned option; the option handle
/// returned by `add_option` is a registration-time snapshot in Rust (C++
/// holds a pointer to the live option), so the handle does not observe the
/// parse: is_set stays false and get_setting stays empty.
#[test]
fn option_handle_does_not_observe_process() {
	let mut p = new_parser();
	let mut opt = register_option(dup(&p), "o");
	let (_argv, argv_ptrs) = c_strings(&["prog", "-o", "out.mov"]);
	let r = oakcommon_commandlineparser_process(dup(&p), argv_ptrs.as_ptr(), 3);
	assert_eq!(r, OAKCOMMON_OK);
	// The snapshot handle still reports the empty setting.
	let r = oakcommon_commandlineoption_get_setting(dup(&opt), null_mut(), 0);
	assert_eq!(r, 1); // empty string
	let mut is_set = true;
	assert_eq!(
		oakcommon_commandlineoption_is_set(dup(&opt), &mut is_set),
		OAKCOMMON_OK
	);
	assert!(!is_set);
	oakcommon_commandlineoption_free(&mut opt);
	oakcommon_commandlineparser_free(&mut p);
}

// ---------------------------------------------------------------------------
// oakcommon_commandlinepositionalargument_get_setting (TRUNCATING)
// ---------------------------------------------------------------------------

#[test]
fn positional_get_setting_size_query() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	assert_eq!(
		oakcommon_commandlinepositionalargument_set_setting(dup(&pos), c_str("abcdef").as_ptr()),
		OAKCOMMON_OK
	);
	let r = oakcommon_commandlinepositionalargument_get_setting(dup(&pos), null_mut(), 0);
	assert_eq!(r, 7); // len("abcdef") + NUL
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_get_setting_short_buffer_truncates_and_returns_required() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	assert_eq!(
		oakcommon_commandlinepositionalargument_set_setting(dup(&pos), c_str("abcdef").as_ptr()),
		OAKCOMMON_OK
	);
	let mut buf = [0xFFu8; 4];
	let r = oakcommon_commandlinepositionalargument_get_setting(
		dup(&pos),
		buf.as_mut_ptr() as *mut c_char,
		4,
	);
	assert_eq!(r, 7);
	assert_eq!(&buf[..3], b"abc");
	assert_eq!(buf[3], 0);
	let mut tiny = [0xFFu8; 1];
	let r = oakcommon_commandlinepositionalargument_get_setting(
		dup(&pos),
		tiny.as_mut_ptr() as *mut c_char,
		1,
	);
	assert_eq!(r, 7);
	assert_eq!(tiny[0], 0);
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_get_setting_exact_fit_writes_full_string() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	assert_eq!(
		oakcommon_commandlinepositionalargument_set_setting(dup(&pos), c_str("abcdef").as_ptr()),
		OAKCOMMON_OK
	);
	let mut buf = [0xFFu8; 7];
	let r = oakcommon_commandlinepositionalargument_get_setting(
		dup(&pos),
		buf.as_mut_ptr() as *mut c_char,
		7,
	);
	assert_eq!(r, 7);
	assert_eq!(buf[6], 0);
	let s = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) };
	assert_eq!(s.to_str().unwrap(), "abcdef");
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_get_setting_non_null_buf_size_zero_writes_nothing() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	assert_eq!(
		oakcommon_commandlinepositionalargument_set_setting(dup(&pos), c_str("abcdef").as_ptr()),
		OAKCOMMON_OK
	);
	let mut buf = [0xFFu8; 8];
	let r = oakcommon_commandlinepositionalargument_get_setting(
		dup(&pos),
		buf.as_mut_ptr() as *mut c_char,
		0,
	);
	assert_eq!(r, 7);
	assert_eq!(buf, [0xFFu8; 8]);
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_get_setting_unset_returns_empty_string() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	let mut buf = [0xFFu8; 1];
	let r = oakcommon_commandlinepositionalargument_get_setting(
		dup(&pos),
		buf.as_mut_ptr() as *mut c_char,
		1,
	);
	assert_eq!(r, 1);
	assert_eq!(buf[0], 0);
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_get_setting_empty_handle_is_invalid() {
	let e = CHandle::null();
	let r = oakcommon_commandlinepositionalargument_get_setting(e, null_mut(), 0);
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn positional_get_setting_negative_buf_size_is_invalid() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	let mut buf = [0u8; 4];
	let r = oakcommon_commandlinepositionalargument_get_setting(
		dup(&pos),
		buf.as_mut_ptr() as *mut c_char,
		-1,
	);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	let r2 = oakcommon_commandlinepositionalargument_get_setting(dup(&pos), null_mut(), -1);
	assert_eq!(r2, OAKCOMMON_E_INVALID);
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_get_setting_null_buf_with_size_is_invalid() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	let r = oakcommon_commandlinepositionalargument_get_setting(dup(&pos), null_mut(), 1);
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

// ---------------------------------------------------------------------------
// oakcommon_commandlinepositionalargument_set_setting / _free
// ---------------------------------------------------------------------------

#[test]
fn positional_set_setting_success() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	let r =
		oakcommon_commandlinepositionalargument_set_setting(dup(&pos), c_str("file.mp4").as_ptr());
	assert_eq!(r, OAKCOMMON_OK);
	// Round-trip through the two-stage getter.
	let mut buf = vec![0u8; 9];
	let r = oakcommon_commandlinepositionalargument_get_setting(
		dup(&pos),
		buf.as_mut_ptr() as *mut c_char,
		9,
	);
	assert_eq!(r, 9);
	let s = unsafe { CStr::from_ptr(buf.as_ptr() as *const c_char) };
	assert_eq!(s.to_str().unwrap(), "file.mp4");
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_set_setting_empty_handle_is_invalid() {
	let e = CHandle::null();
	let r = oakcommon_commandlinepositionalargument_set_setting(e, c_str("v").as_ptr());
	assert_eq!(r, OAKCOMMON_E_INVALID);
}

#[test]
fn positional_set_setting_null_value_is_invalid() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	let r = oakcommon_commandlinepositionalargument_set_setting(dup(&pos), null_mut());
	assert_eq!(r, OAKCOMMON_E_INVALID);
	oakcommon_commandlinepositionalargument_free(&mut pos);
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_free_null_pointer_is_noop() {
	oakcommon_commandlinepositionalargument_free(null_mut());
}

#[test]
fn positional_free_twice_is_safe_and_nulls_ctx() {
	let mut p = new_parser();
	let mut pos = register_positional(dup(&p), "input");
	assert!(!pos.ctx.is_null());
	oakcommon_commandlinepositionalargument_free(&mut pos);
	assert!(pos.ctx.is_null());
	// Second free on the same (now empty) handle must be a no-op.
	oakcommon_commandlinepositionalargument_free(&mut pos);
	assert!(pos.ctx.is_null());
	oakcommon_commandlineparser_free(&mut p);
}

#[test]
fn positional_free_null_handle_struct_is_noop() {
	let mut h = CHandle::null();
	oakcommon_commandlinepositionalargument_free(&mut h);
	assert!(h.ctx.is_null());
}
