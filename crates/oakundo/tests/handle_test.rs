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

//! Tests for the refcounted-handle scaffolding (`handle.rs`).

use std::ffi::c_void;
use std::sync::atomic::{AtomicI32, Ordering};
use std::sync::Arc;

use oakundo::error::{Result, OAKUNDO_E_FAILED, OAKUNDO_E_INVALID, OAKUNDO_OK};
use oakundo::handle::{get, guard, guard_handle, guard_void, make_borrowed, make_owned, CHandle};

/// A value whose `Drop` signals through a shared counter.
struct DropProbe {
	counter: Arc<AtomicI32>,
}

impl DropProbe {
	fn new() -> (Self, Arc<AtomicI32>) {
		let counter = Arc::new(AtomicI32::new(1));
		(
			DropProbe {
				counter: counter.clone(),
			},
			counter,
		)
	}
}

impl Drop for DropProbe {
	fn drop(&mut self) {
		self.counter.fetch_sub(1, Ordering::SeqCst);
	}
}

#[test]
fn null_handle_is_all_zero() {
	let h = CHandle::null();
	assert!(h.is_null());
	assert!(h.ctx.is_null());
	assert!(h.addref.is_none());
	assert!(h.release.is_none());
	assert_eq!(h.abi_version, 0);
}

#[test]
fn make_owned_counts_and_releases() {
	let (value, counter) = DropProbe::new();
	let h = make_owned(value);

	assert!(!h.is_null());
	assert_eq!(h.abi_version, oakundo::handle::OAKUNDO_ABI_VERSION);

	// addref bumps the count; a second release does not double-drop.
	let addref = h.addref.unwrap();
	let release = h.release.unwrap();
	unsafe { addref(h.ctx) };
	unsafe { release(h.ctx) };
	assert_eq!(
		counter.load(Ordering::SeqCst),
		1,
		"still alive after one release"
	);

	// Final release drops the box.
	unsafe { release(h.ctx) };
	assert_eq!(
		counter.load(Ordering::SeqCst),
		0,
		"dropped at refcount zero"
	);
}

#[test]
fn make_owned_get_round_trips() {
	let h = make_owned(42i32);
	let ch = &h;
	let v = unsafe { get::<i32>(ch) };
	assert_eq!(v, Some(&42));

	let empty = CHandle::null();
	assert_eq!(unsafe { get::<i32>(&empty) }, None);
}

#[test]
fn borrowed_release_does_not_free_pointee() {
	let (mut value, counter) = DropProbe::new();
	let b = unsafe { make_borrowed(&mut value as *mut DropProbe) };

	let release = b.release.unwrap();
	unsafe { release(b.ctx) };
	assert_eq!(
		counter.load(Ordering::SeqCst),
		1,
		"borrowed release frees only the shell"
	);

	// The owned value is still usable and is dropped normally.
	drop(value);
	assert_eq!(counter.load(Ordering::SeqCst), 0);
}

#[test]
fn guard_success_error_and_panic() {
	fn ok() -> Result<()> {
		Ok(())
	}
	fn invalid() -> Result<()> {
		Err(oakundo::error::Error::Invalid)
	}
	fn panic() -> Result<()> {
		panic!("boom")
	}
	assert_eq!(guard(ok), OAKUNDO_OK);
	assert_eq!(guard(invalid), OAKUNDO_E_INVALID);
	assert_eq!(guard(panic), OAKUNDO_E_FAILED);
}

#[test]
fn guard_handle_returns_value_or_null() {
	let h = guard_handle(|| Ok(make_owned(5i32)));
	assert!(!h.is_null());
	assert_eq!(h.abi_version, oakundo::handle::OAKUNDO_ABI_VERSION);

	let on_err = guard_handle(|| Err(oakundo::error::Error::NotFound));
	assert!(on_err.is_null());

	let on_panic = guard_handle(|| -> Result<CHandle> { panic!("boom") });
	assert!(on_panic.is_null());
}

#[test]
fn guard_void_swallows_panic() {
	let mut ran = false;
	guard_void(|| ran = true);
	assert!(ran);

	guard_void(|| panic!("swallowed"));
}

#[test]
fn addref_release_null_ctx_is_noop() {
	let h = make_owned(7i32);
	unsafe { (h.addref.unwrap())(std::ptr::null_mut()) };
	unsafe { (h.release.unwrap())(std::ptr::null_mut()) };
	assert!(!h.is_null());
}

#[test]
fn borrowed_handle_binds_to_correct_type() {
	// The borrowed handle boxes a `*mut T`; `get` for that type sees the
	// raw pointer value.
	let mut value = 99u64;
	let h = unsafe { make_borrowed(&mut value as *mut u64) };
	let ch = &h;
	let p = unsafe { get::<*mut u64>(ch) }.unwrap();
	assert_eq!(unsafe { **p }, 99);
}

/// `*mut c_void` helper used to drive callbacks directly in one test.
#[allow(dead_code)]
fn _as_void<T>(p: *mut T) -> *mut c_void {
	p as *mut c_void
}
