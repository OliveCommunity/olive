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

//! Graph arena contract tests (graph.rs / id.rs).

/// add/remove nodes: ids are generation-checked; a stale NodeId fails
/// `get` instead of aliasing a reused slot.
#[test]
fn generational_ids_reject_stale() {
	todo!()
}

/// connect/disconnect round-trip; disconnect of a missing edge is a
/// no-op; duplicate connect is rejected (C++ behavior).
#[test]
fn edge_lifecycle() {
	todo!()
}

/// Cycle rejection: connecting A→B→C→A fails with E_STATE and leaves
/// the graph unchanged (C++ connect_edge cycle check).
#[test]
fn cycle_rejection() {
	todo!()
}

/// Topological order: every edge goes earlier→later; empty graph
/// yields empty order; diamond graph has a valid (stable) order.
#[test]
fn topological_order() {
	todo!()
}

/// remove_node cascades: all edges to/from the node disappear and
/// downstream invalidation fires exactly once (C++ ~Node parity —
/// `// CPP-PARITY: node.cpp` disconnect fan-out).
#[test]
fn remove_node_cascades() {
	todo!()
}

/// Upstream/downstream queries on a diamond graph.
#[test]
fn adjacency_queries() {
	todo!()
}
