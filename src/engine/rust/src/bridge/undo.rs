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

//! oakundo C ABI imports, mirroring the oakundo crate's exports
//! (`src/undo/rust/src/ffi.rs`; headers `include/undo/*.h`).

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

extern "C" {
	/// `oakundo_command_init` — vtable-backed command, refcount 1.
	pub fn oakundo_command_init(
		vtable: *const OakUndoCommandVtable,
		userdata: *mut std::ffi::c_void,
	) -> CHandle;
	/// `oakundo_command_init_multi` — empty multi command, refcount 1.
	pub fn oakundo_command_init_multi() -> CHandle;
	/// `oakundo_command_multi_add_child` (stack takes one child ref).
	pub fn oakundo_command_multi_add_child(multi: CHandle, child: CHandle) -> c_int;
	/// `oakundo_command_multi_child_count`.
	pub fn oakundo_command_multi_child_count(multi: CHandle, out_count: *mut c_int) -> c_int;
	/// `oakundo_command_multi_child` (returned handle carries own ref).
	pub fn oakundo_command_multi_child(
		multi: CHandle,
		index: c_int,
		out_child: *mut CHandle,
	) -> c_int;
	/// `oakundo_command_redo_now`.
	pub fn oakundo_command_redo_now(command: CHandle) -> c_int;
	/// `oakundo_command_undo_now`.
	pub fn oakundo_command_undo_now(command: CHandle) -> c_int;
	/// `oakundo_command_free` — NULL/empty no-op; clears `command->ctx`.
	pub fn oakundo_command_free(command: *mut CHandle);
	/// `oakundo_undostack_init` — fresh stack, refcount 1.
	pub fn oakundo_undostack_init() -> CHandle;
	/// `oakundo_undostack_free` — NULL/empty no-op; clears `stack->ctx`.
	pub fn oakundo_undostack_free(stack: *mut CHandle);
	/// `oakundo_undostack_push` — redo then record; drops redoable tail.
	pub fn oakundo_undostack_push(stack: CHandle, command: CHandle, name: *const c_char) -> c_int;
	/// `oakundo_undostack_push_pre_executed` — record without redoing.
	pub fn oakundo_undostack_push_pre_executed(
		stack: CHandle,
		command: CHandle,
		name: *const c_char,
	) -> c_int;
	/// `oakundo_undostack_undo`.
	pub fn oakundo_undostack_undo(stack: CHandle) -> c_int;
	/// `oakundo_undostack_redo`.
	pub fn oakundo_undostack_redo(stack: CHandle) -> c_int;
	/// `oakundo_undostack_jump` (clamped to 0; `index` is i64).
	pub fn oakundo_undostack_jump(stack: CHandle, index: i64) -> c_int;
	/// `oakundo_undostack_clear`.
	pub fn oakundo_undostack_clear(stack: CHandle) -> c_int;
	/// `oakundo_undostack_can_undo`.
	pub fn oakundo_undostack_can_undo(stack: CHandle, out_value: *mut c_int) -> c_int;
	/// `oakundo_undostack_can_redo`.
	pub fn oakundo_undostack_can_redo(stack: CHandle, out_value: *mut c_int) -> c_int;
	/// `oakundo_undostack_count`.
	pub fn oakundo_undostack_count(stack: CHandle, out_count: *mut i64) -> c_int;
	/// `oakundo_undostack_index`.
	pub fn oakundo_undostack_index(stack: CHandle, out_index: *mut i64) -> c_int;
	/// `oakundo_undostack_command_text` (two-stage string getter).
	pub fn oakundo_undostack_command_text(
		stack: CHandle,
		row: i64,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oakundo_undostack_command_is_done`.
	pub fn oakundo_undostack_command_is_done(
		stack: CHandle,
		row: i64,
		out_value: *mut c_int,
	) -> c_int;
}

/// `include/undo/undocommand.h` — callback table backing a
/// caller-defined undo command.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct OakUndoCommandVtable {
	/// Redo callback (NULL = no-op).
	pub redo: Option<unsafe extern "C" fn(userdata: *mut std::ffi::c_void)>,
	/// Undo callback (NULL = no-op).
	pub undo: Option<unsafe extern "C" fn(userdata: *mut std::ffi::c_void)>,
	/// Destruction callback releasing `userdata`.
	pub free_fn: Option<unsafe extern "C" fn(userdata: *mut std::ffi::c_void)>,
}
