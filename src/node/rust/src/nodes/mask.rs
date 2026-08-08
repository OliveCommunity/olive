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

//! Mask distort effect (C++ `src/node/src/distort/mask/mask.{h,cpp}`,
//! `olive::MaskDistortNode`). In C++ this derives from
//! `PolygonGenerator` (which derives from `GeneratorWithMerge`), so the
//! polygon point editing, the `base_in` merge input and the
//! `points_in`/`color_in` inputs are inherited; the base's Rust home is
//! `super::polygon`.

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Invert input id (C++ `k_invert_input`). Type: bool; default `false`.
pub const INVERT_INPUT: &str = "invert_in";

/// Feather input id (C++ `k_feather_input`). Type: float; default
/// `0.0`; properties: `min = 0.0`.
pub const FEATHER_INPUT: &str = "feather_in";

/// Mask distort node. Renders the inherited polygon as a white mask and
/// multiplies it over the base texture, with optional invert and
/// feather (gaussian blur of the matte). Has no own member fields in
/// C++ beyond the inherited `PolygonGenerator` state.
pub struct MaskDistortNode {
	/// Inherited polygon-generator state (C++ base class
	/// `PolygonGenerator`; provides `points_in`, `color_in`, the base
	/// merge input `base_in` and the polygon shape shader).
	pub polygon: super::polygon::PolygonGenerator,
}

/// Merge fragment shader for the `"mrg"` shader id (C++ loads
/// `:/shaders/multiply.frag`). Text copied verbatim from
/// `engine/shaders/multiply.frag`.
const SHADER_MRG_FRAG: &str = r#"// Input texture
uniform sampler2D tex_a;
uniform sampler2D tex_b;

// Input texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

void main() {
    frag_color = texture(tex_a, ove_texcoord) * texture(tex_b, ove_texcoord);
}
"#;

/// Invert fragment shader for the `"invert"` shader id (C++ loads
/// `:/shaders/invertrgba.frag`). Text copied verbatim from
/// `engine/shaders/invertrgba.frag`.
const SHADER_INVERT_FRAG: &str = r#"// Input texture
uniform sampler2D tex_in;

// Input texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

void main() {
    vec4 color = texture(tex_in, ove_texcoord);
    color = 1.0 - color;
    frag_color = color;
}
"#;

/// Feather (gaussian blur) fragment shader for the `"feather"` shader
/// id (C++ loads `:/shaders/blur.frag`). Text copied verbatim from
/// `engine/shaders/blur.frag`.
const SHADER_FEATHER_FRAG: &str = r#"uniform sampler2D tex_in;
uniform int method_in;
uniform float radius_in;
uniform bool horiz_in;
uniform bool vert_in;
uniform bool repeat_edge_pixels_in;
uniform vec2 resolution_in;

// Directional
uniform float directional_degrees_in;

// Radial
uniform vec2 radial_center_in;

uniform int ove_iteration;

in vec2 ove_texcoord;
out vec4 frag_color;

// Gaussian function uses PI
#define M_PI 3.1415926535897932384626433832795

// Methods
#define METHOD_BOX_BLUR 0
#define METHOD_GAUSSIAN_BLUR 1
#define METHOD_DIRECTIONAL_BLUR 2
#define METHOD_RADIAL_BLUR 3

// Mode
#define MODE_NONE 0
#define MODE_HORIZONTAL 1
#define MODE_VERTICAL 2

// Single gaussian formula (unused, mainly here for documentation/just in case)
//float gaussian(float x, float sigma) {
//    return (1.0/(sigma*sqrt(2.0*M_PI)))*exp(-0.5*pow(x/sigma, 2.0));
//}

// Double gaussian formula, actually used in the code below
// Should be faster than the single gaussian above since it doesn't need sqrt()
float gaussian2(float x, float y, float sigma) {
    return (1.0/((sigma*sigma)*2.0*M_PI))*exp(-0.5*(((x*x) + (y*y))/(sigma*sigma)));
}

int determine_mode() {
    if (radius_in == 0.0) {
        return MODE_NONE;
    }

    if (!horiz_in && !vert_in) {
        return MODE_NONE;
    }

    if (horiz_in && !vert_in) {
        return MODE_HORIZONTAL;
    }

    if (vert_in && !horiz_in) {
        return MODE_VERTICAL;
    }

    if (ove_iteration == 0) {
        return MODE_HORIZONTAL;
    }

    if (ove_iteration == 1) {
        return MODE_VERTICAL;
    }
}

vec4 add_to_composite(vec4 composite, vec2 pixel_coord, float weight)
{
  if (repeat_edge_pixels_in
      || (pixel_coord.x >= 0.0
          && pixel_coord.x < 1.0
          && pixel_coord.y >= 0.0
          && pixel_coord.y < 1.0)) {
      composite += texture(tex_in, pixel_coord) * weight;
  }

  return composite;
}

