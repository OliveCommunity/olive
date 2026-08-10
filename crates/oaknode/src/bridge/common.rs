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

//! oakcommon C ABI calls — now direct Rust calls into the oakcommon crate
//! (single-lib unification, see `docs/zh/plans/riir/single-lib.md`).
//! The XML surface mirrors `include/common/xmlutils.h` and backs the
//! serializer's reader/writer traits. Function names and signatures are
//! unchanged (callers in `src/` and `tests/` are untouched); the
//! `test-stubs` feature and its in-crate mocks were removed because the
//! real oakcommon rlib is now always linked (the mocks would collide with
//! its `#[no_mangle]` exports).

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

/// `oakcommon_xml_reader_init`.
pub fn xml_reader_init(data: *const c_char) -> Option<CHandle> {
	Some(unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_init(data) })
}

/// `oakcommon_xml_reader_free`.
pub fn xml_reader_free(reader: *mut CHandle) {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_free(reader) }
}

/// `oakcommon_xml_reader_read_next_start_element` (advance to the next
/// start element; `found` receives 1/0).
pub fn xml_reader_next_start_element(reader: CHandle) -> Option<bool> {
	let mut found = 0;
	unsafe {
		oakcommon::ffi::xmlutils::oakcommon_xml_reader_read_next_start_element(reader, &mut found);
	}
	Some(found != 0)
}

/// `oakcommon_xml_reader_name` (two-stage).
pub fn xml_reader_name(reader: CHandle) -> Option<String> {
	two_stage_string("oakcommon_xml_reader_name", |buf, size| unsafe {
		Some(oakcommon::ffi::xmlutils::oakcommon_xml_reader_name(reader.clone(), buf, size))
	})
}

/// `oakcommon_xml_reader_read_element_text` (two-stage).
pub fn xml_reader_read_element_text(reader: CHandle) -> Option<String> {
	two_stage_string("oakcommon_xml_reader_read_element_text", |buf, size| unsafe {
		Some(oakcommon::ffi::xmlutils::oakcommon_xml_reader_read_element_text(
			reader.clone(),
			buf,
			size,
		))
	})
}

/// `oakcommon_xml_reader_skip_current_element`.
pub fn xml_reader_skip_current_element(reader: CHandle) -> Option<c_int> {
	Some(unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_skip_current_element(reader) })
}

/// `oakcommon_xml_reader_attribute_count`.
pub fn xml_reader_attribute_count(reader: CHandle) -> Option<c_int> {
	let mut count = 0;
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_count(reader, &mut count) };
	Some(count)
}

/// `oakcommon_xml_reader_attribute_name` (two-stage).
pub fn xml_reader_attribute_name(reader: CHandle, index: c_int) -> Option<String> {
	two_stage_string("oakcommon_xml_reader_attribute_name", |buf, size| unsafe {
		Some(oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_name(
			reader.clone(),
			index,
			buf,
			size,
		))
	})
}

/// `oakcommon_xml_reader_attribute_value` (two-stage).
pub fn xml_reader_attribute_value(reader: CHandle, index: c_int) -> Option<String> {
	two_stage_string("oakcommon_xml_reader_attribute_value", |buf, size| unsafe {
		Some(oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_value(
			reader.clone(),
			index,
			buf,
			size,
		))
	})
}

/// `oakcommon_xml_reader_has_error`.
pub fn xml_reader_has_error(reader: CHandle) -> Option<bool> {
	let mut err = 0;
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_has_error(reader, &mut err) };
	Some(err != 0)
}

/// `oakcommon_xml_writer_init`.
pub fn xml_writer_init() -> Option<CHandle> {
	Some(unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_init() })
}

/// `oakcommon_xml_writer_free`.
pub fn xml_writer_free(writer: *mut CHandle) {
	unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_free(writer) }
}

/// `oakcommon_xml_writer_write_start_element`.
pub fn xml_writer_start_element(writer: CHandle, name: &str) -> Option<c_int> {
	use std::ffi::CString;
	let n = CString::new(name).ok()?;
	Some(unsafe {
		oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_start_element(writer, n.as_ptr())
	})
}

/// `oakcommon_xml_writer_write_attribute`.
pub fn xml_writer_attribute(writer: CHandle, name: &str, value: &str) -> Option<c_int> {
	use std::ffi::CString;
	let n = CString::new(name).ok()?;
	let v = CString::new(value).ok()?;
	Some(unsafe {
		oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_attribute(writer, n.as_ptr(), v.as_ptr())
	})
}

