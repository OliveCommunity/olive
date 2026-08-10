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

//! oakcommon C ABI imports (XML + config + strings). dlsym-resolved
//! (see [`super`]). The XML surface mirrors `include/common/xmlutils.h`
//! and backs the serializer's reader/writer traits.

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

/// `oakcommon_xml_reader_init`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_init(data: *const c_char) -> Option<CHandle> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*const c_char) -> CHandle;
	dlsym::call::<F, CHandle>("oakcommon_xml_reader_init", |f| unsafe { f(data) })
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_init(data: *const c_char) -> Option<CHandle> {
		Some(unsafe { stub::oakcommon_xml_reader_init(data) })
	}


/// `oakcommon_xml_reader_free`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_free(reader: *mut CHandle) {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*mut CHandle);
	if let Some(f) = dlsym::call::<F, ()>("oakcommon_xml_reader_free", |f| unsafe {
		f(reader)
	}) {
		let _ = f;
	}
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_free(reader: *mut CHandle) {
		unsafe { stub::oakcommon_xml_reader_free(reader) };
	}


/// `oakcommon_xml_reader_read_next_start_element` (advance to the next
/// start element; `found` receives 1/0).
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_next_start_element(reader: CHandle) -> Option<bool> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int) -> c_int;
	let mut found = 0;
	dlsym::call::<F, c_int>("oakcommon_xml_reader_read_next_start_element", |f| unsafe {
		f(reader, &mut found)
	})?;
	Some(found != 0)
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_next_start_element(reader: CHandle) -> Option<bool> {
		let mut found = 0;
		let rc = unsafe { stub::oakcommon_xml_reader_read_next_start_element(reader, &mut found) };
		if rc != 0 {
			None
		} else {
			Some(found != 0)
		}
	}


