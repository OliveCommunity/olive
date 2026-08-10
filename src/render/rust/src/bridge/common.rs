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

//! oakcommon C ABI imports (config, file functions, strings).
//!
//! Symbols resolve through [`crate::bridge::dlsym`]; a missing symbol
//! yields the documented fallback (config defaults, the mirrored
//! configuration-location computation) so the crate stays testable and
//! linkable without liboakcommon.

use std::ffi::{c_char, c_int};
use std::sync::Mutex;

/// Read a config string via the two-stage C ABI
/// (`oakcommon_config_get(group, key, buf, n)`, C++ `Config::current()
/// [key].toString()`).
pub fn config_get_string(group: Option<&str>, key: &str) -> Option<String> {
	let group_c = group.and_then(|g| std::ffi::CString::new(g).ok());
	let key_c = std::ffi::CString::new(key).ok()?;
	type F = unsafe extern "C" fn(*const c_char, *const c_char, *mut c_char, c_int) -> c_int;
	crate::bridge::dlsym::call::<F, i32>("oakcommon_config_get", |f| unsafe {
		let group_ptr = group_c.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());
		f(group_ptr, key_c.as_ptr(), std::ptr::null_mut(), 0)
	})
	.and_then(|size| {
		if size <= 1 {
			return None; // missing or empty
		}
		let mut buf = vec![0u8; size as usize];
		let group_ptr = group_c.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());
		let got = crate::bridge::dlsym::call::<F, i32>("oakcommon_config_get", |f| unsafe {
			f(group_ptr, key_c.as_ptr(), buf.as_mut_ptr() as *mut c_char, size)
		})?;
		if got <= 0 {
			return None;
		}
		let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
		Some(String::from_utf8_lossy(&buf[..end]).into_owned())
	})
}

/// `oakcommon_config_get_int(group, key, default)`.
pub fn config_get_int(group: Option<&str>, key: &str, default: i32) -> i32 {
	let group_c = group.and_then(|g| std::ffi::CString::new(g).ok());
	let key_c = match std::ffi::CString::new(key) {
		Ok(c) => c,
		Err(_) => return default,
	};
	type F = unsafe extern "C" fn(*const c_char, *const c_char, c_int) -> c_int;
	crate::bridge::dlsym::call::<F, i32>("oakcommon_config_get_int", |f| unsafe {
		let group_ptr = group_c.as_ref().map_or(std::ptr::null(), |c| c.as_ptr());
		f(group_ptr, key_c.as_ptr(), default)
	})
	.unwrap_or(default)
}

/// The configuration directory.
///
/// Primary path: `oakcommon_filefunctions_get_configuration_location()`
/// (two-stage, needs an `OakFileFunctions` handle obtained through
/// `oakcommon_filefunctions_init()`). Fallback (mirror of
/// `FileFunctions::get_configuration_location()` in common/rust/src/
/// filefunctions.rs): `OAK_CONFIG_DIR` → portable app dir → macOS
/// `~/Library/Application Support` (or `$XDG_CONFIG_HOME`/`~/.config` on
/// other platforms) → temp directory. The fallback keeps `cargo test`
/// deterministic through the `OAK_CONFIG_DIR` env var.
pub fn configuration_location() -> String {
	// 1) Env override (also honored by the C++ side).
	if let Ok(dir) = std::env::var("OAK_CONFIG_DIR") {
		if !dir.is_empty() {
			let _ = std::fs::create_dir_all(&dir);
			return dir;
		}
	}

	// 2) The real C ABI (force-loaded oakcommon in the app process).
	if let Some(path) = configuration_location_via_abi() {
		return path;
	}

	// 3) Mirrored fallback.
	#[cfg(target_os = "macos")]
	let config_root = match std::env::var("HOME") {
		Ok(h) if !h.is_empty() => {
			std::path::PathBuf::from(h).join("Library").join("Application Support")
		}
		_ => std::path::PathBuf::new(),
	};
	#[cfg(not(target_os = "macos"))]
	let config_root = match std::env::var("XDG_CONFIG_HOME") {
		Ok(x) if !x.is_empty() => std::path::PathBuf::from(x),
		_ => match std::env::var("HOME") {
			Ok(h) if !h.is_empty() => std::path::PathBuf::from(h).join(".config"),
			_ => std::path::PathBuf::new(),
		},
	};

	if config_root.as_os_str().is_empty() {
		std::env::temp_dir().to_string_lossy().into_owned()
	} else {
		config_root.to_string_lossy().into_owned()
	}
}