/// `oakcommon_xml_writer_write_characters`.
pub fn xml_writer_characters(writer: CHandle, text: &str) -> Option<c_int> {
	use std::ffi::CString;
	let t = CString::new(text).ok()?;
	Some(unsafe {
		oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_characters(writer, t.as_ptr())
	})
}

/// `oakcommon_xml_writer_write_text_element`.
pub fn xml_writer_text_element(writer: CHandle, name: &str, text: &str) -> Option<c_int> {
	use std::ffi::CString;
	let n = CString::new(name).ok()?;
	let t = CString::new(text).ok()?;
	Some(unsafe {
		oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_text_element(writer, n.as_ptr(), t.as_ptr())
	})
}

/// `oakcommon_xml_writer_write_end_element`.
pub fn xml_writer_end_element(writer: CHandle) -> Option<c_int> {
	Some(unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_end_element(writer) })
}

/// `oakcommon_xml_writer_write_end_document`.
pub fn xml_writer_end_document(writer: CHandle) -> Option<c_int> {
	Some(unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_end_document(writer) })
}

/// `oakcommon_xml_writer_output` (two-stage).
pub fn xml_writer_output(writer: CHandle) -> Option<String> {
	two_stage_string("oakcommon_xml_writer_output", |buf, size| unsafe {
		Some(oakcommon::ffi::xmlutils::oakcommon_xml_writer_output(writer.clone(), buf, size))
	})
}

/// `oakcommon_config_get_int` (config access for node defaults).
pub fn config_get_int(group: &str, key: &str, default: c_int) -> Option<c_int> {
	use std::ffi::CString;
	let g = CString::new(group).ok()?;
	let k = CString::new(key).ok()?;
	Some(unsafe {
		oakcommon::ffi::config::oakcommon_config_get_int(g.as_ptr(), k.as_ptr(), default)
	})
}

// ---------------------------------------------------------------------
// oakcommon videoparams C ABI (sequence/footage stream params)
// ---------------------------------------------------------------------

/// `oakcommon_videoparams_init_basic`: new owned handle (count 1).
pub fn videoparams_init_basic(
	width: c_int,
	height: c_int,
	pixel_format: c_int,
	channels: c_int,
	par_num: c_int,
	par_den: c_int,
	interlacing: c_int,
	divider: c_int,
) -> Option<CHandle> {
	Some(unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_init_basic(
			width, height, pixel_format, channels, par_num, par_den, interlacing, divider,
		)
	})
}

/// `oakcommon_videoparams_set_frame_rate`.
pub fn videoparams_set_frame_rate(params: CHandle, num: c_int, den: c_int) -> Option<c_int> {
	Some(unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_set_frame_rate(params, num, den)
	})
}

/// `oakcommon_videoparams_free` — releases the handle locally (the
/// handle's `release` fn points into the oakcommon box machinery).
pub fn videoparams_free(params: *mut CHandle) {
	if params.is_null() || unsafe { (*params).ctx.is_null() } {
		return;
	}
	let h = unsafe { (*params).clone() };
	if let Some(f) = h.release {
		unsafe { f(h.ctx) };
	}
	unsafe { (*params).ctx = std::ptr::null_mut() };
}

/// `oakcommon_videoparams_get_width` — the value, or the default on
/// error (the caller decides whether the handle is real).
pub fn videoparams_get_width(params: CHandle) -> Option<c_int> {
	let mut v = 0;
	let rc = unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_width(params.clone(), &mut v)
	};
	Some(if rc < 0 { 0 } else { v })
}

/// `oakcommon_videoparams_get_height`.
pub fn videoparams_get_height(params: CHandle) -> Option<c_int> {
	let mut v = 0;
	let rc = unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_height(params.clone(), &mut v)
	};
	Some(if rc < 0 { 0 } else { v })
}

/// `oakcommon_videoparams_get_format`.
pub fn videoparams_get_format(params: CHandle) -> Option<c_int> {
	let mut v = 0;
	let rc = unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_format(params.clone(), &mut v)
	};
	Some(if rc < 0 { 0 } else { v })
}

/// `oakcommon_videoparams_get_channel_count`.
pub fn videoparams_get_channel_count(params: CHandle) -> Option<c_int> {
	let mut v = 0;
	let rc = unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_channel_count(params.clone(), &mut v)
	};
	Some(if rc < 0 { 0 } else { v })
}

