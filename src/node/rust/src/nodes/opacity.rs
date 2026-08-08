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

//! Opacity effect (C++ `src/node/src/effect/opacity/opacityeffect.{h,cpp}`,
//! `olive::OpacityEffect`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Opacity input id (C++ `k_value_input`). Type: float; default `1.0`;
/// properties: `view = percentage`, `min = 0.0`, `max = 1.0`.
pub const VALUE_INPUT: &str = "opacity_in";

/// Opacity effect node. Multiplies a texture's alpha by a 0..1 factor.
pub struct OpacityEffect {
	/// Owned child math node configured to `multiply` (C++ `math_`,
	/// formerly a QObject child).
	math: Box<super::math::MathNode>,
}

/// Fragment shader for the plain opacity path (C++ loads the
/// `:/shaders/opacity.frag` resource in `get_shader_code`).
/// Text copied verbatim from `engine/shaders/opacity.frag`.
const SHADER_FRAG: &str = r#"// Inputs
uniform sampler2D tex_in;
uniform float opacity_in;

// Input texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

void main() {
  frag_color = texture(tex_in, ove_texcoord) * opacity_in;
}
"#;

/// Fragment shader for the `rgbmult` shader id (C++
/// `:/shaders/opacity_rgb.frag`), used when the opacity input is
/// itself a texture. Text copied verbatim from
/// `engine/shaders/opacity_rgb.frag`.
const SHADER_RGB_MULT_FRAG: &str = r#"// Inputs
uniform sampler2D tex_in;
uniform sampler2D opacity_in;

// Input texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

vec3 rgb2hsv(vec3 c)
{
  vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

void main() {
  vec4 value = texture(opacity_in, ove_texcoord);
  float v = rgb2hsv(value.rgb).b;

  vec4 c = texture(tex_in, ove_texcoord);
  c *= v;
  frag_color = c;
}
"#;

impl OpacityEffect {
	/// Fragment shader for the plain opacity path (C++
	/// `get_shader_code()` default branch).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}

	/// Fragment shader for the `rgbmult` request (C++
	/// `get_shader_code()` `"rgbmult"` branch).
	fn shader_rgb_mult_frag() -> &'static str {
		SHADER_RGB_MULT_FRAG
	}
}

impl NodeBehavior for OpacityEffect {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Opacity"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.opacity"
	}

	/// Categories (C++ `category()`; note: C++ files Opacity under
	/// `k_category_filter` even though the sources live in `effect/`).
	fn categories(&self) -> &[Category] {
		&[Category::Filter]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Alter a video's opacity.\n\nThis is equivalent to multiplying a video by a number between 0.0 and 1.0."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Texture", `opacity_in` -> "Opacity".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// texture opacity input -> `rgbmult` shader job; scalar opacity
	/// != 1.0 -> plain shader job; opacity == 1.0 -> pass-through push
	/// of the input texture unchanged.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): dispatch on
	/// request id (`"rgbmult"` vs default) between the two fragment
	/// shaders above.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`); clones the owned child math node too.
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `OpacityEffect::OpacityEffect()`): builds the child
/// math node, adds `tex_in`/`opacity_in` with the defaults and
/// properties documented on the constants, sets the video-effect flag
/// and the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_opacity_effect` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.opacity",
		name: "Opacity",
		categories: &[Category::Filter],
		create,
	});
}
