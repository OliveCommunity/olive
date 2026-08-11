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

//! oakundo C ABI bridge: direct Rust calls into the `oakundo` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oakundo`'s `ffi` (the
//! `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them). Handles cross as the shared
//! [`crate::handle::CHandle`]. Exceptions that keep an `extern "C"`
//! declaration (resolved at link time against the sibling crate in the
//! same dylib) are the host `oakcore_*` symbols and the encoding-params
//! C ABI POD crossings (the facade keeps its own POD mirrors there).

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

/// `include/undo/undocommand.h` — callback table backing a
/// caller-defined undo command. Single-lib unification: aliases the
/// oakundo crate's vtable POD.
pub type OakUndoCommandVtable = oakundo::undocommand::OakUndoCommandVtable;

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_command_init(
	vtable: *const OakUndoCommandVtable,
	userdata: *mut std::ffi::c_void,
) -> CHandle {
	unsafe { oakundo::ffi::command::oakundo_command_init(vtable, userdata) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_command_init_multi() -> CHandle {
	unsafe { oakundo::ffi::command::oakundo_command_init_multi() }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_command_multi_add_child(multi: CHandle, child: CHandle) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_multi_add_child(multi, child) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_command_multi_child_count(multi: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_multi_child_count(multi, out_count) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_command_multi_child(multi: CHandle, index: c_int, out_child: *mut CHandle) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_multi_child(multi, index, out_child) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_command_redo_now(command: CHandle) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_redo_now(command) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_command_undo_now(command: CHandle) -> c_int {
	unsafe { oakundo::ffi::command::oakundo_command_undo_now(command) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_command_free(command: *mut CHandle) {
	unsafe { oakundo::ffi::command::oakundo_command_free(command) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_init() -> CHandle {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_init() }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_free(stack: *mut CHandle) {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_free(stack) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_push(stack: CHandle, command: CHandle, name: *const c_char) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_push(stack, command, name) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_push_pre_executed(
	stack: CHandle,
	command: CHandle,
	name: *const c_char,
) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_push_pre_executed(stack, command, name) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_undo(stack: CHandle) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_undo(stack) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_redo(stack: CHandle) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_redo(stack) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_jump(stack: CHandle, index: i64) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_jump(stack, index) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_clear(stack: CHandle) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_clear(stack) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_can_undo(stack: CHandle, out_value: *mut c_int) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_can_undo(stack, out_value) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_can_redo(stack: CHandle, out_value: *mut c_int) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_can_redo(stack, out_value) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_count(stack: CHandle, out_count: *mut i64) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_count(stack, out_count) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_index(stack: CHandle, out_index: *mut i64) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_index(stack, out_index) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_command_text(
	stack: CHandle,
	row: i64,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_command_text(stack, row, buf, buf_size) }
}

/// Direct call into the `oakundo` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oakundo_undostack_command_is_done(stack: CHandle, row: i64, out_value: *mut c_int) -> c_int {
	unsafe { oakundo::ffi::undostack::oakundo_undostack_command_is_done(stack, row, out_value) }
}
