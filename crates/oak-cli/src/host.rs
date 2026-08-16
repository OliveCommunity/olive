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

//! Host-side `oakcore_audioparams_*` shims.
//!
//! The engine dylib links with `-undefined dynamic_lookup`, leaving a few
//! liboakcore symbols (`oakcore_audioparams_*`, used by
//! `oakengine_renderer_render_audio` and the sequence audio-params path)
//! to be resolved from the host process at runtime. The C++ `cli/main.cpp`
//! host linked liboakcore; the Rust CLI provides the same symbols itself —
//! the binary is the host. `build.rs` passes `-Wl,-export_dynamic` so the
//! linker exports them, and [`exports`] keeps them referenced.
//!
//! Semantics mirror the C++ `olive::core::AudioParams`:
//! `{sample_rate, channel_layout, format, time_base}` with the time base
//! defaulting to `1/sample_rate`, exactly like the liboakcore constructor
//! (`oakcore_audioparams.cpp`).

use std::ffi::c_void;

/// The liboakcore `AudioParams` payload (`oakcore_audioparams.h`).
struct AudioParams {
	sample_rate: i32,
	channel_layout: u64,
	format: i32,
	time_base_num: i32,
	time_base_den: i32,
}

/// `oakcore_audioparams_create` — allocate with `time_base = 1/sample_rate`.
///
/// # Safety
/// None; returns an owned box cast to `void*` (NULL never happens for
/// valid inputs; a zero sample rate keeps the caller's contract intact).
#[no_mangle]
pub unsafe extern "C" fn oakcore_audioparams_create(
	sample_rate: i32,
	channel_layout: u64,
	format: i32,
) -> *mut c_void {
	let den = if sample_rate > 0 { sample_rate } else { 1 };
	Box::into_raw(Box::new(AudioParams {
		sample_rate,
		channel_layout,
		format,
		time_base_num: 1,
		time_base_den: den,
	})) as *mut c_void
}

/// `oakcore_audioparams_free` — NULL no-op.
///
/// # Safety
/// `params` must be a pointer from [`oakcore_audioparams_create`] or NULL.
#[no_mangle]
pub unsafe extern "C" fn oakcore_audioparams_free(params: *mut c_void) {
	unsafe {
		if !params.is_null() {
			drop(Box::from_raw(params as *mut AudioParams));
		}
	}
}

/// `oakcore_audioparams_sample_rate` — 0 for NULL (liboakcore contract).
///
/// # Safety
/// `params` must be a pointer from [`oakcore_audioparams_create`] or NULL.
#[no_mangle]
pub unsafe extern "C" fn oakcore_audioparams_sample_rate(params: *const c_void) -> i32 {
	unsafe {
		if params.is_null() {
			0
		} else {
			(*(params as *const AudioParams)).sample_rate
		}
	}
}

/// `oakcore_audioparams_channel_layout` — 0 for NULL.
///
/// # Safety
/// `params` must be a pointer from [`oakcore_audioparams_create`] or NULL.
#[no_mangle]
pub unsafe extern "C" fn oakcore_audioparams_channel_layout(params: *const c_void) -> u64 {
	unsafe {
		if params.is_null() {
			0
		} else {
			(*(params as *const AudioParams)).channel_layout
		}
	}
}

/// `oakcore_audioparams_format` — 0 for NULL.
///
/// # Safety
/// `params` must be a pointer from [`oakcore_audioparams_create`] or NULL.
#[no_mangle]
pub unsafe extern "C" fn oakcore_audioparams_format(params: *const c_void) -> i32 {
	unsafe {
		if params.is_null() {
			0
		} else {
			(*(params as *const AudioParams)).format
		}
	}
}

/// `oakcore_audioparams_set_time_base` — NULL no-op.
///
/// # Safety
/// `params` must be a pointer from [`oakcore_audioparams_create`] or NULL.
#[no_mangle]
pub unsafe extern "C" fn oakcore_audioparams_set_time_base(
	params: *mut c_void,
	num: i32,
	den: i32,
) {
	unsafe {
		if params.is_null() {
			return;
		}
		let p = &mut *(params as *mut AudioParams);
		p.time_base_num = num;
		p.time_base_den = den;
	}
}

/// Keep-alive references so the linker never drops the host shims
/// (referenced from `main`; the `-export_dynamic` flag exports them for
/// the engine dylib's runtime lookups).
pub fn exports() -> usize {
	let fns: [usize; 6] = [
		oakcore_audioparams_create as *const () as usize,
		oakcore_audioparams_free as *const () as usize,
		oakcore_audioparams_sample_rate as *const () as usize,
		oakcore_audioparams_channel_layout as *const () as usize,
		oakcore_audioparams_format as *const () as usize,
		oakcore_audioparams_set_time_base as *const () as usize,
	];
	fns.iter().sum()
}
