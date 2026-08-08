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

//! Refcounted-handle scaffolding. Same pattern as the oaknode/oakplugin
//! crates (`src/node/rust/src/handle.rs`); intentionally duplicated rather
//! than shared — each module DLL must run its own addref/release code
//! (the function pointers in a handle always point into the DLL that
//! created the object).

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::atomic::{AtomicI32, AtomicU32, Ordering};

use crate::error::{self, OAKCODEC_E_FAILED};

/// Number of boxed handle objects currently alive (leak/debug checking).
///
/// Mirrors `oakcodec::g_alive_count` in `src/codec/c_api/frame.cpp`: every
/// `make_owned` box increments it and `box_release` decrements it when the
/// last reference drops. `oakcodec_debug_alive_count` reports it.
static ALIVE: AtomicI32 = AtomicI32::new(0);

/// ABI version stamped into every handle.
pub const OAKCODEC_ABI_VERSION: u32 = 1;

/// Heap box behind a handle's `ctx`.
pub struct RefBox<T: ?Sized> {
	/// Atomic reference count.
	pub refs: AtomicU32,
	/// Boxed value.
	pub value: T,
}

/// `#[repr(C)]` mirror of the public handle structs
/// (`{ctx, addref, release, abi_version}`).
///
/// Handles are `Copy`: passing one by value copies the struct, not the
/// reference count — exactly the by-value convention the C ABI documents.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(C)]
pub struct CHandle {
	/// Opaque box pointer.
	pub ctx: *mut std::ffi::c_void,
	/// Atomic increment.
	pub addref: Option<unsafe extern "C" fn(*mut std::ffi::c_void)>,
	/// Atomic decrement; destroys at zero.
	pub release: Option<unsafe extern "C" fn(*mut std::ffi::c_void)>,
	/// ABI version.
	pub abi_version: u32,
}

impl CHandle {
	/// The empty handle.
	pub fn null() -> Self {
		CHandle {
			ctx: ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: OAKCODEC_ABI_VERSION,
		}
	}

	/// Whether this is an empty (null) handle.
	pub fn is_null(&self) -> bool {
		self.ctx.is_null()
	}
}

/// Increment the reference count of a boxed `RefBox<T>`.
///
/// # Safety
/// `ptr` must point to a live `RefBox<T>` previously created by this module.
unsafe extern "C" fn box_addref<T: Send + 'static>(ptr: *mut std::ffi::c_void) {
	if ptr.is_null() {
		return;
	}
	let boxed = unsafe { &*(ptr as *const RefBox<T>) };
	boxed.refs.fetch_add(1, Ordering::SeqCst);
}

/// Decrement the reference count; destroys the box at zero.
///
/// # Safety
/// `ptr` must point to a live `RefBox<T>` previously created by this module.
unsafe extern "C" fn box_release<T: Send + 'static>(ptr: *mut std::ffi::c_void) {
	if ptr.is_null() {
		return;
	}
	let boxed = unsafe { &*(ptr as *const RefBox<T>) };
	if boxed.refs.fetch_sub(1, Ordering::SeqCst) == 1 {
		// The last reference: the box is destroyed and the alive count
		// drops with it (mirrors `alive_dec` in c_api/frame.cpp).
		ALIVE.fetch_sub(1, Ordering::SeqCst);
		unsafe { drop(Box::from_raw(ptr as *mut RefBox<T>)) };
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Send + 'static>(value: T) -> CHandle {
	let boxed = Box::new(RefBox {
		refs: AtomicU32::new(1),
		value,
	});
	let ctx = Box::into_raw(boxed) as *mut std::ffi::c_void;
	// Every boxed handle counts toward `oakcodec_debug_alive_count`
	// (mirrors `alive_inc` in c_api/frame.cpp).
	ALIVE.fetch_add(1, Ordering::SeqCst);
	CHandle {
		ctx,
		addref: Some(box_addref::<T>),
		release: Some(box_release::<T>),
		abi_version: OAKCODEC_ABI_VERSION,
	}
}

/// Borrowed handle for an object owned elsewhere.
///
/// Takes ownership of the boxed `T` already allocated at `ptr` (e.g. one
/// passed in from C++). The resulting handle's release drops that box.
///
/// # Safety
/// Caller guarantees `ptr` was allocated with `Box::new` and is not used
/// after this call.
pub unsafe fn make_borrowed<T: Send + 'static>(ptr: *mut T) -> CHandle {
	if ptr.is_null() {
		return CHandle::null();
	}
	// Move ownership into a RefBox so addref/release and get() behave
	// uniformly with owned handles.
	let value = unsafe { *Box::from_raw(ptr) };
	make_owned(value)
}

/// Typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: 'static>(h: &CHandle) -> Option<&T> {
	if h.is_null() {
		return None;
	}
	let boxed = unsafe { &*(h.ctx as *const RefBox<T>) };
	Some(&boxed.value)
}

