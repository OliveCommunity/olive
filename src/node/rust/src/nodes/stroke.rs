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

//! Stroke filter (C++ `src/node/src/filter/stroke/stroke.{h,cpp}`,
//! `olive::StrokeFilterNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Stroke color input id (C++ `k_color_input`). Type: color; default
/// opaque white `(1.0, 1.0, 1.0, 1.0)`.
pub const COLOR_INPUT: &str = "color_in";

/// Stroke radius input id (C++ `k_radius_input`). Type: float; default
/// `10.0`; properties: `min = 0.0`.
pub const RADIUS_INPUT: &str = "radius_in";

/// Stroke opacity input id (C++ `k_opacity_input`). Type: float;
/// default `1.0`; properties: `view = percentage`, `min = 0.0`,
/// `max = 1.0`.
pub const OPACITY_INPUT: &str = "opacity_in";

/// Inner-stroke toggle input id (C++ `k_inner_input`). Type: bool;
/// default `false`.
pub const INNER_INPUT: &str = "inner_in";

/// Stroke filter node. Draws a colored outline around the opaque
/// regions of the input. The C++ class declares no own member fields.
pub struct StrokeFilterNode;

/// Fragment shader (C++ `get_shader_code()` loads
/// `:/shaders/stroke.frag` via FileFunctions for any request). Text
/// copied verbatim from `engine/shaders/stroke.frag`.
const SHADER_FRAG: &str = r#"// Node parameter inputs
uniform sampler2D tex_in;
uniform vec4 color_in;
uniform float radius_in;
uniform float opacity_in;
uniform bool inner_in;
uniform vec2 resolution_in;

// Standard inputs
uniform int ove_iteration;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void) {
    vec4 pixel_here = texture(tex_in, ove_texcoord);

    // Detect no-op situations
    if (radius_in == 0.0
        || opacity_in == 0.0
        || (inner_in && pixel_here.a == 0.0)
        || (!inner_in && pixel_here.a == 1.0)) {
        // No-op, do nothing
        frag_color = pixel_here;
        return;
    }

    float radius = ceil(radius_in);

    float stroke_weight = 0.0;

    // Loop over box
    for (float i=-radius + 0.5; i<=radius; i += 2.0) {
        float x_coord = i / resolution_in.x;

        for (float j=-radius + 0.5; j<=radius; j += 2.0) {
            float y_coord = j / resolution_in.y;

            if (abs(length(vec2(i, j))) < radius) {
                // Get pixel here
                float alpha = texture(tex_in, ove_texcoord + vec2(x_coord, y_coord)).a;

                if (inner_in) {
                    alpha = 1.0 - alpha;
                }

                stroke_weight += alpha;

                if (stroke_weight >= 1.0) {
                    break;
                }
            }
        }

        if (stroke_weight >= 1.0) {
            stroke_weight = 1.0;
            break;
        }
    }

    stroke_weight *= opacity_in;

    if (inner_in) {
        stroke_weight *= pixel_here.a;
    }

    // Make RGBA color
    vec4 stroke_col = color_in * stroke_weight;

    if (inner_in) {
        // Alpha over the stroke over the texture
        stroke_col = pixel_here * (1.0 - stroke_col.a) + stroke_col;
    } else {
        // Alpha over the texture over the stroke
        stroke_col = stroke_col * (1.0 - pixel_here.a) + pixel_here;
    }

    frag_color = stroke_col;
}
"#;

impl StrokeFilterNode {
	/// Fragment shader for any request (C++ `get_shader_code()` ignores
	/// the request id and always returns `stroke.frag`).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for StrokeFilterNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Stroke"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.stroke"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Filter]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Creates a stroke outline around an image."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` -> "Input",
	/// `color_in` -> "Color", `radius_in` -> "Radius", `opacity_in` ->
	/// "Opacity", `inner_in` -> "Inner".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// radius <= 0.0 or opacity <= 0.0 -> pass-through push of the input
	/// texture; otherwise push a shader job with `resolution_in` set to
	/// the texture's virtual resolution.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): the request id is
	/// ignored; always returns the stroke fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `StrokeFilterNode::StrokeFilterNode()`): adds
/// `tex_in`, `color_in`, `radius_in`, `opacity_in`, `inner_in` with the
/// defaults and properties documented on the constants, then sets the
/// video-effect flag and the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_stroke_filter` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.stroke",
		name: "Stroke",
		categories: &[Category::Filter],
		create,
	});
}
