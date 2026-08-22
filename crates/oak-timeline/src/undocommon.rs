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
//! unification, commands are `oak_undo::undocommand::UndoCommand` values
//! (the oakundo C ABI and the oaknode C ABI are both gone); node-graph
//! access goes directly through the oaknode Rust domain
//! ([`crate::util::NodeRef`] + the project's graph arena).
//!
//! `CHandleCommandWrapper` in C++ subclasses `olive::UndoCommand` to wrap a
//! raw `OakUndoCommand`; the Rust equivalent is the crate's own commands
//! boxed as trait objects through [`box_command`] (they implement
//! [`Command`] = `oak_undo::undocommand::Command`), so the wrapper is gone.

use oak_node::graph::NodeEntry;
use oak_undo::undocommand::UndoCommand;

use crate::util::{block_add_to_graph, block_remove_from_graph, NodeRef};

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

/// Trait implemented by every timeline undo command (re-export of the
/// oakundo command trait). The crate's commands are boxed into an
/// [`UndoCommand`] via [`box_command`]; the command's
/// `redo_now`/`undo_now` dispatch to these callbacks.
pub use oak_undo::undocommand::Command;

/// Box a command into an oakundo command value. The command owns the
/// boxed `T`; `redo_now`/`undo_now` dispatch to `T::redo`/`T::undo`, and
/// dropping the command drops `T`.
pub(crate) fn box_command<T: Command + 'static>(cmd: T) -> UndoCommand {
	UndoCommand::new(cmd)
}

/// `MultiUndoCommand` — a command that runs several child commands in order
/// on `redo` and in reverse on `undo` (timelineundocommon.h
/// `MultiUndoCommand`). Children are boxed `Command`s; the whole group wraps
/// into a single oakundo command via [`box_command`].
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

	/// Wrap as an oakundo command value.
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