/// `oakcommon_xml_reader_name` (two-stage).
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_name(reader: CHandle) -> Option<String> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_char, c_int) -> c_int;
	two_stage_string("oakcommon_xml_reader_name", |buf, size| unsafe {
		dlsym::call::<F, c_int>("oakcommon_xml_reader_name", |f| f(reader.clone(), buf, size))
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_name(reader: CHandle) -> Option<String> {
		two_stage_string("oakcommon_xml_reader_name", |buf, size| unsafe {
			Some(stub::oakcommon_xml_reader_name(reader.clone(), buf, size))
		})
	}


/// `oakcommon_xml_reader_read_element_text` (two-stage).
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_read_element_text(reader: CHandle) -> Option<String> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_char, c_int) -> c_int;
	two_stage_string("oakcommon_xml_reader_read_element_text", |buf, size| unsafe {
		dlsym::call::<F, c_int>("oakcommon_xml_reader_read_element_text", |f| {
			f(reader.clone(), buf, size)
		})
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_read_element_text(reader: CHandle) -> Option<String> {
		two_stage_string("oakcommon_xml_reader_read_element_text", |buf, size| unsafe {
			Some(stub::oakcommon_xml_reader_read_element_text(reader.clone(), buf, size))
		})
	}


/// `oakcommon_xml_reader_skip_current_element`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_skip_current_element(reader: CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle) -> c_int;
	dlsym::call::<F, c_int>("oakcommon_xml_reader_skip_current_element", |f| unsafe {
		f(reader)
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_skip_current_element(reader: CHandle) -> Option<c_int> {
		Some(unsafe { stub::oakcommon_xml_reader_skip_current_element(reader) })
	}


/// `oakcommon_xml_reader_attribute_count`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_attribute_count(reader: CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int) -> c_int;
	let mut count = 0;
	dlsym::call::<F, c_int>("oakcommon_xml_reader_attribute_count", |f| unsafe {
		f(reader, &mut count)
	})?;
	Some(count)
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_attribute_count(reader: CHandle) -> Option<c_int> {
		let mut count = 0;
		let rc = unsafe { stub::oakcommon_xml_reader_attribute_count(reader, &mut count) };
		if rc != 0 {
			None
		} else {
			Some(count)
		}
	}


/// `oakcommon_xml_reader_attribute_name` (two-stage).
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_attribute_name(reader: CHandle, index: c_int) -> Option<String> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, c_int, *mut c_char, c_int) -> c_int;
	two_stage_string("oakcommon_xml_reader_attribute_name", |buf, size| unsafe {
		dlsym::call::<F, c_int>("oakcommon_xml_reader_attribute_name", |f| {
			f(reader.clone(), index, buf, size)
		})
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_attribute_name(reader: CHandle, index: c_int) -> Option<String> {
		two_stage_string("oakcommon_xml_reader_attribute_name", |buf, size| unsafe {
			Some(stub::oakcommon_xml_reader_attribute_name(reader.clone(), index, buf, size))
		})
	}


/// `oakcommon_xml_reader_attribute_value` (two-stage).
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_attribute_value(reader: CHandle, index: c_int) -> Option<String> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, c_int, *mut c_char, c_int) -> c_int;
	two_stage_string("oakcommon_xml_reader_attribute_value", |buf, size| unsafe {
		dlsym::call::<F, c_int>("oakcommon_xml_reader_attribute_value", |f| {
			f(reader.clone(), index, buf, size)
		})
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_attribute_value(reader: CHandle, index: c_int) -> Option<String> {
		two_stage_string("oakcommon_xml_reader_attribute_value", |buf, size| unsafe {
			Some(stub::oakcommon_xml_reader_attribute_value(reader.clone(), index, buf, size))
		})
	}


/// `oakcommon_xml_reader_has_error`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_reader_has_error(reader: CHandle) -> Option<bool> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int) -> c_int;
	let mut err = 0;
	dlsym::call::<F, c_int>("oakcommon_xml_reader_has_error", |f| unsafe {
		f(reader, &mut err)
	})?;
	Some(err != 0)
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_reader_has_error(reader: CHandle) -> Option<bool> {
		let mut err = 0;
		let rc = unsafe { stub::oakcommon_xml_reader_has_error(reader, &mut err) };
		if rc != 0 {
			None
		} else {
			Some(err != 0)
		}
	}


/// `oakcommon_xml_writer_init`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_init() -> Option<CHandle> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn() -> CHandle;
	dlsym::call::<F, CHandle>("oakcommon_xml_writer_init", |f| unsafe { f() })
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_init() -> Option<CHandle> {
		Some(unsafe { stub::oakcommon_xml_writer_init() })
	}


/// `oakcommon_xml_writer_free`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_free(writer: *mut CHandle) {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(*mut CHandle);
	if let Some(f) = dlsym::call::<F, ()>("oakcommon_xml_writer_free", |f| unsafe {
		f(writer)
	}) {
		let _ = f;
	}
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_free(writer: *mut CHandle) {
		unsafe { stub::oakcommon_xml_writer_free(writer) };
	}


/// `oakcommon_xml_writer_write_start_element`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_start_element(writer: CHandle, name: &str) -> Option<c_int> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(CHandle, *const c_char) -> c_int;
	let n = CString::new(name).ok()?;
	dlsym::call::<F, c_int>("oakcommon_xml_writer_write_start_element", |f| unsafe {
		f(writer, n.as_ptr())
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_start_element(writer: CHandle, name: &str) -> Option<c_int> {
		let c = std::ffi::CString::new(name).ok()?;
		Some(unsafe { stub::oakcommon_xml_writer_write_start_element(writer, c.as_ptr()) })
	}


/// `oakcommon_xml_writer_write_attribute`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_attribute(writer: CHandle, name: &str, value: &str) -> Option<c_int> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(CHandle, *const c_char, *const c_char) -> c_int;
	let n = CString::new(name).ok()?;
	let v = CString::new(value).ok()?;
	dlsym::call::<F, c_int>("oakcommon_xml_writer_write_attribute", |f| unsafe {
		f(writer, n.as_ptr(), v.as_ptr())
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_attribute(writer: CHandle, name: &str, value: &str) -> Option<c_int> {
		let n = std::ffi::CString::new(name).ok()?;
		let v = std::ffi::CString::new(value).ok()?;
		Some(unsafe { stub::oakcommon_xml_writer_write_attribute(writer, n.as_ptr(), v.as_ptr()) })
	}


/// `oakcommon_xml_writer_write_characters`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_characters(writer: CHandle, text: &str) -> Option<c_int> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(CHandle, *const c_char) -> c_int;
	let t = CString::new(text).ok()?;
	dlsym::call::<F, c_int>("oakcommon_xml_writer_write_characters", |f| unsafe {
		f(writer, t.as_ptr())
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_characters(writer: CHandle, text: &str) -> Option<c_int> {
		let c = std::ffi::CString::new(text).ok()?;
		Some(unsafe { stub::oakcommon_xml_writer_write_characters(writer, c.as_ptr()) })
	}


/// `oakcommon_xml_writer_write_text_element`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_text_element(writer: CHandle, name: &str, text: &str) -> Option<c_int> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(CHandle, *const c_char, *const c_char) -> c_int;
	let n = CString::new(name).ok()?;
	let t = CString::new(text).ok()?;
	dlsym::call::<F, c_int>("oakcommon_xml_writer_write_text_element", |f| unsafe {
		f(writer, n.as_ptr(), t.as_ptr())
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_text_element(writer: CHandle, name: &str, text: &str) -> Option<c_int> {
		let n = std::ffi::CString::new(name).ok()?;
		let t = std::ffi::CString::new(text).ok()?;
		Some(unsafe { stub::oakcommon_xml_writer_write_text_element(writer, n.as_ptr(), t.as_ptr()) })
	}


/// `oakcommon_xml_writer_write_end_element`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_end_element(writer: CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle) -> c_int;
	dlsym::call::<F, c_int>("oakcommon_xml_writer_write_end_element", |f| unsafe {
		f(writer)
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_end_element(writer: CHandle) -> Option<c_int> {
		Some(unsafe { stub::oakcommon_xml_writer_write_end_element(writer) })
	}


/// `oakcommon_xml_writer_write_end_document`.
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_end_document(writer: CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle) -> c_int;
	dlsym::call::<F, c_int>("oakcommon_xml_writer_write_end_document", |f| unsafe {
		f(writer)
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_end_document(writer: CHandle) -> Option<c_int> {
		Some(unsafe { stub::oakcommon_xml_writer_write_end_document(writer) })
	}


/// `oakcommon_xml_writer_output` (two-stage).
#[cfg(not(feature = "test-stubs"))]
pub fn xml_writer_output(writer: CHandle) -> Option<String> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_char, c_int) -> c_int;
	two_stage_string("oakcommon_xml_writer_output", |buf, size| unsafe {
		dlsym::call::<F, c_int>("oakcommon_xml_writer_output", |f| f(writer.clone(), buf, size))
	})
}

	/// Test-stub path (calls the in-crate stub directly).
	#[cfg(feature = "test-stubs")]
pub fn xml_writer_output(writer: CHandle) -> Option<String> {
		two_stage_string("oakcommon_xml_writer_output", |buf, size| unsafe {
			Some(stub::oakcommon_xml_writer_output(writer.clone(), buf, size))
		})
	}


/// `oakcommon_config_get_int` (config access for node defaults).
pub fn config_get_int(group: &str, key: &str, default: c_int) -> Option<c_int> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(*const c_char, *const c_char, c_int) -> c_int;
	let g = CString::new(group).ok()?;
	let k = CString::new(key).ok()?;
	dlsym::call::<F, c_int>("oakcommon_config_get_int", |f| unsafe {
		f(g.as_ptr(), k.as_ptr(), default)
	})
}

// ---------------------------------------------------------------------
// oakcommon videoparams C ABI (sequence/footage stream params)
// ---------------------------------------------------------------------

/// `oakcommon_videoparams_init_basic`: new owned handle (count 1).
#[cfg(not(feature = "test-stubs"))]
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
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(
		c_int,
		c_int,
		c_int,
		c_int,
		c_int,
		c_int,
		c_int,
		c_int,
	) -> CHandle;
	dlsym::call::<F, CHandle>("oakcommon_videoparams_init_basic", |f| unsafe {
		f(width, height, pixel_format, channels, par_num, par_den, interlacing, divider)
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
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
		stub::oakcommon_videoparams_init_basic(
			width,
			height,
			pixel_format,
			channels,
			par_num,
			par_den,
			interlacing,
			divider,
		)
	})
}

/// `oakcommon_videoparams_set_frame_rate`.
#[cfg(not(feature = "test-stubs"))]
pub fn videoparams_set_frame_rate(params: CHandle, num: c_int, den: c_int) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, c_int, c_int) -> c_int;
	dlsym::call::<F, c_int>("oakcommon_videoparams_set_frame_rate", |f| unsafe {
		f(params, num, den)
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn videoparams_set_frame_rate(params: CHandle, num: c_int, den: c_int) -> Option<c_int> {
	Some(unsafe { stub::oakcommon_videoparams_set_frame_rate(params, num, den) })
}

/// `oakcommon_videoparams_free`.
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
/// error/missing symbol (the caller decides whether the handle is real).
#[cfg(not(feature = "test-stubs"))]
pub fn videoparams_get_width(params: CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int) -> c_int;
	dlsym::call::<F, c_int>("oakcommon_videoparams_get_width", |f| {
		let mut v = 0;
		let rc = unsafe { f(params.clone(), &mut v) };
		if rc < 0 { 0 } else { v }
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn videoparams_get_width(params: CHandle) -> Option<c_int> {
	let mut v = 0;
	let rc = unsafe { stub::oakcommon_videoparams_get_width(params, &mut v) };
	Some(if rc < 0 { 0 } else { v })
}

/// `oakcommon_videoparams_get_height`.
#[cfg(not(feature = "test-stubs"))]
pub fn videoparams_get_height(params: CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int) -> c_int;
	dlsym::call::<F, c_int>("oakcommon_videoparams_get_height", |f| {
		let mut v = 0;
		let rc = unsafe { f(params.clone(), &mut v) };
		if rc < 0 { 0 } else { v }
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn videoparams_get_height(params: CHandle) -> Option<c_int> {
	let mut v = 0;
	let rc = unsafe { stub::oakcommon_videoparams_get_height(params, &mut v) };
	Some(if rc < 0 { 0 } else { v })
}

/// `oakcommon_videoparams_get_format`.
#[cfg(not(feature = "test-stubs"))]
pub fn videoparams_get_format(params: CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int) -> c_int;
	dlsym::call::<F, c_int>("oakcommon_videoparams_get_format", |f| {
		let mut v = 0;
		let rc = unsafe { f(params.clone(), &mut v) };
		if rc < 0 { 0 } else { v }
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn videoparams_get_format(params: CHandle) -> Option<c_int> {
	let mut v = 0;
	let rc = unsafe { stub::oakcommon_videoparams_get_format(params, &mut v) };
	Some(if rc < 0 { 0 } else { v })
}

/// `oakcommon_videoparams_get_channel_count`.
#[cfg(not(feature = "test-stubs"))]
pub fn videoparams_get_channel_count(params: CHandle) -> Option<c_int> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int) -> c_int;
	dlsym::call::<F, c_int>("oakcommon_videoparams_get_channel_count", |f| {
		let mut v = 0;
		let rc = unsafe { f(params.clone(), &mut v) };
		if rc < 0 { 0 } else { v }
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn videoparams_get_channel_count(params: CHandle) -> Option<c_int> {
	let mut v = 0;
	let rc = unsafe { stub::oakcommon_videoparams_get_channel_count(params, &mut v) };
	Some(if rc < 0 { 0 } else { v })
}

/// `oakcommon_videoparams_get_frame_rate` — (num, den).
#[cfg(not(feature = "test-stubs"))]
pub fn videoparams_get_frame_rate(params: CHandle) -> Option<(c_int, c_int)> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int, *mut c_int) -> c_int;
	dlsym::call::<F, (c_int, c_int)>("oakcommon_videoparams_get_frame_rate", |f| {
		let mut n = 0;
		let mut d = 0;
		let rc = unsafe { f(params.clone(), &mut n, &mut d) };
		if rc < 0 { (0, 0) } else { (n, d) }
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn videoparams_get_frame_rate(params: CHandle) -> Option<(c_int, c_int)> {
	let mut n = 0;
	let mut d = 0;
	let rc = unsafe { stub::oakcommon_videoparams_get_frame_rate(params, &mut n, &mut d) };
	Some(if rc < 0 { (0, 0) } else { (n, d) })
}

// ---------------------------------------------------------------------
// oakcommon colortransform C ABI (color manager compliance)
// ---------------------------------------------------------------------

/// `oakcommon_colortransform_init_display`: new owned display transform.
#[cfg(not(feature = "test-stubs"))]
pub fn colortransform_init_display(display: &str, view: &str, look: &str) -> Option<CHandle> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(*const c_char, *const c_char, *const c_char) -> CHandle;
	let d = CString::new(display).ok()?;
	let v = CString::new(view).ok()?;
	let l = CString::new(look).ok()?;
	dlsym::call::<F, CHandle>("oakcommon_colortransform_init_display", |f| unsafe {
		f(d.as_ptr(), v.as_ptr(), l.as_ptr())
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn colortransform_init_display(display: &str, view: &str, look: &str) -> Option<CHandle> {
	use std::ffi::CString;
	let d = CString::new(display).ok()?;
	let v = CString::new(view).ok()?;
	let l = CString::new(look).ok()?;
	Some(unsafe { stub::oakcommon_colortransform_init_display(d.as_ptr(), v.as_ptr(), l.as_ptr()) })
}

/// `oakcommon_colortransform_init_output`: new owned output transform.
#[cfg(not(feature = "test-stubs"))]
pub fn colortransform_init_output(output: &str) -> Option<CHandle> {
	use crate::bridge::dlsym;
	use std::ffi::CString;
	type F = unsafe extern "C" fn(*const c_char) -> CHandle;
	let o = CString::new(output).ok()?;
	dlsym::call::<F, CHandle>("oakcommon_colortransform_init_output", |f| unsafe {
		f(o.as_ptr())
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn colortransform_init_output(output: &str) -> Option<CHandle> {
	use std::ffi::CString;
	let o = CString::new(output).ok()?;
	Some(unsafe { stub::oakcommon_colortransform_init_output(o.as_ptr()) })
}

/// `oakcommon_colortransform_free`.
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
#[cfg(not(feature = "test-stubs"))]
pub fn colortransform_is_display(transform: CHandle) -> Option<bool> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_int) -> c_int;
	dlsym::call::<F, bool>("oakcommon_colortransform_is_display", |f| {
		let mut v = 0;
		let rc = unsafe { f(transform.clone(), &mut v) };
		rc >= 0 && v != 0
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
pub fn colortransform_is_display(transform: CHandle) -> Option<bool> {
	let mut v = 0;
	let rc = unsafe { stub::oakcommon_colortransform_is_display(transform, &mut v) };
	Some(rc >= 0 && v != 0)
}

/// Two-stage string fetch of a colortransform field.
#[cfg(not(feature = "test-stubs"))]
fn colortransform_get(sym: &str, transform: CHandle) -> Option<String> {
	use crate::bridge::dlsym;
	type F = unsafe extern "C" fn(CHandle, *mut c_char, c_int) -> c_int;
	two_stage_string(sym, |buf, size| unsafe {
		dlsym::call::<F, c_int>(sym, |f| f(transform.clone(), buf, size))
	})
}

/// Test-stub path.
#[cfg(feature = "test-stubs")]
fn colortransform_get(sym: &str, transform: CHandle) -> Option<String> {
	two_stage_string(sym, |buf, size| unsafe {
		match sym {
			"oakcommon_colortransform_get_display" => {
				Some(stub::oakcommon_colortransform_get_display(transform.clone(), buf, size))
			}
			"oakcommon_colortransform_get_output" => {
				Some(stub::oakcommon_colortransform_get_output(transform.clone(), buf, size))
			}
			"oakcommon_colortransform_get_view" => {
				Some(stub::oakcommon_colortransform_get_view(transform.clone(), buf, size))
			}
			"oakcommon_colortransform_get_look" => {
				Some(stub::oakcommon_colortransform_get_look(transform.clone(), buf, size))
			}
			_ => None,
		}
	})
}

/// `oakcommon_colortransform_get_display`.
pub fn colortransform_get_display(transform: CHandle) -> Option<String> {
	colortransform_get("oakcommon_colortransform_get_display", transform)
}

/// `oakcommon_colortransform_get_output`.
pub fn colortransform_get_output(transform: CHandle) -> Option<String> {
	colortransform_get("oakcommon_colortransform_get_output", transform)
}

/// `oakcommon_colortransform_get_view`.
pub fn colortransform_get_view(transform: CHandle) -> Option<String> {
	colortransform_get("oakcommon_colortransform_get_view", transform)
}

/// `oakcommon_colortransform_get_look`.
pub fn colortransform_get_look(transform: CHandle) -> Option<String> {
	colortransform_get("oakcommon_colortransform_get_look", transform)
}

/// Shared two-stage string fetch: query the required size, then read
/// into an owned buffer. `None` when the symbol is absent or the query
/// returns an error.
fn two_stage_string<F: Fn(*mut c_char, c_int) -> Option<c_int>>(sym: &str, call: F) -> Option<String> {
	let _ = sym;
	let needed = call(std::ptr::null_mut(), 0)?;
	if needed <= 0 {
		return Some(String::new());
	}
	let mut buf = vec![0u8; needed as usize];
	call(buf.as_mut_ptr() as *mut c_char, needed)?;
	buf.pop(); // trailing NUL
	String::from_utf8(buf).ok()
}

/// In-crate implementations of the oakcommon XML C ABI for `cargo test`
/// (`--features test-stubs`). A minimal SAX-style tokenizer/emitter that
/// mirrors the `XmlStreamReader`/`XmlStreamWriter` surface the
/// serializer needs. Real builds (feature off) dlsym oakcommon.
#[cfg(feature = "test-stubs")]
pub(crate) mod stub {
	use super::*;

	/// One parsed XML token.
	#[derive(Clone, Debug, PartialEq)]
	pub(crate) enum Token {
		/// `<name a="v" ...>` with attributes in document order.
		Start {
			name: String,
			attrs: Vec<(String, String)>,
		},
		/// `</name>`.
		End { name: String },
		/// Character data (may be empty).
		Text(String),
	}

	/// Reader state behind an `OakXmlReader` handle.
	pub(crate) struct ReaderState {
		/// Parsed token stream.
		tokens: Vec<Token>,
		/// Cursor into `tokens` (index of the last consumed token).
		cursor: usize,
		/// Error flag.
		has_error: bool,
		/// Cached text of the last consumed element (two-stage getters
		/// call the reader twice; the consume happens once).
		last_text: Option<String>,
	}

	/// Writer state behind an `OakXmlWriter` handle.
	pub(crate) struct WriterState {
		/// Output buffer.
		out: Vec<u8>,
		/// Open-element stack (names).
		stack: Vec<String>,
		/// True when an unclosed start tag is pending (`<name attrs...`
		/// without `>` yet).
		pending_open: bool,
	}

	/// Tokenize `data` into [`Token`]s. Handles `<tag>`, `</tag>`,
	/// attributes, character data, `<?...?>`/`<!--...-->`/`<![CDATA[...]]>`
	/// skipped. Malformed input sets the error flag and stops.
	fn tokenize(data: &str) -> (Vec<Token>, bool) {
		let mut tokens = Vec::new();
		let mut rest = data;
		let mut error = false;
		loop {
			// Character data up to the next '<'.
			match rest.find('<') {
				None => {
					if !rest.is_empty() {
						tokens.push(Token::Text(rest.to_string()));
					}
					break;
				}
				Some(0) => {}
				Some(i) => {
					tokens.push(Token::Text(rest[..i].to_string()));
					rest = &rest[i..];
					continue;
				}
			}
			// rest starts with '<'.
			if let Some(after) = rest.strip_prefix("<!--") {
				match after.find("-->") {
					Some(end) => rest = &after[end + 3..],
					None => {
						error = true;
						break;
					}
				}
				continue;
			}
			if let Some(after) = rest.strip_prefix("<?") {
				match after.find("?>") {
					Some(end) => rest = &after[end + 2..],
					None => {
						error = true;
						break;
					}
				}
				continue;
			}
			if let Some(after) = rest.strip_prefix("<![CDATA[") {
				match after.find("]]>") {
					Some(end) => {
						tokens.push(Token::Text(after[..end].to_string()));
						rest = &after[end + 3..];
					}
					None => {
						error = true;
						break;
					}
				}
				continue;
			}
			// A real element: `<name ...>` or `</name>`.
			let close = rest.find('>');
			let close = match close {
				Some(c) => c,
				None => {
					error = true;
					break;
				}
			};
			let inner = &rest[1..close];
			rest = &rest[close + 1..];
			if let Some(name) = inner.strip_prefix('/') {
				tokens.push(Token::End {
					name: name.trim().to_string(),
				});
				continue;
			}
			// Split name from attributes (first whitespace).
			let (name, attr_text) = match inner.find(char::is_whitespace) {
				Some(i) => (&inner[..i], inner[i..].trim()),
				None => (inner, ""),
			};
			let mut attrs = Vec::new();
			let mut attr_rest = attr_text;
			while !attr_rest.is_empty() {
				// Expect `key="value"` (also single quotes).
				let eq = match attr_rest.find('=') {
					Some(e) => e,
					None => {
						error = true;
						break;
					}
				};
				let key = attr_rest[..eq].trim();
				let after_eq = attr_rest[eq + 1..].trim_start();
				let quote = match after_eq.chars().next() {
					Some('"') => '"',
					Some('\'') => '\'',
					_ => {
						error = true;
						break;
					}
				};
				let val_end = match after_eq[1..].find(quote) {
					Some(e) => e + 1,
					None => {
						error = true;
						break;
					}
				};
				let value = &after_eq[1..val_end];
				attrs.push((key.to_string(), value.to_string()));
				attr_rest = after_eq[val_end + 1..].trim_start();
			}
			tokens.push(Token::Start {
				name: name.to_string(),
				attrs,
			});
		}
		(tokens, error)
	}

	/// `oakcommon_xml_reader_init`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_init(data: *const c_char) -> CHandle {
		let text = unsafe { cstr(data) }.unwrap_or("");
		let (tokens, error) = tokenize(text);
		crate::handle::make_owned(ReaderState {
			tokens,
			cursor: usize::MAX,
			has_error: error,
			last_text: None,
		})
	}

	/// `oakcommon_xml_reader_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_free(reader: *mut CHandle) {
		if reader.is_null() || unsafe { (*reader).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*reader).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*reader).ctx = std::ptr::null_mut() };
	}

	/// `oakcommon_xml_reader_read_next_start_element`: advance to the
	/// next start element at any depth.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_read_next_start_element(
		reader: CHandle,
		found: *mut c_int,
	) -> c_int {
		if reader.ctx.is_null() || found.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = reader.ctx as *mut crate::handle::RefBox<ReaderState>;
		let state = unsafe { &mut (*boxed).value };
		// C++ `xml_read_next_start_element` semantics
		// (`// CPP-PARITY: src/common/src/xmlutils.cpp:279`): advance
		// past the current token; return true on the next start element
		// and FALSE at the first end element (the enclosing scope's
		// close), which bounds the serializer loops. The cursor starts
		// at `usize::MAX` ("before the first token").
		if state.cursor == usize::MAX {
			state.cursor = 0;
		} else if state.cursor < state.tokens.len() {
			state.cursor += 1;
		}
		while state.cursor < state.tokens.len() {
			match &state.tokens[state.cursor] {
				Token::Start { .. } => {
					unsafe { *found = 1 };
					return crate::error::OAKNODE_OK;
				}
				Token::End { .. } => {
					unsafe { *found = 0 };
					return crate::error::OAKNODE_OK;
				}
				Token::Text(_) => state.cursor += 1,
			}
		}
		unsafe { *found = 0 };
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_reader_name` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_name(
		reader: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let name = reader_name_of(reader);
		match name {
			Some(n) => copy(&n, buf, buf_size),
			None => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oakcommon_xml_reader_read_element_text` (two-stage): the text of
	/// the current start element (consumed up to its matching end).
	///
	/// The two-stage convention calls this twice; the first call (with a
	/// null buffer) consumes the element and caches the text, the second
	/// serves the cached value.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_read_element_text(
		reader: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if reader.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = reader.ctx as *mut crate::handle::RefBox<ReaderState>;
		let state = unsafe { &mut (*boxed).value };

		// Second (fill) call: serve the cached text without re-consuming.
		if let Some(cached) = &state.last_text {
			let out = cached.clone();
			state.last_text = None;
			return copy(&out, buf, buf_size);
		}

		let start = state.cursor;
		let name = match &state.tokens.get(start) {
			Some(Token::Start { name, .. }) => name.clone(),
			_ => return crate::error::OAKNODE_E_INVALID,
		};
		// Consume tokens until the matching end at depth 0.
		let mut depth = 0usize;
		let mut text = String::new();
		let mut i = start + 1;
		while i < state.tokens.len() {
			match &state.tokens[i] {
				Token::Start { .. } => depth += 1,
				Token::End { name: n } => {
					if depth == 0 {
						if n == &name {
							state.cursor = i;
							state.last_text = Some(text.clone());
							return copy(&text, buf, buf_size);
						}
						return crate::error::OAKNODE_E_INVALID;
					}
					depth -= 1;
				}
				Token::Text(t) => {
					if depth == 0 {
						text.push_str(t);
					}
				}
			}
			i += 1;
		}
		crate::error::OAKNODE_E_INVALID
	}

	/// `oakcommon_xml_reader_skip_current_element`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_skip_current_element(reader: CHandle) -> c_int {
		if reader.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = reader.ctx as *mut crate::handle::RefBox<ReaderState>;
		let state = unsafe { &mut (*boxed).value };
		let start = state.cursor;
		let name = match &state.tokens.get(start) {
			Some(Token::Start { name, .. }) => name.clone(),
			_ => return crate::error::OAKNODE_E_INVALID,
		};
		let mut depth = 0usize;
		let mut i = start;
		while i < state.tokens.len() {
			match &state.tokens[i] {
				Token::Start { .. } => depth += 1,
				Token::End { name: n } => {
					if depth == 0 {
						return crate::error::OAKNODE_E_INVALID;
					}
					depth -= 1;
					if depth == 0 && n == &name {
						state.cursor = i;
						return crate::error::OAKNODE_OK;
					}
				}
				_ => {}
			}
			i += 1;
		}
		crate::error::OAKNODE_E_INVALID
	}

	/// `oakcommon_xml_reader_attribute_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_attribute_count(
		reader: CHandle,
		count: *mut c_int,
	) -> c_int {
		if reader.ctx.is_null() || count.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let attrs = reader_attrs_of(reader);
		unsafe { *count = attrs.len() as c_int };
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_reader_attribute_name` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_attribute_name(
		reader: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let attrs = reader_attrs_of(reader);
		match attrs.get(index as usize) {
			Some((k, _)) => copy(k, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oakcommon_xml_reader_attribute_value` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_attribute_value(
		reader: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let attrs = reader_attrs_of(reader);
		match attrs.get(index as usize) {
			Some((_, v)) => copy(v, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oakcommon_xml_reader_has_error`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_reader_has_error(
		reader: CHandle,
		has_error: *mut c_int,
	) -> c_int {
		if reader.ctx.is_null() || has_error.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = reader.ctx as *mut crate::handle::RefBox<ReaderState>;
		let state = unsafe { &(*boxed).value };
		unsafe { *has_error = state.has_error as c_int };
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_writer_init`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_init() -> CHandle {
		crate::handle::make_owned(WriterState {
			out: Vec::new(),
			stack: Vec::new(),
			pending_open: false,
		})
	}

	/// `oakcommon_xml_writer_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_free(writer: *mut CHandle) {
		if writer.is_null() || unsafe { (*writer).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*writer).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*writer).ctx = std::ptr::null_mut() };
	}

	/// `oakcommon_xml_writer_write_start_element`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_write_start_element(
		writer: CHandle,
		name: *const c_char,
	) -> c_int {
		let n = match unsafe { cstr(name) } {
			Some(n) => n,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		writer_flush_open(writer.clone());
		writer_push(writer.clone(), format!("<{}", n));
		writer_set_pending(writer.clone(), true);
		writer_open(writer, n.to_string());
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_writer_write_attribute`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_write_attribute(
		writer: CHandle,
		name: *const c_char,
		value: *const c_char,
	) -> c_int {
		let n = match unsafe { cstr(name) } {
			Some(n) => n,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let v = match unsafe { cstr(value) } {
			Some(v) => v,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		writer_push(writer, format!(" {}=\"{}\"", n, xml_escape(v)));
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_writer_write_characters`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_write_characters(
		writer: CHandle,
		text: *const c_char,
	) -> c_int {
		let t = match unsafe { cstr(text) } {
			Some(t) => t,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		writer_flush_open(writer.clone());
		writer_push(writer, xml_escape(t));
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_writer_write_text_element`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_write_text_element(
		writer: CHandle,
		name: *const c_char,
		text: *const c_char,
	) -> c_int {
		let n = match unsafe { cstr(name) } {
			Some(n) => n,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let t = match unsafe { cstr(text) } {
			Some(t) => t,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		writer_flush_open(writer.clone());
		writer_push(writer, format!("<{0}>{1}</{0}>", n, xml_escape(t)));
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_writer_write_end_element`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_write_end_element(writer: CHandle) -> c_int {
		let name = match writer_pop_open(writer.clone()) {
			Some(n) => n,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		writer_flush_open(writer.clone());
		writer_push(writer, format!("</{}>", name));
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_writer_write_end_document`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_write_end_document(writer: CHandle) -> c_int {
		// Close every open element (defensive; the serializer balances).
		let _ = writer;
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_xml_writer_output` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_xml_writer_output(
		writer: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let out = writer_output(writer);
		copy(&out, buf, buf_size)
	}

	// ---- helpers ----

	/// The name of the current start element.
	fn reader_name_of(reader: CHandle) -> Option<String> {
		if reader.ctx.is_null() {
			return None;
		}
		let boxed = reader.ctx as *const crate::handle::RefBox<ReaderState>;
		let state = unsafe { &(*boxed).value };
		match state.tokens.get(state.cursor) {
			Some(Token::Start { name, .. }) => Some(name.clone()),
			_ => None,
		}
	}

	/// Attributes of the current start element.
	fn reader_attrs_of(reader: CHandle) -> Vec<(String, String)> {
		if reader.ctx.is_null() {
			return Vec::new();
		}
		let boxed = reader.ctx as *const crate::handle::RefBox<ReaderState>;
		let state = unsafe { &(*boxed).value };
		match state.tokens.get(state.cursor) {
			Some(Token::Start { attrs, .. }) => attrs.clone(),
			_ => Vec::new(),
		}
	}

	/// Flush a pending open tag's `>`.
	fn writer_flush_open(writer: CHandle) {
		if writer.ctx.is_null() {
			return;
		}
		let boxed = writer.ctx as *mut crate::handle::RefBox<WriterState>;
		let state = unsafe { &mut (*boxed).value };
		if state.pending_open {
			state.out.push(b'>');
			state.pending_open = false;
		}
	}

	/// Mark the pending-open flag.
	fn writer_set_pending(writer: CHandle, pending: bool) {
		if writer.ctx.is_null() {
			return;
		}
		let boxed = writer.ctx as *mut crate::handle::RefBox<WriterState>;
		let state = unsafe { &mut (*boxed).value };
		state.pending_open = pending;
	}

	/// Push raw text onto the writer output.
	fn writer_push(writer: CHandle, s: String) {
		if writer.ctx.is_null() {
			return;
		}
		let boxed = writer.ctx as *mut crate::handle::RefBox<WriterState>;
		let state = unsafe { &mut (*boxed).value };
		state.out.extend_from_slice(s.as_bytes());
	}

	/// Push an open element name (the start tag is already buffered).
	fn writer_open(writer: CHandle, name: String) {
		if writer.ctx.is_null() {
			return;
		}
		let boxed = writer.ctx as *mut crate::handle::RefBox<WriterState>;
		let state = unsafe { &mut (*boxed).value };
		state.stack.push(name);
	}

	/// Pop the most recent open element name.
	fn writer_pop_open(writer: CHandle) -> Option<String> {
		if writer.ctx.is_null() {
			return None;
		}
		let boxed = writer.ctx as *mut crate::handle::RefBox<WriterState>;
		let state = unsafe { &mut (*boxed).value };
		state.stack.pop()
	}

	/// The full output text.
	fn writer_output(writer: CHandle) -> String {
		if writer.ctx.is_null() {
			return String::new();
		}
		let boxed = writer.ctx as *const crate::handle::RefBox<WriterState>;
		let state = unsafe { &(*boxed).value };
		String::from_utf8_lossy(&state.out).into_owned()
	}

	/// Escape XML special characters.
	fn xml_escape(s: &str) -> String {
		s.replace('&', "&amp;")
			.replace('<', "&lt;")
			.replace('>', "&gt;")
			.replace('"', "&quot;")
	}

	/// Safe read of a NUL-terminated C string.
	///
	/// # Safety
	/// `p` must be a valid NUL-terminated string for the call's duration.
	unsafe fn cstr<'a>(p: *const c_char) -> Option<&'a str> {
		if p.is_null() {
			return None;
		}
		unsafe { std::ffi::CStr::from_ptr(p) }.to_str().ok()
	}

	/// Two-stage copy into a caller buffer (same convention as the
	/// crate's `copy_string_out`).
	fn copy(value: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
		let required = value.len() + 1;
		if !buf.is_null() && buf_size > 0 {
			let copy_len = value.len().min(buf_size as usize - 1);
			unsafe {
				std::ptr::copy_nonoverlapping(value.as_ptr() as *const c_char, buf, copy_len);
				*buf.add(copy_len) = 0;
			}
		}
		required as c_int
	}
	// -- videoparams stubs (sequence/footage stream params) -------------

	/// `oakcommon_videoparams_init_basic`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_videoparams_init_basic(
		width: c_int,
		height: c_int,
		pixel_format: c_int,
		channels: c_int,
		par_num: c_int,
		par_den: c_int,
		interlacing: c_int,
		divider: c_int,
	) -> CHandle {
		crate::handle::make_owned(StubVideoParams {
			width,
			height,
			pixel_format,
			channels,
			par_num,
			par_den,
			interlacing,
			divider,
			fps_num: 0,
			fps_den: 0,
		})
	}

	/// `oakcommon_videoparams_set_frame_rate`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_videoparams_set_frame_rate(
		params: CHandle,
		num: c_int,
		den: c_int,
	) -> c_int {
		if params.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = params.ctx as *mut crate::handle::RefBox<StubVideoParams>;
		unsafe {
			(*boxed).value.fps_num = num;
			(*boxed).value.fps_den = den;
		}
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_videoparams_get_width`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_videoparams_get_width(
		params: CHandle,
		out: *mut c_int,
	) -> c_int {
		if params.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = params.ctx as *mut crate::handle::RefBox<StubVideoParams>;
		let v = unsafe { (*boxed).value.width };
		if !out.is_null() {
			unsafe { *out = v };
		}
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_videoparams_get_height`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_videoparams_get_height(
		params: CHandle,
		out: *mut c_int,
	) -> c_int {
		if params.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = params.ctx as *mut crate::handle::RefBox<StubVideoParams>;
		let v = unsafe { (*boxed).value.height };
		if !out.is_null() {
			unsafe { *out = v };
		}
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_videoparams_get_format`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_videoparams_get_format(
		params: CHandle,
		out: *mut c_int,
	) -> c_int {
		if params.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = params.ctx as *mut crate::handle::RefBox<StubVideoParams>;
		let v = unsafe { (*boxed).value.pixel_format };
		if !out.is_null() {
			unsafe { *out = v };
		}
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_videoparams_get_channel_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_videoparams_get_channel_count(
		params: CHandle,
		out: *mut c_int,
	) -> c_int {
		if params.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = params.ctx as *mut crate::handle::RefBox<StubVideoParams>;
		let v = unsafe { (*boxed).value.channels };
		if !out.is_null() {
			unsafe { *out = v };
		}
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_videoparams_get_frame_rate`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_videoparams_get_frame_rate(
		params: CHandle,
		out_num: *mut c_int,
		out_den: *mut c_int,
	) -> c_int {
		if params.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = params.ctx as *mut crate::handle::RefBox<StubVideoParams>;
		if !out_num.is_null() {
			unsafe { *out_num = (*boxed).value.fps_num };
		}
		if !out_den.is_null() {
			unsafe { *out_den = (*boxed).value.fps_den };
		}
		crate::error::OAKNODE_OK
	}

	/// Boxed videoparams stub payload.
	pub(crate) struct StubVideoParams {
		pub width: c_int,
		pub height: c_int,
		pub pixel_format: c_int,
		pub channels: c_int,
		pub par_num: c_int,
		pub par_den: c_int,
		pub interlacing: c_int,
		pub divider: c_int,
		pub fps_num: c_int,
		pub fps_den: c_int,
	}

	// -- colortransform stubs (color manager compliance) -----------------

	/// `oakcommon_colortransform_init_display`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_colortransform_init_display(
		display: *const c_char,
		view: *const c_char,
		look: *const c_char,
	) -> CHandle {
		let d = match unsafe { cstr(display) } {
			Some(s) => s.to_string(),
			None => return CHandle::null(),
		};
		let v = match unsafe { cstr(view) } {
			Some(s) => s.to_string(),
			None => return CHandle::null(),
		};
		let l = match unsafe { cstr(look) } {
			Some(s) => s.to_string(),
			None => return CHandle::null(),
		};
		crate::handle::make_owned(StubColorTransform {
			is_display: true,
			display: d,
			output: String::new(),
			view: v,
			look: l,
		})
	}

	/// `oakcommon_colortransform_init_output`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_colortransform_init_output(
		output: *const c_char,
	) -> CHandle {
		let o = match unsafe { cstr(output) } {
			Some(s) => s.to_string(),
			None => return CHandle::null(),
		};
		crate::handle::make_owned(StubColorTransform {
			is_display: false,
			display: String::new(),
			output: o,
			view: String::new(),
			look: String::new(),
		})
	}

	/// `oakcommon_colortransform_is_display`.
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_colortransform_is_display(
		transform: CHandle,
		out: *mut c_int,
	) -> c_int {
		if transform.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = transform.ctx as *mut crate::handle::RefBox<StubColorTransform>;
		if !out.is_null() {
			unsafe { *out = (*boxed).value.is_display as c_int };
		}
		crate::error::OAKNODE_OK
	}

	/// `oakcommon_colortransform_get_display` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_colortransform_get_display(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if transform.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = transform.ctx as *mut crate::handle::RefBox<StubColorTransform>;
		copy(&unsafe { (*boxed).value.display.clone() }, buf, buf_size)
	}

	/// `oakcommon_colortransform_get_output` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_colortransform_get_output(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if transform.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = transform.ctx as *mut crate::handle::RefBox<StubColorTransform>;
		copy(&unsafe { (*boxed).value.output.clone() }, buf, buf_size)
	}

	/// `oakcommon_colortransform_get_view` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_colortransform_get_view(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if transform.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = transform.ctx as *mut crate::handle::RefBox<StubColorTransform>;
		copy(&unsafe { (*boxed).value.view.clone() }, buf, buf_size)
	}

	/// `oakcommon_colortransform_get_look` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oakcommon_colortransform_get_look(
		transform: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		if transform.ctx.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let boxed = transform.ctx as *mut crate::handle::RefBox<StubColorTransform>;
		copy(&unsafe { (*boxed).value.look.clone() }, buf, buf_size)
	}

	/// Boxed colortransform stub payload.
	pub(crate) struct StubColorTransform {
		pub is_display: bool,
		pub display: String,
		pub output: String,
		pub view: String,
		pub look: String,
	}
}
