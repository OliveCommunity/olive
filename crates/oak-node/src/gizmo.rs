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

//! Gizmo data types (C++ `src/node/src/gizmo/{gizmo,draggable,point,line,
//! path,polygon,screen,text}.h`; the C++ classes are `olive::NodeGizmo`,
//! `DraggableGizmo`, `PointGizmo`, `LineGizmo`, `PathGizmo`,
//! `PolygonGizmo`, `ScreenGizmo`, `TextGizmo`, plus the `PointF`/`LineF`/
//! `RectF` carriers from `mathtypes.h`/`line.h`/`text.h`).
//!
//! Data only — drawing, hit testing, and mouse handling live in the
//! facade/app layer, so the C++ drag callbacks (`drag_start`/`drag_move`/
//! `drag_end`) and `TextGizmo::update_input_html` have no Rust counterpart
//! here.
//!
//! Note: the rough `crate::node::Gizmo` sketch in `node.rs` should
//! eventually be replaced by the types in this module.
//!
//! C++ uses single inheritance (`NodeGizmo` <- `DraggableGizmo` <-
//! `PointGizmo`/`PathGizmo`/`PolygonGizmo`/`ScreenGizmo`); Rust models
//! this by flattening the base-class fields into each concrete struct
//! (the base fields are documented as such on each).

use oak_core::{Rational, TimeRange};

use crate::value::{AudioParams, NodeValue, VideoParams};

/// 2D point (C++ `olive::PointF` in `src/node/src/mathtypes.h`, a de-Qt
/// replacement for `QPointF`).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PointF {
	/// X coordinate.
	pub x: f64,
	/// Y coordinate.
	pub y: f64,
}

impl PointF {
	/// Construct a point (C++ `PointF(x, y)`).
	pub fn new(x: f64, y: f64) -> Self {
		Self { x, y }
	}

	/// Whether both coordinates are zero (C++ `is_null()`).
	pub fn is_null(&self) -> bool {
		self.x == 0.0 && self.y == 0.0
	}

	/// `|x| + |y|` (C++ `manhattan_length()`).
	pub fn manhattan_length(&self) -> f64 {
		self.x.abs() + self.y.abs()
	}
}

/// Line segment (C++ `olive::LineF` in `src/node/src/gizmo/line.h`, a
/// de-Qt replacement for `QLineF`; data carrier only).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct LineF {
	/// Start point (C++ `p1_`).
	pub p1: PointF,
	/// End point (C++ `p2_`).
	pub p2: PointF,
}

/// Rectangle (C++ `olive::RectF` in `src/node/src/gizmo/text.h`, a de-Qt
/// replacement for `QRectF`; text gizmo rect).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct RectF {
	/// Left edge (C++ `x_`).
	pub x: f64,
	/// Top edge (C++ `y_`).
	pub y: f64,
	/// Width (C++ `width_`).
	pub width: f64,
	/// Height (C++ `height_`).
	pub height: f64,
}

/// Loop mode (C++ `enum class LoopMode` in
/// `src/common/src/loopmode.h`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum LoopMode {
	/// Play once (C++ `k_loop_mode_off`).
	#[default]
	Off,
	/// Repeat the clip (C++ `k_loop_mode_loop`).
	Loop,
	/// Hold first/last frame (C++ `k_loop_mode_clamp`).
	Clamp,
}

/// Sequence/render globals a gizmo draws against (C++ `NodeGlobals` in
/// `src/node/src/globals.h`).
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct NodeGlobals {
	/// Video parameters (C++ `video_params_`).
	pub video_params: VideoParams,
	/// Audio parameters (C++ `audio_params_`).
	pub audio_params: AudioParams,
	/// Current time range (C++ `time_`).
	pub time: TimeRange,
	/// Loop mode (C++ `loop_mode_`).
	pub loop_mode: LoopMode,
}

/// Reference to one keyframe track of a node input (C++
/// `NodeKeyframeTrackReference` in `src/node/src/param.h`). Invalid when
/// `track` < 0 (C++ default constructs `track_ = -1`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct KeyframeTrackReference {
	/// Input id (C++ `NodeInput`'s input string).
	pub input_id: String,
	/// Array element (C++ `NodeInput`'s element; -1 = none).
	pub element: i32,
	/// Track index (C++ `track_`; -1 = invalid).
	pub track: i32,
}

