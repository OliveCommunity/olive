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

//! C ABI export layer: implements `include/codec/*.h` verbatim.
//!
//! Organization: one submodule per public header (frame / decoder /
//! encoder / format / conform / proxy / task). The authoritative function
//! list is the header itself; each export only unwraps handles, calls the
//! safe Rust domains, and maps results through [`crate::handle::guard*`].
//! `include/codec/error.h` exports macros only, so it is folded into the
//! preamble below instead of getting its own submodule.
//!
//! Shared helpers live here: the two-stage string convention
//! ([`string_out`]), C-string decoding ([`c_str`]) and in-place handle
//! release ([`free_handle`]) — all mirroring the `c_api/*.cpp` helpers.

/// `include/codec/error.h` — macros only, no exported functions.
///
/// `OAKCODEC_OK` and the `OAKCODEC_E_*` codes are mirrored as
/// [`crate::error`] constants; `OAKCODEC_ABI_VERSION` lives in
/// [`crate::handle`].
pub mod conform;
pub mod decoder;
pub mod encoder;
pub mod format;
pub mod frame;
pub mod proxy;
pub mod task;

use std::ffi::{c_char, c_int};
#[cfg(test)]
use std::sync::Mutex;

use crate::handle::CHandle;

/// Serializes every ffi unit test: they share the global handle ALIVE
/// counter, the injected decoder/encoder registries and the probe error,
/// so exact `alive_count` assertions and registry injection require
/// serial execution. Held poison-tolerant (`into_inner`) so one failing
/// test cannot cascade-fail the rest.
#[cfg(test)]
pub(crate) static TEST_LOCK: Mutex<()> = Mutex::new(());

/// Poison-tolerant lock helper for the ffi tests.
#[cfg(test)]
pub(crate) fn lock_tests() -> std::sync::MutexGuard<'static, ()> {
	TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

/// Two-stage string copy helper (`string_out` in every `c_api/*.cpp`).
///
/// Returns the required buffer size including the trailing NUL; when
/// `buf` is non-NULL and `buf_size > 0` the string is copied truncated to
/// `buf_size - 1` bytes and NUL-terminated.
pub(crate) fn string_out(s: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
	let need = s.len() as c_int + 1;
	if !buf.is_null() && buf_size > 0 {
		let n = (s.len() as c_int).min(buf_size - 1);
		// SAFETY: the caller guarantees `buf` holds `buf_size` bytes.
		unsafe {
			std::ptr::copy_nonoverlapping(s.as_ptr() as *const c_char, buf, n as usize);
			*buf.add(n as usize) = 0;
		}
	}
	need
}

/// Read a NUL-terminated C string; `None` on NULL pointers.
pub(crate) fn c_str(ptr: *const c_char) -> Option<String> {
	if ptr.is_null() {
		return None;
	}
	// SAFETY: `ptr` must be a valid NUL-terminated C string by contract.
	let s = unsafe { std::ffi::CStr::from_ptr(ptr) };
	Some(s.to_string_lossy().into_owned())
}

/// Release a handle in place and null its `ctx` (`free_handle` in
/// `c_api/refcounted.h`); NULL pointer and empty handle are no-ops.
pub(crate) fn free_handle(h: *mut CHandle) {
	if h.is_null() {
		return;
	}
	let handle = unsafe { &mut *h };
	if handle.ctx.is_null() {
		return;
	}
	if let Some(release) = handle.release {
		// SAFETY: `release` targets the box behind `ctx`.
		unsafe { release(handle.ctx) };
	}
	handle.ctx = std::ptr::null_mut();
}

/// Safe view into a handle's boxed value; `None` for empty handles.
///
/// Thin wrapper over [`crate::handle::get`] so the export bodies can call
/// it without `unsafe` blocks everywhere.
///
/// # Safety
/// `T` must be the boxed type; each export asserts it via the handle
/// contract (the same typed box is used by its `make_owned` call).
pub(crate) fn get_box<T: 'static>(h: &CHandle) -> Option<&T> {
	unsafe { crate::handle::get::<T>(h) }
}
