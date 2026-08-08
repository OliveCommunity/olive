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

//! Wave distort effect (C++
//! `src/node/src/distort/wave/wavedistortnode.{h,cpp}`,
//! `olive::WaveDistortNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Frequency input id (C++ `k_frequency_input`). Type: float; default
/// `10`.
pub const FREQUENCY_INPUT: &str = "frequency_in";

/// Intensity input id (C++ `k_intensity_input`). Type: float; default
/// `10`.
pub const INTENSITY_INPUT: &str = "intensity_in";

/// Evolution input id (C++ `k_evolution_input`). Type: float; default
/// `0`.
pub const EVOLUTION_INPUT: &str = "evolution_in";

/// Direction combo input id (C++ `k_vertical_input`). Type: combo;
/// default `false` (0 = "Horizontal"); combo strings: "Horizontal",
/// "Vertical".
pub const VERTICAL_INPUT: &str = "vertical_in";

/// Wave distort node. Displaces the image along a sine wave. Has no own
/// member fields in C++ (state lives in the `Node` inputs).
pub struct WaveDistortNode;

/// Fragment shader (C++ loads the `:/shaders/wave.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/wave.frag`.
const SHADER_FRAG: &str = r#"uniform float frequency_in;
uniform float intensity_in;
uniform float evolution_in;
uniform bool vertical_in;

uniform sampler2D tex_in;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void) {
  vec2 pos = ove_texcoord;

  if (vertical_in) {
    pos.x -= sin((ove_texcoord.y-(evolution_in*0.01))*frequency_in)*intensity_in*0.01;
  } else {
    pos.y -= sin((ove_texcoord.x-(evolution_in*0.01))*frequency_in)*intensity_in*0.01;
  }

  if (pos.x < 0.0 || pos.x >= 1.0 || pos.y < 0.0 || pos.y >= 1.0) {
    discard;
  } else {
    frag_color = texture(tex_in, pos);
  }
}
"#;

impl WaveDistortNode {
	/// Fragment shader (C++ `get_shader_code()`; the request id is
	/// ignored, this is the only shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for WaveDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Wave"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.wave"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Distorts an image along a sine wave."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Input", `frequency_in` -> "Frequency", `intensity_in` ->
	/// "Intensity", `evolution_in` -> "Evolution", `vertical_in` ->
	/// "Direction" (combo strings "Horizontal"/"Vertical").
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// intensity != 0.0 -> shader job over the whole value row rendered
	/// at the texture's own params; intensity == 0.0 -> pass-through
	/// push of the input texture unchanged.
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
	/// request id and always returns the wave fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `WaveDistortNode::WaveDistortNode()`): adds
/// `tex_in`, `frequency_in`, `intensity_in`, `evolution_in` and
/// `vertical_in` with the defaults documented on the constants, sets
/// the video-effect flag and the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.wave`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.wave",
		name: "Wave",
		categories: &[Category::Distort],
		create,
	});
}
