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

//! oaktimeline C ABI imports. The OTIO load task builds tracks through the
//! timeline edit factories (`include/timeline/edit.h`) instead of the raw
//! track-list primitives, matching `src/task/src/project/loadotio/loadotio.cpp`.
//! Signatures mirror the header verbatim.

use crate::bridge::node::OakNodeTrackList;
use crate::bridge::undo::OakUndoCommand;

/// Direct call into the `oaktimeline` crate (single-lib unification).
pub fn oaktimeline_add_track_command(list: OakNodeTrackList) -> OakUndoCommand {
	unsafe { oaktimeline::ffi::edit::oaktimeline_add_track_command(list) }
}
