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

use oakcore_rs::{Rational, TimeRange};

use crate::graph::Graph;
use crate::id::NodeId;
use crate::value::{NodeValueRow, NodeValueTable};

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

/// The traversal engine.
pub struct Traverser {
	/// Value stack / per-node row cache for this pass.
	stack: Vec<(NodeId, NodeValueTable)>,
}

impl Traverser {
	/// New empty engine (reusable across evaluations).
	pub fn new() -> Self {
		todo!()
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
		todo!()
	}

	/// Invalidate walk: mark downstream caches dirty after an input
	/// change (C++ `invalidate_cache` fan-out, signal-free).
	pub fn invalidate_downstream(&self, graph: &Graph, from: NodeId, range: TimeRange) {
		todo!()
	}
}
