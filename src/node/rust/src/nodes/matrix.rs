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

//! Orthographic matrix generator (C++
//! `src/node/src/generator/matrix/matrix.{h,cpp}`,
//! `olive::MatrixGenerator`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Position input id (C++ `k_position_input`). Type: vec2; default
/// `(0.0, 0.0)`.
pub const POSITION_INPUT: &str = "pos_in";

/// Rotation input id (C++ `k_rotation_input`). Type: float; default
/// `0.0`.
pub const ROTATION_INPUT: &str = "rot_in";

/// Scale input id (C++ `k_scale_input`). Type: vec2; default
/// `(1.0, 1.0)`; properties: `min = (0, 0)`, `view = percentage`,
/// `disable1 = true` (the second component starts disabled because
/// uniform scale defaults to on).
pub const SCALE_INPUT: &str = "scale_in";

/// Uniform scale input id (C++ `k_uniform_scale_input`). Type: bool;
/// default `true`; flags: not-connectable, not-keyframable.
pub const UNIFORM_SCALE_INPUT: &str = "uniform_scale_in";

/// Anchor point input id (C++ `k_anchor_input`). Type: vec2; default
/// `(0.0, 0.0)`.
pub const ANCHOR_INPUT: &str = "anchor_in";

/// Orthographic matrix generator node. Builds a 2D transform matrix
/// from position, rotation, scale and anchor inputs. Has no own member
/// fields in C++ (state lives in the `Node` inputs).
pub struct MatrixGenerator;

impl NodeBehavior for MatrixGenerator {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Orthographic Matrix"
	}

	/// Short menu name (C++ `short_name()`).
	fn short_name(&self) -> &str {
		"Ortho"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.ortho"
	}

	/// Categories (C++ `category()`): generator and math.
	fn categories(&self) -> &[Category] {
		&[Category::Generator, Category::Math]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Generate an orthographic matrix using position, rotation, and scale."
	}

	/// Localized input names (C++ `retranslate()`): `pos_in` ->
	/// "Position", `rot_in` -> "Rotation", `scale_in` -> "Scale",
	/// `uniform_scale_in` -> "Uniform Scale", `anchor_in` ->
	/// "Anchor Point".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): builds the matrix via
	/// `generate_matrix(value, false, false, false, Matrix4x4())` and
	/// pushes it as a `k_matrix` value. The C++ helper composes, in
	/// order: translate(position), rotate(rotation around Z),
	/// scale(scale.x, uniform ? scale.x : scale.y, 1), then
	/// translate(-anchor).
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Input value changed (C++ `InputValueChangedEvent`): when
	/// `uniform_scale_in` changes, sets the scale input's `disable1`
	/// property to the uniform-scale value (disabling the Y component
	/// while uniform scale is on).
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `MatrixGenerator::MatrixGenerator()`): adds
/// `pos_in`, `rot_in`, `scale_in`, `uniform_scale_in` and `anchor_in`
/// with the defaults, flags and properties documented on the constants.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.ortho`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.ortho",
		name: "Orthographic Matrix",
		categories: &[Category::Generator, Category::Math],
		create,
	});
}
