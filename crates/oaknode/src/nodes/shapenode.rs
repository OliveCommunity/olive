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
		// The base names come from `ShapeNodeBase::input_name` plus the
		// merge base's `base_in` "Base" (the C++ retranslate chain runs
		// `GeneratorWithMerge::retranslate` first).
		match id {
			TYPE_INPUT => "Type",
			RADIUS_INPUT => "Radius",
			super::generatorwithmerge::BASE_INPUT => "Base",
			_ => super::shapenodebase::ShapeNodeBase::input_name(id),
		}
	}

	/// Evaluate outputs (C++ `value()`): builds a `"shape"` shader job
	/// from the input row, inserting `resolution_in` (the base
	/// texture's virtual resolution when connected, else the sequence
	/// square resolution), renders it at the base texture's params (or
	/// the sequence video params), and pushes it through
	/// `push_mergable_job` (merged over `base_in` when connected).
	///
	/// The Rust model has no shader-job payload: the deferred job
	/// (including the `resolution_in` value and the `"shape"` shader id)
	/// is resolved by the renderer seam, so a null texture handle marks
	/// "renderer must produce this texture" (`// CPP-PARITY: shapenode.cpp`
	/// `value()`).
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		let _ = (core, time);
		super::generatorwithmerge::GeneratorWithMerge::push_mergable_job(
			inputs,
			crate::handle::CHandle::null(),
			table,
		);
	}

	/// Shader code request (C++ `get_shader_code()`): `"shape"` returns
	/// the shape fragment shader; any other request falls through to
	/// the merge base (`"mrg"` -> alpha-over shader, else empty).
	fn shader_code(&self, request: &str) -> Option<String> {
		match request {
			"shape" => Some(Self::shader_frag().to_string()),
			"mrg" => Some(super::generatorwithmerge::merge_shader_frag().to_string()),
			_ => None,
		}
	}

	/// Input value changed (C++ `InputValueChangedEvent`): when
	/// `type_in` changes, sets the hidden flag on `radius_in` unless
	/// the selected type is `k_rounded_rectangle` (2); then chains to
	/// the base implementation.
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		if input == TYPE_INPUT && element == -1 {
			let ty = core.standard_value(TYPE_INPUT, -1).to_double() as i64;
			if let Some(radius) = core.get_input_mut(RADIUS_INPUT) {
				if ty == 2 {
					radius.flags &= !crate::input::flags::HIDDEN;
				} else {
					radius.flags |= crate::input::flags::HIDDEN;
				}
			}
		}
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(ShapeNode))
	}
}

