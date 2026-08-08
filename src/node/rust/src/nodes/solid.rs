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
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): pushes a texture job built
	/// from the whole input row at the sequence video params.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): returns the solid
	/// fragment shader for any request id.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `SolidGenerator::SolidGenerator()`): adds
/// `color_in` with the default documented on the constant.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
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
