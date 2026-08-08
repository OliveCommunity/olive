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

//! Three-way color corrector node (C++
//! `src/node/src/color/threewaycolor/threewaycolor.{h,cpp}`,
//! `olive::ThreeWayColorNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Shadows color input id (C++ `k_shadows_color_input`). Type: color;
/// default neutral gray `{0.5, 0.5, 0.5, 1.0}`.
pub const SHADOWS_COLOR_INPUT: &str = "shadows_color_in";

/// Midtones color input id (C++ `k_midtones_color_input`). Type: color;
/// default neutral gray `{0.5, 0.5, 0.5, 1.0}`.
pub const MIDTONES_COLOR_INPUT: &str = "midtones_color_in";

/// Highlights color input id (C++ `k_highlights_color_input`). Type:
/// color; default neutral gray `{0.5, 0.5, 0.5, 1.0}`.
pub const HIGHLIGHTS_COLOR_INPUT: &str = "highlights_color_in";

/// Shadows amount input id (C++ `k_shadows_amount_input`). Type: float;
/// default `1.0`; properties: `min = 0.0`, `view = percentage`.
pub const SHADOWS_AMOUNT_INPUT: &str = "shadows_amount_in";

/// Midtones amount input id (C++ `k_midtones_amount_input`). Type:
/// float; default `1.0`; properties: `min = 0.0`, `view = percentage`.
pub const MIDTONES_AMOUNT_INPUT: &str = "midtones_amount_in";

/// Highlights amount input id (C++ `k_highlights_amount_input`). Type:
/// float; default `1.0`; properties: `min = 0.0`, `view = percentage`.
pub const HIGHLIGHTS_AMOUNT_INPUT: &str = "highlights_amount_in";

/// Luma coefficients input id (C++ `k_luma_coefficients_input`). Not a
/// declared node input — the C++ never calls `add_input` for it; it is
/// the shader uniform name fed per frame in `value()` from the project
/// color manager's default luma coefficients (Rec. 709
/// `{0.2126, 0.7152, 0.0722}` fallback). Type: vec3.
pub const LUMA_COEFFICIENTS_INPUT: &str = "luma_coefficients_in";

/// Three-way color corrector node. Adjusts shadows, midtones, and
/// highlights separately. The C++ class has no own private members, so
/// this is a unit-like struct (caches/inputs live in `NodeCore`).
pub struct ThreeWayColorNode;

/// Fragment shader (C++ `get_shader_code` loads the
/// `:/shaders/threewaycolor.frag` resource). Text copied verbatim from
/// `engine/shaders/threewaycolor.frag`.
const SHADER_FRAG: &str = r#"uniform sampler2D tex_in;

uniform vec4 shadows_color_in;
uniform vec4 midtones_color_in;
uniform vec4 highlights_color_in;
uniform float shadows_amount_in;
uniform float midtones_amount_in;
uniform float highlights_amount_in;
uniform vec3 luma_coefficients_in;

in vec2 ove_texcoord;
out vec4 frag_color;

vec3 color_offset(vec4 control, float amount)
{
    return (control.rgb - vec3(0.5)) * 2.0 * amount;
}

void main(void)
{
    vec4 source = texture(tex_in, ove_texcoord);
    float luma = clamp(dot(source.rgb, luma_coefficients_in), 0.0, 1.0);

    float shadow_weight = smoothstep(0.75, 0.0, luma);
    float highlight_weight = smoothstep(0.25, 1.0, luma);
    float midtone_weight = clamp(1.0 - abs(luma - 0.5) * 2.0, 0.0, 1.0);

    vec3 adjustment =
        color_offset(shadows_color_in, shadows_amount_in) * shadow_weight +
        color_offset(midtones_color_in, midtones_amount_in) * midtone_weight +
        color_offset(highlights_color_in, highlights_amount_in) * highlight_weight;

    vec3 graded = source.rgb + adjustment * source.rgb * (1.0 - source.rgb);
    frag_color = vec4(clamp(graded, 0.0, 1.0), source.a);
}
"#;

impl ThreeWayColorNode {
	/// Fragment shader for any request (C++ `get_shader_code()` ignores
	/// the request id and always returns this shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for ThreeWayColorNode {
	/// Human-readable name (C++ `name()`, inline in the header).
	fn name(&self) -> &str {
		"Three-Way Color"
	}

	/// Stable type id (C++ `id()`, inline in the header).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.threewaycolor"
	}

	/// Categories (C++ `category()`, inline in the header).
	fn categories(&self) -> &[Category] {
		&[Category::Color]
	}

	/// Description (C++ `description()`, inline in the header).
	fn description(&self) -> &str {
		"Adjusts shadows, midtones, and highlights separately."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` -> "Input",
	/// `shadows_color_in` -> "Shadows", `midtones_color_in` ->
	/// "Midtones", `highlights_color_in` -> "Highlights",
	/// `shadows_amount_in` -> "Shadows Amount", `midtones_amount_in` ->
	/// "Midtones Amount", `highlights_amount_in` -> "Highlights Amount".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): the request id is
	/// ignored; always returns [`SHADER_FRAG`].
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// otherwise builds a `ShaderJob` from the whole input row, inserts
	/// `luma_coefficients_in` as a vec3 from the project color manager's
	/// default luma coefficients (Rec. 709 `{0.2126, 0.7152, 0.0722}`
	/// when no project/manager is attached), and pushes the texture as
	/// that job.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `ThreeWayColorNode::ThreeWayColorNode()`): adds
/// `tex_in` (texture, effect input), the three color inputs with the
/// neutral-gray default, the three amount inputs with the percentage
/// view and `min = 0.0`, and sets the video-effect flag.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.threewaycolor`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.threewaycolor",
		name: "Three-Way Color",
		categories: &[Category::Color],
		create,
	});
}
