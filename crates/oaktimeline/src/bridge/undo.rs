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

//! oakundo C ABI imports (`include/undo/undocommand.h`). Undo commands are
//! created through the C ABI vtable (`oakundo_command_init` with Rust
//! closures as `userdata`) — timeline does not subclass the C++
//! `UndoCommand`; every command struct in this crate exposes `to_command()`
//! that wraps it this way.

use std::ffi::{c_char, c_int, c_void};

use crate::handle::CHandle;

/// `OakUndoCommandVtable` — callback table backing a caller-defined undo
/// command (undocommand.h). Any callback may be NULL (a NULL redo/undo makes
/// that direction a no-op); `free_fn` runs when the command is destroyed.
/// Single-lib unification: aliases the oakundo crate's vtable POD.
pub type OakUndoCommandVtable = oakundo::undocommand::OakUndoCommandVtable;

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_init(vtable: *const OakUndoCommandVtable, userdata: *mut c_void) -> CHandle {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakundo_command_init(vtable, userdata)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakundo::ffi::command::oakundo_command_init(vtable, userdata) }
	}
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_init_multi() -> CHandle {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakundo_command_init_multi()
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakundo::ffi::command::oakundo_command_init_multi() }
	}
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_multi_add_child(multi: CHandle, child: CHandle) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakundo_command_multi_add_child(multi, child)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakundo::ffi::command::oakundo_command_multi_add_child(multi, child) }
	}
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_redo_now(command: CHandle) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakundo_command_redo_now(command)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakundo::ffi::command::oakundo_command_redo_now(command) }
	}
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_undo_now(command: CHandle) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakundo_command_undo_now(command)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakundo::ffi::command::oakundo_command_undo_now(command) }
	}
}

/// Direct call into the `oakundo` crate (single-lib unification).
pub fn oakundo_command_free(command: *mut CHandle) {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakundo_command_free(command)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakundo::ffi::command::oakundo_command_free(command) }
	}
}

/// Direct call into the `oakundo` crate (single-lib unification; the C ABI
/// name is `oakundo_undostack_push` — kept as `oakundo_stack_push` here for
/// source compatibility).
pub fn oakundo_stack_push(stack: CHandle, command: CHandle, text: *const c_char) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oakundo_stack_push(stack, command, text)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oakundo::ffi::undostack::oakundo_undostack_push(stack, command, text) }
	}
}
