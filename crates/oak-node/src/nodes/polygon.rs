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

//! Polygon generator (C++ `src/node/src/generator/polygon/polygon.{h,cpp}`,
//! `olive::PolygonGenerator`). Extends the C++ `GeneratorWithMerge`
//! base (see [`super::generatorwithmerge`]).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Points array input id (C++ `k_points_input`). Type: bezier; flags:
/// array. The C++ constructor resizes the array to 5 and sets split
/// standard values to form a default pentagon: `(0, -135)`,
/// `(135, -45)`, `(90, 120)`, `(-90, 120)`, `(-135, -45)`.
pub const POINTS_INPUT: &str = "points_in";

/// Color input id (C++ `k_color_input`). Type: color; default
/// `(1.0, 1.0, 1.0)` (opaque white).
pub const COLOR_INPUT: &str = "color_in";

/// Polygon generator node.
///
/// The C++ members `poly_gizmo_` (PathGizmo), `gizmo_position_handles_`,
/// `gizmo_bezier_handles_` and `gizmo_bezier_lines_` are GUI gizmo
/// pointers with no Rust equivalent here; gizmos are tracked in
/// `NodeCore::gizmos`, so they are omitted.
pub struct PolygonGenerator;

/// Fragment shader for the `"rgb"` shader id (C++ loads
/// `:/shaders/rgb.frag` in `get_shader_code`), recoloring the rasterized
/// polygon mask with the color input. Text copied verbatim from
/// `engine/shaders/rgb.frag`.
const RGB_SHADER_FRAG: &str = r#"// Input texture
uniform sampler2D texture_in;

// Input texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

// Input color
uniform vec4 color_in;

void main() {
  vec4 color = texture(texture_in, ove_texcoord);
  color.rgb = color_in.rgb * color.a;
  frag_color = color;
}
"#;

impl PolygonGenerator {
	/// Fragment shader for the `"rgb"` request (C++
	/// `get_shader_code()` `"rgb"` branch).
	fn rgb_shader_frag() -> &'static str {
		RGB_SHADER_FRAG
	}
}

impl NodeBehavior for PolygonGenerator {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Polygon"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.polygon"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Generator]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Generate a 2D polygon of any amount of points."
	}

	/// Localized input names (C++ `retranslate()`): the merge-base name
	/// (`base_in` "Base") plus `points_in` -> "Points" and `color_in`
	/// -> "Color".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		match id {
			super::generatorwithmerge::BASE_INPUT => "Base",
			POINTS_INPUT => "Points",
			COLOR_INPUT => "Color",
			_ => id,
		}
	}

	/// Evaluate outputs (C++ `value()`): wraps the generate job
	/// (rasterized at u8 pixel format, then recolored by an `"rgb"`
	/// shader job sampling it as `texture_in` with `color_in`) in a
	/// texture at the sequence video params and pushes it through
	/// `push_mergable_job` (merged over `base_in` when connected).
	///
	/// The C++ chain starts with `get_generate_job()` — a CPU
	/// rasterization of the polygon path (QPainterPath bezier fill into
	/// an RGBA8888 frame via `generate_frame()`), which a
	/// [`ShaderJobPayload`] cannot express: the payload has no generate
	/// phase, and the rasterize -> `"rgb"` recolor -> optional `"mrg"`
	/// alpha-over chain has no Rust equivalent. The output is kept as a
	/// null texture handle marking "renderer must produce this texture";
	/// expressing the chain as payloads is a renderer TODO
	/// (`// CPP-PARITY: polygon.cpp` `value()`).
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oak_core::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		let _ = (core, inputs, time);
		table.push(
			crate::value::ValueType::Texture,
			crate::value::NodeValue::Texture(crate::handle::CHandle::null()),
			None,
		);
	}

	/// Direct frame generation (C++ `generate_frame()`): clears the RGBA
	/// buffer to transparent, builds a closed cubic path through the
	/// bezier points (each segment from the previous point's control
	/// point 2 through the next point's control point 1 to the next
	/// point, closing back to the first), and fills it via the
	/// facade-installed path-fill backend with divider/pixel-aspect
	/// scaling and center translation; without a backend the frame is
	/// left empty (warned once).
	///
	/// The Rust `frame` is an opaque [`crate::handle::CHandle`]
	/// whose bytes cannot be touched, and this crate has no path-fill
	/// backend — so neither the clear nor the fill is representable here
	/// (`// CPP-PARITY: polygon.cpp` `generate_frame`). The path building
	/// itself (C++ `generate_path`/`add_point_to_path`) operates on the
	/// C++ `PainterPath` GUI type, which also has no Rust counterpart.
	fn generate_frame(
		&self,
		core: &NodeCore,
		frame: &mut crate::handle::CHandle,
		time: oak_core::Rational,
	) {
		let _ = (core, frame, time);
	}

	/// Gizmo layout (C++ `update_gizmo_positions()`): resolution comes
	/// from the base texture's virtual resolution or the square
	/// resolution; grows/shrinks the per-point position handles and the
	/// 2-per-point bezier handles/lines to the point count, wires new
	/// handles to the corresponding `points_in` tracks (position:
	/// tracks 0/1; bezier: tracks 2/3 and 4/5, circle-shaped and
	/// smaller), and places every handle/line at point + half
	/// resolution.
	///
	/// The resolution comes from the texture's virtual resolution or the
	/// globals' square resolution (neither available here), and the
	/// per-point/bezier handle placements have no storage in [`Gizmo`] —
	/// not representable (`// CPP-PARITY: polygon.cpp`
	/// `update_gizmo_positions`).
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		let _ = (core, row);
	}

	/// Gizmo drag (C++ `gizmo_drag_move()`): dragging the path gizmo
	/// itself is a no-op (C++ FIXME: drag all points); dragging any
	/// other gizmo offsets its x/y input draggers by the drag delta
	/// from their start values.
	///
	/// The draggers hold per-drag start values and write keyframe tracks,
	/// neither of which the Rust data model carries — not representable
	/// (`// CPP-PARITY: polygon.cpp` `gizmo_drag_move`).
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		let _ = (core, start, x, y, modifiers);
	}

	/// Shader code request (C++ `get_shader_code()`): `"rgb"` returns
	/// the recolor fragment shader; any other request falls through to
	/// the merge base (`"mrg"` -> alpha-over shader, else empty).
	fn shader_code(&self, request: &str) -> Option<String> {
		match request {
			"rgb" => Some(Self::rgb_shader_frag().to_string()),
			"mrg" => Some(super::generatorwithmerge::merge_shader_frag().to_string()),
			_ => None,
		}
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(PolygonGenerator))
	}
}

