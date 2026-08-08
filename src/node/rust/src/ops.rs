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

//! Free functions replacing the C++ `Node` static methods
//! (COVERAGE.md §6/§9).

use oakcore_rs::TimeRange;

use crate::graph::Graph;
use crate::id::NodeId;
use crate::node::Category;

/// Human-readable category name (C++ `Node::get_category_name`).
pub fn category_name(c: Category) -> &'static str {
	todo!()
}

/// Copy inputs (and optionally connections) between two nodes
/// (C++ `Node::copy_inputs` / `copy_input` /
/// `copy_values_of_element`).
pub fn copy_inputs(
	graph: &mut Graph,
	src: NodeId,
	dst: NodeId,
	include_connections: bool,
) -> crate::error::Result<()> {
	todo!()
}

/// Copy a node with its upstream dependency subgraph
/// (C++ `copy_dependency_graph` / `copy_node_in_graph` /
/// `copy_node_and_dependency_graph_minus_items`). Returns the new
/// node ids (source order). Undo packaging happens at the caller via
/// bridge::undo.
pub fn copy_subgraph(
	graph: &mut Graph,
	nodes: &[NodeId],
	exclude_items: bool,
) -> crate::error::Result<Vec<NodeId>> {
	todo!()
}

/// Transform a time range from one node's frame of reference to
/// another's along the connection path (C++ `Node::transform_time_to`).
pub fn transform_time_to(
	graph: &Graph,
	time: TimeRange,
	from: NodeId,
	to: NodeId,
) -> crate::error::Result<TimeRange> {
	todo!()
}

/// Undo-command display strings (C++
/// `get_connect_command_string`/`get_disconnect_command_string`).
pub fn connect_command_string(output: NodeId, input: NodeId, input_id: &str) -> String {
	todo!()
}

/// See [`connect_command_string`].
pub fn disconnect_command_string(output: NodeId, input: NodeId, input_id: &str) -> String {
	todo!()
}

/// Set a keyframed/standard value at a time, returning the undo
/// command (C++ `Node::set_value_at_time` static; command creation
/// via bridge::undo).
pub fn set_value_at_time_command(
	graph: &mut Graph,
	node: NodeId,
	input: &str,
	element: i32,
	time: oakcore_rs::Rational,
	value: &crate::value::NodeValue,
) -> crate::error::Result<crate::handle::CHandle> {
	todo!()
}
