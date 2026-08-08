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

//! Shared shape-node base (C++
//! `src/node/src/generator/shape/shapenodebase.{h,cpp}`,
//! `olive::ShapeNodeBase`).
//!
//! Abstract C++ base (extends `GeneratorWithMerge`, see
//! [`super::generatorwithmerge`]) for shape generators with position,
//! size, color and rect-scaling gizmos. Not instantiable, so this is a
//! helper module, not a [`NodeBehavior`] implementation.

/// Position input id (C++ `k_position_input`). Type: vec2; default
/// `(0, 0)`.
pub const POSITION_INPUT: &str = "pos_in";

/// Size input id (C++ `k_size_input`). Type: vec2; default
/// `(100, 100)`; properties: `min = (0, 0)`.
pub const SIZE_INPUT: &str = "size_in";

/// Color input id (C++ `k_color_input`). Type: color; default
/// `(1.0, 0.0, 0.0, 1.0)`; only added when the base is constructed with
/// `create_color_input = true`.
pub const COLOR_INPUT: &str = "color_in";

/// Helper mirroring the C++ `ShapeNodeBase` base.
///
/// The C++ members `point_gizmo_[k_gizmo_scale_count]` (8 rect-scale
/// point gizmos) and `poly_gizmo_` (whole-rect draggable polygon
/// gizmo) are GUI gizmo pointers with no Rust equivalent here; gizmos
/// are tracked in `NodeCore::gizmos`, so they are omitted. The
/// `k_gizmo_scale_*` index constants come from the C++ gizmo layer.
pub struct ShapeNodeBase;

impl ShapeNodeBase {
	/// Localized base input names (C++ `retranslate()` on top of the
	/// merge base): `pos_in` -> "Position", `size_in` -> "Size", and
	/// `color_in` -> "Color" when the color input exists.
	pub fn input_name(id: &str) -> &str {
		todo!()
	}

	/// Gizmo layout (C++ `update_gizmo_positions()`): centers the rect
	/// around the square-resolution midpoint (also stored as the
	/// `offset` property of `pos_in` so values appear top-left
	/// anchored), then places the 8 scale-point gizmos at the rect's
	/// corners/edge centers and the polygon gizmo on the four corners
	/// in top-left, top-right, bottom-right, bottom-left order.
	pub fn update_gizmo_positions(core: &mut crate::node::NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Undoable rect assignment (C++ `set_rect()`): normalizes the rect
	/// around the sequence center, then pushes undo children setting
	/// `size_in` x/y and `pos_in` x/y standard values.
	pub fn set_rect(core: &mut crate::node::NodeCore, rect: (f64, f64, f64, f64)) {
		todo!()
	}

	/// Gizmo drag (C++ `gizmo_drag_move()`): dragging the polygon
	/// gizmo offsets `pos_in` x/y directly; dragging a scale-point
	/// gizmo resizes with anchor-at-opposite-point semantics —
	/// Alt drags from center, Shift keeps the original aspect ratio
	/// (center-edge gizmos derive the other axis from the ratio,
	/// corner gizmos reconstruct both axes from the original angle and
	/// the new hypotenuse) — and writes the new position/size through
	/// the gizmo's four input draggers.
	pub fn gizmo_drag_move(core: &mut crate::node::NodeCore, x: f64, y: f64, modifiers: u32) {
		todo!()
	}
}
