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

//! oakcommon C ABI bridge: direct Rust calls into the `oakcommon` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oakcommon`'s `ffi` (the
//! `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them). Handles cross as the shared
//! [`crate::handle::CHandle`]. Exceptions that keep an `extern "C"`
//! declaration (resolved at link time against the sibling crate in the
//! same dylib) are the host `oakcore_*` symbols and the encoding-params
//! C ABI POD crossings (the facade keeps its own POD mirrors there).

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

//! oakcommon C ABI imports, mirroring the oakcommon crate's exports
//! (`src/common/rust/src/ffi.rs`; headers `include/common/*.h`). Only the
//! families the facade wraps: config, videoparams, colortransform, xml
//! reader/writer and the decibel helpers.

use std::ffi::{c_char, c_int, c_void};

use crate::handle::CHandle;

/// `include/common/config.h` — error handler callback.
pub type ConfigErrorHandler =
	Option<unsafe extern "C" fn(title: *const c_char, message: *const c_char, userdata: *mut c_void)>;

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_load() -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_load() }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_save() -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_save() }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_reset_defaults() -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_reset_defaults() }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_set(group: *const c_char, key: *const c_char, value: *const c_char) {
	unsafe { oakcommon::ffi::config::oakcommon_config_set(group, key, value) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_get(
		group: *const c_char,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_get(group, key, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_get_int(group: *const c_char, key: *const c_char, fallback: c_int) -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_get_int(group, key, fallback) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_get_int64(
		group: *const c_char,
		key: *const c_char,
		fallback: i64,
	) -> i64 {
	unsafe { oakcommon::ffi::config::oakcommon_config_get_int64(group, key, fallback) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_get_double(group: *const c_char, key: *const c_char, fallback: f64) -> f64 {
	unsafe { oakcommon::ffi::config::oakcommon_config_get_double(group, key, fallback) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_get_bool(group: *const c_char, key: *const c_char, fallback: c_int) -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_get_bool(group, key, fallback) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_set_int(group: *const c_char, key: *const c_char, value: c_int) {
	unsafe { oakcommon::ffi::config::oakcommon_config_set_int(group, key, value) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_set_int64(group: *const c_char, key: *const c_char, value: i64) {
	unsafe { oakcommon::ffi::config::oakcommon_config_set_int64(group, key, value) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_set_double(group: *const c_char, key: *const c_char, value: f64) {
	unsafe { oakcommon::ffi::config::oakcommon_config_set_double(group, key, value) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_set_bool(group: *const c_char, key: *const c_char, value: c_int) {
	unsafe { oakcommon::ffi::config::oakcommon_config_set_bool(group, key, value) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_entry_type(group: *const c_char, key: *const c_char) -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_entry_type(group, key) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_config_set_error_handler(handler: ConfigErrorHandler, userdata: *mut c_void) -> c_int {
	unsafe { oakcommon::ffi::config::oakcommon_config_set_error_handler(handler, userdata) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_init() -> CHandle {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_init() }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_init_basic(
	width: c_int,
	height: c_int,
	pixel_format: c_int,
	nb_channels: c_int,
	pixel_aspect_num: c_int,
	pixel_aspect_den: c_int,
	interlacing: c_int,
	divider: c_int,
) -> CHandle {
	unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_init_basic(
			width, height, pixel_format, nb_channels, pixel_aspect_num, pixel_aspect_den,
			interlacing, divider,
		)
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_init_with_time_base(
	width: c_int,
	height: c_int,
	time_base_num: c_int,
	time_base_den: c_int,
	pixel_format: c_int,
	nb_channels: c_int,
	pixel_aspect_num: c_int,
	pixel_aspect_den: c_int,
	interlacing: c_int,
	divider: c_int,
) -> CHandle {
	unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_init_with_time_base(
			width, height, time_base_num, time_base_den, pixel_format, nb_channels,
			pixel_aspect_num, pixel_aspect_den, interlacing, divider,
		)
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_free(params: *mut CHandle) {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_free(params) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_width(params: CHandle, width: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_width(params, width) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_width(params: CHandle, width: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_width(params, width) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_height(params: CHandle, height: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_height(params, height) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_height(params: CHandle, height: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_height(params, height) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_time_base(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_time_base(params, numerator, denominator) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_time_base(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_time_base(params, numerator, denominator) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_frame_rate(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_frame_rate(params, numerator, denominator) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_frame_rate(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_frame_rate(params, numerator, denominator) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_pixel_aspect_ratio(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_pixel_aspect_ratio(params, numerator, denominator) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_pixel_aspect_ratio(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_pixel_aspect_ratio(params, numerator, denominator) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_format(params: CHandle, format: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_format(params, format) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_format(params: CHandle, format: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_format(params, format) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_interlacing(params: CHandle, interlacing: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_interlacing(params, interlacing) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_interlacing(params: CHandle, interlacing: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_interlacing(params, interlacing) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_divider(params: CHandle, divider: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_divider(params, divider) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_divider(params: CHandle, divider: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_divider(params, divider) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_video_type(params: CHandle, type_: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_video_type(params, type_) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_video_type(params: CHandle, type_: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_video_type(params, type_) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_premultiplied_alpha(
		params: CHandle,
		premultiplied: *mut c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_premultiplied_alpha(params, premultiplied) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_premultiplied_alpha(params: CHandle, premultiplied: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_premultiplied_alpha(params, premultiplied) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_color_range(params: CHandle, color_range: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_color_range(params, color_range) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_set_color_range(params: CHandle, color_range: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_set_color_range(params, color_range) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_is_valid(params: CHandle, valid: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_is_valid(params, valid) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_effective_width(params: CHandle, width: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_effective_width(params, width) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_effective_height(params: CHandle, height: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_effective_height(params, height) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_bytes_per_pixel(params: CHandle, bytes: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_bytes_per_pixel(params, bytes) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_equals(a: CHandle, b: CHandle, out_equal: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_equals(a, b, out_equal) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_format_is_float(pixel_format: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_format_is_float(pixel_format) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_format_name(
		pixel_format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_format_name(pixel_format, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_frame_rate_to_string(
		numerator: c_int,
		denominator: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_frame_rate_to_string(numerator, denominator, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_name_for_divider(
		divider: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_name_for_divider(divider, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_scaled_dimension(dimension: c_int, divider: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_scaled_dimension(dimension, divider) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_generate_auto_divider(width: i64, height: i64) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_generate_auto_divider(width, height) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_divider_for_target_resolution(
		src_width: c_int,
		src_height: c_int,
		target_width: c_int,
		target_height: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_divider_for_target_resolution(src_width, src_height, target_width, target_height) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_bytes_per_channel_for_format(pixel_format: c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_bytes_per_channel_for_format(pixel_format) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_bytes_per_pixel_for_format(
	pixel_format: c_int,
	channels: c_int,
) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_bytes_per_pixel_for_format(pixel_format, channels) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_static_get_bytes_per_pixel(
		pixel_format: c_int,
		channels: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_static_get_bytes_per_pixel(pixel_format, channels) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_buffer_size(params: CHandle, size: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_buffer_size(params, size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_videoparams_get_time_in_timebase_units(
	params: CHandle,
	time_num: c_int,
	time_den: c_int,
	timestamp: *mut i64,
) -> c_int {
	unsafe { oakcommon::ffi::videoparams::oakcommon_videoparams_get_time_in_timebase_units(params, time_num, time_den, timestamp) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_colortransform_init_output(output: *const c_char) -> CHandle {
	unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_init_output(output) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_colortransform_init_display(
		display: *const c_char,
		view: *const c_char,
		look: *const c_char,
	) -> CHandle {
	unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_init_display(display, view, look) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_colortransform_free(transform: *mut CHandle) {
	unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_free(transform) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_colortransform_is_display(transform: CHandle) -> c_int {
	unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_is_display(transform) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_colortransform_get_display(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_get_display(transform, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_colortransform_get_output(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_get_output(transform, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_colortransform_get_view(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_get_view(transform, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_colortransform_get_look(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_get_look(transform, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_init(data: *const c_char) -> CHandle {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_init(data) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_free(reader: *mut CHandle) {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_free(reader) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_read_next_start_element(reader: CHandle, found: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_read_next_start_element(reader, found) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_name(reader: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_name(reader, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_read_element_text(
		reader: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_read_element_text(reader, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_skip_current_element(reader: CHandle) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_skip_current_element(reader) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_attribute_count(reader: CHandle, count: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_count(reader, count) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_attribute_name(
		reader: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_name(reader, index, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_attribute_value(
		reader: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_value(reader, index, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_reader_has_error(reader: CHandle, has_error: *mut c_int) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_has_error(reader, has_error) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_init() -> CHandle {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_init() }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_free(writer: *mut CHandle) {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_free(writer) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_write_start_element(writer: CHandle, name: *const c_char) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_start_element(writer, name) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_write_attribute(
		writer: CHandle,
		name: *const c_char,
		value: *const c_char,
	) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_attribute(writer, name, value) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_write_characters(writer: CHandle, text: *const c_char) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_characters(writer, text) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_write_text_element(
		writer: CHandle,
		name: *const c_char,
		text: *const c_char,
	) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_text_element(writer, name, text) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_write_end_element(writer: CHandle) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_end_element(writer) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_write_end_document(writer: CHandle) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_end_document(writer) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_xml_writer_output(writer: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_output(writer, buf, buf_size) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_decibel_from_linear(linear: f64, out_db: *mut f64) -> c_int {
	unsafe { oakcommon::ffi::misc::oakcommon_decibel_from_linear(linear, out_db) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_decibel_to_linear(db: f64, out_linear: *mut f64) -> c_int {
	unsafe { oakcommon::ffi::misc::oakcommon_decibel_to_linear(db, out_linear) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_decibel_from_logarithmic(logarithmic: f64, out_db: *mut f64) -> c_int {
	unsafe { oakcommon::ffi::misc::oakcommon_decibel_from_logarithmic(logarithmic, out_db) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_decibel_to_logarithmic(db: f64, out_logarithmic: *mut f64) -> c_int {
	unsafe { oakcommon::ffi::misc::oakcommon_decibel_to_logarithmic(db, out_logarithmic) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_decibel_linear_to_logarithmic(linear: f64, out_logarithmic: *mut f64) -> c_int {
	unsafe { oakcommon::ffi::misc::oakcommon_decibel_linear_to_logarithmic(linear, out_logarithmic) }
}

/// Direct call into the `oakcommon` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakcommon_decibel_logarithmic_to_linear(logarithmic: f64, out_linear: *mut f64) -> c_int {
	unsafe { oakcommon::ffi::misc::oakcommon_decibel_logarithmic_to_linear(logarithmic, out_linear) }
}

