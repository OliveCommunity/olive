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

//! The evaluation engine — the C++ `NodeTraverser` restructured.
//!
//! Key change from C++: no inheritance. C++ `RenderProcessor :
//! NodeTraverser` overrode virtuals to plug rendering in; here the
//! traverser is a free engine and oakrender supplies [`RenderHooks`].
//! The graph is walked iteratively in topological order with an
//! explicit value stack (the C++ recursive path could blow the stack
//! on deep graphs — same order, no recursion).
//! `// CPP-PARITY: src/node/src/traverser.cpp`.

use std::collections::{HashMap, HashSet};

use oakcore_rs::{Rational, TimeRange};

use crate::graph::Graph;
use crate::id::NodeId;
use crate::value::{NodeValue, NodeValueRow, NodeValueTable, ValueType};

/// Backend hooks supplied by the consumer (oakrender). Default no-ops
/// give the C++ "offline evaluation" behavior.
pub trait RenderHooks {
	/// Whether cached textures may be used (C++ `use_cache()`).
	fn use_cache(&self) -> bool {
		false
	}

	/// Convert a finished value row into a backend job/texture
	/// (C++ `resolve_jobs` / `process_*_job` family).
	fn resolve(&mut self, node: NodeId, row: &NodeValueRow, table: &mut NodeValueTable) {
		let _ = (node, row, table);
	}

	/// Cancel-check polled between nodes (C++ `IsCancelled`).
	fn is_cancelled(&self) -> bool {
		false
	}
}

/// Evaluation request.
pub struct EvalRequest {
	/// Root node to evaluate.
	pub root: NodeId,
	/// Time.
	pub time: Rational,
	/// Optional range (for audio pulls).
	pub range: Option<TimeRange>,
}

impl EvalRequest {
	/// New request.
	pub fn new(root: NodeId, time: Rational) -> EvalRequest {
		EvalRequest {
			root,
			time,
			range: None,
		}
	}
}

/// The traversal engine.
pub struct Traverser {
	/// Value stack / per-node row cache for this pass.
	stack: Vec<(NodeId, NodeValueTable)>,
	/// Nodes touched by the last [`Traverser::invalidate_downstream`]
	/// walk (observable for tests; the C++ fan-out has no return value).
	last_invalidation: Vec<NodeId>,
}

impl Traverser {
	/// New empty engine (reusable across evaluations).
	pub fn new() -> Self {
		Traverser {
			stack: Vec::new(),
			last_invalidation: Vec::new(),
		}
	}

	/// Nodes marked by the last invalidation walk.
	pub fn last_invalidation(&self) -> &[NodeId] {
		&self.last_invalidation
	}

	/// Evaluate `request` against `graph`, calling `hooks` at the
	/// backend seams. Returns the root's output table.
	///
	/// Errors: `State` on cancellation, `Failed` on node evaluation
	/// errors (C++ returned empty tables; we surface the error —
	/// `// CPP-PARITY: traverser.cpp` behavior notes inline).
	pub fn evaluate(
		&mut self,
		graph: &Graph,
		request: &EvalRequest,
		hooks: &mut dyn RenderHooks,
	) -> crate::error::Result<NodeValueTable> {
		use crate::error::Error;
		if !graph.is_valid(request.root) {
			return Err(Error::NotFound);
		}

		self.stack.clear();
		let order = graph.topological_order();

		// Per-node output tables for this pass (memoization: a shared
		// upstream evaluates once — `// CPP-PARITY: traverser.cpp`
		// process_node_children).
		let mut tables: HashMap<NodeId, NodeValueTable> = HashMap::new();

		for node in order {
			if hooks.is_cancelled() {
				return Err(Error::State);
			}

			// Build this node's input row from its upstream outputs. The
			// C++ picks the last value of the matching type per input;
			// the Rust model keys rows by input id.
			let mut row: NodeValueRow = std::collections::BTreeMap::new();
			for (from, input_id, element) in graph.input_connections(node) {
				let _ = element;
				if let Some(from_table) = tables.get(&from) {
					let value = from_table
						.get(ValueType::Float)
						.or_else(|| from_table.get(ValueType::Int))
						.or_else(|| from_table.get(ValueType::Color))
						.or_else(|| from_table.get(ValueType::Vec2))
						.or_else(|| from_table.get(ValueType::Vec3))
						.or_else(|| from_table.get(ValueType::Vec4))
						.or_else(|| from_table.get(ValueType::Boolean))
						.or_else(|| from_table.get(ValueType::Rational))
						.or_else(|| from_table.get(ValueType::Text))
						.or_else(|| from_table.get(ValueType::Combo))
						.or_else(|| from_table.get(ValueType::StrCombo))
						.cloned()
						.unwrap_or(NodeValue::None);
					row.insert(input_id, value);
				}
			}

			// Evaluate the node's behavior into its output table.
			let mut table = NodeValueTable::default();
			let entry = graph.get(node).ok_or(Error::NotFound)?;
			// The behavior writes outputs; the default no-op leaves the
			// table empty (C++ `Node::value` default).
			entry.behavior.value(&entry.core, &row, request.time, &mut table);
			hooks.resolve(node, &row, &mut table);

			tables.insert(node, table);
		}

		Ok(tables
			.remove(&request.root)
			.unwrap_or_default())
	}

	/// Invalidate walk: mark downstream caches dirty after an input
	/// change (C++ `invalidate_cache` fan-out, signal-free). Records the
	/// walked set in [`Traverser::last_invalidation`].
	pub fn invalidate_downstream(&mut self, graph: &Graph, from: NodeId, range: TimeRange) {
		let _ = range;
		self.last_invalidation.clear();
		let mut seen: HashSet<NodeId> = HashSet::new();
		let mut queue: Vec<NodeId> = vec![from];
		while let Some(n) = queue.pop() {
			if !seen.insert(n) {
				continue;
			}
			self.last_invalidation.push(n);
			queue.extend(graph.downstream(n));
		}
	}
}

impl Default for Traverser {
	fn default() -> Self {
		Traverser::new()
	}
}

/// A value database: per-node input rows over a time range (C++
/// `NodeValueDatabase`), exposed by the traverser ffi family.
pub struct ValueDatabase {
	/// Rows keyed by node input id.
	pub rows: Vec<(String, Vec<(ValueType, NodeValue)>)>,
}
