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

extern "C" {
	// ---- config.h -------------------------------------------------------
	/// `oakcommon_config_load` — reset to defaults, then read config.ini.
	pub fn oakcommon_config_load() -> c_int;
	/// `oakcommon_config_save` — write config.ini (temp file + rename).
	pub fn oakcommon_config_save() -> c_int;
	/// `oakcommon_config_reset_defaults` — drop custom keys.
	pub fn oakcommon_config_reset_defaults() -> c_int;
	/// `oakcommon_config_set` — set a string entry.
	pub fn oakcommon_config_set(group: *const c_char, key: *const c_char, value: *const c_char);
	/// `oakcommon_config_get` — read an entry as string (two-stage).
	pub fn oakcommon_config_get(
		group: *const c_char,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_config_get_int` — INT entry with fallback.
	pub fn oakcommon_config_get_int(group: *const c_char, key: *const c_char, fallback: c_int) -> c_int;
	/// `oakcommon_config_get_int64` — INT64 entry with fallback.
	pub fn oakcommon_config_get_int64(
		group: *const c_char,
		key: *const c_char,
		fallback: i64,
	) -> i64;
	/// `oakcommon_config_get_double` — DOUBLE entry with fallback.
	pub fn oakcommon_config_get_double(group: *const c_char, key: *const c_char, fallback: f64) -> f64;
	/// `oakcommon_config_get_bool` — BOOL entry with fallback.
	pub fn oakcommon_config_get_bool(group: *const c_char, key: *const c_char, fallback: c_int) -> c_int;
	/// `oakcommon_config_set_int` — set an INT entry.
	pub fn oakcommon_config_set_int(group: *const c_char, key: *const c_char, value: c_int);
	/// `oakcommon_config_set_int64` — set an INT64 entry.
	pub fn oakcommon_config_set_int64(group: *const c_char, key: *const c_char, value: i64);
	/// `oakcommon_config_set_double` — set a DOUBLE entry.
	pub fn oakcommon_config_set_double(group: *const c_char, key: *const c_char, value: f64);
	/// `oakcommon_config_set_bool` — set a BOOL entry.
	pub fn oakcommon_config_set_bool(group: *const c_char, key: *const c_char, value: c_int);
	/// `oakcommon_config_entry_type` — the entry's declared type.
	pub fn oakcommon_config_entry_type(group: *const c_char, key: *const c_char) -> c_int;
	/// `oakcommon_config_set_error_handler` — install the UI error handler.
	pub fn oakcommon_config_set_error_handler(handler: ConfigErrorHandler, userdata: *mut c_void) -> c_int;

	// ---- videoparams.h --------------------------------------------------
	/// `oakcommon_videoparams_init` — default video params, refcount 1.
	pub fn oakcommon_videoparams_init() -> CHandle;
	/// `oakcommon_videoparams_init_basic` — width/height/frame-rate params.
	pub fn oakcommon_videoparams_init_basic(
		width: c_int,
		height: c_int,
		time_base_num: c_int,
		time_base_den: c_int,
	) -> CHandle;
	/// `oakcommon_videoparams_init_with_time_base` — params + time base.
	pub fn oakcommon_videoparams_init_with_time_base(
		width: c_int,
		height: c_int,
		time_base_num: c_int,
		time_base_den: c_int,
	) -> CHandle;
	/// `oakcommon_videoparams_free` — NULL/empty no-op; clears `params->ctx`.
	pub fn oakcommon_videoparams_free(params: *mut CHandle);
	/// `oakcommon_videoparams_get_width`.
	pub fn oakcommon_videoparams_get_width(params: CHandle, width: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_width`.
	pub fn oakcommon_videoparams_set_width(params: CHandle, width: c_int) -> c_int;
	/// `oakcommon_videoparams_get_height`.
	pub fn oakcommon_videoparams_get_height(params: CHandle, height: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_height`.
	pub fn oakcommon_videoparams_set_height(params: CHandle, height: c_int) -> c_int;
	/// `oakcommon_videoparams_get_time_base`.
	pub fn oakcommon_videoparams_get_time_base(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_set_time_base`.
	pub fn oakcommon_videoparams_set_time_base(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_frame_rate`.
	pub fn oakcommon_videoparams_get_frame_rate(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_set_frame_rate`.
	pub fn oakcommon_videoparams_set_frame_rate(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_pixel_aspect_ratio`.
	pub fn oakcommon_videoparams_get_pixel_aspect_ratio(
		params: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_set_pixel_aspect_ratio`.
	pub fn oakcommon_videoparams_set_pixel_aspect_ratio(
		params: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_format`.
	pub fn oakcommon_videoparams_get_format(params: CHandle, format: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_format`.
	pub fn oakcommon_videoparams_set_format(params: CHandle, format: c_int) -> c_int;
	/// `oakcommon_videoparams_get_interlacing`.
	pub fn oakcommon_videoparams_get_interlacing(params: CHandle, interlacing: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_interlacing`.
	pub fn oakcommon_videoparams_set_interlacing(params: CHandle, interlacing: c_int) -> c_int;
	/// `oakcommon_videoparams_get_divider`.
	pub fn oakcommon_videoparams_get_divider(params: CHandle, divider: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_divider`.
	pub fn oakcommon_videoparams_set_divider(params: CHandle, divider: c_int) -> c_int;
	/// `oakcommon_videoparams_get_video_type`.
	pub fn oakcommon_videoparams_get_video_type(params: CHandle, type_: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_video_type`.
	pub fn oakcommon_videoparams_set_video_type(params: CHandle, type_: c_int) -> c_int;
	/// `oakcommon_videoparams_get_premultiplied_alpha`.
	pub fn oakcommon_videoparams_get_premultiplied_alpha(
		params: CHandle,
		premultiplied: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_set_premultiplied_alpha`.
	pub fn oakcommon_videoparams_set_premultiplied_alpha(params: CHandle, premultiplied: c_int) -> c_int;
	/// `oakcommon_videoparams_get_color_range`.
	pub fn oakcommon_videoparams_get_color_range(params: CHandle, color_range: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_set_color_range`.
	pub fn oakcommon_videoparams_set_color_range(params: CHandle, color_range: c_int) -> c_int;
	/// `oakcommon_videoparams_get_is_valid`.
	pub fn oakcommon_videoparams_get_is_valid(params: CHandle, valid: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_effective_width` — divider-scaled width.
	pub fn oakcommon_videoparams_get_effective_width(params: CHandle, width: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_effective_height` — divider-scaled height.
	pub fn oakcommon_videoparams_get_effective_height(params: CHandle, height: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_get_bytes_per_pixel` — with the params' format.
	pub fn oakcommon_videoparams_get_bytes_per_pixel(params: CHandle, bytes: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_equals` — user-facing field equality.
	pub fn oakcommon_videoparams_equals(a: CHandle, b: CHandle, out_equal: *mut c_int) -> c_int;
	/// `oakcommon_videoparams_format_is_float` — 1 when the format is float.
	pub fn oakcommon_videoparams_format_is_float(pixel_format: c_int) -> c_int;
	/// `oakcommon_videoparams_get_format_name` — display name (two-stage).
	pub fn oakcommon_videoparams_get_format_name(
		pixel_format: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_frame_rate_to_string` — label (two-stage).
	pub fn oakcommon_videoparams_frame_rate_to_string(
		numerator: c_int,
		denominator: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_name_for_divider` — label (two-stage).
	pub fn oakcommon_videoparams_get_name_for_divider(
		divider: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_scaled_dimension` — size at a divider.
	pub fn oakcommon_videoparams_get_scaled_dimension(dimension: c_int, divider: c_int) -> c_int;
	/// `oakcommon_videoparams_generate_auto_divider` — best divider for size.
	pub fn oakcommon_videoparams_generate_auto_divider(width: i64, height: i64) -> c_int;
	/// `oakcommon_videoparams_get_divider_for_target_resolution`.
	pub fn oakcommon_videoparams_get_divider_for_target_resolution(
		src_width: c_int,
		src_height: c_int,
		target_width: c_int,
		target_height: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_bytes_per_channel_for_format`.
	pub fn oakcommon_videoparams_get_bytes_per_channel_for_format(
		pixel_format: c_int,
		bytes: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_bytes_per_pixel_for_format`.
	pub fn oakcommon_videoparams_get_bytes_per_pixel_for_format(
		pixel_format: c_int,
		bytes: *mut c_int,
	) -> c_int;
	/// `oakcommon_videoparams_static_get_bytes_per_pixel`.
	pub fn oakcommon_videoparams_static_get_bytes_per_pixel(
		pixel_format: c_int,
		channels: c_int,
	) -> c_int;
	/// `oakcommon_videoparams_get_buffer_size`.
	pub fn oakcommon_videoparams_get_buffer_size(params: CHandle, bytes: *mut i64) -> c_int;
	/// `oakcommon_videoparams_get_time_in_timebase_units`.
	pub fn oakcommon_videoparams_get_time_in_timebase_units(
		params: CHandle,
		time_num: i64,
		time_den: i64,
		out: *mut i64,
	) -> c_int;

	// ---- colortransform.h -----------------------------------------------
	/// `oakcommon_colortransform_init_output` — output color space transform.
	pub fn oakcommon_colortransform_init_output(output: *const c_char) -> CHandle;
	/// `oakcommon_colortransform_init_display` — display/view/look transform.
	pub fn oakcommon_colortransform_init_display(
		display: *const c_char,
		view: *const c_char,
		look: *const c_char,
	) -> CHandle;
	/// `oakcommon_colortransform_free` — NULL/empty no-op.
	pub fn oakcommon_colortransform_free(transform: *mut CHandle);
	/// `oakcommon_colortransform_is_display` — 1 for a display transform.
	pub fn oakcommon_colortransform_is_display(transform: CHandle) -> c_int;
	/// `oakcommon_colortransform_get_display` (two-stage string).
	pub fn oakcommon_colortransform_get_display(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_colortransform_get_output` (two-stage string).
	pub fn oakcommon_colortransform_get_output(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_colortransform_get_view` (two-stage string).
	pub fn oakcommon_colortransform_get_view(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_colortransform_get_look` (two-stage string).
	pub fn oakcommon_colortransform_get_look(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;

	// ---- xmlutils.h -----------------------------------------------------
	/// `oakcommon_xml_reader_init` — reader over a NUL-terminated document.
	pub fn oakcommon_xml_reader_init(data: *const c_char) -> CHandle;
	/// `oakcommon_xml_reader_free` — NULL/empty no-op.
	pub fn oakcommon_xml_reader_free(reader: *mut CHandle);
	/// `oakcommon_xml_reader_read_next_start_element` — found 1/0 in `found`.
	pub fn oakcommon_xml_reader_read_next_start_element(reader: CHandle, found: *mut c_int) -> c_int;
	/// `oakcommon_xml_reader_name` (two-stage string).
	pub fn oakcommon_xml_reader_name(reader: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oakcommon_xml_reader_read_element_text` (two-stage string).
	pub fn oakcommon_xml_reader_read_element_text(
		reader: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_xml_reader_skip_current_element`.
	pub fn oakcommon_xml_reader_skip_current_element(reader: CHandle) -> c_int;
	/// `oakcommon_xml_reader_attribute_count`.
	pub fn oakcommon_xml_reader_attribute_count(reader: CHandle, count: *mut c_int) -> c_int;
	/// `oakcommon_xml_reader_attribute_name` (two-stage string).
	pub fn oakcommon_xml_reader_attribute_name(
		reader: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_xml_reader_attribute_value` (two-stage string).
	pub fn oakcommon_xml_reader_attribute_value(
		reader: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakcommon_xml_reader_has_error`.
	pub fn oakcommon_xml_reader_has_error(reader: CHandle, has_error: *mut c_int) -> c_int;
	/// `oakcommon_xml_writer_init` — fresh writer.
	pub fn oakcommon_xml_writer_init() -> CHandle;
	/// `oakcommon_xml_writer_free` — NULL/empty no-op.
	pub fn oakcommon_xml_writer_free(writer: *mut CHandle);
	/// `oakcommon_xml_writer_write_start_element`.
	pub fn oakcommon_xml_writer_write_start_element(writer: CHandle, name: *const c_char) -> c_int;
	/// `oakcommon_xml_writer_write_attribute`.
	pub fn oakcommon_xml_writer_write_attribute(
		writer: CHandle,
		name: *const c_char,
		value: *const c_char,
	) -> c_int;
	/// `oakcommon_xml_writer_write_characters`.
	pub fn oakcommon_xml_writer_write_characters(writer: CHandle, text: *const c_char) -> c_int;
	/// `oakcommon_xml_writer_write_text_element`.
	pub fn oakcommon_xml_writer_write_text_element(
		writer: CHandle,
		name: *const c_char,
		text: *const c_char,
	) -> c_int;
	/// `oakcommon_xml_writer_write_end_element`.
	pub fn oakcommon_xml_writer_write_end_element(writer: CHandle) -> c_int;
	/// `oakcommon_xml_writer_write_end_document`.
	pub fn oakcommon_xml_writer_write_end_document(writer: CHandle) -> c_int;
	/// `oakcommon_xml_writer_output` (two-stage string).
	pub fn oakcommon_xml_writer_output(writer: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;

	// ---- decibel helpers -------------------------------------------------
	/// `oakcommon_decibel_from_linear` — linear amplitude to decibels.
	pub fn oakcommon_decibel_from_linear(linear: f64, out_db: *mut f64) -> c_int;
	/// `oakcommon_decibel_to_linear` — decibels to linear amplitude.
	pub fn oakcommon_decibel_to_linear(db: f64, out_linear: *mut f64) -> c_int;
	/// `oakcommon_decibel_from_logarithmic` — slider position to decibels.
	pub fn oakcommon_decibel_from_logarithmic(logarithmic: f64, out_db: *mut f64) -> c_int;
	/// `oakcommon_decibel_to_logarithmic` — decibels to slider position.
	pub fn oakcommon_decibel_to_logarithmic(db: f64, out_logarithmic: *mut f64) -> c_int;
	/// `oakcommon_decibel_linear_to_logarithmic`.
	pub fn oakcommon_decibel_linear_to_logarithmic(linear: f64, out_logarithmic: *mut f64) -> c_int;
	/// `oakcommon_decibel_logarithmic_to_linear`.
	pub fn oakcommon_decibel_logarithmic_to_linear(logarithmic: f64, out_linear: *mut f64) -> c_int;
}
