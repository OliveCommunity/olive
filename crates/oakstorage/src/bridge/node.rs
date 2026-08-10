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

//! oaknode C ABI imports (project + serializer family; signatures
//! mirror include/node/*.h verbatim).

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

extern "C" {
	/// `oaknode_project_init`.
	pub fn oaknode_project_init() -> CHandle;
	/// `oaknode_project_free`.
	pub fn oaknode_project_free(project: *mut CHandle);
	/// `oaknode_project_load_from_data` (XML text → project).
	pub fn oaknode_project_load_from_data(data: *const c_char) -> CHandle;
	/// `oaknode_project_save_to_data` (project → XML, two-stage string).
	pub fn oaknode_project_save_to_data(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
}
