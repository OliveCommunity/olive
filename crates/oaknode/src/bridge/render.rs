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

//! oakrender C ABI imports (caches, textures, color processors).
//!
//! Symbols resolved via `dlsym(RTLD_DEFAULT)` (see [`super::dlsym`]);
//! every wrapper returns `None`/a neutral value when the symbol is
//! absent (cargo test without liboakrender).

use std::ffi::c_int;

use crate::handle::CHandle;

/// oakrender cache handle (value type).
pub type CacheHandle = CHandle;
/// oakrender texture handle (value type).
pub type TextureHandle = CHandle;
/// oakrender color processor handle (value type).
pub type ColorProcessorHandle = CHandle;

/// Cache kind constants (oakrender `OAKRENDER_CACHE_*`).
pub mod cache_kind {
	/// `OAKRENDER_CACHE_VIDEO_FRAME`.
	pub const VIDEO_FRAME: i32 = 0;
	/// `OAKRENDER_CACHE_THUMBNAIL`.
	pub const THUMBNAIL: i32 = 1;
	/// `OAKRENDER_CACHE_AUDIO_PLAYBACK`.
	pub const AUDIO_PLAYBACK: i32 = 2;
	/// `OAKRENDER_CACHE_AUDIO_WAVEFORM`.
	pub const AUDIO_WAVEFORM: i32 = 3;
}

/// `oakrender_cache_create_for_node`.
pub fn cache_create_for_node(parent: CHandle, kind: i32) -> Option<CHandle> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, i32) -> CHandle;
	dlsym::call::<F, CHandle>("oakrender_cache_create_for_node", |f| unsafe {
		f(parent, kind)
	})
}

/// `oakrender_cache_free`.
pub fn cache_free(cache: *mut CHandle) {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*mut CHandle);
	if let Some(f) = dlsym::call::<F, ()>("oakrender_cache_free", |f| unsafe { f(cache) }) {
		let _ = f;
	}
}

/// `oakrender_cache_invalidate_range`.
pub fn cache_invalidate_range(
	cache: CHandle,
	in_num: i64,
	in_den: i64,
	out_num: i64,
	out_den: i64,
) {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, i64, i64, i64, i64);
	if let Some(f) = dlsym::call::<F, ()>("oakrender_cache_invalidate_range", |f| unsafe {
		f(cache, in_num, in_den, out_num, out_den)
	}) {
		let _ = f;
	}
}

/// `oakrender_cache_set_uuid`.
pub fn cache_set_uuid(cache: CHandle, uuid: &str) -> Option<i32> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(CHandle, *const std::ffi::c_char) -> i32;
	let c = CString::new(uuid).ok()?;
	dlsym::call::<F, i32>("oakrender_cache_set_uuid", |f| unsafe {
		f(cache, c.as_ptr())
	})
}

/// `oakrender_cache_get_uuid` (two-stage).
pub fn cache_get_uuid(cache: CHandle) -> Option<String> {
	use crate::bridge::dlsym;
	use std::ffi::c_char;
	type F = unsafe extern "C" fn(CHandle, *mut c_char, i32) -> i32;
	let needed = dlsym::call::<F, i32>("oakrender_cache_get_uuid", |f| unsafe {
		f(cache.clone(), std::ptr::null_mut(), 0)
	})?;
	if needed <= 0 {
		return None;
	}
	let mut buf = vec![0u8; needed as usize];
	dlsym::call::<F, i32>("oakrender_cache_get_uuid", |f| unsafe {
		f(cache.clone(), buf.as_mut_ptr() as *mut c_char, needed)
	})?;
	buf.pop(); // trailing NUL
	String::from_utf8(buf).ok()
}

/// `oakrender_disk_cache_path` (two-stage): the default cache directory.
pub fn disk_cache_path() -> Option<String> {
	use crate::bridge::dlsym;
	use std::ffi::c_char;
	type F = unsafe extern "C" fn(*mut c_char, i32) -> i32;
	let needed = dlsym::call::<F, i32>("oakrender_disk_cache_path", |f| unsafe {
		f(std::ptr::null_mut(), 0)
	})?;
	if needed <= 0 {
		return None;
	}
	let mut buf = vec![0u8; needed as usize];
	dlsym::call::<F, i32>("oakrender_disk_cache_path", |f| unsafe {
		f(buf.as_mut_ptr() as *mut c_char, needed)
	})?;
	buf.pop(); // trailing NUL
	String::from_utf8(buf).ok()
}

/// `oakrender_color_config_create_default`: load the bundled OCIO
/// config. `None` = symbol absent (cargo test); `Some(Ok(())` =
/// success; `Some(Err(()))` = OCIO error.
pub fn color_config_create_default() -> Option<Result<(), ()>> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn() -> i32;
	let rc = dlsym::call::<F, i32>("oakrender_color_config_create_default", |f| unsafe { f() })?;
	if rc == 0 {
		Some(Ok(()))
	} else {
		Some(Err(()))
	}
}

/// `oakrender_color_config_load_from_filename`: load a config file.
/// Same tri-state as [`color_config_create_default`].
pub fn color_config_load(filename: &str) -> Option<Result<(), ()>> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(*const std::ffi::c_char) -> i32;
	let c = CString::new(filename).ok()?;
	let rc = dlsym::call::<F, i32>("oakrender_color_config_load_from_filename", |f| unsafe {
		f(c.as_ptr())
	})?;
	if rc == 0 {
		Some(Ok(()))
	} else {
		Some(Err(()))
	}
}
