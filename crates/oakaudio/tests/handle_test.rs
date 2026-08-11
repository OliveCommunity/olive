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

//! Handle plumbing contract tests (handle.rs).

use oakaudio::error::{OAKAUDIO_E_FAILED, OAKAUDIO_E_INVALID, OAKAUDIO_OK};
use oakaudio::handle::{alive_count, get, guard, guard_handle, make_borrowed, make_owned, CHandle};

/// make_owned starts at refcount 1; get returns a typed view; dropping the
/// handle decrements to 0.
#[test]
fn owned_lifecycle() {
	let before = alive_count();
	let mut h = make_owned(42u32);
	assert!(!h.is_null());
	assert_eq!(alive_count(), before + 1);

	// SAFETY: `h` boxes a u32 created above.
	let v = unsafe { get::<u32>(&h) }.unwrap();
	assert_eq!(*v, 42);

	// addref/release round-trip through the function pointers.
	let addref = h.addref.unwrap();
	let release = h.release.unwrap();
	// SAFETY: the ctx was created by make_owned.
	unsafe { addref(h.ctx) };
	assert_eq!(alive_count(), before + 1); // count unchanged, still 1 box
	unsafe { release(h.ctx) };

	// Dropping the box (release to zero) decrements the ledger.
	let release = h.release.unwrap();
	// SAFETY: h.ctx is the box created above; refcount is 1.
	unsafe { release(h.ctx) };
	h.ctx = std::ptr::null_mut();
	assert_eq!(alive_count(), before);
}

/// make_borrowed creates a borrow-only handle whose release frees only the
/// box, never the underlying object.
#[test]
fn borrowed_release() {
	let mut value = Box::new(7i32);
	// SAFETY: `value` outlives the handle; the ctx points directly at the
	// box (not a RefBox), so it is read through the raw pointer.
	let mut h = unsafe { make_borrowed(&mut *value) };
	assert!(!h.is_null());

	// SAFETY: `h.ctx` points at the box created above.
	let v = unsafe { &*(h.ctx as *const i32) };
	assert_eq!(*v, 7);

	// release is a no-op: the box still lives.
	let release = h.release.unwrap();
	// SAFETY: noop_ref for borrowed handles.
	unsafe { release(h.ctx) };
	assert_eq!(*value, 7);

	h.ctx = std::ptr::null_mut();
}

/// CHandle::null() yields an empty handle; guard over an Ok(()) returns
/// OAKAUDIO_OK (0) and guard_handle over Ok returns a valid handle.
#[test]
fn null_and_guard_ok() {
	let null = CHandle::null();
	assert!(null.is_null());
	// The shared `null()` stamps no ABI version (single-lib unification).
	assert_eq!(null.abi_version, 0);

	assert_eq!(guard(|| Ok(())), OAKAUDIO_OK);

	let h = guard_handle(|| Ok(make_owned(1u32)));
	assert!(!h.is_null());
	// Release it so the alive ledger returns to baseline (tests share the
	// process-wide ledger and run in parallel).
	// SAFETY: `h.ctx` is the box created above; refcount is 1.
	unsafe { (h.release.unwrap())(h.ctx) };
}

/// guard maps an Err to the negative error code without panicking; a
/// panicking body is caught and returns a failure code rather than
/// unwinding across the FFI boundary.
#[test]
fn guard_error_and_panic() {
	assert_eq!(
		guard(|| Err(oakaudio::error::Error::Invalid)),
		OAKAUDIO_E_INVALID
	);
	assert_eq!(guard(|| Err(oakaudio::error::Error::State)), -60002);
	assert_eq!(
		guard(|| Err(oakaudio::error::Error::Failed("x".to_string()))),
		-60003
	);
	assert_eq!(guard(|| Err(oakaudio::error::Error::NotFound)), -60004);
	assert_eq!(guard(|| Err(oakaudio::error::Error::NoMem)), -60005);

	assert_eq!(guard(|| panic!("boom")), OAKAUDIO_E_FAILED);

	let h = guard_handle(|| Err(oakaudio::error::Error::Invalid));
	assert!(h.is_null());
	let h2 = guard_handle(|| panic!("boom"));
	assert!(h2.is_null());
}
