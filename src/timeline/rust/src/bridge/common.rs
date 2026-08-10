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

//! oakcommon C ABI imports (XML reader/writer + config), mirroring
//! `include/common/xmlutils.h` and `include/common/config.h`.

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_init(data: *const c_char) -> CHandle {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_init(data)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_init(data) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_free(reader: *mut CHandle) {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_free(reader)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_free(reader) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_skip_current_element(reader: CHandle) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_skip_current_element(reader)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_skip_current_element(reader) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification). The
/// real ABI advances the reader and writes a 1/0 `found` flag (the name is
/// read with [`oakcommon_xml_reader_name`]).
pub fn oakcommon_xml_reader_read_next_start_element(reader: CHandle, found: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_read_next_start_element(reader, found)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_read_next_start_element(reader, found) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_name(reader: CHandle, name: *mut c_char, buf_size: c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_name(reader, name, buf_size)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_name(reader, name, buf_size) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_read_element_text(reader: CHandle, text: *mut c_char, buf_size: c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_read_element_text(reader, text, buf_size)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_read_element_text(reader, text, buf_size) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_attribute_count(reader: CHandle, count: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_attribute_count(reader, count)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_count(reader, count) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_attribute_name(reader: CHandle, index: c_int, name: *mut c_char, buf_size: c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_attribute_name(reader, index, name, buf_size)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_name(reader, index, name, buf_size) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_attribute_value(reader: CHandle, index: c_int, value: *mut c_char, buf_size: c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_attribute_value(reader, index, value, buf_size)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_attribute_value(reader, index, value, buf_size) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_reader_has_error(reader: CHandle, has_error: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_reader_has_error(reader, has_error)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_reader_has_error(reader, has_error) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_writer_init() -> CHandle {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_writer_init()
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_init() }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_writer_free(writer: *mut CHandle) {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_writer_free(writer)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_free(writer) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_writer_write_start_element(writer: CHandle, name: *const c_char) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_writer_write_start_element(writer, name)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_start_element(writer, name) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_writer_write_end_element(writer: CHandle) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_writer_write_end_element(writer)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_end_element(writer) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_writer_write_end_document(writer: CHandle) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_writer_write_end_document(writer)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_end_document(writer) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_writer_write_attribute(writer: CHandle, key: *const c_char, value: *const c_char) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_writer_write_attribute(writer, key, value)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_attribute(writer, key, value) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_writer_write_characters(writer: CHandle, text: *const c_char) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_writer_write_characters(writer, text)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_characters(writer, text) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_xml_writer_write_text_element(writer: CHandle, name: *const c_char, text: *const c_char) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_xml_writer_write_text_element(writer, name, text)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::xmlutils::oakcommon_xml_writer_write_text_element(writer, name, text) }
	}
}

/// Direct call into the `oakcommon` crate (single-lib unification).
pub fn oakcommon_config_get_int(group: *const c_char, key: *const c_char, default: c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakcommon_config_get_int(group, key, default)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakcommon::ffi::config::oakcommon_config_get_int(group, key, default) }
	}
}

