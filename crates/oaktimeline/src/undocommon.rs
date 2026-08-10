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

//! Shared node-removal helpers and the C-command wrapper
//! (`src/timeline/src/timelineundocommon.h`). All node-graph access goes
//! through the oaknode C ABI (`bridge::node`) and undo through
//! `bridge::undo`.
//!
//! `CHandleCommandWrapper` in C++ subclasses `olive::UndoCommand` to wrap a
//! raw `OakUndoCommand`; the Rust equivalent holds a command `CHandle` and
//! forwards `redo`/`undo` to `bridge::undo::oakundo_command_redo_now/undo_now`.

use std::ffi::c_void;

use crate::bridge::undo::{oakundo_command_free, oakundo_command_init, oakundo_command_redo_now, oakundo_command_undo_now};
use crate::bridge::node::{
	oaknode_block_as_node, oaknode_command_create_remove_node, oaknode_node_output_connection_count,
};
use crate::handle::CHandle;

/// `node_can_be_removed(Node)`: true when the node has no output
/// connections (timelineundocommon.h).
pub fn node_can_be_removed(node: CHandle) -> bool {
	let mut count = 0;
	// SAFETY: `count` is a valid out pointer.
	let _ = unsafe { oaknode_node_output_connection_count(node, &mut count) };
	count == 0
}

/// `node_can_be_removed(Block)`: delegates via `oaknode_block_as_node`.
pub fn block_can_be_removed(block: CHandle) -> bool {
	node_can_be_removed(unsafe { oaknode_block_as_node(block) })
}

/// `create_remove_command(Node)` — `oaknode_command_create_remove_node`.
pub fn create_remove_command(node: CHandle) -> CHandle {
	unsafe { oaknode_command_create_remove_node(node) }
}

/// `create_remove_command(Block)` — via `oaknode_block_as_node`.
pub fn create_block_remove_command(block: CHandle) -> CHandle {
	create_remove_command(unsafe { oaknode_block_as_node(block) })
}

/// `create_and_run_remove_command(Node)` — create then `redo_now`.
pub fn create_and_run_remove_command(node: CHandle) -> CHandle {
	let command = create_remove_command(node);
	// SAFETY: `command` is a valid handle returned by the bridge.
	let _ = unsafe { oakundo_command_redo_now(command.clone()) };
	command
}

/// `create_and_run_remove_command(Block)` — via `oaknode_block_as_node`.
pub fn create_and_run_block_remove_command(block: CHandle) -> CHandle {
	create_and_run_remove_command(unsafe { oaknode_block_as_node(block) })
}

/// `free_command_handle`: release and null a command handle; NULL/empty
/// no-op.
pub fn free_command_handle(command: *mut CHandle) {
	// SAFETY: passed through to the bridge, which handles NULL.
	unsafe { oakundo_command_free(command) };
}

/// Trait implemented by every timeline undo command. The crate's commands
/// are boxed into an oakundo vtable command via [`box_command`]; the bridge's
/// `redo_now`/`undo_now` dispatch to these callbacks.
pub trait Command {
	/// Apply the change.
	fn redo(&mut self);
	/// Revert the change.
	fn undo(&mut self);
}

/// Generic `redo` callback forwarding to [`Command::redo`].
///
/// # Safety
/// `userdata` must be the `Box<T>` produced by [`box_command`].
unsafe extern "C" fn redo_cb<T: Command>(userdata: *mut c_void) {
	if userdata.is_null() {
		return;
	}
	// SAFETY: box_command allocated a `Box<T>`; still alive because the
	// command handle owns it.
	(unsafe { &mut *(userdata as *mut T) }).redo();
}

/// Generic `undo` callback forwarding to [`Command::undo`].
///
/// # Safety
/// `userdata` must be the `Box<T>` produced by [`box_command`].
unsafe extern "C" fn undo_cb<T: Command>(userdata: *mut c_void) {
	if userdata.is_null() {
		return;
	}
	// SAFETY: as `redo_cb`.
	(unsafe { &mut *(userdata as *mut T) }).undo();
}

