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

//! Corner pin distort effect (C++
//! `src/node/src/distort/cornerpin/cornerpindistortnode.{h,cpp}`,
//! `olive::CornerPinDistortNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, Gizmo, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Perspective-correct interpolation input id (C++
/// `k_perspective_input`). Type: bool; default `true`.
pub const PERSPECTIVE_INPUT: &str = "perspective_in";

/// Top-left corner offset input id (C++ `k_top_left_input`). Type:
/// vec2; default `(0.0, 0.0)`; property `offset` is set per-frame by
/// `update_gizmo_positions()` to the corner's pixel origin.
pub const TOP_LEFT_INPUT: &str = "top_left_in";

/// Top-right corner offset input id (C++ `k_top_right_input`). Type:
/// vec2; default `(0.0, 0.0)`; property `offset` is set per-frame to
/// `(resolution.x, 0)`.
pub const TOP_RIGHT_INPUT: &str = "top_right_in";

/// Bottom-right corner offset input id (C++ `k_bottom_right_input`).
/// Type: vec2; default `(0.0, 0.0)`; property `offset` is set per-frame
/// to the full resolution.
pub const BOTTOM_RIGHT_INPUT: &str = "bottom_right_in";

/// Bottom-left corner offset input id (C++ `k_bottom_left_input`).
/// Type: vec2; default `(0.0, 0.0)`; property `offset` is set per-frame
/// to `(0, resolution.y)`.
pub const BOTTOM_LEFT_INPUT: &str = "bottom_left_in";

/// Number of corner point gizmos (C++ `k_gizmo_corner_count`).
pub const GIZMO_CORNER_COUNT: usize = 4;

/// Corner pin distort node. Warps the image by dragging its four
/// corners, optionally with perspective-correct interpolation.
pub struct CornerPinDistortNode {
	/// Corner drag handles, one per corner in top-left, top-right,
	/// bottom-right, bottom-left order (C++
	/// `PointGizmo *gizmo_resize_handle_[k_gizmo_corner_count]`; each
	/// drags both tracks of its corner input).
	gizmo_resize_handle: [Gizmo; GIZMO_CORNER_COUNT],
	/// Whole-quad outline gizmo (C++ `PolygonGizmo *gizmo_whole_rect_`;
	/// draggable but ignored by `gizmo_drag_move`, so dragging it does
	/// nothing).
	gizmo_whole_rect: Gizmo,
}

/// Fragment shader (C++ loads the `:/shaders/cornerpin.frag` resource
/// in `get_shader_code`). Text copied verbatim from
/// `engine/shaders/cornerpin.frag`.
const SHADER_FRAG: &str = r#"// Input texture
uniform sampler2D ove_maintex;
uniform sampler2D tex_in;
uniform bool perspective_in;

// Input texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

in vec2 q;
in vec2 b1;
in vec2 b2;
in vec2 b3;

float Wedge2D(vec2 v, vec2 w) {
  return (v.x*w.y) - (v.y*w.x);
}

void main() {
    if(perspective_in){
        frag_color = texture(tex_in, ove_texcoord);
    } else {
      float A = Wedge2D(b2, b3);
    float B = Wedge2D(b3, q) - Wedge2D(b1, b2);
    float C = Wedge2D(b1, q);

    vec2 uv;

    // solve for v
    if (abs(A) < 0.001) {
      uv.y = -C/B;
    } else {
      float discrim = B*B - 4.0*A*C;
      uv.y = 0.5 * (-B + sqrt(discrim)) / A;
    }

    // solve for u
    vec2 denom = b1 + uv.y * b3;
    if (abs(denom.x) > abs(denom.y)) {
      uv.x = (q.x - b2.x * uv.y) / denom.x;
    } else {
      uv.x = (q.y - b2.y * uv.y) / denom.y;
    }

    uv.y = 1.0 - uv.y;

    frag_color = texture(tex_in, uv);
  }
}
"#;

/// Vertex shader (C++ loads the `:/shaders/cornerpin.vert` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/cornerpin.vert`.
const SHADER_VERT: &str = r#"uniform bool perspective_in;
uniform vec2 top_left_in;
uniform vec2 top_right_in;
uniform vec2 bottom_left_in;
uniform vec2 bottom_right_in;

uniform vec2 resolution_in;

uniform mat4 ove_mvpmat;

in vec4 a_position;
in vec2 a_texcoord;

out vec2 ove_texcoord;

out vec2 q;
out vec2 b1;
out vec2 b2;
out vec2 b3;

