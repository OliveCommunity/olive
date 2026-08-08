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

use std::any::Any;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicU32, Ordering};

use crate::error::OAKNODE_E_FAILED;

/// ABI version stamped into every handle.
pub const OAKNODE_ABI_VERSION: u32 = 1;

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
#[derive(Clone, Debug)]
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
			abi_version: OAKNODE_ABI_VERSION,
		}
	}
}

/// addref 的实现：原子 +1。拥有型与借用型共用——借用型只延长盒子
/// 的寿命，不延长被借用对象。
unsafe extern "C" fn refbox_addref<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *const RefBox<T>;
		// 调用方保证句柄在借用期内有效（ctx 非空且未被释放）。
		(*rb).refs.fetch_add(1, Ordering::Relaxed);
	}
}

/// release 的实现（拥有型）：原子 -1，归零时回收盒子并销毁内含对象。
unsafe extern "C" fn refbox_release_owned<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		// AcqRel：归零这一侧要能看见最后一次引用前的全部写（含对象
		// 析构所需的内部状态）。
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(rb));
		}
	}
}

/// release 的实现（借用型，[`make_borrowed`] 的产物）：归零时只回收
/// 盒子内存，把内含对象原样忘掉——其所有权仍在借用方手里。
unsafe extern "C" fn refbox_release_borrowed<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			// 部分 move：把 value 移出临时 Box，Box 析构只释放分配；
			// value 用 forget 放弃析构（double-free 防线）。
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
		abi_version: OAKNODE_ABI_VERSION,
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
		abi_version: OAKNODE_ABI_VERSION,
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
/// Panics map to [`OAKNODE_E_FAILED`].
pub fn guard<F: FnOnce() -> crate::error::Result<()>>(f: F) -> i32 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => crate::error::OAKNODE_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => OAKNODE_E_FAILED,
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
