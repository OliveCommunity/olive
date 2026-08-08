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

//! Color Difference Key effect (C++
//! `src/node/src/keying/colordifferencekey/colordifferencekey.{h,cpp}`,
//! `olive::ColorDifferenceKeyNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Garbage matte texture input id (C++ `k_garbage_matte_input`). Type:
/// texture; flags: not-keyframable.
pub const GARBAGE_MATTE_INPUT: &str = "garbage_in";

/// Core matte texture input id (C++ `k_core_matte_input`). Type:
/// texture; flags: not-keyframable.
pub const CORE_MATTE_INPUT: &str = "core_in";

/// Key color input id (C++ `k_color_input`). Type: combo; default `0`;
/// combo strings: "Green", "Blue" (set in `retranslate()`).
pub const COLOR_INPUT: &str = "color_in";

/// Shadows input id (C++ `k_shadows_input`). Type: float; default
/// `1.0`; properties: `min = 0.0`, `base = 0.01`.
pub const SHADOWS_INPUT: &str = "shadows_in";

/// Highlights input id (C++ `k_highlights_input`). Type: float;
/// default `1.0`; properties: `min = 0.0`, `base = 0.01`.
pub const HIGHLIGHTS_INPUT: &str = "highlights_in";

/// Show-mask-only toggle input id (C++ `k_mask_only_input`). Type:
/// boolean; default `false`.
pub const MASK_ONLY_INPUT: &str = "mask_only_in";

/// Color difference key node: keys on how far one channel (green or
/// blue) stands out from the other two, with optional garbage/core
/// mattes. The C++ class has no own members.
pub struct ColorDifferenceKeyNode;

/// Fragment shader (C++ loads the `:/shaders/colordifferencekey.frag`
/// resource in `get_shader_code`). Text copied verbatim from
/// `engine/shaders/colordifferencekey.frag`.
const SHADER_FRAG: &str = r#"uniform sampler2D tex_in;
uniform sampler2D garbage_in;
uniform sampler2D core_in;
uniform int color_in;
uniform bool garbage_in_enabled;
uniform bool core_in_enabled;
uniform float highlights_in;
uniform float shadows_in;
uniform bool mask_only_in;

in vec2 ove_texcoord;
out vec4 frag_color;

#define SCREEN_COLOR_GREEN 0
#define SCREEN_COLOR_BLUE  1

void main(void) {
    vec4 tex_col = texture(tex_in, ove_texcoord);

    // Unassociate RGB before calculating values
    vec4 unassoc = tex_col;
    if (unassoc.a > 0) {
      unassoc.rgb /= unassoc.a;
    }

    // Simple keyer, generates a inverted mask (background is white, foreground black)
    float mask;
    if (color_in == SCREEN_COLOR_GREEN) {
      mask = (unassoc.g - max(unassoc.r, unassoc.b));
    } else{ // Assume SCREEN_COLOR_BLUE
      mask = (unassoc.b - max(unassoc.r, unassoc.g));
    }

    mask = clamp(mask, 0.0, 1.0);

    if (garbage_in_enabled) {
      // Force anything we want to remove to be 1.0
      vec4 garbage = texture(garbage_in, ove_texcoord);
      // Assumes garbage is achromatic
      mask += garbage.r;
      mask = clamp(mask, 0.0, 1.0);
    }

    if (core_in_enabled) {
      // Force anything we want to keep to be 0.1
      vec3 core = texture(core_in, ove_texcoord).rgb;
      vec3 core_invert = 1.0 - core.rgb;
      // Assumes core is achromatic
      mask *= core_invert.r;
      mask = clamp(mask, 0.0, 1.0);
    }

    // Crush blacks and push whites
    mask = highlights_in * (shadows_in * mask - 1.0) + 1.0;
    mask = clamp(mask, 0.0, 1.0);

    // Invert mask
    mask = 1.0 - mask;

    // Multiply color by mask
    tex_col *= mask;

    if (!mask_only_in) {
        frag_color = tex_col;
    } else {
        frag_color = vec4(vec3(mask), 1.0);
    }
}
"#;

impl ColorDifferenceKeyNode {
	/// Fragment shader (C++ `get_shader_code()`; the request is
	/// ignored — there is a single shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for ColorDifferenceKeyNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Color Difference Key"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.colordifferencekey"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Keying]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"A simple color key based on the distance of one color from other colors."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Input", `garbage_in` -> "Garbage Matte", `core_in` ->
	/// "Core Matte", `color_in` -> "Key Color" (combo strings
	/// "Green"/"Blue"), `shadows_in` -> "Shadows", `highlights_in`
	/// -> "Highlights", `mask_only_in` -> "Show Mask Only".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture on `tex_in` ->
	/// push nothing; texture present -> push a `ShaderJob` with the
	/// whole input row inserted.
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

/// Constructor (C++ `ColorDifferenceKeyNode::ColorDifferenceKeyNode()`):
/// adds `tex_in`, `garbage_in`, `core_in`, `color_in`,
/// `highlights_in`, `shadows_in`, and `mask_only_in` with the defaults
/// and properties documented on the constants, sets the video-effect
/// flag, and makes `tex_in` the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.colordifferencekey`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.colordifferencekey",
		name: "Color Difference Key",
		categories: &[Category::Keying],
		create,
	});
}
