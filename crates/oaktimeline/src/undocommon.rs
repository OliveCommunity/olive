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

//! Shared node-removal helpers and the command wrapper
//! (`src/timeline/src/timelineundocommon.h`). Since the single-lib
//! unification, commands are `oakundo::undocommand::UndoCommand` values
//! (the oakundo C ABI and the oaknode C ABI are both gone); node-graph
//! access goes directly through the oaknode Rust domain
//! ([`crate::util::NodeRef`] + the project's graph arena).
//!
//! `CHandleCommandWrapper` in C++ subclasses `olive::UndoCommand` to wrap a
//! raw `OakUndoCommand`; the Rust equivalent is the crate's own commands
//! boxed through [`box_command`], so the wrapper is gone.

use std::ffi::c_void;

use oaknode::graph::NodeEntry;
use oakundo::undocommand::{OakUndoCommandVtable, UndoCommand};

use crate::util::{block_add_to_graph, block_remove_from_graph, NodeRef};

/// An empty (all callbacks absent) command — the failure-path result of
/// the deleted oaknode remove-command factory (an empty command handle).
pub(crate) fn empty_command() -> UndoCommand {
	UndoCommand::from_vtable(
		OakUndoCommandVtable {
			redo: None,
			undo: None,
			free_fn: None,
		},
		std::ptr::null_mut(),
	)
}

/// `oaknode_command_create_remove_node` — a command that detaches a node
/// from the project graph on `redo` (the detached entry is owned by this
/// command) and re-inserts it, identity-preserving, on `undo`. The C++
/// equivalent runs `node->setParent(&memory_manager_)` / the reverse.
struct RemoveNodeCommand {
	/// The node to remove.
	node: NodeRef,
	/// Arena entry detached on `redo`, owned here until `undo`.
	entry: Option<NodeEntry>,
}

impl RemoveNodeCommand {
	/// Construct from the node to remove.
	fn new(node: NodeRef) -> Self {
		Self { node, entry: None }
	}
}

impl Command for RemoveNodeCommand {
	/// `redo`: detach the node from the graph.
	fn redo(&mut self) {
		if self.entry.is_none() {
			self.entry = block_remove_from_graph(&self.node);
		}
	}

	/// `undo`: re-insert the node into the graph at its original slot.
	fn undo(&mut self) {
		if let Some(entry) = self.entry.take() {
			block_add_to_graph(&self.node, Some(entry));
		}
	}
}

/// `node_can_be_removed(Node)`: true when the node has no output
/// connections (timelineundocommon.h).
pub fn node_can_be_removed(node: &NodeRef) -> bool {
	let p = node.project.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
	p.graph.output_connections(node.id).is_empty()
}

/// `node_can_be_removed(Block)`: the block is checked through its
/// generic-node view.
pub fn block_can_be_removed(block: &NodeRef) -> bool {
	node_can_be_removed(block)
}

/// `create_remove_command(Node)` — `oaknode_command_create_remove_node`.
pub fn create_remove_command(node: &NodeRef) -> UndoCommand {
	box_command(RemoveNodeCommand::new(node.clone()))
}

/// `create_remove_command(Block)`.
pub fn create_block_remove_command(block: &NodeRef) -> UndoCommand {
	create_remove_command(block)
}

/// `create_and_run_remove_command(Node)` — create then `redo_now`.
pub fn create_and_run_remove_command(node: &NodeRef) -> UndoCommand {
	let mut command = create_remove_command(node);
	command.redo_now();
	command
}

/// `create_and_run_remove_command(Block)`.
pub fn create_and_run_block_remove_command(block: &NodeRef) -> UndoCommand {
	create_and_run_remove_command(block)
}

/// `free_command_handle`: release and null a command; NULL no-op. With the
/// `UndoCommand` value type, "freeing" is overwriting the pointee with an
/// empty command (the old value drops, running its `free_fn`).
pub fn free_command_handle(command: *mut UndoCommand) {
	if command.is_null() {
		return;
	}
	// SAFETY: the caller passes a valid pointer; overwriting drops the old
	// value and installs the no-op command.
	unsafe { *command = empty_command() };
}

/// Trait implemented by every timeline undo command. The crate's commands
/// are boxed into an oakundo vtable command via [`box_command`]; the
/// command's `redo_now`/`undo_now` dispatch to these callbacks.
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
	// command owns it.
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
	// SAFETY: the command owns this box; dropping it frees the command.
	unsafe { drop(Box::from_raw(userdata as *mut T)) };
}

/// Box a command into an oakundo vtable command value. The command owns
/// the boxed `T`; `redo_now`/`undo_now` dispatch to `T::redo`/`T::undo`,
/// and dropping the command drops `T`.
pub(crate) fn box_command<T: Command + 'static>(cmd: T) -> UndoCommand {
	let userdata = Box::into_raw(Box::new(cmd)) as *mut c_void;
	let vtable = OakUndoCommandVtable {
		redo: Some(redo_cb::<T>),
		undo: Some(undo_cb::<T>),
		free_fn: Some(free_cb::<T>),
	};
	// `from_vtable` copies the vtable and takes ownership of `userdata`.
	UndoCommand::from_vtable(vtable, userdata)
}

/// `MultiUndoCommand` — a command that runs several child commands in order
/// on `redo` and in reverse on `undo` (timelineundocommon.h
/// `MultiUndoCommand`). Children are boxed `Command`s; the whole group wraps
/// into a single oakundo vtable command via [`box_command`].
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

	/// Wrap as an oakundo vtable command value.
	pub fn to_command(self) -> UndoCommand {
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