void main(void) {
    int mode = determine_mode();

    if (mode == MODE_NONE) {
        frag_color = texture(tex_in, ove_texcoord);
        return;
    }

    // We only sample on hard pixels, so we don't accept decimal radii
    float real_radius = ceil(radius_in);

    vec4 composite = vec4(0.0);

    float divider, sigma;

    if (method_in == METHOD_DIRECTIONAL_BLUR || method_in == METHOD_RADIAL_BLUR) {
      // Despite similar math, these are lighter methods perceptually, so we double the radius to
      // better match box/gaussian
      real_radius *= 2.0;
    }

    if (method_in == METHOD_BOX_BLUR || method_in == METHOD_DIRECTIONAL_BLUR) {

        // Calculate the weight of each pixel based on the radius
        divider = 1.0 / real_radius;

    } else if (method_in == METHOD_GAUSSIAN_BLUR) {

        // Using (radius = 3 * sigma) because 3 standard deviations covers 97% of the blur according to this document:
        // http://chemaguerra.com/gaussian-filter-radius/
        sigma = real_radius;
        real_radius *= 3.0;

        // Use gaussian formula to calculate the weight of all pixels
        divider = 0.0;
        for (float i = -real_radius + 0.5; i <= real_radius; i += 2.0) {
            divider += gaussian2(i, 0.0, sigma);
        }

    }

    if (method_in == METHOD_BOX_BLUR || method_in == METHOD_GAUSSIAN_BLUR) {
        for (float i = -real_radius + 0.5; i <= real_radius; i += 2.0) {
            float weight;

            if (method_in == METHOD_BOX_BLUR) {
                weight = divider;
            } else if (method_in == METHOD_GAUSSIAN_BLUR) {
                weight = gaussian2(i, 0.0, sigma) / divider;
            }

            vec2 pixel_coord = ove_texcoord;
            if (mode == MODE_HORIZONTAL) {
                pixel_coord.x += i / resolution_in.x;
            } else if (mode == MODE_VERTICAL) {
                pixel_coord.y += i / resolution_in.y;
            }

            composite = add_to_composite(composite, pixel_coord, weight);
        }
    } else if (method_in == METHOD_DIRECTIONAL_BLUR || method_in == METHOD_RADIAL_BLUR) {
        float angle;

        if (method_in == METHOD_DIRECTIONAL_BLUR) {
          // Convert directional degrees to radians
          angle = (directional_degrees_in*M_PI)/180.0;
        } else {
          // Calculate angle from distance of center to current coordinate
          vec2 distance = (ove_texcoord - 0.5) * (resolution_in) - radial_center_in;
          angle = atan(distance.y/distance.x);

          float multiplier = length(distance) / resolution_in.y * 2.0;

          real_radius = ceil(radius_in * multiplier);
          divider = 1.0 / real_radius;
        }

        // Get angles
        float sin_angle = sin(angle);
        float cos_angle = cos(angle);

        for (float i = -real_radius + 0.5; i <= real_radius; i += 2.0) {
          vec2 pixel_coord = ove_texcoord;

          pixel_coord.y += sin_angle * i / resolution_in.y;
          pixel_coord.x += cos_angle * i / resolution_in.x;

          composite = add_to_composite(composite, pixel_coord, divider);
        }
    }

    frag_color = composite;
}
"#;

impl MaskDistortNode {
	/// Merge fragment shader (C++ `get_shader_code()` `"mrg"` branch).
	fn shader_mrg_frag() -> &'static str {
		SHADER_MRG_FRAG
	}

	/// Invert fragment shader (C++ `get_shader_code()` `"invert"`
	/// branch).
	fn shader_invert_frag() -> &'static str {
		SHADER_INVERT_FRAG
	}

	/// Feather blur fragment shader (C++ `get_shader_code()`
	/// `"feather"` branch).
	fn shader_feather_frag() -> &'static str {
		SHADER_FEATHER_FRAG
	}
}

impl NodeBehavior for MaskDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Mask"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.mask"
	}

	/// Categories (C++ `category()`; note: C++ files Mask under
	/// distort even though it derives from a generator base).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Apply a polygonal mask."
	}

	/// Localized input names (C++ `retranslate()`): the inherited
	/// `base_in` -> "Texture", `invert_in` -> "Invert", `feather_in` ->
	/// "Feather" (plus the `PolygonGenerator` names via its own
	/// retranslate).
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): generates the polygon matte
	/// (via the inherited `get_generate_job`) at the base texture's
	/// params or the global video params when there is no base; if
	/// `invert_in` is set wraps the matte in an `"invert"` shader job;
	/// with a base texture pushes an `"mrg"` multiply merge of base
	/// (`tex_a`) and matte (`tex_b`) — where `feather_in` > 0.0 the
	/// matte is first nested in a two-iteration gaussian `"feather"`
	/// blur job (method gaussian, horiz/vert/repeat-edge true, radius =
	/// feather value, `resolution_in` from the texture or the global
	/// square resolution); without a base texture pushes the matte
	/// itself.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): dispatches on the
	/// request id — `"mrg"` -> multiply merge shader, `"feather"` ->
	/// blur shader, `"invert"` -> invert RGBA shader, anything else ->
	/// the inherited `PolygonGenerator` shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `MaskDistortNode::MaskDistortNode()`): hides the
/// inherited `color_in` input (the mask is always white so the multiply
/// works), then adds `invert_in` and `feather_in` with the defaults and
/// properties documented on the constants. Note: unlike the other
/// distort nodes, the C++ constructor does NOT set the video-effect
/// flag or an effect input here — that state comes from the
/// `PolygonGenerator`/`GeneratorWithMerge` base.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.mask`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.mask",
		name: "Mask",
		categories: &[Category::Distort],
		create,
	});
}
