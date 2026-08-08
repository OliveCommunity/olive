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

extern "C" {
  /// `oakcommon_xml_reader_init` — create a reader over a NUL-terminated
  /// document; the document must outlive the reader.
  pub fn oakcommon_xml_reader_init(data: *const c_char) -> CHandle;
  /// `oakcommon_xml_reader_free` — release the reader; NULL is a no-op.
  pub fn oakcommon_xml_reader_free(reader: *mut CHandle);
  /// `oakcommon_xml_reader_skip_current_element` — skip to the end of the
  /// current element subtree.
  pub fn oakcommon_xml_reader_skip_current_element(reader: CHandle) -> c_int;
  /// `oakcommon_xml_reader_read_next_start_element` — advance to the next
  /// start element, writing its (possibly empty) name into `name`.
  pub fn oakcommon_xml_reader_read_next_start_element(reader: CHandle, name: *mut c_char, buf_size: c_int) -> c_int;
  /// `oakcommon_xml_reader_name` — the current element's name.
  pub fn oakcommon_xml_reader_name(reader: CHandle, name: *mut c_char, buf_size: c_int) -> c_int;
  /// `oakcommon_xml_reader_read_element_text` — read the current element's
  /// text content.
  pub fn oakcommon_xml_reader_read_element_text(reader: CHandle, text: *mut c_char, buf_size: c_int) -> c_int;
  /// `oakcommon_xml_reader_attribute_count` — number of attributes on the
  /// current element.
  pub fn oakcommon_xml_reader_attribute_count(reader: CHandle, count: *mut c_int) -> c_int;
  /// `oakcommon_xml_reader_attribute_name` — name of attribute `i`.
  pub fn oakcommon_xml_reader_attribute_name(reader: CHandle, index: c_int, name: *mut c_char, buf_size: c_int) -> c_int;
  /// `oakcommon_xml_reader_attribute_value` — value of attribute `i`.
  pub fn oakcommon_xml_reader_attribute_value(reader: CHandle, index: c_int, value: *mut c_char, buf_size: c_int) -> c_int;
  /// `oakcommon_xml_reader_has_error` — whether the reader hit an error.
  pub fn oakcommon_xml_reader_has_error(reader: CHandle, has_error: *mut c_int) -> c_int;
  /// `oakcommon_xml_writer_init` — create a writer.
  pub fn oakcommon_xml_writer_init() -> CHandle;
  /// `oakcommon_xml_writer_free` — release the writer; NULL is a no-op.
  pub fn oakcommon_xml_writer_free(writer: *mut CHandle);
  /// `oakcommon_xml_writer_write_start_element` — open `<name>`.
  pub fn oakcommon_xml_writer_write_start_element(writer: CHandle, name: *const c_char) -> c_int;
  /// `oakcommon_xml_writer_write_end_element` — close the current element.
  pub fn oakcommon_xml_writer_write_end_element(writer: CHandle) -> c_int;
  /// `oakcommon_xml_writer_write_end_document` — finish the document.
  pub fn oakcommon_xml_writer_write_end_document(writer: CHandle) -> c_int;
  /// `oakcommon_xml_writer_write_attribute` — write `key="value"`.
  pub fn oakcommon_xml_writer_write_attribute(writer: CHandle, key: *const c_char, value: *const c_char) -> c_int;
  /// `oakcommon_xml_writer_write_characters` — write raw text content.
  pub fn oakcommon_xml_writer_write_characters(writer: CHandle, text: *const c_char) -> c_int;
  /// `oakcommon_xml_writer_write_text_element` — write `<name>text</name>`.
  pub fn oakcommon_xml_writer_write_text_element(writer: CHandle, name: *const c_char, text: *const c_char) -> c_int;
  /// `oakcommon_config_get_int` — read an integer config value (marker default
  /// colour).
  pub fn oakcommon_config_get_int(group: *const c_char, key: *const c_char, default: c_int) -> c_int;
}
