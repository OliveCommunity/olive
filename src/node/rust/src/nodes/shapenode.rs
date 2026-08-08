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

//! Shape generator (C++ `src/node/src/generator/shape/shapenode.{h,cpp}`,
//! `olive::ShapeNode`). Extends the C++ `ShapeNodeBase` /
//! `GeneratorWithMerge` bases (see [`super::shapenodebase`] and
//! [`super::generatorwithmerge`]).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Shape type input id (C++ `k_type_input`). Type: combo; prepended
/// ahead of the base inputs; combo strings (matching the C++ `Type`
/// enum and the `SHAPE_*` constants in the shader): 0 = "Rectangle",
/// 1 = "Ellipse", 2 = "Rounded Rectangle".
pub const TYPE_INPUT: &str = "type_in";

/// Corner radius input id (C++ `k_radius_input`). Type: float; default
/// `20.0`; properties: `min = 0.0`; hidden unless the type is
/// "Rounded Rectangle" (see `input_value_changed`).
pub const RADIUS_INPUT: &str = "radius_in";

/// Base inputs provided by the C++ bases: `base_in`
/// ([`super::generatorwithmerge::BASE_INPUT`]), and `pos_in` /
/// `size_in` / `color_in` ([`super::shapenodebase`]).

/// Shape generator node. Has no own member fields in C++ beyond the
/// base classes (inputs and gizmos live in the core).
pub struct ShapeNode;

/// Fragment shader for the `"shape"` shader id (C++ loads
/// `:/shaders/shape.frag` in `get_shader_code`). Text copied verbatim
/// from `engine/shaders/shape.frag`.
const SHADER_FRAG: &str = r#"// Input texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

// Match with ShapeNode::Type
const int SHAPE_RECTANGLE = 0;
const int SHAPE_ELLIPSE = 1;
const int SHAPE_ROUNDEDRECT = 2;

uniform vec2 pos_in;
uniform vec2 size_in;
uniform int type_in;
uniform vec2 resolution_in;
uniform vec4 color_in;
uniform float radius_in;

vec4 draw_rect(vec2 real_position, vec2 real_size)
{
  if (ove_texcoord.x >= real_position.x && ove_texcoord.y >= real_position.y
      && ove_texcoord.x < real_position.x+real_size.x && ove_texcoord.y < real_position.y+real_size.y) {
    return color_in;
  } else {
    return vec4(0.0, 0.0, 0.0, 0.0);
  }
}

vec4 draw_ellipse(vec2 center, float radius, float aspect_ratio) {
  vec2 offset = ove_texcoord*resolution_in - center;
  offset.x /= aspect_ratio;
  float d = length(offset)-radius;
  float t = clamp(d, 0.0, 1.0);

  return color_in * (1.0-t);
}

void main() {
  vec2 p = pos_in + resolution_in*0.5 - size_in*0.5;

  vec2 real_position = p/resolution_in;
  vec2 real_size = size_in/resolution_in;

  vec4 col = vec4(0.0);

  switch (type_in) {
  case SHAPE_RECTANGLE:
  {
    col = draw_rect(real_position, real_size);
    break;
  }
  case SHAPE_ELLIPSE:
  {
    vec2 center = p+size_in*0.5;
    float radius = size_in.y*0.5;
    float aspect_ratio = size_in.x/size_in.y;
    col = draw_ellipse(center, radius, aspect_ratio);
    break;
  }
  case SHAPE_ROUNDEDRECT:
  {
    // Limit radius so it is never larger than half the shortest size
    float r = min(radius_in, min(size_in.y*0.5, size_in.x*0.5));
    vec2 real_rad = vec2(r / resolution_in.x, r / resolution_in.y);
    if (ove_texcoord.x < real_position.x + real_rad.x && ove_texcoord.y < real_position.y + real_rad.y) {
      // Top-left
      col = draw_ellipse(p + r, r, 1.0);
    } else if (ove_texcoord.x > real_position.x+real_size.x - real_rad.x && ove_texcoord.y < real_position.y + real_rad.y) {
      // Top-right
      col = draw_ellipse(vec2(p.x + size_in.x - r, p.y + r), r, 1.0);
    } else if (ove_texcoord.x < real_position.x + real_rad.x && ove_texcoord.y > real_position.y + real_size.y - real_rad.y) {
      // Bottom-left
      col = draw_ellipse(vec2(p.x + r, p.y + size_in.y - r), r, 1.0);
    } else if (ove_texcoord.x > real_position.x+real_size.x - real_rad.x && ove_texcoord.y > real_position.y + real_size.y - real_rad.y) {
      // Bottom-right
      col = draw_ellipse(vec2(p.x + size_in.x - r, p.y + size_in.y - r), r, 1.0);
    } else {
      col = draw_rect(real_position, real_size);
    }
    break;
  }
  }

  frag_color = col;
}
"#;

impl ShapeNode {
	/// Fragment shader for the `"shape"` request (C++
	/// `get_shader_code()` `"shape"` branch).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for ShapeNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Shape"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.shape"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Generator]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Generate a 2D primitive shape."
	}

	/// Localized input names (C++ `retranslate()`): the base names
	/// (`base_in` "Base", `pos_in` "Position", `size_in` "Size",
	/// `color_in` "Color") plus `type_in` -> "Type" and `radius_in` ->
	/// "Radius"; also sets the `type_in` combo strings to
	/// "Rectangle"/"Ellipse"/"Rounded Rectangle".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): builds a `"shape"` shader job
	/// from the input row, inserting `resolution_in` (the base
	/// texture's virtual resolution when connected, else the sequence
	/// square resolution), renders it at the base texture's params (or
	/// the sequence video params), and pushes it through
	/// `push_mergable_job` (merged over `base_in` when connected).
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): `"shape"` returns
	/// the shape fragment shader; any other request falls through to
	/// the merge base (`"mrg"` -> alpha-over shader, else empty).
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Input value changed (C++ `InputValueChangedEvent`): when
	/// `type_in` changes, sets the hidden flag on `radius_in` unless
	/// the selected type is `k_rounded_rectangle` (2); then chains to
	/// the base implementation.
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `ShapeNode::ShapeNode()`): on top of the
/// `ShapeNodeBase` constructor (which adds `base_in`, `pos_in`,
/// `size_in`, `color_in` and the gizmos), prepends the `type_in` combo
/// and adds `radius_in` with the default and property documented on the
/// constant.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.shape`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.shape",
		name: "Shape",
		categories: &[Category::Generator],
		create,
	});
}
