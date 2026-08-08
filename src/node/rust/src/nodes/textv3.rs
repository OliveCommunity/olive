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

//! Rich text generator v3 (C++
//! `src/node/src/generator/text/textv3.{h,cpp}`,
//! `olive::TextGeneratorV3`, derives from `ShapeNodeBase`).
//!
//! FONT/RASTER BACKEND DEPENDENCY — DELIBERATELY UNDECIDED:
//! The C++ does NOT link any font/raster library directly (no freetype,
//! stb, harfbuzz, etc. anywhere in the tree). Text layout/rasterization
//! was historically Qt's rich text stack (`QTextDocument` fed through
//! Olive's `Html::html_to_doc()` + `QPainter` directly over an
//! RGBA8888-premultiplied buffer); it now runs behind the
//! facade-installed hooks in [`super::textbackend`]. No Rust font crate
//! is chosen here on purpose.

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Text input id (C++ `k_text_input`). Type: text; default
/// `"<p style='font-size: 72pt; color: white;'>Sample Text</p>"`;
/// properties: `vieweronly = true`.
pub const TEXT_INPUT: &str = "text_in";

/// Vertical alignment input id (C++ `k_vertical_alignment_input`).
/// Type: combo; no default; flags: hidden | static; combo strings:
/// "Top", "Middle", "Bottom".
pub const VERTICAL_ALIGNMENT_INPUT: &str = "valign_in";

/// Args enable toggle input id (C++ `k_use_args_input`). Type: boolean;
/// default `true`; flags: hidden | static.
pub const USE_ARGS_INPUT: &str = "use_args_in";

/// Format arguments array input id (C++ `k_args_input`). Type: text;
/// flags: array; properties: `arraystart = 1`.
pub const ARGS_INPUT: &str = "args_in";

/// Vertical alignment (C++ `TextGeneratorV3::VerticalAlignment`, values
/// `k_v_align_top = 0`, `k_v_align_middle = 1`, `k_v_align_bottom = 2`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VerticalAlignment {
	/// Align to the top of the shape rect (C++ `k_v_align_top`).
	Top,
	/// Vertically center in the shape rect (C++ `k_v_align_middle`).
	Middle,
	/// Align to the bottom of the shape rect (C++ `k_v_align_bottom`).
	Bottom,
}

/// Rich text generator v3 (the current "Text" node). Inherits
/// position/size/color inputs and the polygon gizmo from the shape base
/// (C++ `ShapeNodeBase`, to be modelled in `super::shapenodebase`).
///
/// The C++ also owns a `TextGizmo *text_gizmo_` — a GUI-layer viewport
/// gizmo with no Rust equivalent in this crate; it is omitted here and
/// will be re-attached by the facade/gizmo wave (gizmos themselves live
/// in [`NodeCore::gizmos`]).
pub struct TextGeneratorV3 {
	/// Suppresses re-emitting the vertical alignment to the gizmo
	/// while it is being driven by the gizmo (C++ `dont_emit_valign_`).
	dont_emit_valign: bool,
}

impl TextGeneratorV3 {
	/// Map our alignment to the gizmo's alignment int (C++
	/// `get_qt_alignment_from_ours()`): Top -> `TextGizmo::k_align_top`,
	/// Middle -> `TextGizmo::k_align_vcenter`, Bottom ->
	/// `TextGizmo::k_align_bottom` (0 = top, 1 = bottom, 2 = vcenter in
	/// the gizmo's numbering).
	pub fn get_gizmo_alignment_from_ours(v: VerticalAlignment) -> i32 {
		let _ = v;
		todo!()
	}

	/// Map the gizmo's alignment int back to ours (C++
	/// `get_our_alignment_from_qts()`); unknown values map to
	/// [`VerticalAlignment::Top`].
	pub fn get_our_alignment_from_gizmos(v: i32) -> VerticalAlignment {
		let _ = v;
		todo!()
	}

