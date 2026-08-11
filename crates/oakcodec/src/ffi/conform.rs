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

//! `include/codec/conform.h` exports.
//!
//! Complete inventory: create_instance / destroy_instance / get_state /
//! filename_count / filename_at. `OAKCODEC_CONFORM_*` macros are the
//! states.
//!
//! # CPP-PARITY
//! The C++ `c_api/conform.cpp` reports `OAKCODEC_E_STATE` when the
//! singleton is absent (`!ConformManager::instance()`); the Rust
//! [`crate::conformmanager::ConformManager::instance`] is a lazy
//! `'static` singleton that can never be absent, so that branch cannot
//! trigger.

use std::ffi::{c_char, c_int};

use crate::conformmanager::{ConformManager, ConformState};
use crate::handle;

/// `OAKCODEC_CONFORM_EXISTS`.
pub const OAKCODEC_CONFORM_EXISTS: c_int = 0;
/// `OAKCODEC_CONFORM_GENERATING`.
pub const OAKCODEC_CONFORM_GENERATING: c_int = 1;
/// `OAKCODEC_CONFORM_UNAVAILABLE`.
pub const OAKCODEC_CONFORM_UNAVAILABLE: c_int = 2;

/// `oakcodec_conform_create_instance`: create the singleton (always
/// present here, so a no-op).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_conform_create_instance() -> c_int {
	handle::guard_raw(|| {
		let _ = ConformManager::instance();
		crate::error::OAKCODEC_OK
	})
}

/// `oakcodec_conform_destroy_instance`: destroy the singleton (the Rust
/// manager is stateless, so a no-op).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_conform_destroy_instance() -> c_int {
	handle::guard_raw(|| crate::error::OAKCODEC_OK)
}

/// `oakcodec_conform_get_state`: query the conform state of one audio
/// stream.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_conform_get_state(
	cache_path: *const c_char,
	source_filename: *const c_char,
	stream_index: c_int,
	sample_rate: c_int,
	channel_layout: u64,
	sample_format: c_int,
	wait: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let cache = match crate::ffi::c_str(cache_path) {
			Some(c) if !c.is_empty() => c,
			_ => return crate::error::OAKCODEC_E_INVALID,
		};
		let source = match crate::ffi::c_str(source_filename) {
			Some(s) if !s.is_empty() => s,
			_ => return crate::error::OAKCODEC_E_INVALID,
		};
		let m = ConformManager::instance();
		match m.get_conform_state(
			&cache,
			&source,
			stream_index,
			sample_rate,
			channel_layout,
			sample_format,
			wait != 0,
		) {
			Ok(ConformState::Exists) => OAKCODEC_CONFORM_EXISTS,
			Ok(ConformState::Generating) => OAKCODEC_CONFORM_GENERATING,
			Ok(ConformState::Unavailable) | Err(_) => OAKCODEC_CONFORM_UNAVAILABLE,
		}
	})
}

/// `oakcodec_conform_filename_count`: number of conform files for the
/// given stream/params; 0 on invalid arguments.
#[no_mangle]
pub unsafe extern "C" fn oakcodec_conform_filename_count(
	cache_path: *const c_char,
	source_filename: *const c_char,
	stream_index: c_int,
	sample_rate: c_int,
	channel_layout: u64,
	sample_format: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let cache = match crate::ffi::c_str(cache_path) {
			Some(c) if !c.is_empty() => c,
			_ => return 0,
		};
		let source = match crate::ffi::c_str(source_filename) {
			Some(s) if !s.is_empty() => s,
			_ => return 0,
		};
		let m = ConformManager::instance();
		m.get_conform_filename_count(
			&cache,
			&source,
			stream_index,
			sample_rate,
			channel_layout,
			sample_format,
		) as c_int
	})
}

