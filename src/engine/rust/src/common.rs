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

//! `engine/include/oakengine/config.h` and
//! `engine/include/oakengine/videoparams.h` over the oakcommon module.
//!
//! The engine config family uses flat keys; the oakcommon store is
//! `(group, key)` — the facade passes group = NULL. Engine semantics that
//! differ from the module are honored here (a missing key reads as an
//! empty string / 0, not a module error).
//!
//! The engine videoparams family is mostly **facade-local static data**
//! (the standard frame-rate / pixel-aspect / divider tables from
//! `engine/render/videoparams.cpp`) plus POD↔handle conversion over the
//! oakcommon `OakVideoParams` handle; see the mapping notes per function.

use std::ffi::{c_char, c_int, c_void};
use std::sync::{Mutex, OnceLock};

use crate::bridge::common as c;
use crate::error::Error;
use crate::handle::{
	box_handle, free_box, guard, guard_int, guard_void, string_result, OakEngineClipboard,
};

// ---------------------------------------------------------------------------
// config.h
// ---------------------------------------------------------------------------

/// Facade copy of the registered config error handler (the module keeps
/// its own copy for load/save errors; this one backs
/// `oakengine_config_report_error`). The userdata pointer is stored as
/// `usize` so the static stays Send/Sync.
static ERROR_FN: OnceLock<Mutex<Option<(Option<ConfigErrorFn>, usize)>>> = OnceLock::new();

/// `engine/include/oakengine/config.h` error callback.
pub type ConfigErrorFn = unsafe extern "C" fn(title: *const c_char, message: *const c_char, userdata: *mut c_void);

fn error_fn_slot() -> &'static Mutex<Option<(Option<ConfigErrorFn>, usize)>> {
	ERROR_FN.get_or_init(|| Mutex::new(None))
}

/// `oakengine_config_load` — load configuration from disk.
#[no_mangle]
pub extern "C" fn oakengine_config_load() -> c_int {
	guard(|| Error::from_module(unsafe { c::oakcommon_config_load() }))
}

/// `oakengine_config_save` — save configuration to disk.
#[no_mangle]
pub extern "C" fn oakengine_config_save() -> c_int {
	guard(|| Error::from_module(unsafe { c::oakcommon_config_save() }))
}

