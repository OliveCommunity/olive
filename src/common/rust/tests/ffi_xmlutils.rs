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

//! FFI-level integration tests for the C-ABI XML streaming exports in
//! `oakcommon::ffi::xmlutils`, asserted against the C++ oracle
//! `src/common/c_api/xmlutils.cpp`.
//!
//! `ffi::error` and `ffi::error_abi` are documented placeholder modules
//! (`include/common/error.h` exposes no functions; the `OAKCOMMON_OK` /
//! `OAKCOMMON_E_*` constants live in `crate::error`), so the only callable
//! surface here is `xmlutils`. The error constants its exports return are
//! pinned in `error_constants_returned_by_xmlutils_match_header`; the full
//! set is already asserted in `contract.rs`.
//!
//! The contract under test (each point matches the C++ oracle):
//! - exports take a `CHandle` by value and never release it;
//! - two-stage string getters return the required size (NUL included) and
//!   only copy when the buffer is large enough — they never truncate;
//! - out-of-range attribute indexes report `OAKCOMMON_E_NOT_FOUND`;
//! - a malformed document still yields a usable handle whose `has_error`
//!   flag reads back 1 and whose navigation reports no start elements.

use std::ffi::{c_char, CString};

use oakcommon::error::{OAKCOMMON_E_INVALID, OAKCOMMON_E_NOT_FOUND, OAKCOMMON_OK};
use oakcommon::ffi::xmlutils::*;
use oakcommon::handle::{CHandle, OAKCOMMON_ABI_VERSION};

/// Shared sample document: two attributes on the root plus a nested text
/// element. The writer test sequence reproduces this exact document.
const DOC: &str = r#"<root a="1" b="two"><child>text here</child></root>"#;

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

// ---- Reader: handle lifecycle ----

/// `init` with a null data pointer yields an empty handle.
#[test]
fn init_returns_null_handle_for_null_data() {
	let h = oakcommon_xml_reader_init(std::ptr::null());
	assert!(h.is_null());
	assert!(h.ctx.is_null());
	assert!(h.addref.is_none());
	assert!(h.release.is_none());
}

/// `init` over valid data yields a stamped, non-empty handle.
#[test]
fn init_creates_stamped_handle() {
	let h = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	assert!(!h.is_null());
	assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	assert!(h.addref.is_some());
	assert!(h.release.is_some());
}

/// `free` nullifies the handle, is idempotent, and tolerates a null
/// pointer.
#[test]
fn free_nullifies_and_is_idempotent() {
	let mut h = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	assert!(!h.is_null());
	oakcommon_xml_reader_free(&mut h);
	assert!(h.is_null());
	// A second free of the now-empty handle is safe.
	oakcommon_xml_reader_free(&mut h);
	assert!(h.is_null());
	// Freeing a null pointer is safe.
	oakcommon_xml_reader_free(std::ptr::null_mut());
}

// ---- Reader: navigation ----

/// `read_next_start_element` writes 1 while a start element is found and
/// 0 once the document is exhausted (CPP-PARITY: an end element and the
/// end of the document both report 0, not an error).
#[test]
fn read_next_start_element_reports_found() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut found = -1i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1); // root
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1); // child
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 0); // child's end element
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 0); // root's end element
}

/// A null reader handle or a null `found` out-param is `E_INVALID`.
#[test]
fn read_next_start_element_rejects_null_args() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(CHandle::null(), &mut found),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

// ---- Reader: two-stage string getters ----

/// `name` is a two-stage getter over the current element's name; a null
/// handle is `E_INVALID`.
#[test]
fn name_two_stage_getter() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1);
	assert_two_stage_getter(|buf, size| oakcommon_xml_reader_name(dup(&r), buf, size), "root");
	assert_eq!(
		oakcommon_xml_reader_name(CHandle::null(), std::ptr::null_mut(), 0),
		OAKCOMMON_E_INVALID
	);
}

/// Before any token is read the name is the empty string (size 1).
#[test]
fn name_is_empty_before_any_read() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	assert_eq!(oakcommon_xml_reader_name(dup(&r), std::ptr::null_mut(), 0), 1);
}

