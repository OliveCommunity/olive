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
//! created the object).
//!
//! The `AudioManager` is the one exception: it uses the same `CHandle`
//! layout but with singleton semantics (addref/release no-ops). See
//! `manager.rs` and `include/audio/manager.h`.
//!
//! `// CPP-PARITY: src/audio/c_api/refcounted.h` (RefCounted box,
//! make_handle_in_place, free_handle) and `src/audio/c_api/alive.cpp`
//! (alive ledger behind `oakaudio_debug_alive_count`).

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicI32, AtomicU32, Ordering};

use crate::error::{Error, OAKAUDIO_E_FAILED, OAKAUDIO_OK};

/// ABI version stamped into every handle.
pub const OAKAUDIO_ABI_VERSION: u32 = 1;

/// Live-object ledger behind `oakaudio_debug_alive_count`.
///
/// `// CPP-PARITY: src/audio/c_api/alive.cpp` (`g_alive`).
static ALIVE: AtomicI32 = AtomicI32::new(0);

/// Current number of live refcounted oakaudio objects.
pub fn alive_count() -> i32 {
	ALIVE.load(Ordering::Relaxed)
}

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
/// Handles cross the C ABI by value (the C caller copies the 4-field
/// struct), so the type is `Copy`/`Clone` — the oaknode crate only derives
/// `Clone`, but `Copy` matches the C semantics exactly and lets exports and
/// tests pass the same handle around freely. No `Drop`: releasing goes
/// through the explicit `free_*`/`release` path, never on scope exit.
#[repr(C)]
#[derive(Clone, Copy)]
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

// Handles are passed by value across the C ABI and used from the caller's
// thread; the objects behind them synchronize their own state.
unsafe impl Send for CHandle {}

impl CHandle {
	/// The empty handle.
	pub fn null() -> Self {
		CHandle {
			ctx: std::ptr::null_mut(),
			addref: None,
			release: None,
			abi_version: OAKAUDIO_ABI_VERSION,
		}
	}

	/// True when the handle carries no object.
	pub fn is_null(&self) -> bool {
		self.ctx.is_null()
	}
}

/// `// CPP-PARITY: src/audio/c_api/refcounted.h` (`ref_counted_addref`).
unsafe extern "C" fn owned_addref<T: Send + 'static>(ctx: *mut std::ffi::c_void) {
	// SAFETY: `ctx` is either NULL or points to a `RefBox<T>` created by
	// `make_owned`; we only touch it through the reference while it is live.
	if let Some(b) = unsafe { (ctx as *const RefBox<T>).as_ref() } {
		b.refs.fetch_add(1, Ordering::Relaxed);
	}
}

/// `// CPP-PARITY: src/audio/c_api/refcounted.h` (`ref_counted_release`):
/// destroys the box at zero and decrements the alive ledger.
unsafe extern "C" fn owned_release<T: Send + 'static>(ctx: *mut std::ffi::c_void) {
	// SAFETY: `ctx` is either NULL or points to a live `RefBox<T>` created
	// by `make_owned`; the refcount guards against double-free, and the box
	// is only reclaimed once the count reaches zero.
	if let Some(b) = unsafe { (ctx as *const RefBox<T>).as_ref() } {
		if b.refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(unsafe { Box::from_raw(ctx as *mut RefBox<T>) });
			ALIVE.fetch_sub(1, Ordering::Relaxed);
		}
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Send + 'static>(value: T) -> CHandle {
	let b = Box::new(RefBox {
		refs: AtomicU32::new(1),
		value,
	});
	ALIVE.fetch_add(1, Ordering::Relaxed);
	CHandle {
		ctx: Box::into_raw(b) as *mut std::ffi::c_void,
		addref: Some(owned_addref::<T>),
		release: Some(owned_release::<T>),
		abi_version: OAKAUDIO_ABI_VERSION,
	}
}

