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

//! Blur filter (C++ `src/node/src/filter/blur/blur.{h,cpp}`,
//! `olive::BlurFilterNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Blur method input id (C++ `k_method_input`). Type: combo; default
/// `k_gaussian` (1); flags: not-keyframable, not-connectable; combo box
/// strings: "Box", "Gaussian", "Directional", "Radial".
pub const METHOD_INPUT: &str = "method_in";

/// Blur radius input id (C++ `k_radius_input`). Type: float; default
/// `10.0`; properties: `min = 0.0`.
pub const RADIUS_INPUT: &str = "radius_in";

/// Horizontal blur toggle input id (C++ `k_horiz_input`). Type: bool;
/// default `true`; hidden unless the method is box or gaussian.
pub const HORIZ_INPUT: &str = "horiz_in";

/// Vertical blur toggle input id (C++ `k_vert_input`). Type: bool;
/// default `true`; hidden unless the method is box or gaussian.
pub const VERT_INPUT: &str = "vert_in";

/// Repeat-edge-pixels input id (C++ `k_repeat_edge_pixels_input`). Type:
/// bool; default `true`.
pub const REPEAT_EDGE_PIXELS_INPUT: &str = "repeat_edge_pixels_in";

/// Directional angle input id (C++ `k_directional_degrees_input`). Type:
/// float; default `0.0`; hidden unless the method is directional.
pub const DIRECTIONAL_DEGREES_INPUT: &str = "directional_degrees_in";

/// Radial center input id (C++ `k_radial_center_input`). Type: vec2;
/// default `(0.0, 0.0)`; hidden unless the method is radial; property
/// `offset` is set to half the texture resolution at gizmo-update time.
pub const RADIAL_CENTER_INPUT: &str = "radial_center_in";

/// Blur method enum (C++ `BlurFilterNode::Method`).
pub enum Method {
	/// Box blur.
	Box,
	/// Gaussian blur (the default).
	Gaussian,
	/// Directional blur.
	Directional,
	/// Radial blur.
	Radial,
}

/// Blur filter node. Box/gaussian/directional/radial blur, implemented
/// as one iterative shader.
///
/// C++ member `radial_center_gizmo_` is a Qt `PointGizmo` (GUI type);
/// gizmos live in `NodeCore::gizmos`, so no field is kept here.
pub struct BlurFilterNode;

/// Fragment shader (C++ `get_shader_code()` loads
/// `:/shaders/blur.frag` via FileFunctions for any request). Text
/// copied verbatim from `engine/shaders/blur.frag`.
const SHADER_FRAG: &str = r#"uniform sampler2D tex_in;
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

impl BlurFilterNode {
	/// Fragment shader for any request (C++ `get_shader_code()` ignores
	/// the request id and always returns `blur.frag`).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for BlurFilterNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Blur"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.blur"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Filter]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Blurs an image."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` -> "Input",
	/// `method_in` -> "Method" (combo strings "Box", "Gaussian",
	/// "Directional", "Radial"), `radius_in` -> "Radius", `horiz_in` ->
	/// "Horizontal", `vert_in` -> "Vertical", `repeat_edge_pixels_in` ->
	/// "Repeat Edge Pixels", `directional_degrees_in` -> "Direction",
	/// `radial_center_in` -> "Center".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// radius <= 0.0, or box/gaussian with both horiz and vert unchecked
	/// -> pass-through push of the input texture; otherwise push a shader
	/// job with `resolution_in` set to the texture's virtual resolution,
	/// running 2 iterations for box/gaussian when both horiz and vert are
	/// checked (1 otherwise).
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
	/// ignored; always returns the blur fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Gizmo positions (C++ `update_gizmo_positions()`): when the method
	/// is radial and a texture is present, show the radial-center gizmo
	/// at half the texture resolution plus the center input, and set the
	/// input's `offset` property to half the resolution; otherwise hide
	/// the gizmo.
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Gizmo drag (C++ `gizmo_drag_move()`): when the current gizmo is
	/// the radial-center gizmo, drag its x/y input draggers by the drag
	/// delta.
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		todo!()
	}

	/// Input value changed (C++ `InputValueChangedEvent()`): on
	/// `method_in` changes, re-run the hidden-flag update (`horiz_in` /
	/// `vert_in` shown only for box/gaussian, `directional_degrees_in`
	/// only for directional, `radial_center_in` only for radial), then
	/// defer to the base implementation.
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `BlurFilterNode::BlurFilterNode()`): adds `tex_in`,
/// `method_in` (default gaussian), `radius_in`, `horiz_in`/`vert_in`,
/// `directional_degrees_in`, `radial_center_in` with the defaults and
/// properties documented on the constants, hides the method-specific
/// inputs for the default method, adds `repeat_edge_pixels_in`, sets
/// the video-effect flag and the effect input, and adds a draggable
/// anchor-point gizmo bound to both tracks of `radial_center_in`.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_blur_filter` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.blur",
		name: "Blur",
		categories: &[Category::Filter],
		create,
	});
}