/// `oakcodec_conform_filename_at`: the `index`-th conform filename
/// (two-stage string).
#[no_mangle]
pub unsafe extern "C" fn oakcodec_conform_filename_at(
	cache_path: *const c_char,
	source_filename: *const c_char,
	stream_index: c_int,
	sample_rate: c_int,
	channel_layout: u64,
	sample_format: c_int,
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	handle::guard_raw(|| {
		let cache = match crate::ffi::c_str(cache_path) {
			Some(c) if !c.is_empty() => c,
			_ => return crate::error::OAKCODEC_E_INVALID,
		};
		let source = match crate::ffi::c_str(source_filename) {
			Some(s) if !s.is_empty() => s,
			_ => return crate::error::OAKCODEC_E_INVALID,
		};
		let m = ConformManager::instance();
		match m.get_conform_filename(
			&cache,
			&source,
			stream_index,
			sample_rate,
			channel_layout,
			sample_format,
			index as usize,
		) {
			Ok(f) => super::string_out(&f, buf, buf_size),
			Err(crate::error::Error::NotFound) => crate::error::OAKCODEC_E_NOT_FOUND,
			Err(_) => crate::error::OAKCODEC_E_FAILED,
		}
	})
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::conformmanager::test_util::REG_LOCK;
	use crate::error::{OAKCODEC_E_INVALID, OAKCODEC_E_NOT_FOUND};

	fn cstr(s: &str) -> std::ffi::CString {
		std::ffi::CString::new(s).unwrap()
	}

	fn temp_cache(name: &str) -> String {
		let dir = std::env::temp_dir().join(format!(
			"oakcodec_ffi_conform_{}_{}",
			name,
			std::process::id()
		));
		let _ = std::fs::create_dir_all(&dir);
		dir.to_string_lossy().into_owned()
	}

	#[test]
	fn create_destroy_instance_ok() {
		let _g = crate::ffi::lock_tests();
		assert_eq!(
			unsafe { oakcodec_conform_create_instance() },
			crate::error::OAKCODEC_OK
		);
		assert_eq!(
			unsafe { oakcodec_conform_destroy_instance() },
			crate::error::OAKCODEC_OK
		);
	}

	#[test]
	fn get_state_maps_states() {
		let _g = crate::ffi::lock_tests();
		let _g = REG_LOCK.lock().unwrap();
		// No registrar and no files -> UNAVAILABLE.
		let cache = cstr(&temp_cache("state"));
		let src = cstr("media.mp4");
		let rc = unsafe {
			oakcodec_conform_get_state(cache.as_ptr(), src.as_ptr(), 0, 48000, 0x3, 0, 0)
		};
		assert_eq!(rc, OAKCODEC_CONFORM_UNAVAILABLE);

		// Invalid arguments -> E_INVALID.
		let rc = unsafe {
			oakcodec_conform_get_state(std::ptr::null(), src.as_ptr(), 0, 48000, 0x3, 0, 0)
		};
		assert_eq!(rc, OAKCODEC_E_INVALID);
		let empty = cstr("");
		let rc = unsafe {
			oakcodec_conform_get_state(empty.as_ptr(), src.as_ptr(), 0, 48000, 0x3, 0, 0)
		};
		assert_eq!(rc, OAKCODEC_E_INVALID);

		// Write the conform files -> EXISTS.
		let m = ConformManager::instance();
		for i in 0..2 {
			let f = m
				.get_conform_filename(&temp_cache("state"), "media.mp4", 0, 48000, 0x3, 0, i)
				.unwrap();
			std::fs::write(&f, b"pcm").unwrap();
		}
		let rc = unsafe {
			oakcodec_conform_get_state(cache.as_ptr(), src.as_ptr(), 0, 48000, 0x3, 0, 0)
		};
		assert_eq!(rc, OAKCODEC_CONFORM_EXISTS);
	}

	#[test]
	fn filename_count_and_at() {
		let _g = crate::ffi::lock_tests();
		let cache = cstr(&temp_cache("names"));
		let src = cstr("media.mp4");

		// Stereo -> 2 files.
		let rc = unsafe {
			oakcodec_conform_filename_count(cache.as_ptr(), src.as_ptr(), 0, 48000, 0x3, 0)
		};
		assert_eq!(rc, 2);

		// Invalid args -> 0 (not an error).
		let rc = unsafe {
			oakcodec_conform_filename_count(std::ptr::null(), src.as_ptr(), 0, 48000, 0x3, 0)
		};
		assert_eq!(rc, 0);

		// filename_at round-trips the deterministic name.
		let mut buf = [0i8; 512];
		let rc = unsafe {
			oakcodec_conform_filename_at(
				cache.as_ptr(),
				src.as_ptr(),
				0,
				48000,
				0x3,
				0,
				0,
				buf.as_mut_ptr(),
				512,
			)
		};
		assert!(rc > 0);
		let name = crate::ffi::c_str(buf.as_ptr()).unwrap();
		assert!(name.ends_with(".0.pcm"));

		// Out-of-range index -> E_NOT_FOUND.
		let rc = unsafe {
			oakcodec_conform_filename_at(
				cache.as_ptr(),
				src.as_ptr(),
				0,
				48000,
				0x3,
				0,
				5,
				buf.as_mut_ptr(),
				512,
			)
		};
		assert_eq!(rc, OAKCODEC_E_NOT_FOUND);

		// Invalid args -> E_INVALID.
		let rc = unsafe {
			oakcodec_conform_filename_at(
				std::ptr::null(),
				src.as_ptr(),
				0,
				48000,
				0x3,
				0,
				0,
				buf.as_mut_ptr(),
				512,
			)
		};
		assert_eq!(rc, OAKCODEC_E_INVALID);
	}
}
