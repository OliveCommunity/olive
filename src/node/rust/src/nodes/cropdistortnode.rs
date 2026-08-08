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

//! Crop distort effect (C++
//! `src/node/src/distort/crop/cropdistortnode.{h,cpp}`,
//! `olive::CropDistortNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, Gizmo, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Left crop input id (C++ `k_left_input`). Type: float; default `0.0`;
/// properties: `min = 0.0`, `max = 1.0`, `view = percentage` (created by
/// C++ `create_crop_side_input()`).
pub const LEFT_INPUT: &str = "left_in";

/// Top crop input id (C++ `k_top_input`). Type: float; default `0.0`;
/// properties: `min = 0.0`, `max = 1.0`, `view = percentage`.
pub const TOP_INPUT: &str = "top_in";

/// Right crop input id (C++ `k_right_input`). Type: float; default
/// `0.0`; properties: `min = 0.0`, `max = 1.0`, `view = percentage`.
pub const RIGHT_INPUT: &str = "right_in";

/// Bottom crop input id (C++ `k_bottom_input`). Type: float; default
/// `0.0`; properties: `min = 0.0`, `max = 1.0`, `view = percentage`.
pub const BOTTOM_INPUT: &str = "bottom_in";

/// Feather input id (C++ `k_feather_input`). Type: float; default
/// `0.0`; properties: `min = 0.0`.
pub const FEATHER_INPUT: &str = "feather_in";

/// Number of crop point gizmos (C++ `k_gizmo_scale_count` from
/// `node.h`: top-left, top-center, top-right, bottom-left,
/// bottom-center, bottom-right, center-left, center-right).
pub const GIZMO_SCALE_COUNT: usize = 8;

/// Crop distort node. Crops the edges of an image with an optional
/// feather.
pub struct CropDistortNode {
	/// Edge/corner drag handles in `k_gizmo_scale_*` order (C++
	/// `PointGizmo *point_gizmo_[k_gizmo_scale_count]`; each handle drags
	/// the one or two crop inputs it touches).
	point_gizmo: [Gizmo; GIZMO_SCALE_COUNT],
	/// Crop rectangle outline gizmo (C++ `PolygonGizmo *poly_gizmo_`;
	/// drags all four crop inputs).
	poly_gizmo: Gizmo,
	/// Resolution captured by the last `update_gizmo_positions()` call,
	/// used to normalize drag deltas (C++ `Vector2D temp_resolution_`).
	temp_resolution: (f64, f64),
}

/// Fragment shader (C++ loads the `:/shaders/crop.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/crop.frag`.
const SHADER_FRAG: &str = r#"// Input variables
uniform sampler2D tex_in;
uniform float left_in;
uniform float top_in;
uniform float right_in;
uniform float bottom_in;
uniform float feather_in;
uniform vec2 resolution_in;

// Input texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

void main() {
    float multiplier = 1.0;

    vec2 feather_normalized = vec2(feather_in / resolution_in.x, feather_in / resolution_in.y);
    vec2 feather_normalized_half = feather_normalized * 0.5;

    // Calculate left cropping
    float left_adjustment;
    float right_adjustment;
    float top_adjustment;
    float bottom_adjustment;

    if (feather_in == 0.0) {
        if (ove_texcoord.x < left_in
            || ove_texcoord.x > (1.0-right_in)
            || ove_texcoord.y < (top_in)
            || ove_texcoord.y > (1.0-bottom_in)) {
            multiplier = 0.0;
        }
    } else {
        float left_adjustment = clamp((ove_texcoord.x - (left_in - feather_normalized.x*(1.0-left_in))) / feather_normalized.x, 0.0, 1.0);
        multiplier *= left_adjustment;

        float right_adjustment = 1.0-clamp((ove_texcoord.x - ((1.0-right_in) - feather_normalized.x*(right_in))) / feather_normalized.x, 0.0, 1.0);
        multiplier *= right_adjustment;

        float top_adjustment = clamp((ove_texcoord.y - (top_in - feather_normalized.y*(1.0-top_in))) / feather_normalized.y, 0.0, 1.0);
        multiplier *= top_adjustment;

        float bottom_adjustment = 1.0-clamp((ove_texcoord.y - ((1.0-bottom_in) - feather_normalized.y*(bottom_in))) / feather_normalized.y, 0.0, 1.0);
        multiplier *= bottom_adjustment;
    }

    if (multiplier > 0.0) {
        vec4 color = texture(tex_in, ove_texcoord) * multiplier;
        frag_color = color;
    } else {
        frag_color = vec4(0.0);
    }
}
"#;

impl CropDistortNode {
	/// Fragment shader (C++ `get_shader_code()`; the request id is
	/// ignored, this is the only shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for CropDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Crop"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.crop"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Crop the edges of an image."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Texture", `left_in` -> "Left", `top_in` -> "Top", `right_in` ->
	/// "Right", `bottom_in` -> "Bottom", `feather_in` -> "Feather".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): copies the whole value row into
	/// a shader job and inserts `resolution_in` from the texture params;
	/// no texture -> push nothing; any of left/right/top/bottom != 0.0 ->
	/// shader job; all zero -> pass-through push of the input texture
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
	/// request id and always returns the crop fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Gizmo positions (C++ `update_gizmo_positions()`): with a texture,
	/// caches the resolution in `temp_resolution`, converts the four 0..1
	/// crop values to pixel points (left/top straight, right/bottom as
	/// `1.0 - value`), places the eight edge/corner point gizmos (center
	/// handles at the midpoints) and the rectangle polygon gizmo.
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Gizmo drag (C++ `gizmo_drag_move()`): normalizes the mouse delta
	/// by `temp_resolution`, then for each dragger of the current gizmo
	/// adds the delta to its drag-start value with a per-input sign
	/// (left `+x`, top `+y`, right `-x`, bottom `-y`).
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `CropDistortNode::CropDistortNode()`): adds
/// `tex_in`; adds the four crop side inputs via
/// `create_crop_side_input()` (float, default 0.0, min 0.0, max 1.0,
/// percentage view); adds `feather_in` (float, default 0.0, min 0.0);
/// creates the rectangle polygon gizmo and the eight point gizmos bound
/// to their crop inputs; sets the video-effect flag and the effect
/// input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.crop`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.crop",
		name: "Crop",
		categories: &[Category::Distort],
		create,
	});
}