/// `oakcommon_videoparams_get_frame_rate` — (num, den).
pub fn videoparams_get_frame_rate(params: CHandle) -> Option<(c_int, c_int)> {
	let mut n = 0;
	let mut d = 0;
	let rc = unsafe {
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_frame_rate(params.clone(), &mut n, &mut d)
	};
	Some(if rc < 0 { (0, 0) } else { (n, d) })
}

// ---------------------------------------------------------------------
// oakcommon colortransform C ABI (color manager compliance)
// ---------------------------------------------------------------------

/// `oakcommon_colortransform_init_display`: new owned display transform.
pub fn colortransform_init_display(display: &str, view: &str, look: &str) -> Option<CHandle> {
	use std::ffi::CString;
	let d = CString::new(display).ok()?;
	let v = CString::new(view).ok()?;
	let l = CString::new(look).ok()?;
	Some(unsafe {
		oakcommon::ffi::colortransform::oakcommon_colortransform_init_display(
			d.as_ptr(),
			v.as_ptr(),
			l.as_ptr(),
		)
	})
}

/// `oakcommon_colortransform_init_output`: new owned output transform.
pub fn colortransform_init_output(output: &str) -> Option<CHandle> {
	use std::ffi::CString;
	let o = CString::new(output).ok()?;
	Some(unsafe { oakcommon::ffi::colortransform::oakcommon_colortransform_init_output(o.as_ptr()) })
}

/// `oakcommon_colortransform_free` — releases the handle locally.
pub fn colortransform_free(transform: *mut CHandle) {
	if transform.is_null() || unsafe { (*transform).ctx.is_null() } {
		return;
	}
	let h = unsafe { (*transform).clone() };
	if let Some(f) = h.release {
		unsafe { f(h.ctx) };
	}
	unsafe { (*transform).ctx = std::ptr::null_mut() };
}

/// `oakcommon_colortransform_is_display`.
pub fn colortransform_is_display(transform: CHandle) -> Option<bool> {
	Some(unsafe {
		oakcommon::ffi::colortransform::oakcommon_colortransform_is_display(transform) != 0
	})
}

/// `oakcommon_colortransform_get_display` (two-stage).
pub fn colortransform_get_display(transform: CHandle) -> Option<String> {
	two_stage_string("oakcommon_colortransform_get_display", |buf, size| unsafe {
		Some(oakcommon::ffi::colortransform::oakcommon_colortransform_get_display(
			transform.clone(),
			buf,
			size,
		))
	})
}

/// `oakcommon_colortransform_get_output` (two-stage).
pub fn colortransform_get_output(transform: CHandle) -> Option<String> {
	two_stage_string("oakcommon_colortransform_get_output", |buf, size| unsafe {
		Some(oakcommon::ffi::colortransform::oakcommon_colortransform_get_output(
			transform.clone(),
			buf,
			size,
		))
	})
}

/// `oakcommon_colortransform_get_view` (two-stage).
pub fn colortransform_get_view(transform: CHandle) -> Option<String> {
	two_stage_string("oakcommon_colortransform_get_view", |buf, size| unsafe {
		Some(oakcommon::ffi::colortransform::oakcommon_colortransform_get_view(
			transform.clone(),
			buf,
			size,
		))
	})
}

/// `oakcommon_colortransform_get_look` (two-stage).
pub fn colortransform_get_look(transform: CHandle) -> Option<String> {
	two_stage_string("oakcommon_colortransform_get_look", |buf, size| unsafe {
		Some(oakcommon::ffi::colortransform::oakcommon_colortransform_get_look(
			transform.clone(),
			buf,
			size,
		))
	})
}

/// Shared two-stage string fetch: query the required size, then read
/// into an owned buffer. `None` when the query returns an error.
fn two_stage_string<F: Fn(*mut c_char, c_int) -> Option<c_int>>(_sym: &str, call: F) -> Option<String> {
	let needed = call(std::ptr::null_mut(), 0)?;
	if needed <= 0 {
		return Some(String::new());
	}
	let mut buf = vec![0u8; needed as usize];
	call(buf.as_mut_ptr() as *mut c_char, needed)?;
	buf.pop(); // trailing NUL
	String::from_utf8(buf).ok()
}
