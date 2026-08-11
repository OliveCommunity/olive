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

//! C ABI export layer: implements `include/undo/*.h` verbatim.
//!
//! Organization: one submodule per public header. The authoritative
//! function list is the header itself; each submodule below carries a
//! complete inventory comment plus export stubs. Bodies only unwrap
//! handles, call the safe Rust domains, and map results through
//! [`crate::handle::guard*`]. `include/undo/error.h` exports macros
//! only, so it is folded into this module's preamble instead of getting
//! its own submodule.
//!
//! Handles cross the boundary by field copy between the public
//! [`OakUndoCommand`]/[`OakUndoStack`] structs and the crate-internal
//! [`CHandle`]. Stacks live behind a `Mutex` (shared state), commands in
//! a `CommandBox`.

use std::ffi::{c_char, c_int, c_void, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

use crate::error::{Error, Result};
use crate::handle::{guard, guard_handle, guard_void, make_owned, CHandle};
use crate::undocommand::{
	command_from_borrowed, command_from_owned, command_take, command_to_mut, command_to_ref,
	OakUndoCommandVtable, UndoCommand,
};
use crate::undostack::UndoStack;

/// `include/undo/error.h` — macros only, no exported functions.
///
/// `OAKUNDO_OK` and the `OAKUNDO_E_*` codes are mirrored as
/// [`crate::error`] constants; `OAKUNDO_ABI_VERSION` lives in
/// [`crate::handle`].

/// `include/undo/undocommand.h` handle type — `OakUndoCommand`
/// (`{ctx, addref, release, abi_version}`). Single-lib unification:
/// aliases the shared [`CHandle`] (identical layout; the C ABI is
/// unchanged).
pub type OakUndoCommand = CHandle;

/// `include/undo/undostack.h` handle type — `OakUndoStack`. Single-lib
/// unification: aliases the shared [`CHandle`].
pub type OakUndoStack = CHandle;

/// Lock the stack behind `stack` and run `f` on it. `E_INVALID` for an
/// empty handle. A poisoned mutex is recovered (its inner value is
/// still valid).
fn with_stack<R>(stack: OakUndoStack, f: impl FnOnce(&mut UndoStack) -> Result<R>) -> Result<R> {
	// Keep a local handle so `get`'s returned reference lives for the
	// whole call (its lifetime ties to the `&CHandle` argument).
	let ch = stack;
	let m = unsafe { crate::handle::get::<Mutex<UndoStack>>(&ch) }.ok_or(Error::Invalid)?;
	let mut guard = m.lock().unwrap_or_else(|e| e.into_inner());
	f(&mut guard)
}

/// Read a NUL-terminated C string; `NULL` yields an empty string
/// (mirrors the C++ `name ? name : ""`).
fn read_name(name: *const c_char) -> String {
	if name.is_null() {
		String::new()
	} else {
		// SAFETY: `name` is a valid NUL-terminated string supplied by the
		// caller, or NULL (already handled).
		unsafe { CStr::from_ptr(name).to_string_lossy().into_owned() }
	}
}

/// `include/undo/undocommand.h` exports (complete inventory):
/// command_init / command_init_multi / command_multi_add_child /
/// command_multi_child_count / command_multi_child / command_redo_now /
/// command_undo_now / command_free.
pub mod command {
	use super::*;

	/// `oakundo_command_init`: vtable-backed command, refcount 1.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_init(
		vtable: *const OakUndoCommandVtable,
		userdata: *mut c_void,
	) -> OakUndoCommand {
		guard_handle(|| unsafe {
			if vtable.is_null() {
				return Ok(CHandle::null());
			}
			let table = *vtable;
			Ok(command_from_owned(UndoCommand::from_vtable(
				table, userdata,
			)))
		})
	}

	/// `oakundo_command_init_multi`: empty multi command, refcount 1.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_init_multi() -> OakUndoCommand {
		guard_handle(|| unsafe { Ok(command_from_owned(UndoCommand::multi())) })
	}

	/// `oakundo_command_multi_add_child` (stack takes one child ref).
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_multi_add_child(
		multi: OakUndoCommand,
		child: OakUndoCommand,
	) -> c_int {
		guard(|| unsafe {
			let parent = command_to_mut(multi.ctx).ok_or(Error::Invalid)?;
			if !parent.is_multi() {
				return Err(Error::Invalid);
			}
			let child_cmd = command_take(child.ctx)?;
			parent.multi_add_child(child_cmd);
			Ok(())
		})
	}

	/// `oakundo_command_multi_child_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_multi_child_count(
		multi: OakUndoCommand,
		out_count: *mut c_int,
	) -> c_int {
		guard(|| unsafe {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let parent = command_to_ref(multi.ctx).ok_or(Error::Invalid)?;
			if !parent.is_multi() {
				return Err(Error::Invalid);
			}
			*out_count = parent.multi_child_count() as c_int;
			Ok(())
		})
	}

	/// `oakundo_command_multi_child` (returned handle carries own ref).
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_multi_child(
		multi: OakUndoCommand,
		index: c_int,
		out_child: *mut OakUndoCommand,
	) -> c_int {
		guard(|| unsafe {
			if out_child.is_null() {
				return Err(Error::Invalid);
			}
			let parent = command_to_ref(multi.ctx).ok_or(Error::Invalid)?;
			if !parent.is_multi() {
				return Err(Error::Invalid);
			}
			if index < 0 {
				return Err(Error::NotFound);
			}
			let child = parent.multi_child(index as usize)?;
			let ptr = child as *const UndoCommand as *mut UndoCommand;
			*out_child = command_from_borrowed(ptr);
			Ok(())
		})
	}

	/// `oakundo_command_redo_now`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_redo_now(command: OakUndoCommand) -> c_int {
		guard(|| unsafe {
			let c = command_to_mut(command.ctx).ok_or(Error::Invalid)?;
			c.redo_now();
			Ok(())
		})
	}

	/// `oakundo_command_undo_now`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_undo_now(command: OakUndoCommand) -> c_int {
		guard(|| unsafe {
			let c = command_to_mut(command.ctx).ok_or(Error::Invalid)?;
			c.undo_now();
			Ok(())
		})
	}

	/// `oakundo_command_free`: NULL/empty no-op; clears `command->ctx`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_command_free(command: *mut OakUndoCommand) {
		guard_void(|| unsafe {
			if command.is_null() || (*command).ctx.is_null() {
				return;
			}
			if let Some(release) = (*command).release {
				release((*command).ctx);
			}
			(*command).ctx = std::ptr::null_mut();
		})
	}
}

