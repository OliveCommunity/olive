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

//! Refcounted-handle scaffolding. Same pattern as the oaknode/oakcodec
//! crates (`src/node/rust/src/handle.rs`); intentionally duplicated rather
//! than shared — each module DLL must run its own addref/release code
//! (the function pointers in a handle always point into the DLL that
//! created the object).

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, Ordering};

/// ABI version stamped into every handle.
pub const OAKUNDO_ABI_VERSION: u32 = 1;

/// Heap box behind a handle's `ctx`.
pub struct RefBox<T: ?Sized> {
	/// Atomic reference count.
	pub refs: AtomicU32,
	/// Boxed value.
	pub value: T,
}

/// `#[repr(C)]` mirror of the public handle structs
/// (`{ctx, addref, release, abi_version}`).
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
			ctx: std::ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: 0,
		}
	}

	/// Whether this is the empty (zero) handle.
	pub fn is_null(&self) -> bool {
		self.ctx.is_null()
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Send + 'static>(value: T) -> CHandle {
	let boxed = RefBox {
		refs: AtomicU32::new(1),
		value,
	};
	let ctx = Box::into_raw(Box::new(boxed)) as *mut std::ffi::c_void;
	CHandle {
		ctx,
		addref: Some(addref_owned::<T>),
		release: Some(release_owned::<T>),
		abi_version: OAKUNDO_ABI_VERSION,
	}
}

/// Borrowed handle for an object owned elsewhere (release frees only
/// the box).
///
/// # Safety
/// Caller guarantees `ptr` outlives every derived handle.
pub unsafe fn make_borrowed<T: Send + 'static>(ptr: *mut T) -> CHandle {
	let boxed = RefBox {
		refs: AtomicU32::new(1),
		value: ptr,
	};
	let ctx = Box::into_raw(Box::new(boxed)) as *mut std::ffi::c_void;
	CHandle {
		ctx,
		addref: Some(addref_borrowed::<T>),
		release: Some(release_borrowed::<T>),
		abi_version: OAKUNDO_ABI_VERSION,
	}
}

/// Typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: 'static>(h: &CHandle) -> Option<&T> {
	unsafe {
		if h.ctx.is_null() {
			None
		} else {
			Some(&(*((h.ctx as *mut RefBox<T>))).value)
		}
	}
}

/// Panic-catching FFI wrapper for i32-returning exports.
pub fn guard<F: FnOnce() -> crate::error::Result<()>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => crate::error::OAKUNDO_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => crate::error::OAKUNDO_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for handle-returning exports.
pub fn guard_handle<F: FnOnce() -> crate::error::Result<CHandle>>(f: F) -> CHandle {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(h)) => h,
		_ => CHandle::null(),
	}
}

/// Panic-catching FFI wrapper for void exports.
pub fn guard_void<F: FnOnce()>(f: F) {
	let _ = catch_unwind(AssertUnwindSafe(f));
}

/// Owned addref: bump the box's refcount.
unsafe extern "C" fn addref_owned<T: Send + 'static>(ctx: *mut std::ffi::c_void) {
	unsafe {
		if ctx.is_null() {
			return;
		}
		let boxed = ctx as *mut RefBox<T>;
		(&(*boxed).refs).fetch_add(1, Ordering::AcqRel);
	}
}

/// Owned release: drop the box when the refcount hits zero.
unsafe extern "C" fn release_owned<T: Send + 'static>(ctx: *mut std::ffi::c_void) {
	unsafe {
		if ctx.is_null() {
			return;
		}
		let boxed = ctx as *mut RefBox<T>;
		if (&(*boxed).refs).fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(boxed));
		}
	}
}

/// Borrowed addref: bump the shell's refcount.
unsafe extern "C" fn addref_borrowed<T: Send + 'static>(ctx: *mut std::ffi::c_void) {
	unsafe {
		if ctx.is_null() {
			return;
		}
		let boxed = ctx as *mut RefBox<*mut T>;
		(&(*boxed).refs).fetch_add(1, Ordering::AcqRel);
	}
}

/// Borrowed release: free only the shell, never the pointee.
unsafe extern "C" fn release_borrowed<T: Send + 'static>(ctx: *mut std::ffi::c_void) {
	unsafe {
		if ctx.is_null() {
			return;
		}
		let boxed = ctx as *mut RefBox<*mut T>;
		if (&(*boxed).refs).fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(boxed));
		}
	}
}