void main() {
    // The slider inputs only contain the amount they have changed rather than
    // their pixel locations so we adjust them here.
    vec2 t_l = top_left_in;
    vec2 t_r = top_right_in + vec2(resolution_in.x, 0.0);
    vec2 b_r = bottom_right_in + resolution_in;
    vec2 b_l = bottom_left_in + vec2(0.0, resolution_in.y);

    gl_Position = ove_mvpmat * a_position;

    if (perspective_in){
        // Find the center of the quadrilateral by finding where the two diagonals intersect.
        // https://www.reedbeta.com/blog/quadrilateral-interpolation-part-1/

        // Here we calculate the gradient and constant (y = mx + c) for each diagonal.
        float m1 = (t_r.y - b_l.y)/(t_r.x - b_l.x);
        float c1 = b_l.y - m1 * b_l.x;
        float m2 = (b_r.y - t_l.y)/(b_r.x - t_l.x);
        float c2 = t_l.y - m2 * t_l.x;

        // Find the intersection by setting the two line equations equal and rearrange.
        float mid_x = (c2 - c1) / (m1 - m2);
        float mid_y = m1 * mid_x + c1;

        // Find the distance from each corner to our center point
        float d0 = length(vec2(mid_x - b_l.x, mid_y - b_l.y));
        float d1 = length(vec2(b_r.x - mid_x, mid_y - b_r.y));
        float d2 = length(vec2(t_r.x - mid_x, t_r.y - mid_y));
        float d3 = length(vec2(mid_x - t_l.x, t_l.y - mid_y));

        float q  = 1.0;

        /*
        Vertex IDs (aspect ratio irrelevant):
             0_____1
            3|\    |
             | \   |
             |  \  |
             |   \ |
             |____\|2
             4     5
        */

        if (gl_VertexID == 0 || gl_VertexID == 3) {
            q = (d1+d3)/d3;
        } else if (gl_VertexID == 1) {
            q = (d0+d2)/d2;
        } else if (gl_VertexID == 2 || gl_VertexID == 5) {
            q = (d3+d1)/d1;
        } else {
            q = (d2+d0)/d0;
        }

        gl_Position[0] *= q;
        gl_Position[1] *= q;
        gl_Position[3] = q;
    } else{
        // https://www.reedbeta.com/blog/quadrilateral-interpolation-part-2/
        vec2 pos;

        if (gl_VertexID == 0 || gl_VertexID == 3) { // top left
            pos = t_l;
        } else if (gl_VertexID == 1) { // top right
            pos = t_r;
        } else if (gl_VertexID == 2 || gl_VertexID == 5) { // bottom right
            pos = b_r;
        } else if (gl_VertexID == 4) { // bottom left
            pos = b_l;
        }

        q = pos - b_l;
        b1 = b_r - b_l;
        b2 = t_l - b_l;
        b3 = b_l - b_r - t_l + t_r;
    }


    ove_texcoord = a_texcoord;
}
"#;

impl CornerPinDistortNode {
	/// Fragment shader (C++ `get_shader_code()` frag half; the request
	/// id is ignored).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}

	/// Vertex shader (C++ `get_shader_code()` vert half; the request id
	/// is ignored).
	fn shader_vert() -> &'static str {
		SHADER_VERT
	}
}

impl NodeBehavior for CornerPinDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Corner Pin"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.cornerpin"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Distort the image by dragging the corners."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Texture", `perspective_in` -> "Perspective", `top_left_in` ->
	/// "Top Left", `top_right_in` -> "Top Right", `bottom_right_in` ->
	/// "Bottom Right", `bottom_left_in` -> "Bottom Left".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// all four corner sliders at their `(0, 0)` default -> pass-through
	/// push of the input texture unchanged; otherwise build a shader job
	/// with `resolution_in` inserted and custom vertex coordinates: each
	/// corner offset is converted to pixels via `value_to_pixel` and then
	/// to clip space (`/ half_resolution - 1.0`) and pushed as two
	/// triangles (TL, TR, BR / TL, BL, BR).
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
	/// request id and always returns the corner pin fragment and vertex
	/// shaders together.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Gizmo positions (C++ `update_gizmo_positions()`): with a texture,
	/// converts the four corner offsets to pixels (`value_to_pixel`,
	/// which adds the corner's resolution-based origin), sets each corner
	/// input's `offset` property to that origin, sets the polygon gizmo
	/// to the quad TL->TR->BR->BL->TL and each point gizmo to its
	/// corner. Also covers C++ `value_to_pixel()`.
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Gizmo drag (C++ `gizmo_drag_move()`): for a corner handle, drags
	/// both its X and Y track draggers by the mouse delta added to their
	/// drag-start values; dragging the whole-rect polygon gizmo is a
	/// no-op.
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `CornerPinDistortNode::CornerPinDistortNode()`):
/// adds `tex_in`, `perspective_in` and the four corner inputs with the
/// defaults and flags documented on the constants; creates the polygon
/// gizmo and the four corner point gizmos (each bound to both tracks of
/// its corner input); sets the video-effect flag and the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.cornerpin`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.cornerpin",
		name: "Corner Pin",
		categories: &[Category::Distort],
		create,
	});
}
