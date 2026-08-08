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

//! The graph arena: node storage, edges, traversal order.
//!
//! Replaces the C++ pointer web (`Node::parent_`, output_connections_)
//! with a slab arena + edge set. All structural mutation goes through
//! `&mut Graph` methods; evaluation takes `&Graph`.

use std::collections::BTreeSet;

use crate::id::NodeId;
use crate::node::{NodeBehavior, NodeCore};

/// One arena slot.
pub struct NodeEntry {
	/// Shared data.
	pub core: NodeCore,
	/// Polymorphic behavior.
	pub behavior: Box<dyn NodeBehavior>,
	/// Generation for stale-id detection.
	pub generation: u32,
	/// True when the slot is free.
	pub vacant: bool,
}

/// A directed edge: `from` node's output feeds `to` node's `input`
/// (element for array inputs).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Edge {
	/// Source node.
	pub from: NodeId,
	/// Destination node.
	pub to: NodeId,
	/// Destination input id hash + element (string stored arena-side).
	pub input_key: u64,
	/// Array element (-1 = scalar input).
	pub element: i32,
}

/// The node graph. Nodes are owned here, exclusively.
pub struct Graph {
	entries: Vec<NodeEntry>,
	free_list: Vec<u32>,
	edges: BTreeSet<Edge>,
	/// Arena-side storage for edge input ids (key = hash into this map).
	input_names: std::collections::HashMap<u64, String>,
}

impl Graph {
	/// Empty graph.
	pub fn new() -> Self {
		todo!()
	}

	/// Insert a node; returns its id.
	pub fn add_node(&mut self, core: NodeCore, behavior: Box<dyn NodeBehavior>) -> NodeId {
		todo!()
	}

	/// Remove a node and all its edges (C++: ~Node + set_parent(null)
	/// + disconnect_all side effects — see `// CPP-PARITY: node.cpp`).
	pub fn remove_node(&mut self, id: NodeId) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}

	/// Validated access; `None` for stale ids.
	pub fn get(&self, id: NodeId) -> Option<&NodeEntry> {
		todo!()
	}

	/// Mutable access; `None` for stale ids.
	pub fn get_mut(&mut self, id: NodeId) -> Option<&mut NodeEntry> {
		todo!()
	}

	/// Connect `from`'s output to `to.input[element]`
	/// (C++ `Node::connect_edge` incl. cycle rejection).
	pub fn connect(&mut self, from: NodeId, to: NodeId, input: &str, element: i32) -> crate::error::Result<()> {
		todo!()
	}

	/// Disconnect one edge (no-op when absent).
	pub fn disconnect(&mut self, from: NodeId, to: NodeId, input: &str, element: i32) {
		todo!()
	}

	/// Nodes directly upstream of `id` (evaluation order helper).
	pub fn upstream(&self, id: NodeId) -> Vec<NodeId> {
		todo!()
	}

	/// Nodes directly downstream of `id` (invalidation fan-out).
	pub fn downstream(&self, id: NodeId) -> Vec<NodeId> {
		todo!()
	}

	/// Topological order from sources to sinks (Kahn; cycles are
	/// rejected at connect time so this cannot fail).
	pub fn topological_order(&self) -> Vec<NodeId> {
		todo!()
	}
}
