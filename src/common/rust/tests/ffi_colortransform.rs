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

//! FFI-level integration tests for the C-ABI `colortransform` exports in
//! `oakcommon::ffi::colortransform`, asserted against the C++ oracle
//! `src/common/c_api/colortransform.cpp` and the Rust domain
//! `src/common/rust/src/colortransform.rs`.
//!
//! The crate exports the two constructors, `free`, `is_display`, and the
//! four two-stage getters; only the C++-typed `init_from_native` /
//! `get_native` adapters stay with the C++ adapter layer. The tests
//! cover:
//! - constructor success: a stamped (`OAKCOMMON_ABI_VERSION`), non-null
//!   handle with live `addref`/`release` callbacks, whose boxed
//!   [`ColorTransform`] carries the right fields (peeked via
//!   `oakcommon::handle::get`);
//! - constructor failure: a null string argument yields a null handle
//!   (guard semantics: any error or panic also collapses to a null
//!   handle);
//! - the free contract (release once, nullify, no-op on null);
//! - `is_display` / `get_display` / `get_output` / `get_view` /
//!   `get_look` happy paths, two-stage short-buffer behavior, and
//!   empty-handle error codes;
//! - distinct handles plus the ownership contract (init yields reference
//!   count 1; addref/release adjust it atomically; both free cleanly).

use std::ffi::{c_char, CString};
use std::sync::atomic::Ordering;

use oakcommon::colortransform::ColorTransform;
use oakcommon::ffi::colortransform::*;
use oakcommon::handle::{get, RefBox, CHandle, OAKCOMMON_ABI_VERSION};

/// Convert a string slice to a NUL-terminated C string for FFI inputs.
fn to_cstring(s: &str) -> CString {
	CString::new(s).expect("test string must not contain NUL")
}

/// Cheap struct copy: `CHandle` is neither `Clone` nor `Copy`; rebuilding
/// from the same fields duplicates only the handle value — the box stays
/// alive as long as the original handle lives.
fn dup(h: &CHandle) -> CHandle {
	CHandle {
		ctx: h.ctx,
		addref: h.addref,
		release: h.release,
		abi_version: h.abi_version,
	}
}

/// Release one reference and nullify the handle. `oakcommon_colortransform_free`
/// is declared in the header but not exported by this crate, so the free
/// contract is driven through this exact replica of the crate-private
/// `free_handle` (and the C++ oracle): release once, write `CHandle::null()`.
fn free(h: &mut CHandle) {
	if let Some(rel) = h.release {
		unsafe {
			rel(h.ctx);
		}
	}
	*h = CHandle::null();
}

/// Pointer form of [`free`], mirroring `free_handle(*mut CHandle)`: a null
/// pointer is a no-op.
fn free_ptr(p: *mut CHandle) {
	if p.is_null() {
		return;
	}
	unsafe {
		if let Some(h) = p.as_ref() {
			if let Some(rel) = h.release {
				rel(h.ctx);
			}
		}
		p.write(CHandle::null());
	}
}

/// Read the reference count behind an owned handle (test-only peek into
/// the crate-public `RefBox` layout; the box is guaranteed alive here).
unsafe fn refs_of(h: &CHandle) -> u32 {
	unsafe { (*(h.ctx as *const RefBox<ColorTransform>)).refs.load(Ordering::Relaxed) }
}

/// A successful `init_output` yields a stamped, non-null handle whose boxed
/// transform is an output transform with the requested name; `free`
/// nullifies it.
#[test]
fn init_output_success() {
	let mut h = oakcommon_colortransform_init_output(to_cstring("sRGB").as_ptr());
	assert!(!h.is_null());
	assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	assert!(h.addref.is_some());
	assert!(h.release.is_some());

	let t: &ColorTransform = unsafe { get::<ColorTransform>(&h) }.expect("boxed transform");
	assert!(!t.is_display());
	assert_eq!(t.output(), "sRGB");
	assert_eq!(t.display(), "");
	assert_eq!(t.view(), "");
	assert_eq!(t.look(), "");

	free(&mut h);
	assert!(h.is_null());
}

