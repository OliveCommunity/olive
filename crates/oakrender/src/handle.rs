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

//! Refcounted-handle scaffolding (same per-module pattern as the
//! oakplugin/oaknode crates; duplicated on purpose — handle function
//! pointers must run code from the creating DLL).
//!
//! Mirrors `src/render/c_api/internalhandles.h`: every public oakrender
//! handle is `{ctx, addref, release, abi_version}`; `ctx` points at a
//! [`RefBox<T>`] on this crate's heap. `owns == false` boxes (borrowed
//! wrappers) only free the box at zero.
//!
//! Live-object accounting mirrors the C++ `alive_inc`/`alive_dec`:
//! [`make_owned`] counts the handle, the owned release un-counts it, so
//! `oakrender_debug_alive_count()` stays meaningful for leak assertions.

use std::any::Any;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, AtomicUsize, Ordering};

/// ABI version stamped into every handle.
pub const OAKRENDER_ABI_VERSION: u32 = 1;

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
/// source-compatible. `Send + Sync` come from the shared type.
pub use oakcore_rs::handle::CHandle;

/// Global live-object count (owned handles + cancel-atom boxes).
static ALIVE_COUNT: AtomicUsize = AtomicUsize::new(0);

/// Increment the live-object count (owned handle creation).
pub fn alive_inc() {
	ALIVE_COUNT.fetch_add(1, Ordering::Relaxed);
}

/// Decrement the live-object count (owned handle destruction).
pub fn alive_dec() {
	ALIVE_COUNT.fetch_sub(1, Ordering::Relaxed);
}

/// Current live-object count (`oakrender_debug_alive_count`).
pub fn alive_count() -> i32 {
	ALIVE_COUNT.load(Ordering::Relaxed) as i32
}

/// addref implementation: atomic +1. Shared by owned and borrowed boxes —
/// borrowing only extends the box's lifetime, not the borrowed object's.
unsafe extern "C" fn refbox_addref<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *const RefBox<T>;
		// The caller guarantees the handle stays valid for the call.
		(*rb).refs.fetch_add(1, Ordering::Relaxed);
	}
}

/// release implementation (owned): atomic -1; at zero, reclaim the box and
/// destroy the contained object.
unsafe extern "C" fn refbox_release_owned<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		// AcqRel: the thread that drops the last reference must observe all
		// prior writes (including internal state the destructor needs).
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			alive_dec();
			drop(Box::from_raw(rb));
		}
	}
}

/// release implementation (borrowed, produced by [`make_borrowed`]): at
/// zero only reclaim the box memory, forgetting the contained object — its
/// ownership stays with the borrower.
unsafe extern "C" fn refbox_release_borrowed<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			// Partial move: move the value out of the temporary Box so the
			// Box drop only frees the allocation; forget the value so it is
			// never dropped (double-free guard).
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
	alive_inc();
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(refbox_release_owned::<T>),
		abi_version: OAKRENDER_ABI_VERSION,
	}
}

/// Borrowed handle for an object owned elsewhere.
///
/// # Safety
/// Caller guarantees `ptr` outlives every derived handle.
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
		abi_version: OAKRENDER_ABI_VERSION,
	}
}

/// Typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: Any>(h: &CHandle) -> Option<&T> {
	if h.is_null() {
		return None;
	}
	unsafe { Some(&(*(h.ctx as *const RefBox<T>)).value) }
}

/// Typed mutable view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type, and the caller must guarantee the handle
/// is not concurrently borrowed (the C ABI contract: a handle passed by
/// value is exclusively owned for the duration of the call).
pub unsafe fn get_mut<T: Any>(h: &CHandle) -> Option<&mut T> {
	if h.is_null() {
		return None;
	}
	unsafe { Some(&mut (*(h.ctx as *mut RefBox<T>)).value) }
}

/// A boxed handle that does **not** participate in the live-object count
/// (mirrors the C++ borrowed `make_handle(…, owns=false)` boxes): the
/// release only frees the box and its value, never a foreign object.
pub fn make_borrowed_owned<T: Any + Send>(value: T) -> CHandle {
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		value,
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(refbox_release_borrowed::<T>),
		abi_version: OAKRENDER_ABI_VERSION,
	}
}

/// Panic-catching FFI wrapper for i32-returning exports.
pub fn guard<F: FnOnce() -> crate::error::Result<()>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => crate::error::OAKRENDER_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => crate::error::OAKRENDER_E_FAILED,
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

#[cfg(test)]
mod tests {
	use super::*;

	#[derive(Debug, PartialEq)]
	struct Obj(u32);

	#[test]
	fn owned_handle_refcount_and_destruction() {
		let h = make_owned(Obj(7));
		assert!(!h.is_null());
		assert_eq!(h.abi_version, OAKRENDER_ABI_VERSION);
		let before = alive_count();
		// addref/release through the stored function pointers.
		unsafe { h.addref.unwrap()(h.ctx) };
		unsafe { h.release.unwrap()(h.ctx) };
		unsafe { h.release.unwrap()(h.ctx) };
		assert_eq!(alive_count(), before - 1);
	}

	#[test]
	fn get_returns_boxed_value() {
		let h = make_owned(Obj(42));
		let v = unsafe { get::<Obj>(&h) };
		assert_eq!(v, Some(&Obj(42)));
		assert!(unsafe { get::<Obj>(&CHandle::null()) }.is_none());
		unsafe { h.release.unwrap()(h.ctx) };
	}

	#[test]
	fn borrowed_release_does_not_count() {
		let mut obj = Obj(5);
		let before = alive_count();
		let h = unsafe { make_borrowed(&mut obj) };
		assert!(!h.is_null());
		assert_eq!(alive_count(), before, "borrowed boxes are not counted");
		unsafe { h.release.unwrap()(h.ctx) };
		assert_eq!(alive_count(), before);
		// The borrowed value is intact (never dropped).
		assert_eq!(obj, Obj(5));
	}

	#[test]
	fn guard_maps_results() {
		assert_eq!(guard(|| Ok(())), 0);
		assert_eq!(guard(|| Err(crate::error::Error::Invalid)), -70001);
		assert_eq!(guard(|| panic!("boom")), -70003);
	}

	#[test]
	fn guard_handle_and_void_panic_safety() {
		// Panics map to empty handles / are swallowed.
		let h = guard_handle(|| panic!("boom"));
		assert!(h.is_null());
		let h = guard_handle(|| Err(crate::error::Error::State));
		assert!(h.is_null());
		let h = guard_handle(|| Ok(make_owned(Obj(1))));
		assert!(!h.is_null());
		unsafe { h.release.unwrap()(h.ctx) };

		guard_void(|| panic!("swallowed"));
		guard_void(|| {});
	}

	#[test]
	fn make_borrowed_null_yields_empty() {
		let h = unsafe { make_borrowed::<Obj>(std::ptr::null_mut()) };
		assert!(h.is_null());
	}

	#[test]
	fn get_mut_mutates_boxed_value() {
		let h = make_owned(Obj(3));
		unsafe {
			let v = get_mut::<Obj>(&h).unwrap();
			v.0 = 9;
			assert_eq!(get::<Obj>(&h).unwrap().0, 9);
		}
		unsafe { h.release.unwrap()(h.ctx) };
	}

	#[test]
	fn make_borrowed_owned_does_not_count() {
		let before = alive_count();
		let h = make_borrowed_owned(Obj(4));
		assert_eq!(alive_count(), before, "borrowed-owned boxes are not counted");
		assert!(!h.is_null());
		unsafe { h.release.unwrap()(h.ctx) };
		assert_eq!(alive_count(), before);
	}
}
