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

//! oakcore C ABI imports (audio stream parameters). dlsym-resolved (see
//! [`super`]). The `OakAudioParams` object is an opaque raw pointer owned
//! by the caller (`oakcore_audioparams_free`), not a [`crate::handle::CHandle`].

use std::ffi::{c_int, c_void};

#[cfg(not(feature = "test-stubs"))]
/// `oakcore_audioparams_create` — new owned params (release with
/// [`audioparams_free`]).
pub fn audioparams_create(
	sample_rate: c_int,
	channel_layout: u64,
	format: c_int,
) -> Option<*mut c_void> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(c_int, u64, c_int) -> *mut c_void;
	dlsym::call::<F, *mut c_void>("oakcore_audioparams_create", |f| unsafe {
		f(sample_rate, channel_layout, format)
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn audioparams_create(
	sample_rate: c_int,
	channel_layout: u64,
	format: c_int,
) -> Option<*mut c_void> {
	Some(unsafe { stub::oakcore_audioparams_create(sample_rate, channel_layout, format) })
}

/// `oakcore_audioparams_free`.
pub fn audioparams_free(params: *mut c_void) {
	if params.is_null() {
		return;
	}
	#[cfg(feature = "test-stubs")]
	unsafe {
		stub::oakcore_audioparams_free(params);
	}
	#[cfg(not(feature = "test-stubs"))]
	{
		use crate::bridge::dlsym;
		type F = unsafe extern "C" fn(*mut c_void);
		let _ = dlsym::call::<F, ()>("oakcore_audioparams_free", |f| unsafe { f(params) });
	}
}

#[cfg(not(feature = "test-stubs"))]
/// `oakcore_audioparams_sample_rate`.
pub fn audioparams_sample_rate(params: *const c_void) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*const c_void) -> c_int;
	dlsym::call::<F, c_int>("oakcore_audioparams_sample_rate", |f| unsafe { f(params) })
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn audioparams_sample_rate(params: *const c_void) -> Option<c_int> {
	Some(unsafe { stub::oakcore_audioparams_sample_rate(params) })
}

#[cfg(not(feature = "test-stubs"))]
/// `oakcore_audioparams_channel_layout`.
pub fn audioparams_channel_layout(params: *const c_void) -> Option<u64> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*const c_void) -> u64;
	dlsym::call::<F, u64>("oakcore_audioparams_channel_layout", |f| unsafe {
		f(params)
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn audioparams_channel_layout(params: *const c_void) -> Option<u64> {
	Some(unsafe { stub::oakcore_audioparams_channel_layout(params) })
}

#[cfg(not(feature = "test-stubs"))]
/// `oakcore_audioparams_format`.
pub fn audioparams_format(params: *const c_void) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*const c_void) -> c_int;
	dlsym::call::<F, c_int>("oakcore_audioparams_format", |f| unsafe { f(params) })
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn audioparams_format(params: *const c_void) -> Option<c_int> {
	Some(unsafe { stub::oakcore_audioparams_format(params) })
}

/// In-crate implementations of the oakcore audioparams C ABI for
/// `cargo test` (`--features test-stubs`). Mirrors the real object: a
/// plain struct behind the caller-owned pointer.
#[cfg(feature = "test-stubs")]
pub(crate) mod stub {
	use super::*;

	/// `oakcore_audioparams_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcore_audioparams_create(
		sample_rate: c_int,
		channel_layout: u64,
		format: c_int,
	) -> *mut c_void {
		Box::into_raw(Box::new(StubAudioParams {
			sample_rate,
			channel_layout,
			format,
		})) as *mut c_void
	}

	/// `oakcore_audioparams_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcore_audioparams_free(params: *mut c_void) {
		if !params.is_null() {
			unsafe { drop(Box::from_raw(params as *mut StubAudioParams)) };
		}
	}

	/// `oakcore_audioparams_sample_rate`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcore_audioparams_sample_rate(params: *const c_void) -> c_int {
		if params.is_null() {
			return 0;
		}
		unsafe { (*(params as *const StubAudioParams)).sample_rate }
	}

	/// `oakcore_audioparams_channel_layout`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcore_audioparams_channel_layout(params: *const c_void) -> u64 {
		if params.is_null() {
			return 0;
		}
		unsafe { (*(params as *const StubAudioParams)).channel_layout }
	}

	/// `oakcore_audioparams_format`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcore_audioparams_format(params: *const c_void) -> c_int {
		if params.is_null() {
			return 0;
		}
		unsafe { (*(params as *const StubAudioParams)).format }
	}

	/// Boxed audioparams stub payload.
	pub(crate) struct StubAudioParams {
		pub sample_rate: c_int,
		pub channel_layout: u64,
		pub format: c_int,
	}
}