/// A null output string yields a null handle with no callbacks.
#[test]
fn init_output_null_yields_null_handle() {
	let h = oakcommon_colortransform_init_output(std::ptr::null::<c_char>());
	assert!(h.is_null());
	assert!(h.ctx.is_null());
	assert!(h.addref.is_none());
	assert!(h.release.is_none());
	assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
}

/// An empty output name is a valid value: the transform is still an output
/// transform (C++ default-constructor parity).
#[test]
fn init_output_empty_string_succeeds() {
	let mut h = oakcommon_colortransform_init_output(to_cstring("").as_ptr());
	assert!(!h.is_null());
	let t: &ColorTransform = unsafe { get::<ColorTransform>(&h) }.expect("boxed transform");
	assert!(!t.is_display());
	assert_eq!(t.output(), "");
	free(&mut h);
	assert!(h.is_null());
}

/// A successful `init_display` yields a stamped, non-null handle whose
/// boxed transform carries the display/view/look names.
#[test]
fn init_display_success() {
	let mut h = oakcommon_colortransform_init_display(
		to_cstring("DCI-P3").as_ptr(),
		to_cstring("standard").as_ptr(),
		to_cstring("soft").as_ptr(),
	);
	assert!(!h.is_null());
	assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	assert!(h.addref.is_some());
	assert!(h.release.is_some());

	let t: &ColorTransform = unsafe { get::<ColorTransform>(&h) }.expect("boxed transform");
	assert!(t.is_display());
	assert_eq!(t.display(), "DCI-P3");
	assert_eq!(t.view(), "standard");
	assert_eq!(t.look(), "soft");
	assert_eq!(t.output(), "");

	free(&mut h);
	assert!(h.is_null());
}

/// Any single null argument — and all-null — yields a null handle.
#[test]
fn init_display_null_any_arg_yields_null_handle() {
	let d = to_cstring("DCI-P3");
	let v = to_cstring("standard");
	let l = to_cstring("soft");
	let cases = [
		(d.as_ptr(), v.as_ptr(), std::ptr::null::<c_char>()),
		(d.as_ptr(), std::ptr::null::<c_char>(), l.as_ptr()),
		(std::ptr::null::<c_char>(), v.as_ptr(), l.as_ptr()),
		(
			std::ptr::null::<c_char>(),
			std::ptr::null::<c_char>(),
			std::ptr::null::<c_char>(),
		),
	];
	for (display, view, look) in cases {
		let h = oakcommon_colortransform_init_display(display, view, look);
		assert!(h.is_null());
		assert!(h.ctx.is_null());
		assert!(h.addref.is_none());
		assert!(h.release.is_none());
		assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	}
}

/// Empty display/view/look strings are valid; the explicit `is_display`
/// flag keeps the transform identifiable as a display transform.
#[test]
fn init_display_empty_strings_succeed() {
	let mut h = oakcommon_colortransform_init_display(
		to_cstring("").as_ptr(),
		to_cstring("").as_ptr(),
		to_cstring("").as_ptr(),
	);
	assert!(!h.is_null());
	let t: &ColorTransform = unsafe { get::<ColorTransform>(&h) }.expect("boxed transform");
	assert!(t.is_display());
	assert_eq!(t.display(), "");
	assert_eq!(t.view(), "");
	assert_eq!(t.look(), "");
	free(&mut h);
	assert!(h.is_null());
}

/// The free contract: nullify, idempotent, and no-op on a null handle or a
/// null pointer.
#[test]
fn free_contract() {
	// A real handle: free nullifies it and a second free is safe.
	let mut h = oakcommon_colortransform_init_output(to_cstring("sRGB").as_ptr());
	assert!(!h.is_null());
	free(&mut h);
	assert!(h.is_null());
	assert!(h.ctx.is_null());
	assert!(h.release.is_none());
	free(&mut h);
	assert!(h.is_null());

	// Freeing an already-null handle is a no-op.
	free(&mut CHandle::null());

	// Freeing a null pointer is a no-op.
	free_ptr(std::ptr::null_mut());
}

