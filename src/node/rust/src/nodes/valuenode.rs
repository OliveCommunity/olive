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

//! Constant-value generator node (C++
//! `src/node/src/input/value/valuenode.{h,cpp}`, `olive::ValueNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};
use crate::value::ValueType;

/// Type selector input id (C++ `k_type_input`). Type: combo; default
/// `0` (the first entry of [`SUPPORTED_TYPES`]); flags: not-connectable,
/// not-keyframable. Combo strings are the pretty data-type names of the
/// supported types (built in `retranslate()`).
pub const TYPE_INPUT: &str = "type_in";

/// Value input id (C++ `k_value_input`). Type: initially
/// `SUPPORTED_TYPES[0]` (float), switched by `input_value_changed` when
/// `type_in` changes; default: empty variant; flags: not-connectable.
pub const VALUE_INPUT: &str = "value_in";

/// Selectable value types (C++ `k_supported_types`). The C++ list is:
/// float, int, rational, vec2, vec3, vec4, color, text, matrix, font,
/// boolean. The Rust [`ValueType`] enum has no `Matrix` or `Font`
/// variants, so those two entries are omitted here (a behavioral gap to
/// resolve when matrix/font values land in `value.rs`).
pub const SUPPORTED_TYPES: &[ValueType] = &[
	ValueType::Float,
	ValueType::Int,
	ValueType::Rational,
	ValueType::Vec2,
	ValueType::Vec3,
	ValueType::Vec4,
	ValueType::Color,
	ValueType::Text,
	ValueType::Boolean,
];

/// Value node. Holds a single typed constant that can be connected to
/// other nodes' inputs. The C++ class has no own members (its state
/// lives entirely in the two inputs), so this is a unit-like struct.
pub struct ValueNode;

impl NodeBehavior for ValueNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Value"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.value"
	}

	/// Categories (C++ `category()` returns
	/// `{ k_category_generator }`).
	fn categories(&self) -> &[Category] {
		&[Category::Generator]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Create a single value that can be connected to various other inputs."
	}

	/// Localized input names (C++ `retranslate()`): `type_in` ->
	/// "Type", `value_in` -> "Value". The C++ override also sets the
	/// `type_in` combo strings to the pretty names of the supported
	/// types — that part has no trait surface and is noted here only.
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): pushes the `value_in` value
	/// onto the table unchanged.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Input value changed (C++ `InputValueChangedEvent()`): when
	/// `type_in` changes, sets the data type of `value_in` to
	/// `SUPPORTED_TYPES[type index]`; then defers to the base-class
	/// behavior.
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		todo!()
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `ValueNode::ValueNode()`): adds `type_in` and
/// `value_in` with the defaults, flags and properties documented on the
/// constants.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_value_node` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.value",
		name: "Value",
		categories: &[Category::Generator],
		create,
	});
}
