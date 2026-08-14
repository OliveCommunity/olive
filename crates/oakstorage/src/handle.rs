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

//! Refcounted-handle scaffolding (same pattern as the other crates;
//! duplicated on purpose — handle function pointers must run code from
//! the creating DLL).
//!
//! `CHandle` is the canonical shared ABI value-handle type from
//! `oakcore-rs` (single-lib unification, see
//! `docs/zh/plans/riir/single-lib.md`), so a handle returned by
//! `oakstorage_*` is structurally interchangeable with one created by
//! `oaknode_*`: `oakstorage_save` consumes an `OakNodeProject*` handle
//! and `oakstorage_project_take_project` hands one back.

use std::any::Any;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, Ordering};

pub use oakcore_rs::handle::CHandle;

use crate::error::OAKSTORAGE_E_FAILED;

/// ABI version stamped into every handle.
pub const OAKSTORAGE_ABI_VERSION: u32 = 1;

/// Heap box behind a handle's `ctx`.
pub struct RefBox<T: ?Sized> {
	/// Atomic reference count.
	pub refs: AtomicU32,
	/// Boxed value.
	pub value: T,
}

/// addref implementation: atomic +1 (owned and borrowed handles alike —
/// a borrow only extends the box's life, not the borrowed object's).
unsafe extern "C" fn refbox_addref<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *const RefBox<T>;
		// The caller guarantees the handle is live for the borrow period.
		(*rb).refs.fetch_add(1, Ordering::Relaxed);
	}
}

/// release implementation (owned): atomic -1; at zero, reclaim the box
/// and destroy the contained value.
unsafe extern "C" fn refbox_release_owned<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		// AcqRel: the zeroing side sees every write that preceded the
		// last reference (including the state the destructor needs).
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(rb));
		}
	}
}

/// release implementation (borrowed, from [`make_borrowed`]): at zero,
/// free only the box memory and forget the contained value — ownership
/// stays with the borrowing side.
unsafe extern "C" fn refbox_release_borrowed<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			// Partial move: move the value out of the temporary Box (its
			// destructor then only frees the allocation); `forget` skips
			// the value's destructor (double-free defense).
			std::mem::forget((Box::from_raw(rb)).value);
		}
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Any + Send>(value: T) -> CHandle {
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		value,
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(refbox_release_owned::<T>),
		abi_version: OAKSTORAGE_ABI_VERSION,
	}
}

/// Owned handle with count 1 and a caller-provided release routine
/// (used by the ffi layer's alive-counted session boxes, whose release
/// must also update the debug counter).
pub fn make_owned_with<T: Any + Send>(
	value: T,
	release: unsafe extern "C" fn(*mut std::ffi::c_void),
) -> CHandle {
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		value,
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(release),
		abi_version: OAKSTORAGE_ABI_VERSION,
	}
}

/// Borrowed handle for an object owned elsewhere (release frees only
/// the box).
///
/// Semantics: bitwise copy ("borrowed copy"); the borrowed object's
/// destructor is entirely the caller's responsibility — the box never
/// touches it.
///
/// # Safety
/// Caller guarantees `ptr` outlives every derived handle, and that its
/// value is not moved or destroyed for the borrow's lifetime.
pub unsafe fn make_borrowed<T: Any + Send>(ptr: *mut T) -> CHandle {
	if ptr.is_null() {
		return CHandle::null();
	}
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		value: unsafe { std::ptr::read(ptr) },
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(refbox_release_borrowed::<T>),
		abi_version: OAKSTORAGE_ABI_VERSION,
	}
}

/// Typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: Any>(h: &CHandle) -> Option<&T> {
	if h.ctx.is_null() {
		return None;
	}
	unsafe { Some(&(*(h.ctx as *const RefBox<T>)).value) }
}

/// Panic-catching FFI wrapper for i32-returning exports.
///
/// Panics map to [`OAKSTORAGE_E_FAILED`].
pub fn guard<F: FnOnce() -> crate::error::Result<()>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => crate::error::OAKSTORAGE_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => OAKSTORAGE_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for handle-returning exports.
pub fn guard_handle<F: FnOnce() -> crate::error::Result<CHandle>>(f: F) -> CHandle {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(h)) => h,
		Ok(Err(_)) | Err(_) => CHandle::null(),
	}
}

/// Panic-catching FFI wrapper for void exports.
pub fn guard_void<F: FnOnce()>(f: F) {
	let _ = catch_unwind(AssertUnwindSafe(f));
}
