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
//! ## Resolution model
//!
//! Symbols are resolved at runtime with `dlsym(RTLD_DEFAULT)` (the
//! module is force-loaded into the host process, so the real module
//! libraries' symbols are in the global scope). `cargo test` builds
//! without those libraries: a missing symbol surfaces as `None` from
//! the wrapper and the caller maps it to a graceful error. This follows
//! the oakplugin crate template (`src/plugin/rust/src/bridge/mod.rs`).
//!
//! Real linkage for the module dylib is provided by the C++ side's
//! force_load of liboaknode (the staticlib); nothing here is linked
//! directly at compile time.

pub mod codec;
pub mod common;
pub mod core;
pub mod render;
pub mod timeline;
pub mod undo;

/// Shared dlsym runtime resolution (pub for crate tests).
pub mod dlsym {
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
	pub fn resolve(name: &str) -> Option<*mut c_void> {
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
