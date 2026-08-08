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

//! Refcounted-handle scaffolding (same per-module pattern as the other
//! crates; duplicated on purpose — handle function pointers must run
//! code from the creating DLL).

use std::sync::atomic::AtomicU32;

/// ABI version stamped into every handle.
pub const OAKSTORAGE_ABI_VERSION: u32 = 1;

/// Heap box behind a handle's `ctx`.
pub struct RefBox<T: ?Sized> {
	/// Atomic reference count.
	pub refs: AtomicU32,
	/// Boxed value.
	pub value: T,
}

/// `#[repr(C)]` mirror of the public handle structs.
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
		todo!()
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Send + 'static>(value: T) -> CHandle {
	todo!()
}

/// Borrowed handle for an object owned elsewhere.
///
/// # Safety
/// Caller guarantees `ptr` outlives every derived handle.
pub unsafe fn make_borrowed<T: Send + 'static>(ptr: *mut T) -> CHandle {
	todo!()
}

/// Typed view into a handle; `None` for empty handles.
///
/// # Safety
/// `T` must be the boxed type.
pub unsafe fn get<T: 'static>(h: &CHandle) -> Option<&T> {
	todo!()
}

/// Panic-catching FFI wrapper for i32-returning exports.
pub fn guard<F: FnOnce() -> crate::error::Result<()>>(f: F) -> i32 {
	todo!()
}

/// Panic-catching FFI wrapper for handle-returning exports.
pub fn guard_handle<F: FnOnce() -> crate::error::Result<CHandle>>(f: F) -> CHandle {
	todo!()
}

/// Panic-catching FFI wrapper for void exports.
pub fn guard_void<F: FnOnce()>(f: F) {
	todo!()
}