/// No-op addref/release for singleton (borrowed) handles.
///
/// `// CPP-PARITY: src/audio/c_api/manager.cpp` (`singleton_addref` /
/// `singleton_release`).
unsafe extern "C" fn noop_ref(_ctx: *mut std::ffi::c_void) {}

/// Borrowed handle for an object owned elsewhere (addref/release are
/// no-ops; nothing is ever freed through the handle).
///
/// # Safety
/// Caller guarantees `ptr` outlives every derived handle.
pub unsafe fn make_borrowed<T: Send + 'static>(ptr: *mut T) -> CHandle {
	CHandle {
		ctx: ptr as *mut std::ffi::c_void,
		addref: Some(noop_ref),
		release: Some(noop_ref),
		abi_version: OAKAUDIO_ABI_VERSION,
	}
}

/// Typed view into an owned handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: 'static>(h: &CHandle) -> Option<&T> {
	if h.ctx.is_null() {
		return None;
	}
	// SAFETY: caller guarantees `h` is a valid owned handle whose ctx points
	// to a `RefBox<T>`; the handle stays alive through the returned borrow.
	let v = unsafe { &(*(h.ctx as *const RefBox<T>)).value };
	Some(v)
}

/// Typed mutable view into an owned handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get_mut<T: 'static>(h: &CHandle) -> Option<&mut T> {
	if h.ctx.is_null() {
		return None;
	}
	// SAFETY: caller guarantees `h` is a valid owned handle whose ctx points
	// to a `RefBox<T>`; the handle stays alive through the returned borrow,
	// and the caller must not alias it with other live borrows.
	let v = unsafe { &mut (*(h.ctx as *mut RefBox<T>)).value };
	Some(v)
}

/// Shared free() body: release the ctx, no-op on NULL/empty handle.
///
/// `// CPP-PARITY: src/audio/c_api/refcounted.h` (`free_handle`).
///
/// # Safety
/// `self_` must be a valid handle pointer or NULL.
pub unsafe fn free_handle(h: *mut CHandle) {
	// SAFETY: caller guarantees `h` is a valid handle pointer or NULL.
	if let Some(r) = unsafe { h.as_mut() } {
		if !r.ctx.is_null() {
			if let Some(release) = r.release {
				// SAFETY: the release fn belongs to the same DLL and
				// accepts the ctx it originally created.
				unsafe { release(r.ctx) };
			}
			r.ctx = std::ptr::null_mut();
		}
	}
}

/// Panic-catching FFI wrapper for i32-returning exports.
pub fn guard<F: FnOnce() -> crate::error::Result<()>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => OAKAUDIO_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => OAKAUDIO_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for handle-returning exports.
pub fn guard_handle<F: FnOnce() -> crate::error::Result<CHandle>>(f: F) -> CHandle {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(h)) => h,
		Ok(Err(_)) | Err(_) => CHandle::null(),
	}
}

/// Panic-catching FFI wrapper for i32 value-returning exports (errors map
/// to the negative error code).
pub fn guard_int<F: FnOnce() -> crate::error::Result<i32>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(v)) => v,
		Ok(Err(e)) => e.code(),
		Err(_) => OAKAUDIO_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for void exports.
pub fn guard_void<F: FnOnce()>(f: F) {
	let _ = catch_unwind(AssertUnwindSafe(f));
}

/// Copy a human-readable error string into a C buffer (NUL-terminated,
/// truncated to fit). Returns the required size including the NUL.
///
/// `// CPP-PARITY: src/audio/c_api/manager.cpp` (`write_error`).
pub fn write_error(s: &str, buf: *mut std::ffi::c_char, buf_size: i32) {
	if !buf.is_null() && buf_size > 0 {
		let bytes = s.as_bytes();
		let n = bytes.len().min((buf_size - 1) as usize);
		unsafe {
			std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, n);
			*(buf as *mut u8).add(n) = 0;
		}
	}
}

/// Convenience: map a condition to an Invalid error.
pub fn invalid_if(cond: bool) -> crate::error::Result<()> {
	if cond {
		Err(Error::Invalid)
	} else {
		Ok(())
	}
}
