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

//! Refcounted-handle scaffolding. Same pattern as the oakplugin crate
//! (`src/plugin/rust/src/handle.rs`); intentionally duplicated rather
//! than shared — each module DLL must run its own addref/release code
//! (the function pointers in a handle always point into the DLL that
//! created the object).
//!
//! Mirrors the C side (`include/common/handle.h`):
//!
//! ```c
//! typedef struct OakXxx {
//!     void *ctx;
//!     void (*addref)(void *ctx);
//!     void (*release)(void *ctx);
//!     uint32_t abi_version;
//! } OakXxx;
//! ```
//!
//! Handles are passed by value; `ctx` points to a heap [`RefBox<T>`].
//! The `addref`/`release` function pointers always point into this crate.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, Ordering};

/// ABI version stamped into every handle.
pub const OAKCOMMON_ABI_VERSION: u32 = 1;

/// Heap box behind a handle's `ctx`.
///
/// `value` is deliberately the FIRST field so that `ctx` (which points at
/// the box head) aliases the boxed value: the C-facing `ffi.rs` code
/// frequently casts `ctx` directly to `*mut T` / `*const T`, and that cast
/// only lands on the real value when it sits at offset 0. `refs` follows,
/// located via field access (never a raw offset) by the addref/release
/// thunks. `#[repr(C)]` locks the layout so those direct casts are sound.
// CPP-PARITY: matches `include/common/handle.h` where `ctx` is an opaque
// pointer; the C side never dereferences it, so this layout is private to
// this crate.
#[repr(C)]
pub struct RefBox<T: Sized> {
	/// Boxed value (at offset 0 — see module doc).
	pub value: T,
	/// Atomic reference count.
	pub refs: AtomicU32,
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
		Self {
			ctx: std::ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: OAKCOMMON_ABI_VERSION,
		}
	}

	/// Whether this is an empty handle (`ctx == NULL`).
	pub fn is_null(&self) -> bool {
		self.ctx.is_null()
	}
}

/// addref thunk: atomically increments the count. Shared by owned and
/// borrowed boxes — for a borrowed handle addref only extends the life of
/// the box, not of the borrowed object.
unsafe extern "C" fn refbox_addref<T: Send + Sized + 'static>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *const RefBox<T>;
		// Caller guarantees the handle is alive (ctx non-null, not yet
		// released) for the duration of the borrow.
		(*rb).refs.fetch_add(1, Ordering::Relaxed);
	}
}

/// release thunk (owned): atomically decrements; at zero the box and its
/// value are destroyed.
unsafe extern "C" fn refbox_release_owned<T: Send + Sized + 'static>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		// AcqRel: the side that reaches zero must observe all writes made
		// before the final release (including internal state the value's
		// destructor needs).
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(rb));
		}
	}
}

/// release thunk (borrowed, produced by [`make_borrowed`]): at zero only
/// the box allocation is reclaimed; the value inside is forgotten — its
/// ownership remains with the borrower.
unsafe extern "C" fn refbox_release_borrowed<T: Send + Sized + 'static>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			// Partial move: move the value out of a temporary Box, then
			// forget it so the Box drop only frees the allocation and the
			// value's destructor never runs (double-free guard).
			std::mem::forget((Box::from_raw(rb)).value);
		}
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Send + Sized + 'static>(value: T) -> CHandle {
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		value,
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(refbox_release_owned::<T>),
		abi_version: OAKCOMMON_ABI_VERSION,
	}
}

/// Borrowed handle for an object owned elsewhere (release frees only
/// the box).
///
/// The value is bit-copied into the box ("borrowing copy"); the box never
/// runs the value's destructor — the borrower owns the original object
/// and is responsible for destroying it.
///
/// # Safety
/// Caller guarantees `ptr` outlives every derived handle and that its
/// value is neither moved nor destroyed for the duration of the borrow.
pub unsafe fn make_borrowed<T: Send + Sized + 'static>(ptr: *mut T) -> CHandle {
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
		abi_version: OAKCOMMON_ABI_VERSION,
	}
}

/// Typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: Sized + 'static>(h: &CHandle) -> Option<&T> {
	if h.is_null() {
		return None;
	}
	unsafe { Some(&(*(h.ctx as *const RefBox<T>)).value) }
}

/// Mutable typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type, and the caller must not alias the returned
/// reference with any other live reference into the same handle.
pub unsafe fn get_mut<T: Sized + 'static>(h: &CHandle) -> Option<&mut T> {
	if h.is_null() {
		return None;
	}
	unsafe { Some(&mut (*(h.ctx as *mut RefBox<T>)).value) }
}

/// Panic-catching FFI wrapper for i32-returning exports.
pub fn guard<F: FnOnce() -> crate::error::Result<()>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => crate::error::OAKCOMMON_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => crate::error::OAKCOMMON_E_FAILED,
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
	use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
	use std::sync::Arc;

	use super::*;

	/// Test payload that counts how many times its destructor ran.
	struct DropCounter {
		/// Shared counter bumped by `Drop::drop`.
		drops: Arc<AtomicUsize>,
	}

	impl DropCounter {
		/// A new counter plus its shared tally.
		fn new() -> (Self, Arc<AtomicUsize>) {
			let drops = Arc::new(AtomicUsize::new(0));
			(
				DropCounter {
					drops: Arc::clone(&drops),
				},
				drops,
			)
		}
	}

	impl Drop for DropCounter {
		fn drop(&mut self) {
			self.drops.fetch_add(1, AtomicOrdering::SeqCst);
		}
	}

	/// Read the refcount behind a handle (test-only peek).
	unsafe fn refs_of<T: Send + Sized + 'static>(h: &CHandle) -> u32 {
		unsafe { (*(h.ctx as *const RefBox<T>)).refs.load(Ordering::Relaxed) }
	}

	#[test]
	fn null_handle_is_null_and_stamped() {
		let h = CHandle::null();
		assert!(h.is_null());
		assert!(h.ctx.is_null());
		assert!(h.addref.is_none());
		assert!(h.release.is_none());
		assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
	}

	#[test]
	fn make_owned_starts_at_one_ref_and_exposes_value() {
		let (value, drops) = DropCounter::new();
		let h = make_owned(value);
		assert!(!h.is_null());
		assert_eq!(h.abi_version, OAKCOMMON_ABI_VERSION);
		assert!(h.addref.is_some());
		assert!(h.release.is_some());
		unsafe {
			assert_eq!(refs_of::<DropCounter>(&h), 1);
			// get() sees the boxed value.
			let v: &DropCounter = get::<DropCounter>(&h).unwrap();
			assert_eq!(v.drops.load(AtomicOrdering::SeqCst), 0);
			(h.release.unwrap())(h.ctx);
		}
		// Releasing the last ref destroyed the box and ran the destructor.
		assert_eq!(drops.load(AtomicOrdering::SeqCst), 1);
	}

	#[test]
	fn owned_addref_release_balance_then_drop_at_zero() {
		let (value, drops) = DropCounter::new();
		let h = make_owned(value);
		unsafe {
			(h.addref.unwrap())(h.ctx);
			(h.addref.unwrap())(h.ctx);
			assert_eq!(refs_of::<DropCounter>(&h), 3);

			(h.release.unwrap())(h.ctx);
			assert_eq!(refs_of::<DropCounter>(&h), 2);
			assert_eq!(drops.load(AtomicOrdering::SeqCst), 0);
			(h.release.unwrap())(h.ctx);
			assert_eq!(refs_of::<DropCounter>(&h), 1);
			assert_eq!(drops.load(AtomicOrdering::SeqCst), 0);
			(h.release.unwrap())(h.ctx);
		}
		// Final release to zero ran the destructor exactly once.
		assert_eq!(drops.load(AtomicOrdering::SeqCst), 1);
	}

	#[test]
	fn borrowed_release_to_zero_does_not_run_value_destructor() {
		let (value, drops) = DropCounter::new();
		let mut value = value;
		let h = unsafe { make_borrowed(&mut value) };
		assert!(!h.is_null());
		unsafe {
			assert_eq!(refs_of::<DropCounter>(&h), 1);
			(h.addref.unwrap())(h.ctx);
			assert_eq!(refs_of::<DropCounter>(&h), 2);
			(h.release.unwrap())(h.ctx);
			assert_eq!(refs_of::<DropCounter>(&h), 1);
			// Releasing the borrowed box to zero frees only the box.
			(h.release.unwrap())(h.ctx);
		}
		// The value's destructor must NOT have run; ownership stayed here.
		assert_eq!(drops.load(AtomicOrdering::SeqCst), 0);
		drop(value);
		assert_eq!(drops.load(AtomicOrdering::SeqCst), 1);
	}

	#[test]
	fn borrowed_handle_sees_borrowed_value_contents() {
		let mut data: u64 = 0xdead_beef;
		let h = unsafe { make_borrowed(&mut data) };
		unsafe {
			let v: &u64 = get::<u64>(&h).unwrap();
			assert_eq!(*v, 0xdead_beef);
			(h.release.unwrap())(h.ctx);
		}
	}

	#[test]
	fn make_borrowed_null_ptr_yields_null_handle() {
		let h = unsafe { make_borrowed::<u64>(std::ptr::null_mut()) };
		assert!(h.is_null());
		assert!(h.addref.is_none());
		assert!(h.release.is_none());
	}

	#[test]
	fn get_on_null_handle_is_none() {
		let h = CHandle::null();
		assert!(unsafe { get::<u64>(&h) }.is_none());
	}

	#[test]
	fn get_returns_typed_view_of_owned_box() {
		let h = make_owned(String::from("hello"));
		unsafe {
			let s: &String = get::<String>(&h).unwrap();
			assert_eq!(s, "hello");
			(h.release.unwrap())(h.ctx);
		}
	}

	#[test]
	fn guard_maps_result_to_status_code() {
		assert_eq!(guard(|| Ok(())), crate::error::OAKCOMMON_OK);
		assert_eq!(
			guard(|| Err(crate::error::Error::Invalid)),
			crate::error::OAKCOMMON_E_INVALID
		);
		assert_eq!(
			guard(|| Err(crate::error::Error::State)),
			crate::error::OAKCOMMON_E_STATE
		);
		assert_eq!(
			guard(|| Err(crate::error::Error::Failed("x".into()))),
			crate::error::OAKCOMMON_E_FAILED
		);
		assert_eq!(
			guard(|| Err(crate::error::Error::NotFound)),
			crate::error::OAKCOMMON_E_NOT_FOUND
		);
		assert_eq!(
			guard(|| Err(crate::error::Error::NoMem)),
			crate::error::OAKCOMMON_E_NOMEM
		);
	}

	#[test]
	fn guard_catches_panic_as_e_failed() {
		let code = guard(|| -> crate::error::Result<()> { panic!("kaboom") });
		assert_eq!(code, crate::error::OAKCOMMON_E_FAILED);
	}

	#[test]
	fn guard_handle_passes_through_success() {
		let h = guard_handle(|| Ok(make_owned(42u32)));
		assert!(!h.is_null());
		unsafe {
			assert_eq!(*get::<u32>(&h).unwrap(), 42);
			(h.release.unwrap())(h.ctx);
		}
	}

	#[test]
	fn guard_handle_maps_err_and_panic_to_null() {
		let h = guard_handle(|| Err(crate::error::Error::NoMem));
		assert!(h.is_null());
		let h = guard_handle(|| -> crate::error::Result<CHandle> { panic!("kaboom") });
		assert!(h.is_null());
	}

	#[test]
	fn guard_void_runs_closure_and_swallows_panic() {
		let ran = Arc::new(AtomicUsize::new(0));
		let r = Arc::clone(&ran);
		guard_void(move || {
			r.fetch_add(1, AtomicOrdering::SeqCst);
		});
		assert_eq!(ran.load(AtomicOrdering::SeqCst), 1);
		// A panicking closure must not unwind across the FFI boundary.
		guard_void(|| panic!("kaboom"));
	}
}
