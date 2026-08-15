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

//! `olive::UndoStack` — the undo/redo history stack. Mirrors
//! `src/undo/src/undostack.h` and `include/undo/undostack.h`.
//!
//! Two deques: `commands` (done commands, oldest at the front) and
//! `undone` (undone commands, most-recently-undone at the front). The
//! fresh stack holds a single "New/Open Project" empty command so that
//! `can_undo` is false at the bottom (see `undostack.cpp::clear`).

use std::collections::VecDeque;
use std::ffi::{c_char, c_int, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Mutex;

use crate::error::{Error, OAKUNDO_E_FAILED, Result};
use crate::handle::{get, guard, guard_handle, guard_void, make_owned, CHandle};
use crate::undocommand::{command_take, UndoCommand};

/// Maximum number of retained history rows (`k_max_undo_commands`).
pub const K_MAX_UNDO_COMMANDS: usize = 200;

/// The invariant bottom-of-stack command ("New/Open Project"); not
/// undoable, so `can_undo` returns false at index 0.
pub struct EmptyCommand;

impl EmptyCommand {
	/// A no-op command (redo/undo do nothing).
	pub fn new() -> UndoCommand {
		UndoCommand::empty()
	}
}

impl Default for EmptyCommand {
	fn default() -> Self {
		EmptyCommand
	}
}

/// One history row: the command plus its user-visible label.
pub struct CommandEntry {
	/// The command.
	pub command: UndoCommand,
	/// User-visible label.
	pub name: String,
}

/// `olive::UndoStack`.
pub struct UndoStack {
	/// Done commands, oldest at the front.
	commands: VecDeque<CommandEntry>,
	/// Undone commands, most-recently-undone at the front.
	undone: VecDeque<CommandEntry>,
}

/// See `UndoCommand::Send`.
unsafe impl Send for UndoStack {}

impl UndoStack {
	/// New stack; contains a single empty "New/Open Project" command.
	pub fn new() -> Self {
		let mut stack = UndoStack {
			commands: VecDeque::new(),
			undone: VecDeque::new(),
		};
		stack.clear();
		stack
	}

	/// Push `command` and execute its redo; drops any redoable tail and
	/// evicts the oldest row beyond the cap.
	pub fn push(&mut self, command: UndoCommand, name: &str) {
		if command.is_multi() && command.multi_child_count() == 0 {
			return;
		}
		if self.can_redo() {
			self.undone.clear();
		}
		let mut command = command;
		command.redo_and_set_modified();
		self.commands.push_back(CommandEntry {
			command,
			name: name.to_string(),
		});
		if self.commands.len() > K_MAX_UNDO_COMMANDS {
			self.commands.pop_front();
		}
	}

	/// Push an already-executed command (redo skipped); `set_done(true)`.
	pub fn push_pre_executed(&mut self, command: UndoCommand, name: &str) {
		if command.is_multi() && command.multi_child_count() == 0 {
			return;
		}
		if self.can_redo() {
			self.undone.clear();
		}
		let mut command = command;
		command.set_done(true);
		self.commands.push_back(CommandEntry {
			command,
			name: name.to_string(),
		});
		if self.commands.len() > K_MAX_UNDO_COMMANDS {
			self.commands.pop_front();
		}
	}

	/// Undo/redo until the done-command count equals `index`.
	pub fn jump(&mut self, index: i64) {
		let index = index.max(0);
		while (self.commands.len() as i64) > index && self.can_undo() {
			let _ = self.undo();
		}
		while (self.commands.len() as i64) < index && self.can_redo() {
			let _ = self.redo();
		}
	}

	/// Delete all commands and push a fresh empty command.
	pub fn clear(&mut self) {
		self.commands.clear();
		self.undone.clear();
		self.commands.push_back(CommandEntry {
			command: EmptyCommand::new(),
			name: "New/Open Project".to_string(),
		});
	}

	/// Whether an undo is possible (more than the bottom empty command).
	pub fn can_undo(&self) -> bool {
		!self.commands.is_empty()
			&& !self
				.commands
				.back()
				.map_or(true, |entry| entry.command.is_empty())
	}

	/// Whether a redo is possible.
	pub fn can_redo(&self) -> bool {
		!self.undone.is_empty()
	}

	/// Undo the most recently done command, if any.
	pub fn undo(&mut self) -> Result<()> {
		if self.can_undo() {
			if let Some(entry) = self.commands.pop_back() {
				let mut command = entry.command;
				command.undo_and_set_modified();
				self.undone.push_front(CommandEntry {
					command,
					name: entry.name,
				});
			}
		}
		Ok(())
	}

	/// Redo the most recently undone command, if any.
	pub fn redo(&mut self) -> Result<()> {
		if self.can_redo() {
			if let Some(entry) = self.undone.pop_front() {
				let mut command = entry.command;
				command.redo_and_set_modified();
				self.commands.push_back(CommandEntry {
					command,
					name: entry.name,
				});
			}
		}
		Ok(())
	}

	/// Total number of history rows (done + undone commands).
	pub fn command_count(&self) -> i64 {
		(self.commands.len() + self.undone.len()) as i64
	}

	/// Number of done commands (the current position).
	pub fn done_count(&self) -> i64 {
		self.commands.len() as i64
	}

	/// Whether the row at `row` is currently done.
	pub fn command_is_done(&self, row: i64) -> Result<bool> {
		if row < 0 || row >= self.command_count() {
			return Err(Error::NotFound);
		}
		Ok(row < self.commands.len() as i64)
	}

	/// Label of the history row at `row`.
	pub fn command_name(&self, row: i64) -> Result<&str> {
		if row < 0 || row >= self.command_count() {
			return Err(Error::NotFound);
		}
		let row = row as usize;
		if row < self.commands.len() {
			Ok(self.commands[row].name.as_str())
		} else {
			Ok(self.undone[row - self.commands.len()].name.as_str())
		}
	}
}

impl Default for UndoStack {
	fn default() -> Self {
		Self::new()
	}
}

// ---------------------------------------------------------------------------
// Handle-level stack API (sunk from the former C ABI export layer)
// ---------------------------------------------------------------------------

/// Read a NUL-terminated C string; `NULL` yields an empty string
/// (mirrors the C++ `name ? name : ""`).
fn read_name(name: *const c_char) -> String {
	if name.is_null() {
		String::new()
	} else {
		// SAFETY: `name` is a valid NUL-terminated string supplied by the
		// caller, or NULL (already handled).
		unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned()
	}
}

/// Lock the stack behind `stack` and run `f` on it. `E_INVALID` for an
/// empty handle. A poisoned mutex is recovered (its inner value is still
/// valid).
pub fn with_stack<R>(stack: &CHandle, f: impl FnOnce(&mut UndoStack) -> Result<R>) -> Result<R> {
	// SAFETY: the stack handle always boxes a `Mutex<UndoStack>` (created
	// by `undostack_init`).
	let m = unsafe { get::<Mutex<UndoStack>>(stack) }.ok_or(Error::Invalid)?;
	let mut guard = m.lock().unwrap_or_else(|e| e.into_inner());
	f(&mut guard)
}

/// Create a fresh stack handle (refcount 1) (`oakundo_undostack_init`).
pub fn undostack_init() -> CHandle {
	guard_handle(|| Ok(make_owned(Mutex::new(UndoStack::new()))))
}

/// Release a stack handle in place; `NULL` / empty handles are no-ops
/// (`oakundo_undostack_free`).
pub fn undostack_free(stack: *mut CHandle) {
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

/// Push a command handle onto the stack (redo then record; the stack
/// takes ownership of the command value, leaving a non-owning shell)
/// (`oakundo_undostack_push`).
pub fn undostack_push(stack: CHandle, command: CHandle, name: *const c_char) -> c_int {
	guard(|| {
		with_stack(&stack, |s| unsafe {
			let cmd = command_take(command.ctx)?;
			let name = read_name(name);
			s.push(cmd, &name);
			Ok(())
		})
	})
}

/// Push an already-executed command handle (redo skipped)
/// (`oakundo_undostack_push_pre_executed`).
pub fn undostack_push_pre_executed(stack: CHandle, command: CHandle, name: *const c_char) -> c_int {
	guard(|| {
		with_stack(&stack, |s| unsafe {
			let cmd = command_take(command.ctx)?;
			let name = read_name(name);
			s.push_pre_executed(cmd, &name);
			Ok(())
		})
	})
}

/// Undo one command on the stack (`oakundo_undostack_undo`).
pub fn undostack_undo(stack: CHandle) -> c_int {
	guard(|| with_stack(&stack, |s| s.undo()))
}

/// Redo one command on the stack (`oakundo_undostack_redo`).
pub fn undostack_redo(stack: CHandle) -> c_int {
	guard(|| with_stack(&stack, |s| s.redo()))
}

/// Jump to a done-command index (clamped to 0; `index` is i64)
/// (`oakundo_undostack_jump`).
pub fn undostack_jump(stack: CHandle, index: i64) -> c_int {
	guard(|| {
		with_stack(&stack, |s| {
			s.jump(index);
			Ok(())
		})
	})
}

/// Clear the stack back to the empty bottom command
/// (`oakundo_undostack_clear`).
pub fn undostack_clear(stack: CHandle) -> c_int {
	guard(|| {
		with_stack(&stack, |s| {
			s.clear();
			Ok(())
		})
	})
}

/// Write whether an undo is possible (`oakundo_undostack_can_undo`).
pub fn undostack_can_undo(stack: CHandle, out_value: *mut c_int) -> c_int {
	guard(|| unsafe {
		if out_value.is_null() {
			return Err(Error::Invalid);
		}
		with_stack(&stack, |s| {
			*out_value = if s.can_undo() { 1 } else { 0 };
			Ok(())
		})
	})
}

/// Write whether a redo is possible (`oakundo_undostack_can_redo`).
pub fn undostack_can_redo(stack: CHandle, out_value: *mut c_int) -> c_int {
	guard(|| unsafe {
		if out_value.is_null() {
			return Err(Error::Invalid);
		}
		with_stack(&stack, |s| {
			*out_value = if s.can_redo() { 1 } else { 0 };
			Ok(())
		})
	})
}

/// Write the total history row count (`oakundo_undostack_count`).
pub fn undostack_count(stack: CHandle, out_count: *mut i64) -> c_int {
	guard(|| unsafe {
		if out_count.is_null() {
			return Err(Error::Invalid);
		}
		with_stack(&stack, |s| {
			*out_count = s.command_count();
			Ok(())
		})
	})
}

/// Write the done-command count (`oakundo_undostack_index`).
pub fn undostack_index(stack: CHandle, out_index: *mut i64) -> c_int {
	guard(|| unsafe {
		if out_index.is_null() {
			return Err(Error::Invalid);
		}
		with_stack(&stack, |s| {
			*out_index = s.done_count();
			Ok(())
		})
	})
}

/// Two-stage label getter for the row at `row`: returns the required
/// size (including NUL), or an error code; copies (truncating) when a
/// buffer is supplied (`oakundo_undostack_command_text`).
pub fn undostack_command_text(stack: CHandle, row: i64, buf: *mut c_char, buf_size: c_int) -> c_int {
	let result = catch_unwind(AssertUnwindSafe(|| -> Result<i32> {
		with_stack(&stack, |s| {
			if row < 0 || row >= s.command_count() {
				return Err(Error::NotFound);
			}
			let name = s.command_name(row)?;
			let required = (name.len() + 1) as i32;
			if !buf.is_null() && buf_size > 0 {
				let copy_len = name.len().min((buf_size as usize).saturating_sub(1));
				let bytes = name.as_bytes();
				// SAFETY: `buf` points to `buf_size` writable bytes and we
				// write at most `copy_len` (+ one NUL) of them.
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
		Err(_) => OAKUNDO_E_FAILED,
	}
}

/// Write whether the row at `row` is done (`oakundo_undostack_command_is_done`).
pub fn undostack_command_is_done(stack: CHandle, row: i64, out_value: *mut c_int) -> c_int {
	guard(|| unsafe {
		if out_value.is_null() {
			return Err(Error::Invalid);
		}
		with_stack(&stack, |s| {
			*out_value = if s.command_is_done(row)? { 1 } else { 0 };
			Ok(())
		})
	})
}
