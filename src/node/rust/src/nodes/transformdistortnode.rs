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

//! Transform distort effect (C++
//! `src/node/src/distort/transform/transformdistortnode.{h,cpp}`,
//! `olive::TransformDistortNode`). In C++ this derives from
//! `MatrixGenerator` (`super::matrix`), which provides the
//! `pos_in`/`rot_in`/`scale_in`/`uniform_scale_in`/`anchor_in` inputs
//! and the `generate_matrix` helper.

use crate::factory::NodeMeta;
use crate::node::{Category, Gizmo, NodeBehavior, NodeCore};

/// Parent matrix input id (C++ `k_parent_input`). Type: matrix; no
/// default (identity when unconnected).
pub const PARENT_INPUT: &str = "parent_in";

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input. Note: C++
/// `prepend_input`s it so it appears before the inherited matrix
/// inputs.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Auto-scale combo input id (C++ `k_autoscale_input`). Type: combo;
/// default `0` (`AutoScaleType::None`); combo strings: "None", "Fit",
/// "Fill", "Stretch".
pub const AUTOSCALE_INPUT: &str = "autoscale_in";

/// Interpolation combo input id (C++ `k_interpolation_input`). Type:
/// combo; default `2` (mipmapped bilinear); combo strings: "Nearest
/// Neighbor", "Bilinear", "Mipmapped Bilinear".
pub const INTERPOLATION_INPUT: &str = "interpolation_in";

/// Number of scale point gizmos (C++ `k_gizmo_scale_count` from
/// `node.h`: top-left, top-center, top-right, bottom-left,
/// bottom-center, bottom-right, center-left, center-right).
pub const GIZMO_SCALE_COUNT: usize = 8;

/// Auto-scale mode (C++ `AutoScaleType`); values match the
/// `autoscale_in` combo indices.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum AutoScaleType {
	/// No auto-scaling.
	None = 0,
	/// Fit the texture inside the sequence frame (letterbox).
	Fit = 1,
	/// Fill the sequence frame (crop overflow).
	Fill = 2,
	/// Stretch the texture to the sequence frame ignoring aspect.
	Stretch = 3,
}

/// Rotation direction for wrap-around detection (C++ private enum
/// `RotationDirection`).
#[derive(Clone, Copy, PartialEq, Eq)]
enum RotationDirection {
	/// No direction established yet.
	None,
	/// Clockwise (C++ `k_direction_positive`).
	Positive,
	/// Counter-clockwise (C++ `k_direction_negative`).
	Negative,
}

/// Which axes a scale gizmo drags (C++ private enum `GizmoScaleType`).
#[derive(Clone, Copy, PartialEq, Eq)]
enum GizmoScaleType {
	/// Horizontal center handles (C++ `k_gizmo_scale_x_only`).
	XOnly,
	/// Vertical center handles (C++ `k_gizmo_scale_y_only`).
	YOnly,
	/// Corner handles (C++ `k_gizmo_scale_both`).
	Both,
}

/// Transform distort node. Applies a 2D position/rotation/scale/anchor
/// transform to a texture, equivalent to multiplying by an orthographic
/// matrix.
///
/// The C++ class also keeps a `Matrix4x4 gizmo_inverted_transform_`
/// drag-state member; `Matrix4x4` has no Rust equivalent in this crate
/// yet, so the field is omitted here (it is recomputed at drag start
/// and consumed within the drag).
pub struct TransformDistortNode {
	/// Inherited matrix-generator state (C++ base class
	/// `MatrixGenerator`; provides the transform inputs and
	/// `generate_matrix`).
	pub matrix: super::matrix::MatrixGenerator,
	/// Angle between the drag-start mouse position and the anchor
	/// (C++ `double gizmo_start_angle_`).
	gizmo_start_angle: f64,
	/// Anchor point in screen space captured at drag start (C++
	/// `PointF gizmo_anchor_pt_`).
	gizmo_anchor_pt: (f64, f64),
	/// Whether uniform scale was on at scale-drag start (C++
	/// `bool gizmo_scale_uniform_`).
	gizmo_scale_uniform: bool,
	/// Previous-frame raw rotation angle (C++ `double
	/// gizmo_last_angle_`).
	gizmo_last_angle: f64,
	/// Previous-frame perpendicular rotation angle used to disambiguate
	/// wrap-around (C++ `double gizmo_last_alt_angle_`).
	gizmo_last_alt_angle: f64,
	/// Number of full revolutions accumulated while rotation dragging
	/// (C++ `int gizmo_rotate_wrap_`).
	gizmo_rotate_wrap: i32,
	/// Last raw rotation direction (C++ `gizmo_rotate_last_dir_`).
	gizmo_rotate_last_dir: RotationDirection,
	/// Last perpendicular rotation direction (C++
	/// `gizmo_rotate_last_alt_dir_`).
	gizmo_rotate_last_alt_dir: RotationDirection,
	/// Axes dragged by the current scale gizmo (C++
	/// `gizmo_scale_axes_`).
	gizmo_scale_axes: GizmoScaleType,
	/// Scale reference point captured at drag start (texture-space
	/// anchor, flipped for right/bottom handles; C++ `Vector2D
	/// gizmo_scale_anchor_`).
	gizmo_scale_anchor: (f64, f64),
	/// Scale drag handles in `k_gizmo_scale_*` order (C++
	/// `PointGizmo *point_gizmo_[k_gizmo_scale_count]`; absolute drag
	/// value behavior, bound to both tracks of the inherited
	/// `scale_in`).
	point_gizmo: [Gizmo; GIZMO_SCALE_COUNT],
	/// Anchor point handle (C++ `PointGizmo *anchor_gizmo_`; anchor
	/// shape, drags the inherited `anchor_in` and `pos_in` tracks).
	anchor_gizmo: Gizmo,
	/// Frame outline handle (C++ `PolygonGizmo *poly_gizmo_`; drags the
	/// inherited `pos_in` tracks).
	poly_gizmo: Gizmo,
	/// Rotation ring handle (C++ `ScreenGizmo *rotation_gizmo_`;
	/// absolute drag value behavior, bound to the inherited `rot_in`).
	rotation_gizmo: Gizmo,
}

