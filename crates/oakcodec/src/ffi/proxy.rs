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

//! `include/codec/proxy.h` exports.
//!
//! Complete inventory: create_instance / destroy_instance / params_default
//! / get_state / state_to_string / get_proxy_directory / get_proxy_filename
//! / get_working_filename / get_or_start / find_ffmpeg.
//!
//! # CPP-PARITY
//! The C++ `oakcodec_proxy_params.include_audio` is an `int`, mirrored
//! byte-for-byte by [`crate::proxymanager::ProxyParams`]; the POD structs
//! are still defined here (rather than reused) so the ffi layer never
//! depends on the crate-internal type's layout. A NULL `params` maps to
//! the crate's compiled-in defaults (`ProxyParams::default`), matching
//! `to_native` in `c_api/proxy.cpp`.

use std::ffi::{c_char, c_int};

use crate::handle;
use crate::proxymanager::{ProxyManager, ProxyParams, ProxyState};

/// `OAKCODEC_PROXY_STATE_MISSING`.
pub const OAKCODEC_PROXY_STATE_MISSING: c_int = 0;
/// `OAKCODEC_PROXY_STATE_GENERATING`.
pub const OAKCODEC_PROXY_STATE_GENERATING: c_int = 1;
/// `OAKCODEC_PROXY_STATE_READY`.
pub const OAKCODEC_PROXY_STATE_READY: c_int = 2;
/// `OAKCODEC_PROXY_STATE_FAILED`.
pub const OAKCODEC_PROXY_STATE_FAILED: c_int = 3;

/// `oakcodec_proxy_params` — POD mirror of `include/codec/proxy.h`.
#[allow(missing_docs)]
#[repr(C)]
pub struct oakcodec_proxy_params {
	pub width: c_int,
	pub height: c_int,
	pub divider: c_int,
	pub version: c_int,
	pub crf: c_int,
	pub include_audio: c_int,
	pub extension: [u8; 32],
	pub preset: [u8; 32],
}

/// `oakcodec_proxy_result` — POD result of `oakcodec_proxy_get_or_start`.
#[allow(missing_docs)]
#[repr(C)]
pub struct oakcodec_proxy_result {
	pub state: c_int,
	pub filename: [u8; 1024],
}

/// Convert the C POD to the crate's [`ProxyParams`] (`to_native` in
/// `c_api/proxy.cpp`); NULL maps to the compiled-in defaults.
fn to_native(p: *const oakcodec_proxy_params) -> ProxyParams {
	if p.is_null() {
		return ProxyParams::default();
	}
	let p = unsafe { &*p };
	ProxyParams {
		width: p.width,
		height: p.height,
		divider: p.divider,
		version: p.version,
		crf: p.crf,
		include_audio: p.include_audio,
		extension: p.extension,
		preset: p.preset,
	}
}

/// Copy a NUL-terminated byte array into a C buffer (truncated).
fn copy_cstr(src: &[u8], dst: &mut [u8]) {
	dst.fill(0);
	let n = src.len().min(dst.len().saturating_sub(1));
	dst[..n].copy_from_slice(&src[..n]);
}

/// `oakcodec_proxy_create_instance`: create the singleton (always present
/// here, so a no-op).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_create_instance() -> c_int {
	handle::guard_raw(|| {
		let _ = ProxyManager::instance();
		crate::error::OAKCODEC_OK
	})
}

/// `oakcodec_proxy_destroy_instance`: destroy the singleton (the Rust
/// manager is stateless, so a no-op).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_destroy_instance() -> c_int {
	handle::guard_raw(|| crate::error::OAKCODEC_OK)
}

/// `oakcodec_proxy_params_default`: fill `out` with the compiled-in
/// default proxy parameters.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_params_default(out: *mut oakcodec_proxy_params) -> c_int {
	handle::guard(|| {
		if out.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let n = ProxyManager::proxy_params_from_config();
		unsafe {
			(*out).width = n.width;
			(*out).height = n.height;
			(*out).divider = n.divider;
			(*out).version = n.version;
			(*out).crf = n.crf;
			(*out).include_audio = n.include_audio;
			copy_cstr(&n.extension, &mut (*out).extension);
			copy_cstr(&n.preset, &mut (*out).preset);
		}
		Ok(())
	})
}

