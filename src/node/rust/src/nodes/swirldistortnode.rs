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

//! Swirl distort effect (C++
//! `src/node/src/distort/swirl/swirldistortnode.{h,cpp}`,
//! `olive::SwirlDistortNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, Gizmo, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Radius input id (C++ `k_radius_input`). Type: float; default `200`;
/// properties: `min = 0`.
pub const RADIUS_INPUT: &str = "radius_in";

/// Angle input id (C++ `k_angle_input`). Type: float; default `10`;
/// properties: `base = 0.1`.
pub const ANGLE_INPUT: &str = "angle_in";

/// Center position input id (C++ `k_position_input`). Type: vec2;
/// default `(0, 0)`.
pub const POSITION_INPUT: &str = "pos_in";

/// Swirl distort node. Rotates the image around a center point with a
/// falloff by radius.
pub struct SwirlDistortNode {
	/// Center position drag handle (C++ `PointGizmo *gizmo_`; anchor
	/// point shape, drags both tracks of `pos_in`).
	gizmo: Gizmo,
}

/// Fragment shader (C++ loads the `:/shaders/swirl.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/swirl.frag`.
const SHADER_FRAG: &str = r#"// Swirl effect parameters
uniform float radius_in;
uniform float angle_in;
uniform vec2 pos_in;
uniform vec2 resolution_in;

uniform sampler2D tex_in;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void) {
  vec2 center = resolution_in*0.5 + pos_in;

  vec2 uv = ove_texcoord;

  vec2 tc = uv * resolution_in;
  tc -= center;
  float dist = length(tc);
  if (dist < radius_in) {
    float percent = (radius_in - dist) / radius_in;
    float theta = percent * percent * -angle_in;
    float s = sin(theta);
    float c = cos(theta);
    tc = vec2(dot(tc, vec2(c, -s)), dot(tc, vec2(s, c)));
  }
  tc += center;
  frag_color = texture(tex_in, tc / resolution_in);
}
"#;

impl SwirlDistortNode {
	/// Fragment shader (C++ `get_shader_code()`; the request id is
	/// ignored, this is the only shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for SwirlDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Swirl"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.swirl"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Distorts an image by swirling it around a center point."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Input", `radius_in` -> "Radius", `angle_in` -> "Angle",
	/// `pos_in` -> "Position".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// angle != 0.0 AND radius != 0.0 -> shader job over the whole value
	/// row with `resolution_in` inserted from the texture's virtual
	/// resolution; otherwise pass-through push of the input texture
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
	/// request id and always returns the swirl fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Gizmo positions (C++ `update_gizmo_positions()`): places the
	/// position gizmo at half the globals' square resolution plus the
	/// `pos_in` offset (note: unlike Ripple, this does not require a
	/// texture).
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

/// Constructor (C++ `SwirlDistortNode::SwirlDistortNode()`): adds
/// `tex_in`, `radius_in`, `angle_in` and `pos_in` with the defaults and
/// properties documented on the constants; creates the anchor-shaped
/// position point gizmo bound to both tracks of `pos_in`; sets the
/// video-effect flag and the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.swirl`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.swirl",
		name: "Swirl",
		categories: &[Category::Distort],
		create,
	});
}