impl TransformDistortNode {
	/// Static matrix composition (C++
	/// `adjust_matrix_by_resolutions()`): scale to a 2x2 square in
	/// sequence space, apply the texture offset and the generated
	/// matrix, scale back out to texture size, then apply the
	/// auto-scale mode (stretch: per-axis; fit/fill: uniform by width
	/// or height depending on the aspect-ratio comparison).
	///
	/// Also covers the C++ private helpers `generate_auto_scaled_matrix`,
	/// `create_scale_point`, `is_a_scale_gizmo` and
	/// `get_direction_from_angles`.
	fn adjust_matrix_by_resolutions() {
		todo!()
	}
}

impl NodeBehavior for TransformDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Transform"
	}

	/// Short menu name (C++ `short_name()`; overrides the
	/// `MatrixGenerator` short name "Ortho" to just return `name()`).
	fn short_name(&self) -> &str {
		"Transform"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.transform"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Transform an image in 2D space. Equivalent to multiplying by an orthographic matrix."
	}

	/// Localized input names (C++ `retranslate()`): `parent_in` ->
	/// "Parent", `autoscale_in` -> "Auto-Scale" (combo strings "None",
	/// "Fit", "Fill", "Stretch"), `tex_in` -> "Texture",
	/// `interpolation_in` -> "Interpolation" (combo strings "Nearest
	/// Neighbor", "Bilinear", "Mipmapped Bilinear"), plus the inherited
	/// `MatrixGenerator` input names via its retranslate.
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): generates the matrix from the
	/// inherited transform inputs (position/rotation/scale/anchor,
	/// folded with `parent_in`) and always pushes it as a `k_matrix`
	/// value. With a texture: builds the auto-scaled real matrix via
	/// `adjust_matrix_by_resolutions`; if it is not identity, renders a
	/// shader job at the GLOBAL video params (the transform may change
	/// the size) binding `ove_maintex` and `ove_mvpmat` with the
	/// interpolation selected by `interpolation_in`, and pushes that;
	/// identity matrix (or no texture) -> pass-through push of the
	/// input texture value.
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
	/// request id and returns a default (empty) `ShaderCode` — the
	/// node relies on the renderer's default vertex/fragment shaders,
	/// so this maps to `None`.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Gizmo transform/positions (C++ `update_gizmo_positions()` and
	/// `gizmo_transformation()`). Positions: with a texture, builds the
	/// rectangle matrix (sequence half-res scale folded with the
	/// auto-scaled generated matrix), maps the unit square through it
	/// for the polygon gizmo, places the anchor gizmo at the mapped
	/// origin, the eight scale handles at the mapped unit-square
	/// corners/edge midpoints, and sets the `offset` input property of
	/// `pos_in` (half sequence res + texture offset) and `anchor_in`
	/// (half texture size). Transformation: with a texture returns the
	/// auto-scaled generated matrix (generated with an identity parent);
	/// without one falls back to the base `MatrixGenerator`
	/// transformation.
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Gizmo drag callbacks (C++ `gizmo_drag_start()` /
	/// `gizmo_drag_move()`). Start: anchor gizmo -> stores the inverted
	/// anchor-aware generated matrix; scale handle -> requires a
	/// texture, records uniform-scale state, screen-space anchor point,
	/// per-handle axes (corners both, left/right center X, top/bottom
	/// center Y), the texture-space scale anchor flipped for
	/// right/bottom handles, and the inverted fully-generated matrix;
	/// rotation gizmo -> records the anchor point, start/last angles
	/// (raw and perpendicular) and resets the wrap counter and
	/// direction. Move: polygon gizmo -> drags the position X/Y tracks
	/// by the mouse delta; anchor gizmo -> drags the anchor tracks by
	/// the inverse-matrix-mapped delta and the position tracks by the
	/// raw delta; rotation gizmo -> accumulates wrap-aware angle
	/// (detecting direction reversal across ±pi using the perpendicular
	/// angle) and drags the rotation track by the degree difference
	/// from the start angle; scale handle -> maps the mouse delta into
	/// inverted-matrix space relative to the anchor, then drags the
	/// scale tracks by the normalized magnitude per the handle's axes,
	/// collapsing to a single uniform value (diagonal ratio for corner
	/// handles) when uniform scale is on.
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `TransformDistortNode::TransformDistortNode()`):
/// adds `parent_in` (matrix), `autoscale_in` (combo, default 0) and
/// `interpolation_in` (combo, default 2), and PREPENDS `tex_in`
/// (texture, not-keyframable) ahead of the inherited `MatrixGenerator`
/// inputs; creates the rotation screen gizmo (absolute drag behavior,
/// bound to `rot_in`), the frame polygon gizmo (bound to `pos_in`), the
/// anchor point gizmo (anchor shape, bound to `anchor_in` and `pos_in`)
/// and the eight scale point gizmos (absolute drag behavior, bound to
/// `scale_in`); sets the video-effect flag and the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.transform`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.transform",
		name: "Transform",
		categories: &[Category::Distort],
		create,
	});
}
