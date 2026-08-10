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

//! C ABI imports from other oak modules.
//!
//! Every submodule follows the same dual-mode pattern as `src/plugin/rust`
//! (bridge/mod.rs): the default path resolves symbols at runtime through
//! [`dlsym`] (`RTLD_DEFAULT`; the module is force-loaded into the app
//! process, so oaknode/oakcommon symbols are in the global scope); when a
//! symbol is missing — e.g. `cargo test` without the C++ modules linked —
//! the wrapper returns a documented fallback (empty handle / negative
//! error code) instead of a link error.

pub mod common;
pub mod node;

/// Shared `dlsym(RTLD_DEFAULT)` runtime resolution.
pub(crate) mod dlsym {
	use std::ffi::{c_char, c_void};

	/// RTLD_DEFAULT (macOS: -2; Linux: 0).
	#[cfg(target_os = "macos")]
	pub(crate) const RTLD_DEFAULT: *mut c_void = -2isize as *mut c_void;
	#[cfg(target_os = "linux")]
	pub(crate) const RTLD_DEFAULT: *mut c_void = 0isize as *mut c_void;

	extern "C" {
		fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
	}

	/// Resolve a global-scope symbol; `None` when missing.
	pub(crate) fn resolve(name: &str) -> Option<*mut c_void> {
		let c = std::ffi::CString::new(name).ok()?;
		let p = unsafe { dlsym(RTLD_DEFAULT, c.as_ptr()) };
		if p.is_null() {
			None
		} else {
			Some(p)
		}
	}

	/// Resolve and call by signature; `None` when the symbol is missing.
	///
	/// # Safety
	/// The caller guarantees `T` matches the symbol's real function type.
	pub(crate) fn call<T, R>(name: &str, f: impl FnOnce(T) -> R) -> Option<R>
	where
		T: Copy,
	{
		let p = resolve(name)?;
		let f_ptr: T = unsafe { std::mem::transmute_copy(&p) };
		Some(f(f_ptr))
	}
}
