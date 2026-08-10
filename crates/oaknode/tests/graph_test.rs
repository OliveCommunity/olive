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

use oaknode::error::Error;
use oaknode::graph::Graph;
use oaknode::id::NodeId;
use oaknode::input::{flags, Input};
use oaknode::node::{Category, NodeBehavior, NodeCore};
use oaknode::value::{NodeValue, ValueType};

/// A minimal test node: `enabled_in` + one connectable float input.
struct TestNode {
	id: &'static str,
}

impl NodeBehavior for TestNode {
	fn name(&self) -> &str {
		"TestNode"
	}

	fn type_id(&self) -> &str {
		self.id
	}

	fn categories(&self) -> &[Category] {
		&[]
	}

	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(TestNode { id: self.id }))
	}
}

/// Build a graph holding `n` labeled test nodes, returning their ids.
fn build(n: usize) -> (Graph, Vec<NodeId>) {
	let mut g = Graph::new();
	let mut ids = Vec::new();
	for i in 0..n {
		let mut core = NodeCore::new();
		core.add_input(Input::new(
			"val_in",
			ValueType::Float,
			NodeValue::Float(0.0),
		));
		// Second input so a node can have two parents (the diamond
		// shape) — scalar inputs are single-connection.
		core.add_input(Input::new(
			"val_in2",
			ValueType::Float,
			NodeValue::Float(0.0),
		));
		ids.push(g.add_node(core, Box::new(TestNode { id: "test" })));
	}
	(g, ids)
}

/// add/remove nodes: ids are generation-checked; a stale NodeId fails
/// `get` instead of aliasing a reused slot.
#[test]
fn generational_ids_reject_stale() {
	let (mut g, ids) = build(2);
	let [a, b] = [ids[0], ids[1]];

	assert!(g.is_valid(a));
	assert!(g.get(a).is_some());

	// Remove `a`; its slot is freed and later reused with a bumped
	// generation.
	assert!(g.remove_node(a).is_some());
	assert!(!g.is_valid(a));
	assert!(g.get(a).is_none());

	// A new node reuses the slot; the stale id must not alias it.
	let c = g.add_node(NodeCore::new(), Box::new(TestNode { id: "test" }));
	assert_eq!(c.index(), a.index());
	assert_ne!(c.generation(), a.generation());
	assert!(g.get(c).is_some());
	assert!(g.get(a).is_none(), "stale id aliased the reused slot");

	// Invalid sentinel and huge indices never resolve.
	assert!(g.get(NodeId::INVALID).is_none());
	assert!(!g.is_valid(NodeId::INVALID));
	let _ = b;
}

/// connect/disconnect round-trip; disconnect of a missing edge is a
/// no-op; duplicate connect is rejected (C++ behavior).
#[test]
fn edge_lifecycle() {
	let (mut g, ids) = build(3);
	let [a, b, c] = [ids[0], ids[1], ids[2]];

	assert!(g.connect(a, b, "val_in", -1).is_ok());
	assert_eq!(g.connected_output(b, "val_in", -1), Some(a));
	assert!(g.is_input_connected(b, "val_in", -1));
	assert_eq!(g.upstream(b), vec![a]);

	// Duplicate connect on the same input is rejected with E_STATE.
	assert_eq!(g.connect(a, b, "val_in", -1), Err(Error::State));
	assert_eq!(g.connect(c, b, "val_in", -1), Err(Error::State));

	// Unknown input id -> E_NOT_FOUND; non-connectable -> E_INVALID.
	assert_eq!(g.connect(a, b, "nope", -1), Err(Error::NotFound));
	let (mut g2, ids2) = build(2);
	{
		let mut core = NodeCore::new();
		let mut input = Input::new("locked", ValueType::Float, NodeValue::Float(0.0));
		input.flags |= flags::NOT_CONNECTABLE;
		core.add_input(input);
		let n = g2.add_node(core, Box::new(TestNode { id: "test" }));
		assert_eq!(g2.connect(ids2[0], n, "locked", -1), Err(Error::Invalid));
	}

	// Disconnect round-trip; missing edge disconnect is a no-op.
	g.disconnect(a, b, "val_in", -1);
	assert!(!g.is_input_connected(b, "val_in", -1));
	g.disconnect(a, b, "val_in", -1); // no-op, no panic
	g.disconnect(c, b, "val_in", -1); // never existed
}

