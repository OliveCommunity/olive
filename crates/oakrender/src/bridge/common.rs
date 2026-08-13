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

//! oakcommon C ABI calls (config, file functions, strings) — now direct
//! Rust calls into the oakcommon crate (single-lib unification, see
//! `docs/zh/plans/riir/single-lib.md`). The configuration-location and
//! disk-cache helpers delegate to oakcommon's own implementation.

use std::ffi::{c_char, c_int, c_void};
use std::sync::Mutex;

/// `oakcore_audioparams_sample_rate` (host-provided; M12 P1 — the audio
/// ticket reads the output format from the params handle).
pub fn audioparams_sample_rate(params: *const c_void) -> c_int {
	unsafe extern "C" {
		fn oakcore_audioparams_sample_rate(params: *const c_void) -> c_int;
	}
	unsafe { oakcore_audioparams_sample_rate(params) }
}

/// `oakcore_audioparams_channel_layout` (host-provided).
pub fn audioparams_channel_layout(params: *const c_void) -> u64 {
	unsafe extern "C" {
		fn oakcore_audioparams_channel_layout(params: *const c_void) -> u64;
	}
	unsafe { oakcore_audioparams_channel_layout(params) }
}

/// `oakcore_audioparams_channel_count` (host-provided).
pub fn audioparams_channel_count(params: *const c_void) -> c_int {
	unsafe extern "C" {
		fn oakcore_audioparams_channel_count(params: *const c_void) -> c_int;
	}
	unsafe { oakcore_audioparams_channel_count(params) }
}

/// Read a config string via the two-stage C ABI
/// (`oakcommon_config_get(group, key, buf, n)`, C++ `Config::current()
/// [key].toString()`).
pub fn config_get_string(group: Option<&str>, key: &str) -> Option<String> {
	let group_c = group.and_then(|g| std::ffi::CString::new(g).ok());
	let key_c = std::ffi::CString::new(key).ok()?;
	let group_ptr = || group_c.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());
	let size = unsafe {
		oakcommon::ffi::config::oakcommon_config_get(
			group_ptr(),
			key_c.as_ptr(),
			std::ptr::null_mut(),
			0,
		)
	};
	if size <= 1 {
		return None; // missing or empty
	}
	let mut buf = vec![0u8; size as usize];
	let got = unsafe {
		oakcommon::ffi::config::oakcommon_config_get(
			group_ptr(),
			key_c.as_ptr(),
			buf.as_mut_ptr() as *mut c_char,
			size,
		)
	};
	if got <= 0 {
		return None;
	}
	let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
	Some(String::from_utf8_lossy(&buf[..end]).into_owned())
}

/// `oakcommon_config_get_int(group, key, default)`.
pub fn config_get_int(group: Option<&str>, key: &str, default: i32) -> i32 {
	let group_c = group.and_then(|g| std::ffi::CString::new(g).ok());
	let key_c = match std::ffi::CString::new(key) {
		Ok(c) => c,
		Err(_) => return default,
	};
	let group_ptr = group_c.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());
	unsafe { oakcommon::ffi::config::oakcommon_config_get_int(group_ptr, key_c.as_ptr(), default) }
}

/// The configuration directory — oakcommon's implementation
/// (`FileFunctions::get_configuration_location`, honoring `OAK_CONFIG_DIR`
/// and the platform fallbacks).
pub fn configuration_location() -> String {
	oakcommon::filefunctions::FileFunctions::new()
		.get_configuration_location()
		.unwrap_or_default()
}

/// Serializes tests that mutate `OAK_CONFIG_DIR` / `OAK_RENDER_BACKEND`
/// (env is process-global; the manager tests share this lock too).
pub static ENV_TEST_LOCK: Mutex<()> = Mutex::new(());

/// The default disk cache directory (C++ `DiskManager::
/// get_default_disk_cache_path`): `<configuration_location>/mediacache`.
pub fn default_disk_cache_path() -> String {
	oakcommon::filefunctions::default_disk_cache_path()
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn config_missing_key_falls_back() {
		// The real config store is empty under cargo test: defaults apply.
		assert_eq!(config_get_int(None, "GraphicsBackend", 7), 7);
		assert_eq!(config_get_string(None, "missing"), None);
	}

	#[test]
	fn configuration_location_uses_env_override() {
		let _guard = crate::bridge::common::ENV_TEST_LOCK
			.lock()
			.unwrap_or_else(|e| e.into_inner());
		let dir = std::env::temp_dir().join("oakrender-config-test");
		std::env::set_var("OAK_CONFIG_DIR", &dir);
		let loc = configuration_location();
		assert_eq!(loc, dir.to_string_lossy());
		std::env::remove_var("OAK_CONFIG_DIR");
	}

	#[test]
	fn default_disk_cache_path_is_under_config() {
		let _guard = crate::bridge::common::ENV_TEST_LOCK
			.lock()
			.unwrap_or_else(|e| e.into_inner());
		let dir = std::env::temp_dir().join("oakrender-cache-test");
		std::env::set_var("OAK_CONFIG_DIR", &dir);
		let p = default_disk_cache_path();
		assert!(p.ends_with("/mediacache") || p.ends_with("\\mediacache"));
		std::env::remove_var("OAK_CONFIG_DIR");
	}
}