	/// Expand `%N` placeholders with args (C++ `format_string()`):
	/// `%%` yields a literal `%`; `%` followed by digits parses an int
	/// (out-of-int-range parses fail to 0, making the index -1) and
	/// substitutes `args[index - 1]` when in range, otherwise expands
	/// to nothing; a lone `%` before a non-digit/non-`%` is copied
	/// verbatim.
	pub fn format_string(input: &str, args: &[String]) -> String {
		let _ = (input, args);
		todo!()
	}

	/// Gizmo activated callback (C++ `gizmo_activated()`): sets
	/// `use_args_in` to `false` and `dont_emit_valign_ = true`.
	fn gizmo_activated(&mut self, core: &mut NodeCore) {
		let _ = core;
		todo!()
	}

	/// Gizmo deactivated callback (C++ `gizmo_deactivated()`): sets
	/// `use_args_in` to `true` and `dont_emit_valign_ = true`.
	fn gizmo_deactivated(&mut self, core: &mut NodeCore) {
		let _ = core;
		todo!()
	}

	/// Push an undoable change of the vertical alignment input (C++
	/// `set_vertical_alignment_undoable()`, formerly a
	/// `NodeParamSetStandardValueCommand` on the undo stack).
	fn set_vertical_alignment_undoable(&mut self, core: &mut NodeCore, a: i32) {
		let _ = (core, a);
		todo!()
	}
}

impl NodeBehavior for TextGeneratorV3 {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Text"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.text3"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Generator]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Generate rich text."
	}

	/// Localized input names (C++ `retranslate()`): `text_in` ->
	/// "Text", `valign_in` -> "Vertical Alignment" (combo strings
	/// Top/Middle/Bottom), `args_in` -> "Arguments"; the base class
	/// retranslate covers the inherited shape inputs.
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): if `use_args_in` is set and
	/// the args array is non-empty, expand `%N` placeholders in the
	/// text via [`Self::format_string`]; if the resulting text is
	/// non-empty, push a merged texture generate job (params from the
	/// incoming base texture when present, else the global video
	/// params, forced to `PixelFormat::u8` and the project's default
	/// input color space, with the expanded text inserted back into
	/// the job); otherwise pass the base input texture through
	/// unchanged.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Direct frame generation (C++ `generate_frame()`): clears the
	/// RGBA8888-premultiplied frame to transparent, then (only when a
	/// measure backend is installed) lays out the text as Olive HTML at
	/// 96 DPI (3780 dots/meter) wrapped to the shape size X, computes
	/// the base offset from the shape position re-centered into frame
	/// space, applies the vertical alignment to the draw offset (top:
	/// none; middle: `size.y/2 - doc.height/2`; bottom: `size.y -
	/// doc.height`), clips to the shape rect at the base offset, and
	/// renders over the buffer via the render backend. With no measure
	/// backend installed, warns once and leaves the cleared frame
	/// untouched.
	fn generate_frame(
		&self,
		core: &NodeCore,
		frame: &mut crate::bridge::render::TextureHandle,
		time: oakcore_rs::Rational,
	) {
		todo!()
	}

	/// Gizmo position update (C++ `update_gizmo_positions()`): after
	/// the base update, sets the text gizmo rect to the bounding rect
	/// of the polygon gizmo's polygon (empty polygon -> zero rect) and
	/// feeds it the current `text_in` HTML.
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Input value changed (C++ `InputValueChangedEvent()`): when
	/// `valign_in` changes and `dont_emit_valign_` is not set, forwards
	/// the new alignment to the text gizmo; then defers to the base
	/// implementation.
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `TextGeneratorV3::TextGeneratorV3()`): builds the
/// shape base without its own gizmo behavior (`ShapeNodeBase(false)`),
/// adds `text_in`, `valign_in`, `use_args_in` and `args_in` with the
/// defaults, flags and properties documented on the constants, sets the
/// inherited `size_in` standard value to `(400, 300)`, creates the
/// `TextGizmo` bound to `text_in`, and initializes
/// `dont_emit_valign_ = false`.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_text_generator_v3` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.text3",
		name: "Text",
		categories: &[Category::Generator],
		create,
	});
}