impl Default for KeyframeTrackReference {
	/// C++ default constructor: empty input, `track = -1`.
	fn default() -> Self {
		Self {
			input_id: String::new(),
			element: -1,
			track: -1,
		}
	}
}

impl KeyframeTrackReference {
	/// Whether the reference is usable (C++ `is_valid()`).
	pub fn is_valid(&self) -> bool {
		self.track >= 0
	}
}

/// Drag state for one input (C++ `NodeInputDragger` in
/// `src/node/src/inputdragger.h`). Data only: the C++ `start`/`drag`/
/// `end` methods write keyframes and push undo commands — that behavior
/// belongs to the facade/app layer; the C++ static
/// `input_being_dragged` flag is global UI state and is not modeled
/// here.
#[derive(Clone, Debug)]
pub struct InputDragger {
	/// Target input/track (C++ `input_`).
	pub input: KeyframeTrackReference,
	/// Time at drag start (C++ `time_`).
	pub time: Rational,
	/// Value at drag start (C++ `start_value_`).
	pub start_value: NodeValue,
	/// Current/last dragged value (C++ `end_value_`).
	pub end_value: NodeValue,
	/// Whether a drag is in progress (C++ `is_started()`).
	pub started: bool,
}

impl Default for InputDragger {
	/// C++ default constructor: no drag in progress, values unset.
	fn default() -> Self {
		Self {
			input: KeyframeTrackReference::default(),
			time: Rational::default(),
			start_value: NodeValue::None,
			end_value: NodeValue::None,
			started: false,
		}
	}
}

/// What the X/Y coordinates emitted during a drag mean (C++
/// `DraggableGizmo::DragValueBehavior` in
/// `src/node/src/gizmo/draggable.h`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum DragValueBehavior {
	/// Exact mouse coordinates in sequence pixels (C++ `k_absolute`;
	/// the C++ default).
	#[default]
	Absolute,
	/// Movement since the last move event (C++
	/// `k_delta_from_previous`).
	DeltaFromPrevious,
	/// Movement from the start of the drag (C++ `k_delta_from_start`).
	DeltaFromStart,
}

/// Base gizmo data (C++ `olive::NodeGizmo` private members in
/// `src/node/src/gizmo/gizmo.h`).
///
/// The C++ `parent_` node pointer is omitted: ownership and
/// registration run through `NodeCore::gizmos` (mirroring
/// `Node::add_gizmo()`/`remove_gizmo()`), so a back-pointer has no Rust
/// equivalent here.
#[derive(Clone, Debug, Default)]
pub struct GizmoBase {
	/// Sequence globals the gizmo draws against (C++ `globals_`).
	pub globals: NodeGlobals,
	/// Visibility flag (C++ `visible_`).
	pub visible: bool,
}

/// Draggable base data (C++ `olive::DraggableGizmo` in
/// `src/node/src/gizmo/draggable.h`).
#[derive(Clone, Debug, Default)]
pub struct DraggableGizmo {
	/// Base gizmo data (C++ `NodeGizmo` base-class members).
	pub base: GizmoBase,
	/// Keyframed inputs this gizmo drags (C++ `inputs_`; appended by
	/// `add_input()`).
	pub inputs: Vec<KeyframeTrackReference>,
	/// Per-input drag state, parallel to `inputs` (C++ `draggers_`).
	pub draggers: Vec<InputDragger>,
	/// Drag coordinate semantics (C++ `drag_value_behavior_`).
	pub drag_value_behavior: DragValueBehavior,
}

/// Point gizmo shape (C++ `PointGizmo::Shape` in
/// `src/node/src/gizmo/point.h`).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum PointShape {
	/// Square handle (C++ `k_square`; the C++ default constructor's
	/// shape).
	#[default]
	Square,
	/// Circle handle (C++ `k_circle`).
	Circle,
	/// Anchor-point handle (C++ `k_anchor_point`).
	AnchorPoint,
}

