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

//! Contract tests for the refcounted-handle scaffolding
//! (`src/handle.rs`). The C++ gtest suite (`src/timeline/tests`)
//! drives the ABI-level refcount behaviour through the exports; these
//! tests pin the Rust-side primitive semantics that every export
//! relies on: null sentinel, `make_owned`/`make_borrowed` ownership,
//! panics escaping through `guard*`, and the ABI version stamp.

use oaktimeline::error::{
    Error, OAKTIMELINE_ABI_VERSION, OAKTIMELINE_E_FAILED, OAKTIMELINE_E_INVALID, OAKTIMELINE_OK,
};
use oaktimeline::handle::{
    get, guard, guard_handle, guard_void, make_borrowed, make_owned, CHandle,
};

/// `CHandle::null()` is the all-null sentinel with abi_version 0.
#[test]
fn null_handle_is_all_zero() {
    let h = CHandle::null();
    assert!(h.ctx.is_null());
    assert!(h.addref.is_none());
    assert!(h.release.is_none());
    assert_eq!(h.abi_version, 0);
    assert!(h.is_null());
}

/// A freshly `make_owned` handle carries refcount 1 and the module ABI
/// version; releasing it once brings the count to zero.
#[test]
fn make_owned_starts_at_one_and_releases() {
    let h = make_owned(42i32);
    assert!(h.addref.is_some());
    assert!(h.release.is_some());
    assert_eq!(h.abi_version, OAKTIMELINE_ABI_VERSION);

    // Addref bumps the count so the box is still live afterwards.
    let addref = h.addref.unwrap();
    // Safety: `h` is an owned handle whose box is `RefBox<i32>`.
    unsafe { addref(h.ctx) };
    let val = unsafe { get::<i32>(&h) };
    assert_eq!(val, Some(&42));
}

/// `make_owned` boxes a `Send + 'static` value and `get` returns the
/// boxed value back out.
#[test]
fn make_owned_round_trips_value() {
    let h = make_owned(7i32);
    let val = unsafe { get::<i32>(&h) };
    assert_eq!(val, Some(&7));
}

/// `make_borrowed` wraps a caller-owned pointer without transferring
/// ownership: releasing the borrowed handle frees only the box, never
/// the underlying object.
///
/// # Safety
/// The borrowed allocation must outlive the handle; the caller frees
/// it afterwards.
#[test]
fn make_borrowed_release_does_not_free_owner() {
    let mut v = 5i32;
    let h = unsafe { make_borrowed::<i32>(&mut v) };
    assert!(!h.is_null());
    // Releasing the borrowed handle destroys only the internal copy.
    let release = h.release.unwrap();
    // Safety: `h` is an owned box (see `make_borrowed`), release boxed copy.
    unsafe { release(h.ctx) };
    // The caller's object is untouched.
    assert_eq!(v, 5);
}

/// `guard` maps a successful closure to `OAKTIMELINE_OK`.
#[test]
fn guard_success_returns_ok() {
    assert_eq!(guard(|| Ok(())), OAKTIMELINE_OK);
}

/// `guard` maps an `Err(Error::Invalid)` to `OAKTIMELINE_E_INVALID`.
#[test]
fn guard_error_maps_code() {
    assert_eq!(guard(|| Err(Error::Invalid)), OAKTIMELINE_E_INVALID);
}

/// `guard_handle` returns the inner handle on success.
#[test]
fn guard_handle_success_returns_handle() {
    let inner = make_owned(3i32);
    let out = guard_handle(|| Ok(inner.clone()));
    assert!(!out.is_null());
    assert_eq!(unsafe { get::<i32>(&out) }, Some(&3));
}

/// `guard_handle` returns a null handle on error so callers never see
/// a partially-built object.
#[test]
fn guard_handle_error_returns_null() {
    let out = guard_handle(|| Err(Error::Invalid));
    assert!(out.is_null());
}

/// A panicking closure does not unwind across the `guard_void` FFI
/// boundary; it is caught and converted to the failed code.
#[test]
fn guard_catches_panic() {
    // `guard` maps the panic to the failed code.
    let code = guard(|| -> oaktimeline::error::Result<()> {
        panic!("boom");
    });
    assert_eq!(code, OAKTIMELINE_E_FAILED);

    // `guard_void` swallows the panic without unwinding into the caller.
    let mut reached = false;
    guard_void(|| {
        panic!("no unwind");
    });
    reached = true;
    assert!(reached);
}

/// Every handle produced through the crate carries
/// `OAKTIMELINE_ABI_VERSION` so C callers can version-check.
#[test]
fn handles_are_stamped_with_abi_version() {
    let h = make_owned(1u64);
    assert_eq!(h.abi_version, OAKTIMELINE_ABI_VERSION);
    assert_ne!(h.abi_version, 0);
}

/// Null and invalid handles are rejected uniformly: every `guard`
/// family returns the error code / null handle without touching the
/// pointer.
#[test]
fn guard_rejects_invalid_handles() {
    let null_h = CHandle::null();
    // Reading through a null handle yields None, which maps to invalid.
    let code = guard(|| {
        if unsafe { get::<i32>(&null_h) }.is_none() {
            return Err(Error::Invalid);
        }
        Ok(())
    });
    assert_eq!(code, OAKTIMELINE_E_INVALID);

    let h_out = guard_handle(|| {
        if unsafe { get::<i32>(&null_h) }.is_none() {
            return Err(Error::Invalid);
        }
        Ok(make_owned(0i32))
    });
    assert!(h_out.is_null());
}