/// `read_element_text` is a two-stage getter and caches its result, so a
/// second read returns the same text even though the stream was consumed
/// by the first (CPP-PARITY with the C++ `XmlReaderState::cached_text`).
#[test]
fn read_element_text_two_stage_with_cache() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1);
	assert_two_stage_getter(
		|buf, size| oakcommon_xml_reader_read_element_text(dup(&r), buf, size),
		"text here",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_xml_reader_read_element_text(dup(&r), buf, size),
		"text here",
	);
}

/// When not on a start element the text is empty (CPP-PARITY: the C++
/// reader returns an empty string).
#[test]
fn read_element_text_not_on_start_element_is_empty() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 0);
	assert_eq!(
		oakcommon_xml_reader_read_element_text(dup(&r), std::ptr::null_mut(), 0),
		1
	);
	assert_eq!(
		oakcommon_xml_reader_read_element_text(CHandle::null(), std::ptr::null_mut(), 0),
		OAKCOMMON_E_INVALID
	);
}

/// `skip_current_element` consumes the current element and its subtree; a
/// null handle is `E_INVALID`.
#[test]
fn skip_current_element_skips_subtree() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1);
	assert_eq!(oakcommon_xml_reader_skip_current_element(dup(&r)), OAKCOMMON_OK);
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 0);
	assert_eq!(
		oakcommon_xml_reader_skip_current_element(CHandle::null()),
		OAKCOMMON_E_INVALID
	);
}

// ---- Reader: attributes ----

/// `attribute_count` reports 0 before any token and the real count on a
/// start element; a null handle or a null count out-param is `E_INVALID`.
#[test]
fn attribute_count_reports_attributes() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut count = -1i32;
	assert_eq!(
		oakcommon_xml_reader_attribute_count(dup(&r), &mut count),
		OAKCOMMON_OK
	);
	assert_eq!(count, 0);
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1);
	assert_eq!(
		oakcommon_xml_reader_attribute_count(dup(&r), &mut count),
		OAKCOMMON_OK
	);
	assert_eq!(count, 2);
	assert_eq!(
		oakcommon_xml_reader_attribute_count(CHandle::null(), &mut count),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_reader_attribute_count(dup(&r), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

/// `attribute_name` / `attribute_value` are two-stage getters over the
/// attributes of the current start element, in document order.
#[test]
fn attribute_name_and_value_two_stage() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1);
	assert_two_stage_getter(
		|buf, size| oakcommon_xml_reader_attribute_name(dup(&r), 0, buf, size),
		"a",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_xml_reader_attribute_value(dup(&r), 0, buf, size),
		"1",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_xml_reader_attribute_name(dup(&r), 1, buf, size),
		"b",
	);
	assert_two_stage_getter(
		|buf, size| oakcommon_xml_reader_attribute_value(dup(&r), 1, buf, size),
		"two",
	);
}

/// An out-of-range attribute index is `E_NOT_FOUND` (CPP-PARITY), while a
/// null handle is `E_INVALID`.
#[test]
fn attribute_out_of_range_is_not_found() {
	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1);
	assert_eq!(
		oakcommon_xml_reader_attribute_name(dup(&r), 2, std::ptr::null_mut(), 0),
		OAKCOMMON_E_NOT_FOUND
	);
	assert_eq!(
		oakcommon_xml_reader_attribute_name(dup(&r), -1, std::ptr::null_mut(), 0),
		OAKCOMMON_E_NOT_FOUND
	);
	assert_eq!(
		oakcommon_xml_reader_attribute_value(dup(&r), 2, std::ptr::null_mut(), 0),
		OAKCOMMON_E_NOT_FOUND
	);
	assert_eq!(
		oakcommon_xml_reader_attribute_name(CHandle::null(), 0, std::ptr::null_mut(), 0),
		OAKCOMMON_E_INVALID
	);
}

// ---- Reader: error reporting ----

/// `has_error` is 0 for a well-formed document and 1 for a malformed one;
/// a malformed document still navigates safely and reports no start
/// elements (CPP-PARITY: parse failures surface lazily through
/// `has_error`, never at init time).
#[test]
fn has_error_flags_malformed_documents() {
	let mut err = -1i32;

	let r = oakcommon_xml_reader_init(to_cstring(DOC).as_ptr());
	assert_eq!(oakcommon_xml_reader_has_error(dup(&r), &mut err), OAKCOMMON_OK);
	assert_eq!(err, 0);

	let bad = oakcommon_xml_reader_init(to_cstring("<a></b>").as_ptr());
	assert!(!bad.is_null());
	assert_eq!(oakcommon_xml_reader_has_error(dup(&bad), &mut err), OAKCOMMON_OK);
	assert_eq!(err, 1);
	let mut found = -1i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&bad), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 0);

	let empty = oakcommon_xml_reader_init(to_cstring("").as_ptr());
	assert!(!empty.is_null());
	assert_eq!(oakcommon_xml_reader_has_error(dup(&empty), &mut err), OAKCOMMON_OK);
	assert_eq!(err, 1);

	assert_eq!(
		oakcommon_xml_reader_has_error(CHandle::null(), &mut err),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_reader_has_error(dup(&r), std::ptr::null_mut()),
		OAKCOMMON_E_INVALID
	);
}