/// Two output transforms are distinct handles (different boxes); init
/// yields reference count 1, addref/release adjust it atomically, and both
/// free cleanly. `dup` copies the handle value but shares the same box.
#[test]
fn init_outputs_are_distinct_and_release_cleanly() {
	let mut a = oakcommon_colortransform_init_output(to_cstring("sRGB").as_ptr());
	let mut b = oakcommon_colortransform_init_output(to_cstring("Display P3").as_ptr());
	assert!(!a.is_null());
	assert!(!b.is_null());
	assert_ne!(a.ctx, b.ctx);

	// dup copies the handle value and shares the same underlying box.
	let c = dup(&a);
	assert_eq!(c.ctx, a.ctx);

	// Ownership contract: count 1 at init, atomic addref/release.
	unsafe {
		assert_eq!(refs_of(&a), 1);
		assert_eq!(refs_of(&b), 1);
		(a.addref.unwrap())(a.ctx);
		assert_eq!(refs_of(&a), 2);
		(a.release.unwrap())(a.ctx);
		assert_eq!(refs_of(&a), 1);
	}

	// Both release cleanly (single release + nullify, no double-free).
	free(&mut a);
	assert!(a.is_null());
	free(&mut b);
	assert!(b.is_null());
}

/// `is_display` distinguishes display transforms from output transforms
/// and reports OAKCOMMON_E_INVALID on an empty handle.
#[test]
fn is_display_paths() {
	let mut d = oakcommon_colortransform_init_display(
		to_cstring("sRGB").as_ptr(),
		to_cstring("standard").as_ptr(),
		to_cstring("").as_ptr(),
	);
	let mut o = oakcommon_colortransform_init_output(to_cstring("sRGB").as_ptr());
	assert_eq!(oakcommon_colortransform_is_display(dup(&d)), 1);
	assert_eq!(oakcommon_colortransform_is_display(dup(&o)), 0);
	assert_eq!(
		oakcommon_colortransform_is_display(CHandle::null()),
		oakcommon::error::OAKCOMMON_E_INVALID
	);
	free(&mut d);
	free(&mut o);
}

/// Two-stage getters: exact-fit returns content, short buffer leaves the
/// buffer untouched but reports the required size, empty handle yields
/// OAKCOMMON_E_INVALID.
#[test]
fn getters_two_stage_and_empty_handle() {
	let mut d = oakcommon_colortransform_init_display(
		to_cstring("P3").as_ptr(),
		to_cstring("std").as_ptr(),
		to_cstring("soft").as_ptr(),
	);

	let mut buf = [0i8; 32];
	let need = oakcommon_colortransform_get_display(dup(&d), buf.as_mut_ptr(), 32);
	assert_eq!(need, 3); // "P3" + NUL
	assert_eq!(unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(), "P3");

	let need = oakcommon_colortransform_get_view(dup(&d), buf.as_mut_ptr(), 2);
	assert_eq!(need, 4); // "std" + NUL, too small: nothing written

	let need = oakcommon_colortransform_get_look(dup(&d), buf.as_mut_ptr(), 32);
	assert_eq!(need, 5);
	assert_eq!(unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(), "soft");

	let o = oakcommon_colortransform_init_output(to_cstring("sRGB").as_ptr());
	let need = oakcommon_colortransform_get_output(o, buf.as_mut_ptr(), 32);
	assert_eq!(need, 5);
	assert_eq!(unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(), "sRGB");

	assert_eq!(
		oakcommon_colortransform_get_display(CHandle::null(), buf.as_mut_ptr(), 32),
		oakcommon::error::OAKCOMMON_E_INVALID
	);
	free(&mut d);
}

/// The real `oakcommon_colortransform_free` export: releases once,
/// nullifies, no-ops on a null pointer.
#[test]
fn free_export_contract() {
	let mut h = oakcommon_colortransform_init_output(to_cstring("sRGB").as_ptr());
	assert!(!h.is_null());
	oakcommon_colortransform_free(&mut h);
	assert!(h.is_null());
	oakcommon_colortransform_free(&mut h); // already null: no-op
	oakcommon_colortransform_free(std::ptr::null_mut()); // null pointer: no-op
}
