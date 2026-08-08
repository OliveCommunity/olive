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

//! Flip distort effect (C++
//! `src/node/src/distort/flip/flipdistortnode.{h,cpp}`,
//! `olive::FlipDistortNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Horizontal flip input id (C++ `k_horizontal_input`). Type: bool;
/// default `false`.
pub const HORIZONTAL_INPUT: &str = "horiz_in";

/// Vertical flip input id (C++ `k_vertical_input`). Type: bool;
/// default `false`.
pub const VERTICAL_INPUT: &str = "vert_in";

/// Flip distort node. Mirrors the image horizontally and/or vertically.
/// Has no own member fields in C++ (state lives in the `Node` inputs).
pub struct FlipDistortNode;

/// Fragment shader (C++ loads the `:/shaders/flip.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/flip.frag`.
const SHADER_FRAG: &str = r#"uniform sampler2D tex_in;
uniform bool horiz_in;
uniform bool vert_in;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void) {
    if (!horiz_in && !vert_in) {
        frag_color = texture(tex_in, ove_texcoord);
        return;
    }

    vec2 new_coord = ove_texcoord;

    if (horiz_in) new_coord.x = 1.0 - new_coord.x;
    if (vert_in) new_coord.y = 1.0 - new_coord.y;

    frag_color = texture(tex_in, new_coord);
}
"#;

impl FlipDistortNode {
	/// Fragment shader (C++ `get_shader_code()`; the request id is
	/// ignored, this is the only shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for FlipDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Flip"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.flip"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Flips an image horizontally or vertically"
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Input", `horiz_in` -> "Horizontal", `vert_in` -> "Vertical".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// either flip flag set -> shader job over the whole value row;
	/// neither set -> pass-through push of the input texture unchanged.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): ignores the
	/// request id and always returns the flip fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `FlipDistortNode::FlipDistortNode()`): adds
/// `tex_in`, `horiz_in` and `vert_in` with the defaults and flags
/// documented on the constants, sets the video-effect flag and the
/// effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.flip`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.flip",
		name: "Flip",
		categories: &[Category::Distort],
		create,
	});
}