/// Panic-catching FFI wrapper for i32-returning exports.
pub fn guard<F: FnOnce() -> error::Result<()>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => error::OAKCODEC_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => OAKCODEC_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for handle-returning exports.
pub fn guard_handle<F: FnOnce() -> error::Result<CHandle>>(f: F) -> CHandle {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(h)) => h,
		Ok(Err(_)) | Err(_) => CHandle::null(),
	}
}

/// Panic-catching FFI wrapper for void exports.
pub fn guard_void<F: FnOnce()>(f: F) {
	let _ = catch_unwind(AssertUnwindSafe(f));
}

/// Panic-catching FFI wrapper for exports that return a raw `i32` code
/// directly (neither `Result` nor a handle). On panic, `OAKCODEC_E_FAILED`.
pub fn guard_raw<F: FnOnce() -> i32>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(code) => code,
		Err(_) => OAKCODEC_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for exports that return a raw `i64` directly
/// (e.g. `oakcodec_decoder_get_image_sequence_index`). On panic,
/// `OAKCODEC_E_FAILED`.
pub fn guard_i64<F: FnOnce() -> i64>(f: F) -> i64 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(v) => v,
		Err(_) => OAKCODEC_E_FAILED as i64,
	}
}

/// Number of live boxed handle objects (see [`ALIVE`]).
pub fn alive_count() -> i32 {
	ALIVE.load(Ordering::SeqCst)
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::error::OAKCODEC_E_INVALID;

	#[test]
	fn make_owned_lifecycle_tracks_alive_count() {
		// The shared ffi test lock serializes the crate's `alive_count`
		// assertions against every other test that creates handles.
		let _g = crate::ffi::lock_tests();
		let before = alive_count();
		let h = make_owned(42u32);
		assert!(!h.is_null());
		assert_eq!(alive_count(), before + 1);

		// addref/release cycle keeps the box alive.
		let addref = h.addref.unwrap();
		let release = h.release.unwrap();
		// SAFETY: `h.ctx` is a live RefBox<u32>.
		unsafe { addref(h.ctx) };
		// SAFETY: second reference released; box stays (refs 2 -> 1).
		unsafe { release(h.ctx) };
		assert_eq!(alive_count(), before + 1);

		// Release the owned reference: box destroyed.
		// SAFETY: last reference.
		unsafe { release(h.ctx) };
		assert_eq!(alive_count(), before);
	}

	#[test]
	fn make_borrowed_null_is_null_handle() {
		let h = unsafe { make_borrowed::<u32>(std::ptr::null_mut()) };
		assert!(h.is_null());
	}

	#[test]
	fn make_borrowed_takes_ownership() {
		let _g = crate::ffi::lock_tests();
		let before = alive_count();
		let raw = Box::into_raw(Box::new(7u32));
		let h = unsafe { make_borrowed(raw) };
		assert!(!h.is_null());
		assert_eq!(alive_count(), before + 1);
		assert_eq!(unsafe { *get::<u32>(&h).unwrap() }, 7);
		unsafe { h.release.unwrap()(h.ctx) };
		assert_eq!(alive_count(), before);
	}

	#[test]
	fn addref_on_null_ctx_is_noop() {
		// A handle with function pointers but a null ctx: both thunks no-op.
		let h = CHandle {
			ctx: std::ptr::null_mut(),
			addref: Some(box_addref::<u32>),
			release: Some(box_release::<u32>),
			abi_version: OAKCODEC_ABI_VERSION,
		};
		// SAFETY: ctx is null; the thunks guard on it.
		unsafe { h.addref.unwrap()(h.ctx) };
		// SAFETY: ctx is null; the thunks guard on it.
		unsafe { h.release.unwrap()(h.ctx) };
	}

	#[test]
	fn guard_maps_results_and_panics() {
		assert_eq!(guard(|| Ok(())), crate::error::OAKCODEC_OK);
		assert_eq!(guard(|| Err(crate::error::Error::Invalid)), OAKCODEC_E_INVALID);
		assert_eq!(guard(|| panic!("boom")), crate::error::OAKCODEC_E_FAILED);

		let ok = guard_handle(|| Ok(make_owned(1u32)));
		assert!(!ok.is_null());
		assert!(guard_handle(|| Err::<CHandle, _>(crate::error::Error::Invalid)).is_null());
		assert!(guard_handle(|| panic!("boom")).is_null());

		assert_eq!(guard_raw(|| 5), 5);
		assert_eq!(guard_raw(|| panic!("boom")), crate::error::OAKCODEC_E_FAILED);

		assert_eq!(guard_i64(|| 5), 5);
		assert_eq!(guard_i64(|| panic!("boom")), crate::error::OAKCODEC_E_FAILED as i64);

		let mut called = false;
		guard_void(|| called = true);
		assert!(called);
		guard_void(|| panic!("boom"));
	}

	#[test]
	fn null_handle_helpers() {
		let h = CHandle::null();
		assert!(h.is_null());
		assert_eq!(h.abi_version, OAKCODEC_ABI_VERSION);
	}
}
