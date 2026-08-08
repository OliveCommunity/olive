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
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): wraps the generate job
	/// (rasterized at u8 pixel format, then recolored by an `"rgb"`
	/// shader job sampling it as `texture_in` with `color_in`) in a
	/// texture at the sequence video params and pushes it through
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

	/// Direct frame generation (C++ `generate_frame()`): clears the RGBA
	/// buffer to transparent, builds a closed cubic path through the
	/// bezier points (each segment from the previous point's control
	/// point 2 through the next point's control point 1 to the next
	/// point, closing back to the first), and fills it via the
	/// facade-installed path-fill backend with divider/pixel-aspect
	/// scaling and center translation; without a backend the frame is
	/// left empty (warned once).
	fn generate_frame(
		&self,
		core: &NodeCore,
		frame: &mut crate::bridge::render::TextureHandle,
		time: oakcore_rs::Rational,
	) {
		todo!()
	}

	/// Gizmo layout (C++ `update_gizmo_positions()`): resolution comes
	/// from the base texture's virtual resolution or the square
	/// resolution; grows/shrinks the per-point position handles and the
	/// 2-per-point bezier handles/lines to the point count, wires new
	/// handles to the corresponding `points_in` tracks (position:
	/// tracks 0/1; bezier: tracks 2/3 and 4/5, circle-shaped and
	/// smaller), and places every handle/line at point + half
	/// resolution.
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Gizmo drag (C++ `gizmo_drag_move()`): dragging the path gizmo
	/// itself is a no-op (C++ FIXME: drag all points); dragging any
	/// other gizmo offsets its x/y input draggers by the drag delta
	/// from their start values.
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): `"rgb"` returns
	/// the recolor fragment shader; any other request falls through to
	/// the merge base (`"mrg"` -> alpha-over shader, else empty).
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `PolygonGenerator::PolygonGenerator()`): on top of
/// the `GeneratorWithMerge` constructor (which adds `base_in`), adds
/// the `points_in` bezier array and `color_in`, resizes the array to
/// the default pentagon documented on [`POINTS_INPUT`], and creates the
/// path gizmo.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
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
