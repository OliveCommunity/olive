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
//! than shared — each module DLL must run its own addref/release code (the
//! function pointers in a handle always point into the DLL that created the
//! object).

use std::sync::atomic::AtomicU32;

/// ABI version stamped into every handle.
pub const OAKTASK_ABI_VERSION: u32 = 1;

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
#[derive(Clone, Copy, Debug, Default)]
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

// Handles are shared between the calling thread and the manager worker
// threads (same ownership model as a C++ shared_ptr). The raw pointer and
// function-pointer fields are inherently `!Send`/`!Sync`; the box behind
// `ctx` is either `Send + Sync` (task state, atomics) or a plain pointer
// (borrowed handles), both of which are safe to share through the
// refcounted handle.
unsafe impl Send for CHandle {}
unsafe impl Sync for CHandle {}

unsafe extern "C" fn noop_addref(_ctx: *mut std::ffi::c_void) {}

unsafe extern "C" fn noop_release(_ctx: *mut std::ffi::c_void) {}

unsafe extern "C" fn owned_addref<T: 'static>(ctx: *mut std::ffi::c_void) {
	if !ctx.is_null() {
		// CPP-PARITY: src/task/c_api/taskhandle.h (task_addref)
		unsafe {
			(*(ctx as *const RefBox<T>)).refs.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
		}
	}
}

unsafe extern "C" fn owned_release<T: 'static>(ctx: *mut std::ffi::c_void) {
	if ctx.is_null() {
		return;
	}
	// CPP-PARITY: src/task/c_api/taskhandle.h (task_release)
	let b = ctx as *const RefBox<T>;
	let last = unsafe { (*b).refs.fetch_sub(1, std::sync::atomic::Ordering::SeqCst) };
	if last == 1 {
		unsafe {
			drop(Box::from_raw(ctx as *mut RefBox<T>));
		}
	}
}

/// Release function for borrowed handles: destroys only the box, never the
/// pointee.
///
/// CPP-PARITY: src/task/c_api/taskhandle.h (wrap_borrowed)
unsafe extern "C" fn borrowed_release<T: 'static>(ctx: *mut std::ffi::c_void) {
	if ctx.is_null() {
		return;
	}
	let b = ctx as *const RefBox<*mut T>;
	let last = unsafe { (*b).refs.fetch_sub(1, std::sync::atomic::Ordering::SeqCst) };
	if last == 1 {
		unsafe {
			drop(Box::from_raw(ctx as *mut RefBox<*mut T>));
		}
	}
}

impl CHandle {
	/// The empty handle. `ctx` is null but the fn pointers and ABI version
	/// are stamped so callers can unconditionally call `release`.
	pub fn null() -> Self {
		CHandle {
			ctx: std::ptr::null_mut(),
			addref: Some(noop_addref),
			release: Some(noop_release),
			abi_version: OAKTASK_ABI_VERSION,
		}
	}

	/// Whether the handle is empty (`ctx == NULL`).
	pub fn is_null(&self) -> bool {
		self.ctx.is_null()
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Send + 'static>(value: T) -> CHandle {
	let b = Box::new(RefBox { refs: AtomicU32::new(1), value });
	CHandle {
		ctx: Box::into_raw(b) as *mut std::ffi::c_void,
		addref: Some(owned_addref::<T>),
		release: Some(owned_release::<T>),
		abi_version: OAKTASK_ABI_VERSION,
	}
}

/// Borrowed handle for an object owned elsewhere (release frees only the
/// box).
///
/// # Safety
/// Caller guarantees `ptr` outlives every derived handle.
pub unsafe fn make_borrowed<T: Send + 'static>(ptr: *mut T) -> CHandle {
	if ptr.is_null() {
		return CHandle::null();
	}
	let b = Box::new(RefBox { refs: AtomicU32::new(1), value: ptr });
	CHandle {
		ctx: Box::into_raw(b) as *mut std::ffi::c_void,
		addref: Some(owned_addref::<*mut T>),
		release: Some(borrowed_release::<T>),
		abi_version: OAKTASK_ABI_VERSION,
	}
}

/// Typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: 'static>(h: &CHandle) -> Option<&T> {
	if h.ctx.is_null() {
		return None;
	}
	unsafe { Some(&(*(h.ctx as *const RefBox<T>)).value) }
}

/// Typed mutable view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type, and the handle must not be concurrently
/// shared mutably.
pub unsafe fn get_mut<T: 'static>(h: &CHandle) -> Option<&mut T> {
	if h.ctx.is_null() {
		return None;
	}
	unsafe { Some(&mut (*(h.ctx as *mut RefBox<T>)).value) }
}

/// Panic-catching FFI wrapper for i32-returning exports.
pub fn guard<F: FnOnce() -> crate::error::Result<()>>(f: F) -> i32 {
	match std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)) {
		Ok(Ok(())) => crate::error::OAKTASK_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => crate::error::OAKTASK_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for handle-returning exports.
pub fn guard_handle<F: FnOnce() -> crate::error::Result<CHandle>>(f: F) -> CHandle {
	match std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)) {
		Ok(Ok(h)) => h,
		Ok(Err(_)) | Err(_) => CHandle::null(),
	}
}

/// Panic-catching FFI wrapper for void exports.
pub fn guard_void<F: FnOnce()>(f: F) {
	let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(f));
}
