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

//! oakundo C ABI imports. Undo commands are created through the C ABI
//! vtable (`oakundo_command_init` with Rust closures as userdata) —
//! no C++ UndoCommand subclassing exists on this side.

use std::ffi::c_int;

use crate::handle::CHandle;

extern "C" {
	/// `oakundo_command_init` (vtable command).
	pub fn oakundo_command_init(vtable: *const std::ffi::c_void, userdata: *mut std::ffi::c_void) -> CHandle;
	/// `oakundo_command_init_multi`.
	pub fn oakundo_command_init_multi() -> CHandle;
	/// `oakundo_command_multi_add_child`.
	pub fn oakundo_command_multi_add_child(multi: CHandle, child: CHandle) -> c_int;
	/// `oakundo_command_redo_now`.
	pub fn oakundo_command_redo_now(command: CHandle) -> c_int;
	/// `oakundo_command_undo_now`.
	pub fn oakundo_command_undo_now(command: CHandle) -> c_int;
	/// `oakundo_command_free`.
	pub fn oakundo_command_free(command: *mut CHandle);
	/// `oakundo_stack_push` (facade-owned stack).
	pub fn oakundo_stack_push(stack: CHandle, command: CHandle, text: *const std::ffi::c_char) -> c_int;
}