/// `oakcodec_proxy_get_state`: state of a proxy file on disk
/// (`OAKCODEC_PROXY_STATE_MISSING` for NULL/empty/absent).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_get_state(proxy_filename: *const c_char) -> c_int {
	handle::guard_raw(|| {
		let f = match crate::ffi::c_str(proxy_filename) {
			Some(f) if !f.is_empty() => f,
			_ => return OAKCODEC_PROXY_STATE_MISSING,
		};
		ProxyManager::get_proxy_state(&f) as c_int
	})
}

/// `oakcodec_proxy_state_to_string` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_state_to_string(
	state: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let s = match state {
			0 => ProxyManager::proxy_state_to_string(ProxyState::Missing),
			1 => ProxyManager::proxy_state_to_string(ProxyState::Generating),
			2 => ProxyManager::proxy_state_to_string(ProxyState::Ready),
			3 => ProxyManager::proxy_state_to_string(ProxyState::Failed),
			_ => return crate::error::OAKCODEC_E_INVALID,
		};
		super::string_out(&s, buf, buf_size)
	})
}

/// `oakcodec_proxy_get_proxy_directory` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_get_proxy_directory(
	cache_path: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let cache = match crate::ffi::c_str(cache_path) {
			Some(c) => c,
			None => return crate::error::OAKCODEC_E_INVALID,
		};
		match ProxyManager::get_proxy_directory(&cache) {
			Ok(s) => super::string_out(&s, buf, buf_size),
			Err(_) => crate::error::OAKCODEC_E_FAILED,
		}
	})
}

/// `oakcodec_proxy_get_proxy_filename` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_get_proxy_filename(
	cache_path: *const c_char,
	source_filename: *const c_char,
	stream_index: c_int,
	params: *const oakcodec_proxy_params,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let cache = match crate::ffi::c_str(cache_path) {
			Some(c) => c,
			None => return crate::error::OAKCODEC_E_INVALID,
		};
		let source = match crate::ffi::c_str(source_filename) {
			Some(s) => s,
			None => return crate::error::OAKCODEC_E_INVALID,
		};
		let native = to_native(params);
		match ProxyManager::get_proxy_filename(&cache, &source, stream_index, &native) {
			Ok(s) => super::string_out(&s, buf, buf_size),
			Err(_) => crate::error::OAKCODEC_E_FAILED,
		}
	})
}

/// `oakcodec_proxy_get_working_filename` (two-stage).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_get_working_filename(
	proxy_filename: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let proxy = match crate::ffi::c_str(proxy_filename) {
			Some(p) => p,
			None => return crate::error::OAKCODEC_E_INVALID,
		};
		match ProxyManager::get_working_filename(&proxy) {
			Ok(s) => super::string_out(&s, buf, buf_size),
			Err(_) => crate::error::OAKCODEC_E_FAILED,
		}
	})
}

/// `oakcodec_proxy_get_or_start`: get or start generating a proxy for
/// `source_filename`.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_get_or_start(
	cache_path: *const c_char,
	source_filename: *const c_char,
	stream_index: c_int,
	params: *const oakcodec_proxy_params,
	out: *mut oakcodec_proxy_result,
) -> c_int {
	handle::guard(|| {
		if out.is_null() {
			return Err(crate::error::Error::Invalid);
		}
		let cache = match crate::ffi::c_str(cache_path) {
			Some(c) => c,
			None => return Err(crate::error::Error::Invalid),
		};
		let source = match crate::ffi::c_str(source_filename) {
			Some(s) => s,
			None => return Err(crate::error::Error::Invalid),
		};
		let native = to_native(params);
		let (state, filename) = ProxyManager::instance()
			.get_or_start(&cache, &source, stream_index, &native)
			.map_err(|_| crate::error::Error::Failed("get_or_start failed".to_string()))?;
		unsafe {
			let out_ref = &mut *out;
			out_ref.state = state as c_int;
			// Truncate to 1023 chars + NUL, matching `snprintf` in
			// `c_api/proxy.cpp`.
			let n = filename.len().min(1023);
			out_ref.filename[..n].copy_from_slice(&filename.as_bytes()[..n]);
			out_ref.filename[n] = 0;
		}
		Ok(())
	})
}

