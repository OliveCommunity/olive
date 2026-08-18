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

//! Refcounted-handle scaffolding for the oakengine facade boundary.
//!
//! Single-lib unification made module-to-module calls plain Rust; the
//! facade (oakengine) is the only remaining consumer of `CHandle`s in
//! this crate — it boxes oaknode domain objects (`Project`,
//! `NodeRef`) and small ABI payloads behind [`CHandle`]s so the frozen
//! C API keeps working unchanged, and oakstorage reuses the same boxes
//! for the write-through session. This module is that surface:
//! [`make_owned`]/[`make_owned_with`] create the boxes, [`get`] borrows
//! their payloads, [`RefBox`] is the box layout.
//!
//! The crate's own object references never travel through handles, and
//! the panic-catching `guard*` wrappers from the old FFI era were
//! removed together with the crate's C exports (oakengine has its own
//! guard layer).

use std::any::Any;
use std::sync::atomic::{AtomicU32, Ordering};

/// ABI version stamped into every handle.
pub const OAKNODE_ABI_VERSION: u32 = 1;

/// Heap box behind a handle's `ctx`.
///
/// `repr(C)`: the field order is the stable prefix layout
/// [`RefBoxHeader`] relies on for type discrimination across boxes of
/// different payloads sharing one handle channel (the render seam
/// downcasts plugin-job boxes vs texture boxes).
#[repr(C)]
pub struct RefBox<T: ?Sized> {
	/// Atomic reference count.
	pub refs: AtomicU32,
	/// Type identity of the boxed value (stamped by [`make_owned`];
	/// [`get_checked`] compares it before reading [`RefBox::value`]).
	pub type_id: std::any::TypeId,
	/// Boxed value.
	pub value: T,
}

/// The fixed-size prefix of every [`RefBox`] (layout-stable across
/// payload types because [`RefBox`] is `repr(C)`).
#[repr(C)]
struct RefBoxHeader {
	/// Mirror of [`RefBox::refs`] (present for the layout prefix;
	/// never read here).
	_refs: AtomicU32,
	type_id: std::any::TypeId,
}

/// The shared ABI value-handle type (single-lib unification, see
/// `docs/zh/plans/riir/single-lib.md`): one canonical
/// `{ctx, addref, release, abi_version}` type in `oakcore-rs`, re-exported
/// here so the facade's handle scaffolding stays source-compatible.
pub use oakcore_rs::handle::CHandle;

/// addref implementation: atomic +1. Shared by owned and facade boxes —
/// a borrowed copy only extends the box's lifetime, never the borrowed
/// object's.
unsafe extern "C" fn refbox_addref<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *const RefBox<T>;
		// Caller guarantees the handle is alive (ctx non-null and not
		// released) for the duration of the call.
		(*rb).refs.fetch_add(1, Ordering::Relaxed);
	}
}

/// release implementation (owned): atomic -1, frees the box and destroys
/// the boxed value at zero.
unsafe extern "C" fn refbox_release_owned<T: Any + Send>(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<T>;
		// AcqRel: the zeroing side must observe every write from the last
		// reference (including state the destructor needs).
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(rb));
		}
	}
}

/// Owned handle with count 1; empty on allocation failure.
pub fn make_owned<T: Any + Send>(value: T) -> CHandle {
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		type_id: std::any::TypeId::of::<T>(),
		value,
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(refbox_release_owned::<T>),
		abi_version: OAKNODE_ABI_VERSION,
	}
}

/// Owned handle with count 1 and a caller-provided release routine
/// (used by the facade for the alive-counted project boxes, where the
/// release must also update the debug counter).
pub fn make_owned_with<T: Any + Send>(
	value: T,
	release: unsafe extern "C" fn(*mut std::ffi::c_void),
) -> CHandle {
	let rb = Box::into_raw(Box::new(RefBox {
		refs: AtomicU32::new(1),
		type_id: std::any::TypeId::of::<T>(),
		value,
	}));
	CHandle {
		ctx: rb as *mut std::ffi::c_void,
		addref: Some(refbox_addref::<T>),
		release: Some(release),
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

/// Typed view with type discrimination: `None` when the handle is
/// empty **or** boxes a different payload type (the render seam probes
/// texture-channel handles for plugin-job payloads this way without
/// knowing the producer).
///
/// # Safety
/// `h` must be either empty or a live handle created by
/// [`make_owned`]/[`make_owned_with`] for the duration of the call.
pub unsafe fn get_checked<T: Any>(h: &CHandle) -> Option<&T> {
	if h.ctx.is_null() {
		return None;
	}
	// SAFETY: every live box starts with the repr(C) RefBox prefix;
	// the caller guarantees the handle is alive.
	let header = unsafe { &*(h.ctx as *const RefBoxHeader) };
	if header.type_id != std::any::TypeId::of::<T>() {
		return None;
	}
	unsafe { Some(&(*(h.ctx as *const RefBox<T>)).value) }
}
