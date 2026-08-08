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
#[repr(C)]
pub struct OakUndoCommandVtable {
  /// `redo` callback (NULL = no-op).
  pub redo: Option<unsafe extern "C" fn(*mut c_void)>,
  /// `undo` callback (NULL = no-op).
  pub undo: Option<unsafe extern "C" fn(*mut c_void)>,
  /// `free_fn` — releases `userdata` on destruction.
  pub free_fn: Option<unsafe extern "C" fn(*mut c_void)>,
}

extern "C" {
  /// `oakundo_command_init` — create a command backed by C callbacks; takes
  /// ownership of `userdata`, copies `vtable`.
  pub fn oakundo_command_init(vtable: *const OakUndoCommandVtable, userdata: *mut c_void) -> CHandle;
  /// `oakundo_command_init_multi` — an empty multi command.
  pub fn oakundo_command_init_multi() -> CHandle;
  /// `oakundo_command_multi_add_child` — add a child to a multi command.
  pub fn oakundo_command_multi_add_child(multi: CHandle, child: CHandle) -> c_int;
  /// `oakundo_command_redo_now` — run the command's redo outside a stack.
  pub fn oakundo_command_redo_now(command: CHandle) -> c_int;
  /// `oakundo_command_undo_now` — run the command's undo outside a stack.
  pub fn oakundo_command_undo_now(command: CHandle) -> c_int;
  /// `oakundo_command_free` — release one reference; NULL is a no-op.
  pub fn oakundo_command_free(command: *mut CHandle);
  /// `oakundo_stack_push` — push a command onto a facade-owned stack.
  pub fn oakundo_stack_push(stack: CHandle, command: CHandle, text: *const c_char) -> c_int;
}
