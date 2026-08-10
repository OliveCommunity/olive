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

//! Solid color generator (C++ `src/node/src/generator/solid/solid.{h,cpp}`,
//! `olive::SolidGenerator`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Color input id (C++ `k_color_input`). Type: color; default
/// `(1.0, 0.0, 0.0, 1.0)` (red — "a color that isn't black").
pub const COLOR_INPUT: &str = "color_in";

/// Solid color generator node. Has no own member fields in C++.
pub struct SolidGenerator;

/// Fragment shader (C++ loads the `:/shaders/solid.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/solid.frag`.
const SHADER_FRAG: &str = r#"uniform vec4 color_in;

out vec4 frag_color;

void main(void) {
    frag_color = color_in;
}
"#;

impl SolidGenerator {
	/// Fragment shader for all shader requests (C++
	/// `get_shader_code()` ignores the request id).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for SolidGenerator {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Solid"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.solidgenerator"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Generator]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Generate a solid color."
	}

	/// Localized input names (C++ `retranslate()`): `color_in` ->
	/// "Color".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		match id {
			COLOR_INPUT => "Color",
			_ => id,
		}
	}

	/// Evaluate outputs (C++ `value()`): pushes a texture job built
	/// from the whole input row at the sequence video params.
	///
	/// The Rust model has no shader-job payload: the job is deferred to
	/// the renderer seam, so a null texture handle marks "renderer must
	/// produce this texture" (`// CPP-PARITY: solid.cpp` value()).
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		let _ = (core, inputs, time);
		table.push(
			crate::value::ValueType::Texture,
			crate::value::NodeValue::Texture(crate::handle::CHandle::null()),
			None,
		);
	}

	/// Shader code request (C++ `get_shader_code()`): returns the solid
	/// fragment shader for any request id.
	fn shader_code(&self, request: &str) -> Option<String> {
		let _ = request;
		Some(Self::shader_frag().to_string())
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(SolidGenerator))
	}
}

/// Constructor (C++ `SolidGenerator::SolidGenerator()`): adds
/// `color_in` with the default documented on the constant.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();
	let mut color = crate::input::Input::new(
		COLOR_INPUT,
		crate::value::ValueType::Color,
		crate::value::NodeValue::Color([1.0, 0.0, 0.0, 1.0]),
	);
	color.properties = vec![("view".to_string(), crate::value::NodeValue::Text("color".into()))];
	core.add_input(color);
	(core, Box::new(SolidGenerator))
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::node::NodeBehavior;
	use crate::value::{NodeValue, NodeValueTable, ValueType};
	use oakcore_rs::Rational;

	#[test]
	fn input_names() {
		let n = SolidGenerator;
		assert_eq!(n.input_name(COLOR_INPUT), "Color");
		assert_eq!(n.input_name("other_in"), "other_in");
	}

	#[test]
	fn create_wires_inputs() {
		let (core, behavior) = create();
		assert_eq!(behavior.type_id(), "org.olivevideoeditor.Olive.solidgenerator");
		assert_eq!(
			core.get_input(COLOR_INPUT).unwrap().default,
			NodeValue::Color([1.0, 0.0, 0.0, 1.0])
		);
	}

	#[test]
	fn value_pushes_deferred_job() {
		let (core, behavior) = create();
		let mut table = NodeValueTable::default();
		behavior.value(
			&core,
			&crate::value::NodeValueRow::default(),
			Rational::new(0, 1),
			&mut table,
		);
		assert!(table.get(ValueType::Texture).is_some());
	}

	#[test]
	fn shader_code_returns_solid_shader() {
		let n = SolidGenerator;
		let code = n.shader_code("anything").unwrap();
		assert!(code.contains("uniform vec4 color_in;"));
	}

	#[test]
	fn duplicate_clones() {
		let (core, behavior) = create();
		let dup = behavior.duplicate(&core).unwrap();
		assert_eq!(dup.name(), "Solid");
	}
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.solidgenerator`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.solidgenerator",
		name: "Solid",
		categories: &[Category::Generator],
		create,
	});
}
