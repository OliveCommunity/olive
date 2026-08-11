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

//! Refcounted-handle scaffolding. Same pattern as the oaknode crate
//! (`src/node/rust/src/handle.rs`); intentionally duplicated rather
//! than shared — each module DLL must run its own addref/release code
//! (the function pointers in a handle always point into the DLL that
//! created the object). Value handles (`OakTimelineMarkerList`,
//! `OakTimelineWorkArea`) share this box.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, Ordering};

use crate::error::{Result, OAKTIMELINE_E_FAILED, OAKTIMELINE_OK};

/// ABI version stamped into every oaktimeline handle.
pub const OAKTIMELINE_ABI_VERSION: u32 = 1;

/// Heap box behind a handle's `ctx`.
pub struct RefBox<T: ?Sized> {
	/// Atomic reference count.
	pub refs: AtomicU32,
	/// Boxed value.
	pub value: T,
}

/// The shared ABI value-handle type (single-lib unification, see
/// `docs/zh/plans/riir/single-lib.md`): one canonical
/// `{ctx, addref, release, abi_version}` type in `oakcore-rs`, re-exported
/// here so the crate's `ffi.rs` signatures and handle scaffolding stay
/// source-compatible.
pub use oakcore_rs::handle::CHandle;

/// Generic `addref` implementation: increments the box's reference count.
///
/// # Safety
/// `ptr` must point to a `RefBox<T>`.
unsafe extern "C" fn addref_box<T: 'static>(ptr: *mut c_void) {
	if ptr.is_null() {
		return;
	}
	let rb = unsafe { &*(ptr as *const RefBox<T>) };
	rb.refs.fetch_add(1, Ordering::SeqCst);
}

/// Generic `release` implementation: decrements the reference count and
/// destroys the box at zero.
///
/// # Safety
/// `ptr` must point to a `RefBox<T>`.
unsafe extern "C" fn release_box<T: 'static>(ptr: *mut c_void) {
	if ptr.is_null() {
		return;
	}
	let rb = ptr as *mut RefBox<T>;
	let prev = unsafe { (*rb).refs.fetch_sub(1, Ordering::SeqCst) };
	if prev == 1 {
		// Last reference: reclaim the box.
		drop(unsafe { Box::from_raw(rb) });
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Send + 'static>(value: T) -> CHandle {
	let boxed = Box::new(RefBox {
		refs: AtomicU32::new(1),
		value,
	});
	let ptr = Box::into_raw(boxed) as *mut c_void;
	CHandle {
		ctx: ptr,
		addref: Some(addref_box::<T>),
		release: Some(release_box::<T>),
		abi_version: OAKTIMELINE_ABI_VERSION,
	}
}

/// Borrowed handle for an object owned elsewhere (release frees only
/// the box).
///
/// The box holds a detached copy of `*ptr`, so the underlying object is
/// never touched by the handle's release; `get` returns the copy. The
/// caller retains ownership of `ptr`.
///
/// # Safety
/// Caller guarantees `ptr` is valid for reading for the duration of the
/// call.
pub unsafe fn make_borrowed<T: Send + 'static>(ptr: *mut T) -> CHandle {
	let value = unsafe { ptr.read() };
	make_owned(value)
}

/// Typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: 'static>(h: &CHandle) -> Option<&T> {
	if h.ctx.is_null() {
		return None;
	}
	if h.addref.is_some() {
		// Owned (or borrowed-via-copy) handle: `ctx` points at a `RefBox<T>`.
		let rb = unsafe { &*(h.ctx as *const RefBox<T>) };
		Some(&rb.value)
	} else {
		// Borrowed handle wrapping a raw object pointer (e.g. test-stub
		// handles): `ctx` is the object itself, not a `RefBox<T>`.
		Some(unsafe { &*(h.ctx as *const T) })
	}
}

/// Mutable typed view into a handle; `None` for empty handles. Used by
/// undo commands to mutate the boxed value they hold a handle to.
///
/// # Safety
/// `T` must be the boxed type, and the caller must guarantee exclusive
/// access to the boxed value for the duration of the borrow (no two
/// mutable views alive at once).
pub unsafe fn get_mut<T: 'static>(h: &CHandle) -> Option<&mut T> {
	if h.ctx.is_null() {
		return None;
	}
	if h.addref.is_some() {
		// Owned (or borrowed-via-copy) handle: `ctx` points at a `RefBox<T>`.
		let rb = unsafe { &mut *(h.ctx as *mut RefBox<T>) };
		Some(&mut rb.value)
	} else {
		// Borrowed handle wrapping a raw object pointer (e.g. test-stub
		// handles): `ctx` is the object itself, not a `RefBox<T>`.
		Some(unsafe { &mut *(h.ctx as *mut T) })
	}
}

/// Panic-catching FFI wrapper for i32-returning exports.
pub fn guard<F: FnOnce() -> Result<()>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => OAKTIMELINE_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => OAKTIMELINE_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for handle-returning exports.
pub fn guard_handle<F: FnOnce() -> Result<CHandle>>(f: F) -> CHandle {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(h)) => h,
		_ => CHandle::null(),
	}
}

/// Panic-catching FFI wrapper for void exports.
pub fn guard_void<F: FnOnce()>(f: F) {
	let _ = catch_unwind(AssertUnwindSafe(f));
}

/// Panic-catching FFI wrapper for exports returning an `i32` value that is
/// not an error code (e.g. two-stage string lengths): a successful closure
/// returns its value verbatim, errors map through `Error::code`, and a
/// panic becomes `OAKTIMELINE_E_FAILED`.
pub fn guard_i32<F: FnOnce() -> Result<i32>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(v)) => v,
		Ok(Err(e)) => e.code(),
		Err(_) => OAKTIMELINE_E_FAILED,
	}
}
