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

//! Math node (C++ `src/node/src/math/math/math.{h,cpp}`,
//! `olive::MathNode`); behavior shared with other binary math nodes
//! lives in [`super::mathbase`] (C++ `MathNodeBase`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Operation/method input id (C++ `k_method_in`). Type: combo;
/// flags: not-connectable, not-keyframable; combo strings are the
/// operation names Add/Subtract/Multiply/Divide/Power.
pub const METHOD_INPUT: &str = "method_in";

/// Operand A input id (C++ `k_param_a_in`). Type: float; default
/// `0.0`; properties: `decimalplaces = 8`, `autotrim = true`.
pub const PARAM_A_INPUT: &str = "param_a_in";

/// Operand B input id (C++ `k_param_b_in`). Type: float; default
/// `0.0`; properties: `decimalplaces = 8`, `autotrim = true`.
pub const PARAM_B_INPUT: &str = "param_b_in";

/// Operand C input id (C++ `k_param_c_in`). Declared as a static but
/// never added in the constructor — reserved/unused upstream.
pub const PARAM_C_INPUT: &str = "param_c_in";

/// Math node: applies a binary arithmetic operation to two values.
/// Unit-like — the C++ class has no own data members (inputs live in
/// [`NodeCore`], shared logic in [`super::mathbase`]).
pub struct MathNode;

impl MathNode {
	/// Construct the behavior struct (C++ `MathNode()`; the node's
	/// inputs are wired in [`create`]). Exposed so other nodes can own
	/// a child math node (e.g. `super::opacity` multiplies via one).
	pub fn new() -> Box<MathNode> {
		todo!()
	}
}

impl NodeBehavior for MathNode {
	/// Human-readable name (C++ `name()`): if the node is parented
	/// (i.e. owned as a child of another node) and the current
	/// operation has a name, returns that operation name
	/// (Add/Subtract/Multiply/Divide/Power); otherwise "Math".
	fn name(&self) -> &str {
		todo!()
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.math"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Math]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Perform a mathematical operation between two values."
	}

	/// Localized input names (C++ `retranslate()`): `method_in` ->
	/// "Method", `param_a_in`/`param_b_in` -> "Value"; also sets the
	/// combo strings on `method_in` to the five operation names.
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): pushes both operands into
	/// single-value tables, runs the [`super::mathbase::PairingCalculator`]
	/// heuristic, and if a pairing was found delegates to
	/// [`super::mathbase::MathNodeBase::value_internal`] with the
	/// current operation; otherwise pushes nothing.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Process a span of samples (C++ `process_samples()`): delegates
	/// to [`super::mathbase::MathNodeBase::process_samples_internal`]
	/// with the current operation and the `param_a_in`/`param_b_in`
	/// ids (only used for the sample*number pairing).
	fn process_samples(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		range: oakcore_rs::TimeRange,
		output: &mut crate::value::SampleBuffer,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): delegates to
	/// [`super::mathbase::MathNodeBase::shader_code_internal`] with the
	/// request id (expected `"<op>.<pairing>.<type_a>.<type_b>"`) and
	/// the `param_a_in`/`param_b_in` uniform names.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `MathNode::MathNode()`): adds `method_in` as a
/// not-connectable/not-keyframable combo, and `param_a_in`/
/// `param_b_in` as float inputs defaulting to 0.0 with
/// `decimalplaces = 8` and `autotrim = true`.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_math_node` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.math",
		name: "Math",
		categories: &[Category::Math],
		create,
	});
}