/// `oakcodec_proxy_find_ffmpeg` (two-stage; empty string when none found).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_proxy_find_ffmpeg(
	configured_path: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let configured = crate::ffi::c_str(configured_path).unwrap_or_default();
		let s = ProxyManager::find_ffmpeg(&configured);
		super::string_out(&s, buf, buf_size)
	})
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::conformmanager::test_util::{accept_cb, REG_LOCK};
	use crate::error::{OAKCODEC_E_INVALID, OAKCODEC_E_STATE};
	use crate::task::set_task_submit_cb_extern;

	fn cstr(s: &str) -> std::ffi::CString {
		std::ffi::CString::new(s).unwrap()
	}

	fn temp_cache(name: &str) -> String {
		let dir = std::env::temp_dir().join(format!("oakcodec_ffi_proxy_{}_{}", name, std::process::id()));
		let _ = std::fs::create_dir_all(&dir);
		dir.to_string_lossy().into_owned()
	}

	fn defaults() -> oakcodec_proxy_params {
		let mut p: oakcodec_proxy_params = unsafe { std::mem::zeroed() };
		let rc = unsafe { oakcodec_proxy_params_default(&mut p) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		p
	}

	#[test]
	fn create_destroy_and_params_default() {
		let _g = crate::ffi::lock_tests();
		assert_eq!(unsafe { oakcodec_proxy_create_instance() }, crate::error::OAKCODEC_OK);
		assert_eq!(unsafe { oakcodec_proxy_destroy_instance() }, crate::error::OAKCODEC_OK);

		let p = defaults();
		assert_eq!(p.width, 1280);
		assert_eq!(p.height, 720);
		assert_eq!(p.divider, 1);
		assert_eq!(p.version, 1);
		assert_eq!(p.crf, 23);
		assert_eq!(p.include_audio, 1);
		assert_eq!(&p.extension[..3], b"mp4");
		assert_eq!(&p.preset[..8], b"veryfast");

		let rc = unsafe { oakcodec_proxy_params_default(std::ptr::null_mut()) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
	}

	#[test]
	fn get_state_and_state_to_string() {
		let _g = crate::ffi::lock_tests();
		let cache = temp_cache("state");
		let p = defaults();
		let src = cstr("media.mp4");
		let cache_c = cstr(&cache);

		// Resolve the proxy filename, then query its state.
		let mut name = [0i8; 1024];
		let rc = unsafe { oakcodec_proxy_get_proxy_filename(cache_c.as_ptr(), src.as_ptr(), 0, &p, name.as_mut_ptr(), 1024) };
		assert!(rc > 0);
		let proxy = crate::ffi::c_str(name.as_ptr()).unwrap();
		assert!(proxy.contains("1280x720"));

		// Missing by default.
		let pc = cstr(&proxy);
		assert_eq!(unsafe { oakcodec_proxy_get_state(pc.as_ptr()) }, OAKCODEC_PROXY_STATE_MISSING);
		assert_eq!(unsafe { oakcodec_proxy_get_state(std::ptr::null()) }, OAKCODEC_PROXY_STATE_MISSING);

		// Ready once the file exists.
		std::fs::create_dir_all(std::path::Path::new(&proxy).parent().unwrap()).unwrap();
		std::fs::write(&proxy, b"x").unwrap();
		assert_eq!(unsafe { oakcodec_proxy_get_state(pc.as_ptr()) }, OAKCODEC_PROXY_STATE_READY);

		// state_to_string mapping + invalid range.
		let mut buf = [0i8; 64];
		let rc = unsafe { oakcodec_proxy_state_to_string(2, buf.as_mut_ptr(), 64) };
		assert_eq!(rc, 6); // "ready" + NUL
		assert_eq!(crate::ffi::c_str(buf.as_ptr()).as_deref(), Some("ready"));
		let rc = unsafe { oakcodec_proxy_state_to_string(7, buf.as_mut_ptr(), 64) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
	}

	#[test]
	fn directory_working_and_get_or_start() {
		let _g = crate::ffi::lock_tests();
		let cache = temp_cache("getorstart");
		let cache_c = cstr(&cache);
		let src = cstr("media.mp4");
		let p = defaults();

		// get_proxy_directory.
		let mut buf = [0i8; 512];
		let rc = unsafe { oakcodec_proxy_get_proxy_directory(cache_c.as_ptr(), buf.as_mut_ptr(), 512) };
		assert!(rc > 0);
		assert_eq!(
			crate::ffi::c_str(buf.as_ptr()).as_deref(),
			Some(format!("{}/proxy", cache).as_str())
		);

		// get_working_filename appends ".working.mp4".
		let proxy = format!("{}/proxy/{}-0.1280x720.v1.a1.mp4", cache, 12345);
		let pc = cstr(&proxy);
		let rc = unsafe { oakcodec_proxy_get_working_filename(pc.as_ptr(), buf.as_mut_ptr(), 512) };
		assert!(rc > 0);
		assert_eq!(
			crate::ffi::c_str(buf.as_ptr()).as_deref(),
			Some(format!("{}.working.mp4", proxy).as_str())
		);

		// get_or_start without a registrar -> Missing.
		let _g = REG_LOCK.lock().unwrap();
		set_task_submit_cb_extern(None, std::ptr::null_mut());
		let mut out: oakcodec_proxy_result = unsafe { std::mem::zeroed() };
		let rc = unsafe { oakcodec_proxy_get_or_start(cache_c.as_ptr(), src.as_ptr(), 0, &p, &mut out) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(out.state, OAKCODEC_PROXY_STATE_MISSING);

		// With a registrar and no files -> Generating.
		set_task_submit_cb_extern(Some(accept_cb), std::ptr::null_mut());
		let rc = unsafe { oakcodec_proxy_get_or_start(cache_c.as_ptr(), src.as_ptr(), 0, &p, &mut out) };
		assert_eq!(rc, crate::error::OAKCODEC_OK);
		assert_eq!(out.state, OAKCODEC_PROXY_STATE_GENERATING);

		// Invalid args.
		let rc = unsafe { oakcodec_proxy_get_or_start(std::ptr::null(), src.as_ptr(), 0, &p, &mut out) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
		let rc = unsafe { oakcodec_proxy_get_or_start(cache_c.as_ptr(), src.as_ptr(), 0, &p, std::ptr::null_mut()) };
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// get_proxy_directory / get_proxy_filename / get_working_filename
		// argument validation.
		let rc = unsafe { oakcodec_proxy_get_proxy_directory(std::ptr::null(), buf.as_mut_ptr(), 512) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
		let rc = unsafe { oakcodec_proxy_get_proxy_filename(std::ptr::null(), src.as_ptr(), 0, &p, buf.as_mut_ptr(), 512) };
		assert_eq!(rc, OAKCODEC_E_INVALID);
		let rc = unsafe { oakcodec_proxy_get_working_filename(std::ptr::null(), buf.as_mut_ptr(), 512) };
		assert_eq!(rc, OAKCODEC_E_INVALID);

		set_task_submit_cb_extern(None, std::ptr::null_mut());
	}

	#[test]
	fn find_ffmpeg_uses_configured_path() {
		let _g = crate::ffi::lock_tests();
		// The current test binary is a real executable: the configured
		// path resolves to an absolute path.
		let me = std::env::current_exe().unwrap();
		let mc = cstr(me.to_str().unwrap());
		let mut buf = [0i8; 1024];
		let rc = unsafe { oakcodec_proxy_find_ffmpeg(mc.as_ptr(), buf.as_mut_ptr(), 1024) };
		assert!(rc > 0);
		let found = crate::ffi::c_str(buf.as_ptr()).unwrap();
		assert!(found.starts_with('/'));

		// NULL configured path falls back to the search (empty or absolute).
		let rc = unsafe { oakcodec_proxy_find_ffmpeg(std::ptr::null(), buf.as_mut_ptr(), 1024) };
		assert!(rc > 0);
		let found = crate::ffi::c_str(buf.as_ptr()).unwrap();
		assert!(found.is_empty() || found.starts_with('/'));
	}
}
