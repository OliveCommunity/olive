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

//! Optional `oakengine_*` families, resolved from the loaded
//! `liboakengine` at call time.
//!
//! Two families of the frozen C ABI are not exported by the current Rust
//! facade and therefore cannot be declared in [`crate::ffi`]'s link block:
//!
//!   - `init.h` — `oakengine_init` / `oakengine_shutdown`. The C++ engine
//!     core (`EngineCore::instance()`) was removed with the C++ tree; the
//!     Rust facade has no process-global init state, so nothing needs
//!     initializing. The subcommands still go through the prescribed
//!     `oakengine_init(OAKENGINE_INIT_*)` call sequence; when the symbol
//!     is absent the call is a documented no-op returning
//!     `OAKENGINE_OK`. (`OAKENGINE_INIT_RENDER` semantics are provided by
//!     the real `oakengine_render_manager_init` export instead.)
//!   - `exporter.h` — `oakengine_export_render` /
//!     `oakengine_export_last_error` /
//!     `oakengine_export_set_progress_callback`. The facade never wrapped
//!     the exporter assembly layer (its `oakengine_export_render_with_params`
//!     is an unbacked stub returning `OAKENGINE_E_FAILED`), so mp4 export
//!     through the engine is not available yet. The transcode command
//!     attempts the real call; when the family is absent it reports the
//!     engine's export error (or a fixed explanation) and exits 1.
//!
//! Resolution uses `dlsym(RTLD_DEFAULT, ...)`: `liboakengine` is a direct
//! dependency of the binary, so its exports are in the global scope. Each
//! symbol is looked up once and cached; the lookup itself never fails the
//! build, so `cargo check`/`cargo build` stay green regardless of which
//! symbols the dylib currently carries.

#![allow(dead_code)]
#![allow(clippy::missing_safety_doc)]

use std::ffi::{c_char, c_double, c_int, c_void};
use std::sync::OnceLock;

use crate::ffi::{OakEngineSequence, OakExportOptions};

/// `oakengine_init` (init.h).
pub type InitFn = unsafe extern "C" fn(flags: c_int) -> c_int;
/// `oakengine_shutdown` (init.h).
pub type ShutdownFn = unsafe extern "C" fn() -> c_int;
/// `oakengine_export_render` (exporter.h).
pub type ExportRenderFn = unsafe extern "C" fn(
	seq: *mut OakEngineSequence,
	path: *const c_char,
	in_ts: i64,
	out_ts: i64,
	width: c_int,
	height: c_int,
	opts: *const OakExportOptions,
) -> c_int;
/// `oakengine_export_last_error` (exporter.h).
pub type ExportLastErrorFn = unsafe extern "C" fn(buf: *mut c_char, buf_size: c_int) -> c_int;
/// `oakengine_export_set_progress_callback` (exporter.h).
pub type ExportSetProgressFn = unsafe extern "C" fn(
	f: Option<unsafe extern "C" fn(c_double, *mut c_void)>,
	userdata: *mut c_void,
);

#[cfg(unix)]
extern "C" {
	fn dlsym(handle: *mut c_void, name: *const c_char) -> *mut c_void;
}

#[cfg(target_os = "macos")]
const RTLD_DEFAULT: *mut c_void = -2isize as *mut c_void;
#[cfg(all(unix, not(target_os = "macos")))]
const RTLD_DEFAULT: *mut c_void = std::ptr::null_mut();

/// Resolve one facade symbol with `dlsym`. `None` when the loaded dylib
/// does not export it (or the platform has no dlsym).
#[cfg(unix)]
fn lookup<T: Copy>(name: &str) -> Option<T> {
	let cname = std::ffi::CString::new(name).ok()?;
	let ptr = unsafe { dlsym(RTLD_DEFAULT, cname.as_ptr()) };
	if ptr.is_null() {
		return None;
	}
	// SAFETY: dlsym returns the address of a live function with the ABI
	// `name` names; the cast pins its signature.
	Some(unsafe { std::mem::transmute_copy::<*mut c_void, T>(&ptr) })
}

