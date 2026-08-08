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

//! oakcommon C ABI imports (XML + config + strings).

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

extern "C" {
	/// `oakcommon_xml_reader_init`.
	pub fn oakcommon_xml_reader_init(data: *const c_char) -> CHandle;
	/// `oakcommon_xml_reader_free`.
	pub fn oakcommon_xml_reader_free(reader: *mut CHandle);
	/// `oakcommon_xml_writer_init`.
	pub fn oakcommon_xml_writer_init() -> CHandle;
	/// `oakcommon_xml_writer_free`.
	pub fn oakcommon_xml_writer_free(writer: *mut CHandle);
	/// `oakcommon_config_get_int` (config access for node defaults).
	pub fn oakcommon_config_get_int(group: *const c_char, key: *const c_char, default: c_int) -> c_int;
}
