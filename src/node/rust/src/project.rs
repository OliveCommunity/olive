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

//! Project: owns the graph, the folder tree, settings, and the undo
//! stack binding. Mirrors C++ `olive::Project`.

use std::sync::{Arc, Mutex};

use crate::graph::Graph;
use crate::id::NodeId;

/// The project. Shared ownership via `Arc<Mutex<Project>>` is what the
/// public C ABI handles box.
pub struct Project {
	/// The node graph (all nodes incl. sequences).
	pub graph: Graph,
	/// Root folder of the bin tree.
	pub root: NodeId,
	/// Arbitrary project settings (C++ `setting_` map).
	pub settings: std::collections::HashMap<String, String>,
	/// Project file path (empty = unsaved).
	pub filename: String,
	/// Dirty flag.
	pub modified: bool,
	/// Session UUID (cache filename base).
	pub uuid: String,
}

impl Project {
	/// New empty project (no root folder until [`Project::initialize`]).
	pub fn new() -> Arc<Mutex<Project>> {
		todo!()
	}

	/// Create the root folder and default state (C++ `initialize()`).
	pub fn initialize(&mut self) -> crate::error::Result<()> {
		todo!()
	}

	/// Remove all nodes and reset to a blank project (C++ `clear()`).
	pub fn clear(&mut self) -> crate::error::Result<()> {
		todo!()
	}

	/// Deep-copy the whole project for background render isolation
	/// (replaces oakrender's C++ ProjectCopier: the copy happens here,
	/// inside the module that owns the data — see M-series note on
	/// render→node decoupling).
	pub fn deep_copy(&self) -> crate::error::Result<Arc<Mutex<Project>>> {
		todo!()
	}

	/// Incremental sync of a deep copy after edits (C++
	/// ProjectCopier::queue_update semantics): applies the recorded
	/// change set to `copy`.
	pub fn sync_copy(&self, copy: &mut Project, changes: &[ChangeRecord]) -> crate::error::Result<()> {
		todo!()
	}
}

/// A recorded structural change for incremental copy sync (replaces
/// the C++ signal-driven copier updates).
#[derive(Clone, Debug)]
pub enum ChangeRecord {
	/// A node was added.
	NodeAdded(NodeId),
	/// A node was removed (id of the copy-side node).
	NodeRemoved(NodeId),
	/// An edge change.
	EdgeChanged {
		/// Source.
		from: NodeId,
		/// Destination.
		to: NodeId,
		/// Input id.
		input: String,
		/// Element.
		element: i32,
		/// Connected or disconnected.
		connected: bool,
	},
	/// A parameter value changed.
	ValueChanged {
		/// Node.
		node: NodeId,
		/// Input id.
		input: String,
		/// Element.
		element: i32,
	},
}