/// Constructor (C++ `PolygonGenerator::PolygonGenerator()`): on top of
/// the `GeneratorWithMerge` constructor (which adds `base_in`), adds
/// the `points_in` bezier array and `color_in`, resizes the array to
/// the default pentagon documented on [`POINTS_INPUT`], and creates the
/// path gizmo.
///
/// C++ declares `points_in` as `k_bezier` (6 tracks per element: the
/// point and two control points); the crate has no bezier value type, so
/// [`ValueType::Vec4`] substitutes and only the position tracks (0/1)
/// are carried by the standard values below — the control-point tracks
/// default to zero and are dropped (`// CPP-PARITY: polygon.cpp`
/// constructor).
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();

	// GeneratorWithMerge constructor: `base_in` texture input, made the
	// effect input, video-effect flag.
	let mut base = crate::input::Input::new(
		super::generatorwithmerge::BASE_INPUT,
		crate::value::ValueType::Texture,
		crate::value::NodeValue::None,
	);
	base.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(base);

	let mut points = crate::input::Input::new(
		POINTS_INPUT,
		crate::value::ValueType::Vec4,
		crate::value::NodeValue::Vec4([0.0, 0.0, 0.0, 0.0]),
	);
	points.flags |= crate::input::flags::ARRAY;
	points.array_size = 5;
	core.add_input(points);

	let color = crate::input::Input::new(
		COLOR_INPUT,
		crate::value::ValueType::Color,
		crate::value::NodeValue::Color([1.0, 1.0, 1.0, 1.0]),
	);
	core.add_input(color);

	// The Default Pentagon(tm): element standard values (position tracks
	// only — the bezier control-point tracks are not representable).
	let pentagon: [(f64, f64); 5] = [
		(0.0, -135.0),
		(135.0, -45.0),
		(90.0, 120.0),
		(-90.0, 120.0),
		(-135.0, -45.0),
	];
	for (i, (x, y)) in pentagon.iter().enumerate() {
		core.set_standard_value(
			POINTS_INPUT,
			i as i32,
			crate::value::NodeValue::Vec4([*x, *y, 0.0, 0.0]),
		);
	}

	core.flags |= crate::node::flags::VIDEO_EFFECT;
	core.effect_input = super::generatorwithmerge::BASE_INPUT.to_string();

	(core, Box::new(PolygonGenerator))
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::node::NodeBehavior;
	use crate::value::{NodeValue, NodeValueTable, ValueType};
	use oak_core::Rational;

	#[test]
	fn input_names() {
		let n = PolygonGenerator;
		assert_eq!(
			n.input_name(super::super::generatorwithmerge::BASE_INPUT),
			"Base"
		);
		assert_eq!(n.input_name(POINTS_INPUT), "Points");
		assert_eq!(n.input_name(COLOR_INPUT), "Color");
	}

	#[test]
	fn create_wires_inputs_and_flags() {
		let (core, behavior) = create();
		assert_eq!(behavior.type_id(), "org.olivevideoeditor.Olive.polygon");
		let points = core.get_input(POINTS_INPUT).unwrap();
		assert_eq!(points.array_size, 5);
		assert_ne!(points.flags & crate::input::flags::ARRAY, 0);
		// Default pentagon element values (position tracks).
		assert_eq!(
			core.standard_value(POINTS_INPUT, 0),
			NodeValue::Vec4([0.0, -135.0, 0.0, 0.0])
		);
		assert_eq!(
			core.standard_value(POINTS_INPUT, 4),
			NodeValue::Vec4([-135.0, -45.0, 0.0, 0.0])
		);
		assert_eq!(
			core.get_input(COLOR_INPUT).unwrap().default,
			NodeValue::Color([1.0, 1.0, 1.0, 1.0])
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
	fn value_with_base_merges() {
		let (core, behavior) = create();
		let inputs = crate::value::NodeValueRow::from([(
			super::super::generatorwithmerge::BASE_INPUT.to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		)]);
		let mut table = NodeValueTable::default();
		behavior.value(&core, &inputs, Rational::new(0, 1), &mut table);
		assert!(table.get(ValueType::Texture).is_some());
	}

	#[test]
	fn shader_code_dispatches() {
		let n = PolygonGenerator;
		let rgb = n.shader_code("rgb").unwrap();
		assert!(rgb.contains("color.rgb = color_in.rgb * color.a;"));
		let mrg = n.shader_code("mrg").unwrap();
		assert!(mrg.contains("base_col *= 1.0 - blend_col.a;"));
		assert!(n.shader_code("other").is_none());
	}

	#[test]
	fn duplicate_clones() {
		let (core, behavior) = create();
		let dup = behavior.duplicate(&core).unwrap();
		assert_eq!(dup.name(), "Polygon");
	}
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.polygon`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.polygon",
		name: "Polygon",
		categories: &[Category::Generator],
		create,
	});
}