/// Point gizmo (C++ `olive::PointGizmo` in
/// `src/node/src/gizmo/point.h`; base `DraggableGizmo`).
#[derive(Clone, Debug, Default)]
pub struct PointGizmo {
	/// Draggable base data (C++ `DraggableGizmo` base-class members).
	pub draggable: DraggableGizmo,
	/// Handle shape (C++ `shape_`).
	pub shape: PointShape,
	/// Handle position in sequence pixels (C++ `point_`).
	pub point: PointF,
	/// Render the handle smaller (C++ `smaller_`).
	pub smaller: bool,
}

/// Line gizmo (C++ `olive::LineGizmo` in `src/node/src/gizmo/line.h`;
/// base `NodeGizmo`, not draggable).
#[derive(Clone, Debug, Default)]
pub struct LineGizmo {
	/// Base gizmo data (C++ `NodeGizmo` base-class members).
	pub base: GizmoBase,
	/// The line in sequence pixels (C++ `line_`).
	pub line: LineF,
}

/// Path gizmo (C++ `olive::PathGizmo` in `src/node/src/gizmo/path.h`;
/// base `DraggableGizmo`).
///
/// The C++ class has no own members — its former `QPainterPath` (a
/// drawing/hit-test primitive) was removed; path storage and drawing
/// belong to the app layer. The type is kept so the gizmo hierarchy
/// remains distinguishable.
#[derive(Clone, Debug, Default)]
pub struct PathGizmo {
	/// Draggable base data (C++ `DraggableGizmo` base-class members).
	pub draggable: DraggableGizmo,
}

/// Polygon gizmo (C++ `olive::PolygonGizmo` in
/// `src/node/src/gizmo/polygon.h`; base `DraggableGizmo`).
///
/// Point-in-polygon testing (formerly `QPolygonF::containsPoint`)
/// belongs to the app layer.
#[derive(Clone, Debug, Default)]
pub struct PolygonGizmo {
	/// Draggable base data (C++ `DraggableGizmo` base-class members).
	pub draggable: DraggableGizmo,
	/// Polygon vertices (C++ `polygon_`, de-Qt'd from `QPolygonF` to a
	/// plain vector of points).
	pub polygon: Vec<PointF>,
}

/// Screen gizmo (C++ `olive::ScreenGizmo` in
/// `src/node/src/gizmo/screen.h`; base `DraggableGizmo`).
///
/// The C++ class has no own members; it exists only to distinguish the
/// whole-screen drag target in the gizmo hierarchy.
#[derive(Clone, Debug, Default)]
pub struct ScreenGizmo {
	/// Draggable base data (C++ `DraggableGizmo` base-class members).
	pub draggable: DraggableGizmo,
}

/// Text gizmo vertical alignment (C++ `TextGizmo::VerticalAlignment` in
/// `src/node/src/gizmo/text.h`, formerly `Qt::Alignment`; values match
/// the oakengine facade).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum VerticalAlignment {
	/// Align to the top (C++ `k_align_top` = 0).
	#[default]
	Top,
	/// Align to the bottom (C++ `k_align_bottom` = 1).
	Bottom,
	/// Align vertically centered (C++ `k_align_vcenter` = 2).
	VCenter,
}

/// Text gizmo (C++ `olive::TextGizmo` in `src/node/src/gizmo/text.h`;
/// base `NodeGizmo`, not draggable).
///
/// `TextGizmo::update_input_html()` (pushing edited HTML back into the
/// connected input at a time) is behavioral and belongs to the
/// facade/app layer, so it is not declared here.
#[derive(Clone, Debug, Default)]
pub struct TextGizmo {
	/// Base gizmo data (C++ `NodeGizmo` base-class members).
	pub base: GizmoBase,
	/// Text bounds in sequence pixels (C++ `rect_`).
	pub rect: RectF,
	/// HTML content (C++ `text_`).
	pub html: String,
	/// Input track this gizmo edits (C++ `input_`).
	pub input: KeyframeTrackReference,
	/// Vertical alignment (C++ `valign_`).
	pub vertical_alignment: VerticalAlignment,
}