/// `oakengine_config_get_string` — read a string value (buf/size).
/// Returns the string length, 0 when the key is missing or empty.
#[no_mangle]
pub unsafe extern "C" fn oakengine_config_get_string(
	key: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if key.is_null() {
			return Err(Error::Invalid);
		}
		let rc = c::oakcommon_config_get(std::ptr::null(), key, buf, buf_size);
		// Engine contract: a missing key reads as an empty string.
		if rc == -10004 {
			Ok(0)
		} else if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_config_set_string` — write a string value.
#[no_mangle]
pub unsafe extern "C" fn oakengine_config_set_string(
	key: *const c_char,
	value: *const c_char,
) -> c_int {
	guard(|| unsafe {
		if key.is_null() {
			return Err(Error::Invalid);
		}
		let value = if value.is_null() { empty_cstr() } else { value };
		c::oakcommon_config_set(std::ptr::null(), key, value);
		Ok(())
	})
}

/// `oakengine_config_get_int` — read an integer value (fallback when the
/// key is missing or not convertible).
#[no_mangle]
pub extern "C" fn oakengine_config_get_int(key: *const c_char, default_value: i64) -> i64 {
	crate::handle::guard_i64(|| unsafe {
		if key.is_null() {
			return Ok(default_value);
		}
		Ok(c::oakcommon_config_get_int64(std::ptr::null(), key, default_value))
	})
}

/// `oakengine_config_set_int` — write an integer value.
#[no_mangle]
pub unsafe extern "C" fn oakengine_config_set_int(key: *const c_char, value: i64) -> c_int {
	guard(|| unsafe {
		if key.is_null() {
			return Err(Error::Invalid);
		}
		c::oakcommon_config_set_int64(std::ptr::null(), key, value);
		Ok(())
	})
}

/// `oakengine_config_set_error_handler` — register the error callback
/// (NULL clears it). Forwards to oakcommon and keeps a facade copy for
/// `oakengine_config_report_error`.
#[no_mangle]
pub extern "C" fn oakengine_config_set_error_handler(
	fn_: Option<ConfigErrorFn>,
	userdata: *mut c_void,
) -> c_int {
	guard(|| {
		let mut slot = error_fn_slot().lock().unwrap_or_else(|e| e.into_inner());
		*slot = Some((fn_, userdata as usize));
		let rc = unsafe { c::oakcommon_config_set_error_handler(fn_, userdata) };
		if rc != 0 {
			return Err(Error::Module(rc));
		}
		Ok(())
	})
}

/// `oakengine_config_report_error` — report an error through the
/// registered handler (logged and discarded when none is set).
#[no_mangle]
pub unsafe extern "C" fn oakengine_config_report_error(title: *const c_char, message: *const c_char) -> c_int {
	guard(|| unsafe {
		let slot = error_fn_slot().lock().unwrap_or_else(|e| e.into_inner());
		if let Some((Some(fn_), userdata)) = *slot {
			let title = if title.is_null() { empty_cstr() } else { title };
			let message = if message.is_null() { empty_cstr() } else { message };
			fn_(title, message, userdata as *mut c_void);
		}
		Ok(())
	})
}

/// Static empty C string used where the engine treats NULL as "".
static EMPTY_CSTR: std::ffi::c_char = 0;

/// Pointer to the static empty C string.
pub(crate) fn empty_cstr() -> *const c_char {
	&EMPTY_CSTR as *const c_char
}

// ---------------------------------------------------------------------------
// videoparams.h — static tables (ported from engine/render/videoparams.cpp)
// ---------------------------------------------------------------------------

/// Standard frame rates as num/den
/// (`VideoParams::k_supported_frame_rates`).
const SUPPORTED_FRAME_RATES: &[(c_int, c_int)] = &[
	(10, 1),
	(15, 1),
	(24000, 1001),
	(24, 1),
	(25, 1),
	(30000, 1001),
	(30, 1),
	(48000, 1001),
	(48, 1),
	(50, 1),
	(60000, 1001),
	(60, 1),
];

/// Standard pixel aspect ratios as num/den
/// (`VideoParams::k_standard_pixel_aspects`).
const STANDARD_PIXEL_ASPECTS: &[(c_int, c_int)] = &[
	(1, 1),
	(8, 9),
	(32, 27),
	(16, 15),
	(64, 45),
	(4, 3),
];

/// Supported preview dividers (`VideoParams::k_supported_dividers`).
const SUPPORTED_DIVIDERS: &[c_int] = &[1, 2, 3, 4, 6, 8, 12, 16];

/// Engine-internal video channel count (RGBA).
const INTERNAL_CHANNEL_COUNT: c_int = 4;

/// `oakengine_video_params_supported_frame_rate_count`.
#[no_mangle]
pub extern "C" fn oakengine_video_params_supported_frame_rate_count() -> c_int {
	guard_int(|| Ok(SUPPORTED_FRAME_RATES.len() as c_int))
}

/// `oakengine_video_params_supported_frame_rate_at` — num/den at `index`.
#[no_mangle]
pub extern "C" fn oakengine_video_params_supported_frame_rate_at(
	index: c_int,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if num.is_null() || den.is_null() {
			return Err(Error::Invalid);
		}
		match SUPPORTED_FRAME_RATES.get(index as usize) {
			Some((n, d)) => {
				*num = *n;
				*den = *d;
				Ok(())
			}
			None => Err(Error::Invalid),
		}
	})
}

/// `oakengine_video_params_frame_rate_to_string` — label of a frame rate.
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_frame_rate_to_string(
	num: c_int,
	den: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let rc = c::oakcommon_videoparams_frame_rate_to_string(num, den, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_video_params_standard_pixel_aspect_count`.
#[no_mangle]
pub extern "C" fn oakengine_video_params_standard_pixel_aspect_count() -> c_int {
	guard_int(|| Ok(STANDARD_PIXEL_ASPECTS.len() as c_int))
}

/// `oakengine_video_params_standard_pixel_aspect_at` — num/den at `index`.
#[no_mangle]
pub extern "C" fn oakengine_video_params_standard_pixel_aspect_at(
	index: c_int,
	num: *mut c_int,
	den: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if num.is_null() || den.is_null() {
			return Err(Error::Invalid);
		}
		match STANDARD_PIXEL_ASPECTS.get(index as usize) {
			Some((n, d)) => {
				*num = *n;
				*den = *d;
				Ok(())
			}
			None => Err(Error::Invalid),
		}
	})
}

/// `oakengine_video_params_standard_pixel_aspect_name` — display name of
/// the `index`-th standard pixel aspect. Built from the table the way the
/// C++ `VideoParams::standard_pixel_aspect_list()` populates the combo:
/// square = "Square", others = "num:den".
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_standard_pixel_aspect_name(
	index: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		match STANDARD_PIXEL_ASPECTS.get(index as usize) {
			Some((1, 1)) => Ok(crate::handle::write_string("Square", buf, buf_size)),
			Some((n, d)) => Ok(crate::handle::write_string(
				&format!("{n}:{d}"),
				buf,
				buf_size,
			)),
			None => Err(Error::Invalid),
		}
	})
}