/// Constructor (C++ `ShapeNode::ShapeNode()`): on top of the
/// `ShapeNodeBase` constructor (which adds `base_in`, `pos_in`,
/// `size_in`, `color_in` and the gizmos), prepends the `type_in` combo
/// and adds `radius_in` with the default and property documented on the
/// constant.
///
/// Input order matches the C++: `enabled_in`, `type_in` (prepended
/// ahead of the base inputs), `base_in`, `pos_in`, `size_in`,
/// `color_in`, `radius_in`. The base's gizmos are GUI gizmos with no
/// Rust equivalent (see [`super::shapenodebase`]), so no gizmos are
/// registered.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();

	// ShapeNodeBase constructor (GeneratorWithMerge + shape inputs).
	let mut base = crate::input::Input::new(
		super::generatorwithmerge::BASE_INPUT,
		crate::value::ValueType::Texture,
		crate::value::NodeValue::None,
	);
	base.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(base);

	let pos = crate::input::Input::new(
		super::shapenodebase::POSITION_INPUT,
		crate::value::ValueType::Vec2,
		crate::value::NodeValue::Vec2([0.0, 0.0]),
	);
	core.add_input(pos);

	let mut size = crate::input::Input::new(
		super::shapenodebase::SIZE_INPUT,
		crate::value::ValueType::Vec2,
		crate::value::NodeValue::Vec2([100.0, 100.0]),
	);
	size.properties = vec![("min".to_string(), crate::value::NodeValue::Vec2([0.0, 0.0]))];
	core.add_input(size);

	let color = crate::input::Input::new(
		super::shapenodebase::COLOR_INPUT,
		crate::value::ValueType::Color,
		crate::value::NodeValue::Color([1.0, 0.0, 0.0, 1.0]),
	);
	core.add_input(color);

	// ShapeNode: prepend `type_in` ahead of the base inputs (index 1,
	// after `enabled_in`), then append `radius_in`.
	core.inputs.insert(
		1,
		crate::input::Input::new(
			TYPE_INPUT,
			crate::value::ValueType::Combo,
			crate::value::NodeValue::Combo(0),
		),
	);

	let mut radius = crate::input::Input::new(
		RADIUS_INPUT,
		crate::value::ValueType::Float,
		crate::value::NodeValue::Float(20.0),
	);
	radius.properties = vec![("min".to_string(), crate::value::NodeValue::Float(0.0))];
	core.add_input(radius);

	// GeneratorWithMerge constructor side effects.
	core.flags |= crate::node::flags::VIDEO_EFFECT;
	core.effect_input = super::generatorwithmerge::BASE_INPUT.to_string();

	(core, Box::new(ShapeNode))
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::node::NodeBehavior;
	use crate::value::{NodeValue, NodeValueTable, ValueType};
	use oakcore_rs::Rational;

	#[test]
	fn input_names() {
		let n = ShapeNode;
		assert_eq!(n.input_name(TYPE_INPUT), "Type");
		assert_eq!(n.input_name(RADIUS_INPUT), "Radius");
		assert_eq!(
			n.input_name(super::super::generatorwithmerge::BASE_INPUT),
			"Base"
		);
		assert_eq!(
			n.input_name(super::super::shapenodebase::POSITION_INPUT),
			"Position"
		);
		assert_eq!(
			n.input_name(super::super::shapenodebase::SIZE_INPUT),
			"Size"
		);
		assert_eq!(
			n.input_name(super::super::shapenodebase::COLOR_INPUT),
			"Color"
		);
	}

	#[test]
	fn create_wires_inputs_and_flags() {
		let (core, behavior) = create();
		assert_eq!(behavior.type_id(), "org.olivevideoeditor.Olive.shape");
		// `type_in` is prepended right after `enabled_in`.
		assert_eq!(core.inputs[1].id, TYPE_INPUT);
		assert_eq!(core.inputs[1].default, NodeValue::Combo(0));
		assert_eq!(
			core.get_input(RADIUS_INPUT).unwrap().default,
			NodeValue::Float(20.0)
		);
		assert_eq!(
			core.get_input(super::super::shapenodebase::SIZE_INPUT)
				.unwrap()
				.default,
			NodeValue::Vec2([100.0, 100.0])
		);
		assert_eq!(
			core.get_input(super::super::shapenodebase::COLOR_INPUT)
				.unwrap()
				.default,
			NodeValue::Color([1.0, 0.0, 0.0, 1.0])
		);
		assert_eq!(
			core.effect_input,
			super::super::generatorwithmerge::BASE_INPUT
		);
		assert_ne!(core.flags & crate::node::flags::VIDEO_EFFECT, 0);
	}

	#[test]
	fn value_pushes_deferred_job() {
		let (core, behavior) = create();
		let mut table = NodeValueTable::default();
		behavior.value(
			&core,
			&crate::value::NodeValueRow::default(),
			Rational::new(0, 1),
			&mut table,
		);
		assert!(table.get(ValueType::Texture).is_some());
	}

	#[test]
	fn shader_code_dispatches() {
		let n = ShapeNode;
		let shape = n.shader_code("shape").unwrap();
		assert!(shape.contains("const int SHAPE_RECTANGLE = 0;"));
		let mrg = n.shader_code("mrg").unwrap();
		assert!(mrg.contains("base_col *= 1.0 - blend_col.a;"));
		assert!(n.shader_code("other").is_none());
	}

	#[test]
	fn input_value_changed_toggles_radius_hidden() {
		let (mut core, behavior) = create();
		let mut b = behavior;
		// Default type is rectangle (0): radius_in becomes hidden.
		b.input_value_changed(&mut core, TYPE_INPUT, -1);
		assert_ne!(
			core.get_input(RADIUS_INPUT).unwrap().flags & crate::input::flags::HIDDEN,
			0
		);

		// Rounded rectangle (2): radius_in is shown.
		core.set_standard_value(TYPE_INPUT, -1, NodeValue::Combo(2));
		b.input_value_changed(&mut core, TYPE_INPUT, -1);
		assert_eq!(
			core.get_input(RADIUS_INPUT).unwrap().flags & crate::input::flags::HIDDEN,
			0
		);
	}

	#[test]
	fn duplicate_clones() {
		let (core, behavior) = create();
		let dup = behavior.duplicate(&core).unwrap();
		assert_eq!(dup.name(), "Shape");
	}
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
