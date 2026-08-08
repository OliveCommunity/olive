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

//! oaktimeline C ABI imports (sequence markers/work area, edit
//! commands used by sequence setup).

use crate::handle::CHandle;

extern "C" {
	/// `oaktimeline_marker_list_create`.
	pub fn oaktimeline_marker_list_create() -> CHandle;
	/// `oaktimeline_marker_list_free`.
	pub fn oaktimeline_marker_list_free(list: *mut CHandle);
	/// `oaktimeline_workarea_create`.
	pub fn oaktimeline_workarea_create() -> CHandle;
	/// `oaktimeline_workarea_free`.
	pub fn oaktimeline_workarea_free(w: *mut CHandle);
	/// `oaktimeline_add_track_command`.
	pub fn oaktimeline_add_track_command(list: CHandle) -> CHandle;
}