/// `oakengine_video_params_format_pixel_aspect_ratio_string` — format a
/// printf-style template with the pixel aspect ratio.
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_format_pixel_aspect_ratio_string(
	format: *const c_char,
	num: c_int,
	den: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if format.is_null() {
			return Err(Error::Invalid);
		}
		let template = crate::handle::read_cstr(format);
		// The engine formats a single "%1" placeholder with num/den.
		let rendered = if template.contains("%1") {
			template.replace("%1", &format!("{num}:{den}"))
		} else {
			template
		};
		Ok(crate::handle::write_string(&rendered, buf, buf_size))
	})
}

/// `oakengine_video_params_supported_divider_count`.
#[no_mangle]
pub extern "C" fn oakengine_video_params_supported_divider_count() -> c_int {
	SUPPORTED_DIVIDERS.len() as c_int
}

/// `oakengine_video_params_supported_divider_at` — divider at `index`
/// (-1 when out of range).
#[no_mangle]
pub extern "C" fn oakengine_video_params_supported_divider_at(index: c_int) -> c_int {
	guard_int(|| {
		Ok(match SUPPORTED_DIVIDERS.get(index as usize) {
			Some(d) => *d,
			None => -1,
		})
	})
}

/// `oakengine_video_params_divider_name` — display name of a divider
/// (`VideoParams::get_name_for_divider`, ported).
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_divider_name(
	divider: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		if divider <= 0 {
			return Err(Error::Invalid);
		}
		let rc = c::oakcommon_videoparams_get_name_for_divider(divider, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_video_params_format_is_float` — 1 when the format is float.
#[no_mangle]
pub extern "C" fn oakengine_video_params_format_is_float(format: c_int) -> c_int {
	guard_int(|| Ok(unsafe { c::oakcommon_videoparams_format_is_float(format) }))
}

/// `oakengine_video_params_pixel_format_name` — display name of a format.
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_pixel_format_name(
	format: c_int,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	guard_int(|| unsafe {
		let rc = c::oakcommon_videoparams_get_format_name(format, buf, buf_size);
		if rc < 0 {
			Err(Error::Module(rc))
		} else {
			Ok(string_result(rc))
		}
	})
}

/// `oakengine_video_params_effective_size` — divider-scaled dimensions.
#[no_mangle]
pub extern "C" fn oakengine_video_params_effective_size(
	width: c_int,
	height: c_int,
	divider: c_int,
	out_width: *mut c_int,
	out_height: *mut c_int,
) -> c_int {
	guard(|| unsafe {
		if width <= 0 || height <= 0 || divider <= 0 {
			return Err(Error::Invalid);
		}
		if !out_width.is_null() {
			*out_width = c::oakcommon_videoparams_get_scaled_dimension(width, divider);
		}
		if !out_height.is_null() {
			*out_height = c::oakcommon_videoparams_get_scaled_dimension(height, divider);
		}
		Ok(())
	})
}

/// `oakengine_video_params_make` — fill an `oak_video_params` POD.
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_make(
	p: *mut OakVideoParamsPod,
	width: c_int,
	height: c_int,
	time_base_num: c_int,
	time_base_den: c_int,
	format: c_int,
	pixel_aspect_num: c_int,
	pixel_aspect_den: c_int,
	interlacing: c_int,
	color_range: c_int,
	divider: c_int,
) -> c_int {
	guard(|| unsafe {
		if p.is_null() {
			return Err(Error::Invalid);
		}
		(*p).width = width;
		(*p).height = height;
		(*p).time_base_num = time_base_num;
		(*p).time_base_den = time_base_den;
		(*p).format = format;
		(*p).pixel_aspect_num = pixel_aspect_num;
		(*p).pixel_aspect_den = pixel_aspect_den;
		(*p).interlacing = interlacing;
		(*p).color_range = color_range;
		(*p).divider = divider;
		(*p).video_type = 0;
		(*p).premultiplied_alpha = 0;
		Ok(())
	})
}

/// `oakengine_video_params_create` — create an engine-side VideoParams
/// from a POD (returns an opaque engine pointer; free with
/// `oakengine_video_params_free`).
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_create(pod: *const OakVideoParamsPod) -> *mut c_void {
	crate::handle::guard_ptr(|| unsafe {
		if pod.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let params = c::oakcommon_videoparams_init();
		if params.is_null() {
			return Ok(std::ptr::null_mut());
		}
		let mut rc = c::oakcommon_videoparams_set_width(params, (*pod).width);
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_height(params, (*pod).height);
		}
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_time_base(params, (*pod).time_base_num, (*pod).time_base_den);
		}
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_format(params, (*pod).format);
		}
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_pixel_aspect_ratio(
				params,
				(*pod).pixel_aspect_num,
				(*pod).pixel_aspect_den,
			);
		}
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_interlacing(params, (*pod).interlacing);
		}
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_color_range(params, (*pod).color_range);
		}
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_divider(params, (*pod).divider);
		}
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_video_type(params, (*pod).video_type);
		}
		if rc == 0 {
			rc = c::oakcommon_videoparams_set_premultiplied_alpha(params, (*pod).premultiplied_alpha);
		}
		if rc != 0 {
			let mut p = params;
			c::oakcommon_videoparams_free(&mut p);
			return Ok(std::ptr::null_mut());
		}
		Ok(box_handle::<OakEngineClipboard>(params).cast())
	})
}

/// `oakengine_video_params_free` — free a params object.
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_free(params: *mut c_void) {
	guard_void(|| unsafe {
		free_box(params.cast::<OakEngineClipboard>());
	})
}

/// `oakengine_video_params_equal` — 1 when all user-facing fields match.
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_equal(
	a: *const OakVideoParamsPod,
	b: *const OakVideoParamsPod,
) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if a.is_null() || b.is_null() {
			return Ok(0);
		}
		Ok(compare_pod(&*a, &*b))
	})
}

fn compare_pod(a: &OakVideoParamsPod, b: &OakVideoParamsPod) -> c_int {
	let same = a.width == b.width
		&& a.height == b.height
		&& a.time_base_num == b.time_base_num
		&& a.time_base_den == b.time_base_den
		&& a.format == b.format
		&& a.pixel_aspect_num == b.pixel_aspect_num
		&& a.pixel_aspect_den == b.pixel_aspect_den
		&& a.interlacing == b.interlacing
		&& a.color_range == b.color_range
		&& a.divider == b.divider
		&& a.video_type == b.video_type
		&& a.premultiplied_alpha == b.premultiplied_alpha;
	if same {
		1
	} else {
		0
	}
}

/// `oakengine_video_params_is_valid` — 1 when the POD describes a usable
/// video stream.
#[no_mangle]
pub unsafe extern "C" fn oakengine_video_params_is_valid(p: *const OakVideoParamsPod) -> c_int {
	crate::handle::guard_int(|| unsafe {
		if p.is_null() {
			return Ok(0);
		}
		let pod = &*p;
		let valid = pod.width > 0
			&& pod.height > 0
			&& pod.pixel_aspect_num > 0
			&& pod.pixel_aspect_den > 0
			&& pod.format >= 0
			&& pod.time_base_den > 0;
		Ok(if valid { 1 } else { 0 })
	})
}

/// `oakengine_video_params_bytes_per_pixel` — bytes per pixel of
/// `format` with `channels` channels.
#[no_mangle]
pub extern "C" fn oakengine_video_params_bytes_per_pixel(format: c_int, channels: c_int) -> c_int {
	guard_int(|| Ok(unsafe { c::oakcommon_videoparams_static_get_bytes_per_pixel(format, channels) }))
}

/// `oakengine_video_params_internal_channel_count` — RGBA.
#[no_mangle]
pub extern "C" fn oakengine_video_params_internal_channel_count() -> c_int {
	guard_int(|| Ok(INTERNAL_CHANNEL_COUNT))
}

/// `engine/include/oakengine/videoparams.h` — POD mirror of VideoParams'
/// user-facing fields. Rust mirror of `oak_video_params`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakVideoParamsPod {
	/// Width.
	pub width: c_int,
	/// Height.
	pub height: c_int,
	/// Frame duration numerator (e.g. 1001/30000 s).
	pub time_base_num: c_int,
	/// Frame duration denominator.
	pub time_base_den: c_int,
	/// PixelFormat::Format value.
	pub format: c_int,
	/// Pixel aspect numerator.
	pub pixel_aspect_num: c_int,
	/// Pixel aspect denominator.
	pub pixel_aspect_den: c_int,
	/// Interlacing value.
	pub interlacing: c_int,
	/// ColorRange value.
	pub color_range: c_int,
	/// Preview resolution divider (1 = full).
	pub divider: c_int,
	/// VideoParams::Type value.
	pub video_type: c_int,
	/// 0/1 premultiplied alpha.
	pub premultiplied_alpha: c_int,
}