#[cfg(not(unix))]
fn lookup<T: Copy>(_name: &str) -> Option<T> {
	None
}

/// Cached `oakengine_init` resolution.
static INIT: OnceLock<Option<InitFn>> = OnceLock::new();
/// Cached `oakengine_shutdown` resolution.
static SHUTDOWN: OnceLock<Option<ShutdownFn>> = OnceLock::new();
/// Cached `oakengine_export_render` resolution.
static EXPORT_RENDER: OnceLock<Option<ExportRenderFn>> = OnceLock::new();
/// Cached `oakengine_export_last_error` resolution.
static EXPORT_LAST_ERROR: OnceLock<Option<ExportLastErrorFn>> = OnceLock::new();
/// Cached `oakengine_export_set_progress_callback` resolution.
static EXPORT_SET_PROGRESS: OnceLock<Option<ExportSetProgressFn>> = OnceLock::new();

/// `oakengine_init(flags)` — see the module docs for the absent-symbol
/// behavior. Returns `OAKENGINE_E_FAILED` when a present symbol reports
/// failure, `OAKENGINE_OK` otherwise.
///
/// # Safety
/// The resolved function follows the engine init.h contract.
pub unsafe fn engine_init(flags: c_int) -> c_int {
	let cell = INIT.get_or_init(|| lookup::<InitFn>("oakengine_init"));
	match *cell {
		Some(f) => unsafe { f(flags) },
		None => crate::ffi::OAKENGINE_OK,
	}
}

/// `oakengine_shutdown()` — no-op when the symbol is absent.
///
/// # Safety
/// The resolved function follows the engine init.h contract.
pub unsafe fn engine_shutdown() -> c_int {
	let cell = SHUTDOWN.get_or_init(|| lookup::<ShutdownFn>("oakengine_shutdown"));
	match *cell {
		Some(f) => unsafe { f() },
		None => crate::ffi::OAKENGINE_OK,
	}
}

/// `oakengine_export_render(...)` through the resolved symbol.
///
/// Returns `Some(rc)` when the exporter family is present (the engine
/// answer), `None` when the loaded dylib does not export it.
///
/// # Safety
/// `seq`/`path`/`opts` must follow the engine exporter.h contract.
pub unsafe fn export_render(
	seq: *mut OakEngineSequence,
	path: *const c_char,
	in_ts: i64,
	out_ts: i64,
	width: c_int,
	height: c_int,
	opts: *const OakExportOptions,
) -> Option<c_int> {
	let cell = EXPORT_RENDER.get_or_init(|| lookup::<ExportRenderFn>("oakengine_export_render"));
	match *cell {
		Some(f) => Some(unsafe { f(seq, path, in_ts, out_ts, width, height, opts) }),
		None => None,
	}
}

/// `oakengine_export_last_error` through the resolved symbol; the fixed
/// explanation below when the family is absent.
///
/// # Safety
/// The resolved function follows the engine exporter.h contract.
pub unsafe fn export_last_error() -> String {
	let cell =
		EXPORT_LAST_ERROR.get_or_init(|| lookup::<ExportLastErrorFn>("oakengine_export_last_error"));
	match *cell {
		Some(f) => crate::ffi::string_get(|buf, size| unsafe { f(buf, size) }),
		None => "the exporter family (exporter.h) is not exported by the built liboakengine: \
		        oakengine_export_render/oakengine_export_last_error are not wrapped (the facade's \
		        oakengine_export_render_with_params is an unbacked stub)"
			.to_string(),
	}
}

/// `oakengine_export_set_progress_callback` through the resolved symbol;
/// a no-op when absent (the CLI has no progress UI).
///
/// # Safety
/// The resolved function follows the engine exporter.h contract.
pub unsafe fn export_set_progress_callback(
	f: Option<unsafe extern "C" fn(c_double, *mut c_void)>,
	userdata: *mut c_void,
) {
	let cell = EXPORT_SET_PROGRESS
		.get_or_init(|| lookup::<ExportSetProgressFn>("oakengine_export_set_progress_callback"));
	if let Some(fn_) = *cell {
		unsafe { fn_(f, userdata) };
	}
}
