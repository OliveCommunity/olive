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

//! Ripple distort effect (C++
//! `src/node/src/distort/ripple/rippledistortnode.{h,cpp}`,
//! `olive::RippleDistortNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, Gizmo, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Evolution input id (C++ `k_evolution_input`). Type: float; default
/// `0`.
pub const EVOLUTION_INPUT: &str = "evolution_in";

/// Intensity input id (C++ `k_intensity_input`). Type: float; default
/// `100`.
pub const INTENSITY_INPUT: &str = "intensity_in";

/// Frequency input id (C++ `k_frequency_input`). Type: float; default
/// `1`; properties: `base = 0.01`.
pub const FREQUENCY_INPUT: &str = "frequency_in";

/// Center position input id (C++ `k_position_input`). Type: vec2;
/// default `(0, 0)`.
pub const POSITION_INPUT: &str = "position_in";

/// Stretch input id (C++ `k_stretch_input`). Type: bool; default
/// `false`; when off the ripple is aspect-corrected in the shader.
pub const STRETCH_INPUT: &str = "stretch_in";

/// Ripple distort node. Radiates concentric wave displacement from a
/// center point.
pub struct RippleDistortNode {
	/// Center position drag handle (C++ `PointGizmo *gizmo_`; anchor
	/// point shape, drags both tracks of `position_in`).
	gizmo: Gizmo,
}

/// Fragment shader (C++ loads the `:/shaders/ripple.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/ripple.frag`.
const SHADER_FRAG: &str = r#"uniform float evolution_in;
uniform float intensity_in;
uniform float frequency_in;
uniform vec2 position_in;
uniform bool stretch_in;

uniform vec2 resolution_in;
uniform sampler2D tex_in;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void) {
  vec2 center = position_in/resolution_in;

  vec2 adj_texcoord = ove_texcoord;

  adj_texcoord -= 0.5;
  if (!stretch_in) {
    // Adjust by aspect ratio
    float ar = (resolution_in.x/resolution_in.y);
    if (resolution_in.x > resolution_in.y) {
      adj_texcoord.y /= ar;
      center.y /= ar;
    } else {
      adj_texcoord.x *= ar;
      center.x *= ar;
    }
  }
  adj_texcoord += 0.5;
  center += 0.5;

  adj_texcoord -= center;

  float len = length(adj_texcoord);
  vec2 uv = ove_texcoord + (adj_texcoord/len)*cos((frequency_in)*(len*12.0-evolution_in))*(intensity_in*0.0005);
  frag_color = texture(tex_in, uv);
}
"#;

impl RippleDistortNode {
	/// Fragment shader (C++ `get_shader_code()`; the request id is
	/// ignored, this is the only shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for RippleDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Ripple"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.ripple"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Distorts an image with a ripple effect."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Input", `frequency_in` -> "Frequency", `intensity_in` ->
	/// "Intensity", `evolution_in` -> "Evolution", `position_in` ->
	/// "Position", `stretch_in` -> "Stretch".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// intensity != 0.0 -> shader job over the whole value row with
	/// `resolution_in` inserted from the texture's virtual resolution;
	/// intensity == 0.0 -> pass-through push of the input texture
	/// unchanged.
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
	/// request id and always returns the ripple fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Gizmo positions (C++ `update_gizmo_positions()`): with a texture,
	/// places the position gizmo at the texture's half resolution plus
	/// the `position_in` offset.
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Gizmo drag (C++ `gizmo_drag_move()`): drags the position input's
	/// X and Y track draggers by the mouse delta added to their
	/// drag-start values.
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `RippleDistortNode::RippleDistortNode()`): adds
/// `tex_in`, `evolution_in`, `intensity_in`, `frequency_in`,
/// `position_in` and `stretch_in` with the defaults and properties
/// documented on the constants; creates the anchor-shaped position point
/// gizmo bound to both tracks of `position_in`; sets the video-effect
/// flag and the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.ripple`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.ripple",
		name: "Ripple",
		categories: &[Category::Distort],
		create,
	});
}