/// Generic `free` callback dropping the boxed command.
///
/// # Safety
/// `userdata` must be the `Box<T>` produced by [`box_command`].
unsafe extern "C" fn free_cb<T: Command>(userdata: *mut c_void) {
	if userdata.is_null() {
		return;
	}
	// SAFETY: the handle owns this box; destroying the handle drops it.
	unsafe { drop(Box::from_raw(userdata as *mut T)) };
}

/// Box a command into an oakundo vtable command handle. The handle owns the
/// boxed `T`; `redo_now`/`undo_now` dispatch to `T::redo`/`T::undo`, and
/// freeing the handle drops `T`.
pub(crate) fn box_command<T: Command + 'static>(cmd: T) -> CHandle {
	let userdata = Box::into_raw(Box::new(cmd)) as *mut c_void;
	let vtable = crate::bridge::undo::OakUndoCommandVtable {
		redo: Some(redo_cb::<T>),
		undo: Some(undo_cb::<T>),
		free_fn: Some(free_cb::<T>),
	};
	// SAFETY: `vtable` and `userdata` remain valid for the command's lifetime
	// (the bridge copies the vtable and owns `userdata`).
	unsafe { oakundo_command_init(&vtable, userdata) }
}

/// `CHandleCommandWrapper` — an oakundo vtable command exposed as a
/// timeline-level command. `redo`/`undo` forward to `bridge::undo`;
/// dropping frees the handle.
pub struct CHandleCommandWrapper {
	/// Wrapped command handle.
	command: CHandle,
}

impl CHandleCommandWrapper {
	/// Construct over an owned command handle.
	pub fn new(command: CHandle) -> Self {
		Self { command }
	}

	/// Whether the wrapped command is non-empty.
	pub fn is_valid(&self) -> bool {
		!self.command.is_null()
	}

	/// `redo`: forward to `oakundo_command_redo_now`.
	pub fn redo(&mut self) {
		if !self.command.is_null() {
			// SAFETY: `self.command` is a valid handle while it is non-empty.
			let _ = unsafe { oakundo_command_redo_now(self.command.clone()) };
		}
	}

	/// `undo`: forward to `oakundo_command_undo_now`.
	pub fn undo(&mut self) {
		if !self.command.is_null() {
			// SAFETY: `self.command` is a valid handle while it is non-empty.
			let _ = unsafe { oakundo_command_undo_now(self.command.clone()) };
		}
	}
}

impl Drop for CHandleCommandWrapper {
	fn drop(&mut self) {
		if !self.command.is_null() {
			// SAFETY: `self.command` is a valid owned handle.
			unsafe { oakundo_command_free(&mut self.command) };
		}
	}
}

/// `MultiUndoCommand` — a command that runs several child commands in order
/// on `redo` and in reverse on `undo` (timelineundocommon.h
/// `MultiUndoCommand`). Children are boxed `Command`s; the whole group wraps
/// into a single oakundo vtable handle via [`box_command`].
pub struct MultiUndoCommand {
  /// Child commands, run in order on `redo`.
  commands: Vec<Box<dyn Command>>,
}

impl MultiUndoCommand {
  /// A new, empty multi command.
  pub fn new() -> Self {
    Self {
      commands: Vec::new(),
    }
  }

  /// Append a child command; ownership transfers to the group.
  pub fn add_child(&mut self, command: Box<dyn Command>) {
    self.commands.push(command);
  }

  /// Whether the group has any children yet.
  pub fn empty(&self) -> bool {
    self.commands.is_empty()
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Command for MultiUndoCommand {
  /// `redo`: run children in order.
  fn redo(&mut self) {
    for c in self.commands.iter_mut() {
      c.redo();
    }
  }

  /// `undo`: run children in reverse.
  fn undo(&mut self) {
    for c in self.commands.iter_mut().rev() {
      c.undo();
    }
  }
}


