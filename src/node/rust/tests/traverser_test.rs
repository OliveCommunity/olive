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

//! Traverser (evaluation engine) contract tests.

/// A linear chain of test nodes evaluates in topological order and the
/// root table contains the expected value (engine order, not node
/// math, is under test).
#[test]
fn linear_chain_evaluation_order() {
	todo!()
}

/// Diamond graph: shared upstream evaluates once (memoization),
/// both branches see the same table identity.
#[test]
fn diamond_evaluates_shared_node_once() {
	todo!()
}

/// Cancellation: hook returning cancelled stops evaluation with
/// E_STATE and no partial result is returned.
#[test]
fn cancellation_stops_evaluation() {
	todo!()
}

/// Deep chain (10k nodes) completes without recursion (stack-safe;
/// the C++ recursive path could overflow).
#[test]
fn deep_graph_is_iterative() {
	todo!()
}

/// invalidate_downstream marks exactly the downstream caches and only
/// once per node on a diamond (signal-free fan-out parity).
#[test]
fn invalidation_fanout() {
	todo!()
}