/// Resolve the configuration location through the oakcommon C ABI
/// (`oakcommon_filefunctions_init` + `_get_configuration_location`).
fn configuration_location_via_abi() -> Option<String> {
	type InitF = unsafe extern "C" fn() -> crate::handle::CHandle;
	let handle =
		crate::bridge::dlsym::call::<InitF, crate::handle::CHandle>("oakcommon_filefunctions_init", |f| {
			unsafe { f() }
		})?;
	if handle.is_null() {
		return None;
	}
	type GetF = unsafe extern "C" fn(crate::handle::CHandle, *mut c_char, c_int) -> c_int;
	let size = crate::bridge::dlsym::call::<GetF, i32>(
		"oakcommon_filefunctions_get_configuration_location",
		|f| unsafe { f(handle, std::ptr::null_mut(), 0) },
	)?;
	let result = if size <= 1 {
		None
	} else {
		let mut buf = vec![0u8; size as usize];
		let got = crate::bridge::dlsym::call::<GetF, i32>(
			"oakcommon_filefunctions_get_configuration_location",
			|f| unsafe { f(handle, buf.as_mut_ptr() as *mut c_char, size) },
		)?;
		if got <= 0 {
			None
		} else {
			let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
			Some(String::from_utf8_lossy(&buf[..end]).into_owned())
		}
	};
	type FreeF = unsafe extern "C" fn(*mut crate::handle::CHandle);
	let mut h = handle;
	let _ = crate::bridge::dlsym::call::<FreeF, ()>("oakcommon_filefunctions_free", |f| unsafe {
		f(&mut h)
	});
	result
}

/// Serializes tests that mutate `OAK_CONFIG_DIR` / `OAK_RENDER_BACKEND`
/// (env is process-global; the manager tests share this lock too).
pub static ENV_TEST_LOCK: Mutex<()> = Mutex::new(());

/// The default disk cache directory (C++ `DiskManager::
/// get_default_disk_cache_path`): `<configuration_location>/mediacache`.
pub fn default_disk_cache_path() -> String {
	std::path::Path::new(&configuration_location())
		.join("mediacache")
		.to_string_lossy()
		.into_owned()
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn config_missing_symbol_falls_back() {
		// No liboakcommon in cargo test: defaults apply.
		assert_eq!(config_get_int(None, "GraphicsBackend", 7), 7);
		assert_eq!(config_get_string(None, "missing"), None);
	}

	#[test]
	fn configuration_location_uses_env_override() {
		let _guard = crate::bridge::common::ENV_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
		let dir = std::env::temp_dir().join("oakrender-config-test");
		std::env::set_var("OAK_CONFIG_DIR", &dir);
		let loc = configuration_location();
		assert_eq!(loc, dir.to_string_lossy());
		std::env::remove_var("OAK_CONFIG_DIR");
	}

	#[test]
	fn default_disk_cache_path_is_under_config() {
		let _guard = crate::bridge::common::ENV_TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
		let dir = std::env::temp_dir().join("oakrender-cache-test");
		std::env::set_var("OAK_CONFIG_DIR", &dir);
		let p = default_disk_cache_path();
		assert!(p.ends_with("/mediacache") || p.ends_with("\\mediacache"));
		std::env::remove_var("OAK_CONFIG_DIR");
	}
}