// ---- Writer: handle lifecycle ----

/// `writer_init` yields a stamped handle; `free` nullifies it, is
/// idempotent, and tolerates a null pointer.
#[test]
fn writer_init_free_and_idempotent() {
	let mut w = oakcommon_xml_writer_init();
	assert!(!w.is_null());
	assert_eq!(w.abi_version, OAKCOMMON_ABI_VERSION);
	assert!(w.addref.is_some());
	assert!(w.release.is_some());
	oakcommon_xml_writer_free(&mut w);
	assert!(w.is_null());
	oakcommon_xml_writer_free(&mut w);
	assert!(w.is_null());
	oakcommon_xml_writer_free(std::ptr::null_mut());
}

// ---- Writer: operations ----

/// Every writer export rejects a null handle with `E_INVALID`, and the
/// string-taking exports reject null strings.
#[test]
fn writer_rejects_null_arguments() {
	let w = oakcommon_xml_writer_init();
	assert_eq!(
		oakcommon_xml_writer_write_start_element(CHandle::null(), to_cstring("a").as_ptr()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(
			CHandle::null(),
			to_cstring("a").as_ptr(),
			to_cstring("b").as_ptr(),
		),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_characters(CHandle::null(), to_cstring("x").as_ptr()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_text_element(
			CHandle::null(),
			to_cstring("a").as_ptr(),
			to_cstring("b").as_ptr(),
		),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_end_element(CHandle::null()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_end_document(CHandle::null()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_output(CHandle::null(), std::ptr::null_mut(), 0),
		OAKCOMMON_E_INVALID
	);

	assert_eq!(
		oakcommon_xml_writer_write_start_element(dup(&w), std::ptr::null()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(dup(&w), std::ptr::null(), to_cstring("b").as_ptr()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(dup(&w), to_cstring("a").as_ptr(), std::ptr::null()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_characters(dup(&w), std::ptr::null()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_text_element(dup(&w), std::ptr::null(), to_cstring("b").as_ptr()),
		OAKCOMMON_E_INVALID
	);
	assert_eq!(
		oakcommon_xml_writer_write_text_element(dup(&w), to_cstring("a").as_ptr(), std::ptr::null()),
		OAKCOMMON_E_INVALID
	);
}

/// The writer sequence builds the exact sample document, and `output` is a
/// two-stage getter over it.
#[test]
fn writer_builds_document_and_output_two_stage() {
	let w = oakcommon_xml_writer_init();
	assert_eq!(
		oakcommon_xml_writer_write_start_element(dup(&w), to_cstring("root").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(dup(&w), to_cstring("a").as_ptr(), to_cstring("1").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(dup(&w), to_cstring("b").as_ptr(), to_cstring("two").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_text_element(
			dup(&w),
			to_cstring("child").as_ptr(),
			to_cstring("text here").as_ptr(),
		),
		OAKCOMMON_OK
	);
	assert_eq!(oakcommon_xml_writer_write_end_element(dup(&w)), OAKCOMMON_OK);
	assert_eq!(oakcommon_xml_writer_write_end_document(dup(&w)), OAKCOMMON_OK);
	assert_two_stage_getter(|buf, size| oakcommon_xml_writer_output(dup(&w), buf, size), DOC);
}

/// The writer's output round-trips through the reader.
#[test]
fn writer_output_round_trips_through_reader() {
	let w = oakcommon_xml_writer_init();
	assert_eq!(
		oakcommon_xml_writer_write_start_element(dup(&w), to_cstring("root").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(dup(&w), to_cstring("a").as_ptr(), to_cstring("1").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(dup(&w), to_cstring("b").as_ptr(), to_cstring("two").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_text_element(
			dup(&w),
			to_cstring("child").as_ptr(),
			to_cstring("text here").as_ptr(),
		),
		OAKCOMMON_OK
	);
	assert_eq!(oakcommon_xml_writer_write_end_element(dup(&w)), OAKCOMMON_OK);

	let mut out = vec![0u8; 64];
	let needed = oakcommon_xml_writer_output(dup(&w), out.as_mut_ptr() as *mut c_char, 64);
	assert_eq!(needed, (DOC.len() + 1) as i32);
	assert_eq!(&out[..DOC.len()], DOC.as_bytes());
	assert_eq!(out[DOC.len()], 0);

	let r = oakcommon_xml_reader_init(out.as_ptr() as *const c_char);
	assert!(!r.is_null());
	let mut found = 0i32;
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1);
	assert_two_stage_getter(|buf, size| oakcommon_xml_reader_name(dup(&r), buf, size), "root");
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 1);
	assert_two_stage_getter(
		|buf, size| oakcommon_xml_reader_read_element_text(dup(&r), buf, size),
		"text here",
	);
	assert_eq!(
		oakcommon_xml_reader_read_next_start_element(dup(&r), &mut found),
		OAKCOMMON_OK
	);
	assert_eq!(found, 0);
}

/// Documented no-op writer operations (attribute without an open start
/// tag, end element on an empty stack, end document with nothing open)
/// still return `OK`.
#[test]
fn writer_noop_operations_return_ok() {
	let w = oakcommon_xml_writer_init();
	assert_eq!(
		oakcommon_xml_writer_write_attribute(dup(&w), to_cstring("a").as_ptr(), to_cstring("b").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(oakcommon_xml_writer_write_end_element(dup(&w)), OAKCOMMON_OK);
	assert_eq!(oakcommon_xml_writer_write_end_document(dup(&w)), OAKCOMMON_OK);
	assert_eq!(
		oakcommon_xml_writer_write_characters(dup(&w), to_cstring("x").as_ptr()),
		OAKCOMMON_OK
	);
	assert_two_stage_getter(|buf, size| oakcommon_xml_writer_output(dup(&w), buf, size), "x");
}

/// An empty element with attributes serializes as `<a k="v"/>`.
#[test]
fn writer_self_closing_empty_element() {
	let w = oakcommon_xml_writer_init();
	assert_eq!(
		oakcommon_xml_writer_write_start_element(dup(&w), to_cstring("a").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(dup(&w), to_cstring("k").as_ptr(), to_cstring("v").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(oakcommon_xml_writer_write_end_element(dup(&w)), OAKCOMMON_OK);
	assert_two_stage_getter(|buf, size| oakcommon_xml_writer_output(dup(&w), buf, size), r#"<a k="v"/>"#);
}

/// Text and attribute values are escaped for the five predefined XML
/// entities.
#[test]
fn writer_escapes_text_and_attributes() {
	let w = oakcommon_xml_writer_init();
	assert_eq!(
		oakcommon_xml_writer_write_text_element(
			dup(&w),
			to_cstring("e").as_ptr(),
			to_cstring("hi & bye <there>").as_ptr(),
		),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_start_element(dup(&w), to_cstring("a").as_ptr()),
		OAKCOMMON_OK
	);
	assert_eq!(
		oakcommon_xml_writer_write_attribute(
			dup(&w),
			to_cstring("q").as_ptr(),
			to_cstring("x\"y&z").as_ptr(),
		),
		OAKCOMMON_OK
	);
	assert_eq!(oakcommon_xml_writer_write_end_element(dup(&w)), OAKCOMMON_OK);
	assert_two_stage_getter(
		|buf, size| oakcommon_xml_writer_output(dup(&w), buf, size),
		r#"<e>hi &amp; bye &lt;there&gt;</e><a q="x&quot;y&amp;z"/>"#,
	);
}

// ---- error / error_abi ----

/// The constants the xmlutils exports return are defined in `crate::error`
/// and match `include/common/error.h` (the `ffi::error` / `ffi::error_abi`
/// modules only point here). The full set is pinned in `contract.rs`.
#[test]
fn error_constants_returned_by_xmlutils_match_header() {
	assert_eq!(OAKCOMMON_OK, 0);
	assert_eq!(OAKCOMMON_E_INVALID, -10001);
	assert_eq!(OAKCOMMON_E_NOT_FOUND, -10004);
}
