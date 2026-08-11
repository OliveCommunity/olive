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

//! oakundo C ABI imports. Import/load tasks build undo commands through the
//! C ABI vtable (`oakundo_command_init` with Rust closures as userdata) —
//! no C++ UndoCommand subclassing exists on this side. Mirrors
//! `include/undo/undocommand.h` verbatim.

use std::ffi::{c_int, c_void};

use crate::handle::CHandle;

/// `OakUndoCommand` (`include/undo/undocommand.h`).
pub type OakUndoCommand = CHandle;

/// Mirror of `OakUndoCommandVtable` (`include/undo/undocommand.h`).
pub type OakUndoCommandVtable = oakundo::undocommand::OakUndoCommandVtable;

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_init(
	vtable: *const OakUndoCommandVtable,
	userdata: *mut c_void,
) -> OakUndoCommand {
	unsafe { oakundo::ffi::command::oakundo_command_init(vtable, userdata) }
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_init_multi() -> OakUndoCommand {
	unsafe { oakundo::ffi::command::oakundo_command_init_multi() }
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_multi_add_child(multi: OakUndoCommand, child: OakUndoCommand) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_multi_add_child(multi, child) }
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_multi_child_count(multi: OakUndoCommand, out_count: *mut c_int) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_multi_child_count(multi, out_count) }
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_multi_child(
	multi: OakUndoCommand,
	index: c_int,
	out_child: *mut OakUndoCommand,
) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_multi_child(multi, index, out_child) }
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_redo_now(command: OakUndoCommand) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_redo_now(command) }
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_undo_now(command: OakUndoCommand) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_undo_now(command) }
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_free(command: *mut OakUndoCommand) {
	unsafe { oakundo::ffi::command::oakundo_command_free(command) }
}
