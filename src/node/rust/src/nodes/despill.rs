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

//! Despill effect (C++ `src/node/src/keying/despill/despill.{h,cpp}`,
//! `olive::DespillNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Key color input id (C++ `k_color_input`). Type: combo; default `0`;
/// combo strings: "Green", "Blue" (set in `retranslate()`).
pub const COLOR_INPUT: &str = "color_in";

/// Despill method input id (C++ `k_method_input`). Type: combo;
/// default `0`; combo strings: "Average", "Double Red Average",
/// "Double Average", "Limit" (set in `retranslate()`).
pub const METHOD_INPUT: &str = "method_in";

/// Preserve-luminance toggle input id (C++
/// `k_preserve_luminance_input`; note the value ends in `_input`, not
/// `_in`). Type: boolean; default `false`.
pub const PRESERVE_LUMINANCE_INPUT: &str = "preserve_luminance_input";

/// Despill node: removes green/blue screen spill from the keyed
/// foreground using one of several channel-averaging methods. The C++
/// class has no own members.
pub struct DespillNode;

/// Fragment shader (C++ loads the `:/shaders/despill.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/despill.frag`. The `luma_coeffs` uniform is not a
/// node input — it is injected into the shader job by `value()`.
const SHADER_FRAG: &str = r#"uniform sampler2D tex_in;
uniform int color_in;
uniform int method_in;
uniform bool preserve_luminance_input;
uniform vec3 luma_coeffs;

in vec2 ove_texcoord;
out vec4 frag_color;

#define AVERAGE             0
#define DOUBLE_RED_AVERAGE  1
#define DOUBLE_AVERAGE      2
#define BLUE_LIMIT          3

void main(void) {
    vec4 original_col = texture(tex_in, ove_texcoord);
    vec4 tex_col = original_col;
    float color_average = 0.0;

    if(color_in == 0) { // Green screen
        switch (method_in) {
        case AVERAGE:
            color_average = dot(tex_col.rb, vec2(0.5)); // (tex_col.r + tex_col.b) / 2.0
            tex_col.g = tex_col.g > color_average ? color_average: tex_col.g;
            break;
        case DOUBLE_RED_AVERAGE:
            color_average = dot(tex_col.rb, vec2(2.0, 1.0) / 3.0); // (2.0 * tex_col.r + tex_col.b) / 3.0
            tex_col.g = tex_col.g > color_average ? color_average : tex_col.g;
            break;
        case DOUBLE_AVERAGE:
            color_average = dot(tex_col.br, vec2(2.0, 1.0) / 3.0); // (2.0 * tex_col.b + tex_col.r) / 3.0
            tex_col.g = tex_col.g > color_average ? color_average : tex_col.g;
            break;
        case BLUE_LIMIT:
            tex_col.g = tex_col.g > tex_col.b ? tex_col.b : tex_col.g;
            break;
        }
    } else { // Blue screen
        switch (method_in) {
        case AVERAGE:
            color_average = dot(tex_col.rg, vec2(0.5)); // (tex_col.r + tex_col.g) / 2.0
            tex_col.b = tex_col.b > color_average ? color_average : tex_col.b;
            break;
        case DOUBLE_RED_AVERAGE:
            color_average = dot(tex_col.rg, vec2(2.0, 1.0) / 3.0); // (2.0 * tex_col.r + tex_col.g) / 3.0
            tex_col.b = tex_col.b > color_average ? color_average : tex_col.b;
            break;
        case DOUBLE_AVERAGE:
            color_average = dot(tex_col.gr, vec2(2.0, 1.0) / 3.0); // (2.0 * tex_col.g+ tex_col.r) / 3.0
            tex_col.b = tex_col.b > color_average ? color_average : tex_col.b;
            break;
        case BLUE_LIMIT:
            tex_col.b = tex_col.b > tex_col.g ? tex_col.g : tex_col.b;
            break;
        }
    }

    if (preserve_luminance_input) {
        vec4 diff = original_col - tex_col;
        float luma = dot(abs(diff.rgb), luma_coeffs);
        tex_col.rgb += vec3(luma);
    }

    frag_color = tex_col;
}
"#;

impl DespillNode {
	/// Fragment shader (C++ `get_shader_code()`; the request is
	/// ignored — there is a single shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for DespillNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Despill"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.despill"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Keying]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Selection of simple despill operations"
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Input", `color_in` -> "Key Color" (combo strings
	/// "Green"/"Blue"), `method_in` -> "Method" (combo strings
	/// "Average"/"Double Red Average"/"Double Average"/"Limit"),
	/// `preserve_luminance_input` -> "Preserve Luminance".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): builds a `ShaderJob` from
	/// the whole input row, then inserts a `luma_coeffs` vec3 taken
	/// from the project's color manager default luma coefficients
	/// (falling back to Rec.709 `0.2126/0.7152/0.0722` when there is
	/// no project or color manager); pushes the job only when
	/// `tex_in` holds a texture.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): returns the
	/// single fragment shader regardless of the request id.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `DespillNode::DespillNode()`): adds `tex_in`,
/// `color_in`, `method_in`, and `preserve_luminance_input` with the
/// defaults documented on the constants, sets the video-effect flag,
/// and makes `tex_in` the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.despill`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.despill",
		name: "Despill",
		categories: &[Category::Keying],
		create,
	});
}
