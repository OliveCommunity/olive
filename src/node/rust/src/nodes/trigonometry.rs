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

//! Trigonometry node (C++
//! `src/node/src/math/trigonometry/trigonometry.{h,cpp}`,
//! `olive::TrigonometryNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Operation/method input id (C++ `k_method_in`). Type: combo;
/// flags: not-connectable, not-keyframable; combo strings: Sine,
/// Cosine, Tangent, Inverse Sine, Inverse Cosine, Inverse Tangent,
/// Hyperbolic Sine, Hyperbolic Cosine, Hyperbolic Tangent.
pub const METHOD_INPUT: &str = "method_in";

/// Operand input id (C++ `k_x_in`). Type: float; default `0.0`.
pub const X_INPUT: &str = "x_in";

/// Trigonometry operation (C++ private `TrigonometryNode::Operation`;
/// discriminants are the `method_in` combo indices).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operation {
	/// `sin(x)` (C++ `k_op_sine`).
	Sine,
	/// `cos(x)` (C++ `k_op_cosine`).
	Cosine,
	/// `tan(x)` (C++ `k_op_tangent`).
	Tangent,
	/// `asin(x)` (C++ `k_op_arc_sine`).
	ArcSine,
	/// `acos(x)` (C++ `k_op_arc_cosine`).
	ArcCosine,
	/// `atan(x)` (C++ `k_op_arc_tangent`).
	ArcTangent,
	/// `sinh(x)` (C++ `k_op_hyp_sine`).
	HyperbolicSine,
	/// `cosh(x)` (C++ `k_op_hyp_cosine`).
	HyperbolicCosine,
	/// `tanh(x)` (C++ `k_op_hyp_tangent`).
	HyperbolicTangent,
}

/// Trigonometry node. Unit-like — the C++ class has no own data
/// members (its `Operation` enum is modeled above; inputs live in
/// [`NodeCore`]).
pub struct TrigonometryNode;

impl NodeBehavior for TrigonometryNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Trigonometry"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.trigonometry"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Math]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Perform a trigonometry operation on a value."
	}

	/// Localized input names (C++ `retranslate()`): `method_in` ->
	/// "Method", `x_in` -> "Value"; also sets the combo strings on
	/// `method_in` to the nine function names documented on
	/// [`METHOD_INPUT`].
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): reads `x_in` as a double,
	/// applies the [`Operation`] selected by `method_in`
	/// (sin/cos/tan/asin/acos/atan/sinh/cosh/tanh), and pushes the
	/// result as a float.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `TrigonometryNode::TrigonometryNode()`): adds
/// `method_in` as a not-connectable/not-keyframable combo and `x_in`
/// as a float input defaulting to 0.0.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_trigonometry_node` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.trigonometry",
		name: "Trigonometry",
		categories: &[Category::Math],
		create,
	});
}
