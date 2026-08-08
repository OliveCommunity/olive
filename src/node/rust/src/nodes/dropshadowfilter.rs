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

//! Drop shadow filter (C++
//! `src/node/src/filter/dropshadow/dropshadowfilter.{h,cpp}`,
//! `olive::DropShadowFilter`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Shadow color input id (C++ `k_color_input`). Type: color; default
/// black `(0.0, 0.0, 0.0)`.
pub const COLOR_INPUT: &str = "color_in";

/// Shadow distance input id (C++ `k_distance_input`). Type: float;
/// default `10.0`.
pub const DISTANCE_INPUT: &str = "distance_in";

/// Shadow angle input id (C++ `k_angle_input`). Type: float; default
/// `135.0`.
pub const ANGLE_INPUT: &str = "angle_in";

/// Shadow softness input id (C++ `k_softness_input`). Type: float;
/// default `10.0`; properties: `min = 0.0`.
pub const SOFTNESS_INPUT: &str = "radius_in";

/// Shadow opacity input id (C++ `k_opacity_input`). Type: float;
/// default `1.0`; properties: `min = 0.0`, `view = percentage`.
pub const OPACITY_INPUT: &str = "opacity_in";

/// Fast/low-quality toggle input id (C++ `k_fast_input`). Type: bool;
/// default `false`.
pub const FAST_INPUT: &str = "fast_in";

/// Drop shadow filter node. Adds a colored, blurred, offset copy of the
/// input's alpha behind the image. The C++ class declares no own member
/// fields.
pub struct DropShadowFilter;

/// Fragment shader (C++ `get_shader_code()` loads
/// `:/shaders/dropshadow.frag` via FileFunctions for any request). Text
/// copied verbatim from `engine/shaders/dropshadow.frag`.
const SHADER_FRAG: &str = r#"uniform sampler2D tex_in;
uniform vec4 color_in;
uniform float distance_in;
uniform float angle_in;
uniform float radius_in;
uniform float opacity_in;
uniform vec2 resolution_in;
uniform sampler2D previous_iteration_in;
uniform bool fast_in;

uniform int ove_iteration;

in vec2 ove_texcoord;
out vec4 frag_color;

// Gaussian function uses PI
#define M_PI 3.1415926535897932384626433832795

// Single gaussian formula (unused, mainly here for documentation/just in case)
//float gaussian(float x, float sigma) {
//    return (1.0/(sigma*sqrt(2.0*M_PI)))*exp(-0.5*pow(x/sigma, 2.0));
//}

// Double gaussian formula, actually used in the code below
// Should be faster than the single gaussian above since it doesn't need sqrt()
float gaussian2(float x, float y, float sigma) {
  return (1.0/((sigma*sigma)*2.0*M_PI))*exp(-0.5*(((x*x) + (y*y))/(sigma*sigma)));
}

void main(void) {
  if (ove_iteration == 2 || radius_in == 0.0) {

    // Merge step
    vec4 composite = texture(tex_in, ove_texcoord);
    if (composite.a < 1.0) {
      // Convert degrees to radians
      float shadow_angle = ((angle_in + 90.0)*M_PI)/180.0;

      vec2 shadow_offset = vec2(cos(shadow_angle) * distance_in, sin(shadow_angle) * distance_in);
      shadow_offset /= resolution_in;
      shadow_offset += ove_texcoord;

      vec4 shadow_color = texture(previous_iteration_in, shadow_offset);

      shadow_color.rgb = color_in.rgb * shadow_color.a;
      shadow_color *= 1.0 - composite.a;
      shadow_color *= opacity_in;

      composite += shadow_color;
    }
    frag_color = composite;

  } else {
    // We only sample on hard pixels, so we don't accept decimal radii
    float real_radius = ceil(radius_in);

    vec4 composite = vec4(0.0);

    float divider, sigma;

    if (fast_in) {

      // Calculate the weight of each pixel based on the radius
      divider = 1.0 / real_radius;

    } else {

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

    for (float i = -real_radius + 0.5; i <= real_radius; i += 2.0) {
      float weight;

      if (fast_in) {
        weight = divider;
      } else {
        weight = gaussian2(i, 0.0, sigma) / divider;
      }

      vec2 pixel_coord = ove_texcoord;
      vec4 tex_col;
      if (ove_iteration == 0) {
        pixel_coord.x += i / resolution_in.x;

        // Pull from main texture
        tex_col = texture(tex_in, pixel_coord);
      } else if (ove_iteration == 1) {
        pixel_coord.y += i / resolution_in.y;

        // Pull from previous iteration
        tex_col = texture(previous_iteration_in, pixel_coord);
      }

      composite += tex_col * weight;
    }

    frag_color = composite;
  }

}
"#;

impl DropShadowFilter {
	/// Fragment shader for any request (C++ `get_shader_code()` ignores
	/// the request id and always returns `dropshadow.frag`).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for DropShadowFilter {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Drop Shadow"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.dropshadow"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Filter]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Adds a drop shadow to an image."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Texture", `color_in` -> "Color", `distance_in` -> "Distance",
	/// `angle_in` -> "Angle", `radius_in` -> "Softness", `opacity_in` ->
	/// "Opacity", `fast_in` -> "Faster (Lower Quality)".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// otherwise push a shader job with `resolution_in` set to the
	/// texture's virtual resolution and `previous_iteration_in` bound to
	/// the input texture; when softness is non-zero the job runs 3
	/// iterations feeding back through `previous_iteration_in`.
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
	/// ignored; always returns the dropshadow fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `DropShadowFilter::DropShadowFilter()`): adds
/// `tex_in`, `color_in`, `distance_in`, `angle_in`, `radius_in`
/// (softness), `opacity_in`, `fast_in` with the defaults and properties
/// documented on the constants, then sets the effect input and the
/// video-effect flag.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_drop_shadow_filter` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.dropshadow",
		name: "Drop Shadow",
		categories: &[Category::Filter],
		create,
	});
}
