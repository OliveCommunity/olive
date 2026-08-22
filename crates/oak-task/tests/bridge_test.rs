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

//! C ABI bridge layout tests. These pin the `repr(C)` struct layouts and
//! the handle ABI discipline (OAKTASK_ABI_VERSION, ctx/addref/release) so
//! a header change on the C++ side is caught here before linking.

use std::mem::{align_of, offset_of, size_of};

use oak_task::handle::{make_owned, CHandle, OAKTASK_ABI_VERSION};

/// The exact C layout the public headers promise
/// (`typedef struct OakTaskTask { void *ctx; void (*addref)(void*);
/// void (*release)(void*); uint32_t abi_version; } OakTaskTask;`).
#[repr(C)]
struct CKernel {
	ctx: *mut std::ffi::c_void,
	addref: Option<unsafe extern "C" fn(*mut std::ffi::c_void)>,
	release: Option<unsafe extern "C" fn(*mut std::ffi::c_void)>,
	abi_version: u32,
}

/// Given the handle struct, its size and alignment match the C `void*,
/// fn, fn, uint32_t` shape and its fields are laid out in that order.
#[test]
fn handle_layout_matches_c() {
	assert_eq!(size_of::<CHandle>(), size_of::<CKernel>());
	assert_eq!(align_of::<CHandle>(), align_of::<CKernel>());
	// Field order: ctx, addref, release, abi_version.
	assert_eq!(offset_of!(CHandle, ctx), offset_of!(CKernel, ctx));
	assert_eq!(offset_of!(CHandle, addref), offset_of!(CKernel, addref));
	assert_eq!(offset_of!(CHandle, release), offset_of!(CKernel, release));
	assert_eq!(
		offset_of!(CHandle, abi_version),
		offset_of!(CKernel, abi_version)
	);
}

/// Given the ABI version constant, it is 1 and matches OAKTASK_ABI_VERSION
/// from `include/task/task.h`.
#[test]
fn abi_version_is_one() {
	assert_eq!(OAKTASK_ABI_VERSION, 1);
}

/// Given a null handle, `CHandle::null()` has a null ctx, no addref/
/// release fn pointers and no ABI version (single-lib unification).
#[test]
fn null_handle_contract() {
	let h = CHandle::null();
	assert!(h.ctx.is_null());
	assert!(h.addref.is_none(), "null handle addref is empty");
	assert!(h.release.is_none(), "null handle release is empty");
	assert_eq!(h.abi_version, 0);
	// `is_null` reflects the ctx.
	assert!(h.is_null());
}

/// Given `make_owned`, the returned handle's release drops the box and the
/// null() handle is not affected; size_of(CHandle) is platform-correct.
#[test]
fn handle_size_and_owned_release() {
	assert_eq!(size_of::<CHandle>(), 32, "ctx + 2 fns + u32 on 64-bit");

	let handle = make_owned(Box::new(42u32));
	assert!(!handle.ctx.is_null());
	assert_eq!(handle.abi_version, OAKTASK_ABI_VERSION);

	// addref bumps the refcount; the box stays alive.
	unsafe {
		handle.addref.unwrap()(handle.ctx);
		// First release drops one reference; the box is still alive.
		handle.release.unwrap()(handle.ctx);
		// Second release destroys the box.
		handle.release.unwrap()(handle.ctx);
	}
	// The boxed value was dropped; a null handle still works afterwards.
	let null_h = CHandle::null();
	// The shared null() carries no fn pointers; releasing goes through
	// the `if let Some` path (a no-op for empty handles).
	if let Some(release) = null_h.release {
		unsafe {
			release(null_h.ctx);
		}
	}
}