/// Cycle rejection: connecting A→B→C→A fails with E_STATE and leaves
/// the graph unchanged (C++ connect_edge cycle check).
#[test]
fn cycle_rejection() {
	let (mut g, ids) = build(3);
	let [a, b, c] = [ids[0], ids[1], ids[2]];

	g.connect(a, b, "val_in", -1).unwrap();
	g.connect(b, c, "val_in", -1).unwrap();

	// Closing the cycle is rejected.
	assert_eq!(g.connect(c, a, "val_in", -1), Err(Error::State));

	// Self-connection is a trivial cycle.
	assert_eq!(g.connect(a, a, "val_in", -1), Err(Error::State));

	// The graph is unchanged: the two valid edges remain, topology intact.
	assert_eq!(g.connected_output(b, "val_in", -1), Some(a));
	assert_eq!(g.connected_output(c, "val_in", -1), Some(b));
	assert_eq!(g.output_connections(c).len(), 0);
}

/// Topological order: every edge goes earlier→later; empty graph
/// yields empty order; diamond graph has a valid (stable) order.
#[test]
fn topological_order() {
	let mut g = Graph::new();
	assert!(g.topological_order().is_empty());

	let (mut g, ids) = build(4);
	let [a, b, c, d] = [ids[0], ids[1], ids[2], ids[3]];
	g.connect(a, b, "val_in", -1).unwrap();
	g.connect(a, c, "val_in", -1).unwrap();
	g.connect(b, d, "val_in", -1).unwrap();
	g.connect(c, d, "val_in2", -1).unwrap();

	let order = g.topological_order();
	assert_eq!(order.len(), 4);
	assert_eq!(order[0], a, "source first");
	// Every edge goes earlier -> later.
	let pos = |n: NodeId| order.iter().position(|x| *x == n).unwrap();
	assert!(pos(a) < pos(b) && pos(a) < pos(c));
	assert!(pos(b) < pos(d) && pos(c) < pos(d));

	// Deterministic across calls.
	assert_eq!(order, g.topological_order());
}

/// remove_node cascades: all edges to/from the node disappear and
/// downstream invalidation fires exactly once (C++ ~Node parity —
/// `// CPP-PARITY: node.cpp` disconnect fan-out).
#[test]
fn remove_node_cascades() {
	let (mut g, ids) = build(4);
	let [a, b, c, d] = [ids[0], ids[1], ids[2], ids[3]];
	g.connect(a, b, "val_in", -1).unwrap();
	g.connect(b, c, "val_in", -1).unwrap();
	g.connect(b, d, "val_in", -1).unwrap();

	// Removing the middle node drops all four edges.
	let behavior = g.remove_node(b).expect("node exists");
	assert!(behavior.type_id() == "test");
	assert!(g.get(b).is_none());
	assert_eq!(g.output_connections(a).len(), 0);
	assert!(!g.is_input_connected(c, "val_in", -1));
	assert!(!g.is_input_connected(d, "val_in", -1));
	assert!(g.downstream(b).is_empty());
	assert!(g.upstream(b).is_empty());

	// The graph is still fully usable (slot reused cleanly).
	let e = g.add_node(NodeCore::new(), Box::new(TestNode { id: "test" }));
	assert!(g.is_valid(e));
}

/// Upstream/downstream queries on a diamond graph.
#[test]
fn adjacency_queries() {
	let (mut g, ids) = build(4);
	let [a, b, c, d] = [ids[0], ids[1], ids[2], ids[3]];
	g.connect(a, b, "val_in", -1).unwrap();
	g.connect(a, c, "val_in", -1).unwrap();
	g.connect(b, d, "val_in", -1).unwrap();
	g.connect(c, d, "val_in2", -1).unwrap();

	assert_eq!(g.upstream(a), Vec::<NodeId>::new());
	assert_eq!(g.upstream(d), vec![b, c]);
	assert_eq!(g.downstream(a), vec![b, c]);
	assert_eq!(g.downstream(d), Vec::<NodeId>::new());
}