/// `include/undo/undostack.h` exports (complete inventory):
/// undostack_init / undostack_free / undostack_push /
/// undostack_push_pre_executed / undostack_undo / undostack_redo /
/// undostack_jump / undostack_clear / undostack_can_undo /
/// undostack_can_redo / undostack_count / undostack_index /
/// undostack_command_text / undostack_command_is_done.
pub mod undostack {
	use super::*;

	/// `oakundo_undostack_init`: fresh stack, refcount 1.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_init() -> OakUndoStack {
		guard_handle(|| Ok(make_owned(Mutex::new(UndoStack::new()))))
	}

	/// `oakundo_undostack_free`: NULL/empty no-op; clears `stack->ctx`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_free(stack: *mut OakUndoStack) {
		guard_void(|| unsafe {
			if stack.is_null() || (*stack).ctx.is_null() {
				return;
			}
			if let Some(release) = (*stack).release {
				release((*stack).ctx);
			}
			(*stack).ctx = std::ptr::null_mut();
		})
	}

	/// `oakundo_undostack_push`: redo then record; drops redoable tail.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_push(
		stack: OakUndoStack,
		command: OakUndoCommand,
		name: *const c_char,
	) -> c_int {
		guard(|| {
			with_stack(stack, |s| unsafe {
				let cmd = command_take(command.ctx)?;
				let name = read_name(name);
				s.push(cmd, &name);
				Ok(())
			})
		})
	}

	/// `oakundo_undostack_push_pre_executed`: record without redoing.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_push_pre_executed(
		stack: OakUndoStack,
		command: OakUndoCommand,
		name: *const c_char,
	) -> c_int {
		guard(|| {
			with_stack(stack, |s| unsafe {
				let cmd = command_take(command.ctx)?;
				let name = read_name(name);
				s.push_pre_executed(cmd, &name);
				Ok(())
			})
		})
	}

	/// `oakundo_undostack_undo`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_undo(stack: OakUndoStack) -> c_int {
		guard(|| with_stack(stack, |s| s.undo()))
	}

	/// `oakundo_undostack_redo`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_redo(stack: OakUndoStack) -> c_int {
		guard(|| with_stack(stack, |s| s.redo()))
	}

	/// `oakundo_undostack_jump` (clamped to 0; `index` is i64).
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_jump(stack: OakUndoStack, index: i64) -> c_int {
		guard(|| {
			with_stack(stack, |s| {
				s.jump(index);
				Ok(())
			})
		})
	}

	/// `oakundo_undostack_clear`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_clear(stack: OakUndoStack) -> c_int {
		guard(|| {
			with_stack(stack, |s| {
				s.clear();
				Ok(())
			})
		})
	}

	/// `oakundo_undostack_can_undo`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_can_undo(
		stack: OakUndoStack,
		out_value: *mut c_int,
	) -> c_int {
		guard(|| unsafe {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			with_stack(stack, |s| {
				*out_value = if s.can_undo() { 1 } else { 0 };
				Ok(())
			})
		})
	}

	/// `oakundo_undostack_can_redo`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_can_redo(
		stack: OakUndoStack,
		out_value: *mut c_int,
	) -> c_int {
		guard(|| unsafe {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			with_stack(stack, |s| {
				*out_value = if s.can_redo() { 1 } else { 0 };
				Ok(())
			})
		})
	}

	/// `oakundo_undostack_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_count(
		stack: OakUndoStack,
		out_count: *mut i64,
	) -> c_int {
		guard(|| unsafe {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			with_stack(stack, |s| {
				*out_count = s.command_count();
				Ok(())
			})
		})
	}

	/// `oakundo_undostack_index`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_index(
		stack: OakUndoStack,
		out_index: *mut i64,
	) -> c_int {
		guard(|| unsafe {
			if out_index.is_null() {
				return Err(Error::Invalid);
			}
			with_stack(stack, |s| {
				*out_index = s.done_count();
				Ok(())
			})
		})
	}

	/// `oakundo_undostack_command_text` (two-stage string getter).
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_command_text(
		stack: OakUndoStack,
		row: i64,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let result = catch_unwind(AssertUnwindSafe(|| -> Result<i32> {
			with_stack(stack, |s| {
				if row < 0 || row >= s.command_count() {
					return Err(Error::NotFound);
				}
				let name = s.command_name(row)?;
				let required = (name.len() + 1) as i32;
				if !buf.is_null() && buf_size > 0 {
					let copy_len = name.len().min((buf_size as usize).saturating_sub(1));
					let bytes = name.as_bytes();
					// SAFETY: `buf` points to `buf_size` writable bytes and
					// we write at most `copy_len` (+ one NUL) of them.
					unsafe {
						std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, copy_len);
						*buf.add(copy_len) = 0;
					}
				}
				Ok(required)
			})
		}));
		match result {
			Ok(Ok(required)) => required,
			Ok(Err(e)) => e.code(),
			Err(_) => crate::error::OAKUNDO_E_FAILED,
		}
	}

	/// `oakundo_undostack_command_is_done`.
	#[no_mangle]
	pub unsafe extern "C" fn oakundo_undostack_command_is_done(
		stack: OakUndoStack,
		row: i64,
		out_value: *mut c_int,
	) -> c_int {
		guard(|| unsafe {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			with_stack(stack, |s| {
				*out_value = if s.command_is_done(row)? { 1 } else { 0 };
				Ok(())
			})
		})
	}
}
