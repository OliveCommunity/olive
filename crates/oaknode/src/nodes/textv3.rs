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
use crate::value::{NodeValue, NodeValueRow, NodeValueTable};
use oakcore_rs::Rational;

use super::textbackend::{TextLayoutMode, TextLayoutRequest, TextLayoutSize, TextRenderTransform};

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

impl VerticalAlignment {
	/// From a combo index (C++ `static_cast<VerticalAlignment>`); unknown
	/// values map to [`VerticalAlignment::Top`] like the C++ cast's
	/// callers assume.
	fn from_int(v: i32) -> VerticalAlignment {
		match v {
			1 => VerticalAlignment::Middle,
			2 => VerticalAlignment::Bottom,
			_ => VerticalAlignment::Top,
		}
	}
}

/// The C++ `k_input_flag_static` mask: not-connectable +
/// not-keyframable.
const STATIC_FLAGS: u32 =
	crate::input::flags::NOT_CONNECTABLE | crate::input::flags::NOT_KEYFRAMABLE;

/// Rich text generator v3 (the current "Text" node). Inherits
/// position/size/color inputs and the polygon gizmo from the shape base
/// (C++ `ShapeNodeBase`, modelled in [`super::shapenodebase`]).
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

/// `Variant::to_string()` for the text input (Text payload, with a
/// numeric fallback for mis-typed connections).
fn to_text(v: &NodeValue) -> String {
	match v {
		NodeValue::Text(s) => s.clone(),
		other => other.to_double().to_string(),
	}
}

/// `Variant::to_bool()` for the args toggle.
fn to_bool(v: &NodeValue) -> bool {
	match v {
		NodeValue::Boolean(b) => *b,
		other => other.to_double() != 0.0,
	}
}

/// `Variant::to_vec2()` for the inherited position/size inputs.
fn to_vec2(v: &NodeValue) -> [f64; 2] {
	match v {
		NodeValue::Vec2(a) => *a,
		other => [other.to_double(), 0.0],
	}
}

impl TextGeneratorV3 {
	/// Map our alignment to the gizmo's alignment int (C++
	/// `get_qt_alignment_from_ours()`): Top -> `TextGizmo::k_align_top`,
	/// Middle -> `TextGizmo::k_align_vcenter`, Bottom ->
	/// `TextGizmo::k_align_bottom` (0 = top, 1 = bottom, 2 = vcenter in
	/// the gizmo's numbering).
	pub fn get_gizmo_alignment_from_ours(v: VerticalAlignment) -> i32 {
		match v {
			VerticalAlignment::Top => 0,
			VerticalAlignment::Middle => 2,
			VerticalAlignment::Bottom => 1,
		}
	}

	/// Map the gizmo's alignment int back to ours (C++
	/// `get_our_alignment_from_qts()`); unknown values map to
	/// [`VerticalAlignment::Top`].
	pub fn get_our_alignment_from_gizmos(v: i32) -> VerticalAlignment {
		match v {
			1 => VerticalAlignment::Bottom,
			2 => VerticalAlignment::Middle,
			_ => VerticalAlignment::Top,
		}
	}

	/// The current alignment (C++ `get_vertical_alignment()`): the
	/// `valign_in` standard value as a [`VerticalAlignment`].
	pub fn vertical_alignment(core: &NodeCore) -> VerticalAlignment {
		VerticalAlignment::from_int(
			core.standard_value(VERTICAL_ALIGNMENT_INPUT, -1)
				.to_double() as i32,
		)
	}

	/// Expand `%N` placeholders with args (C++ `format_string()`):
	/// `%%` yields a literal `%`; `%` followed by digits parses an int
	/// (out-of-int-range parses fail to 0, making the index -1) and
	/// substitutes `args[index - 1]` when in range, otherwise expands
	/// to nothing; a lone `%` before a non-digit/non-`%` is copied
	/// verbatim.
	pub fn format_string(input: &str, args: &[String]) -> String {
		let bytes = input.as_bytes();
		let mut output = String::new();
		let mut i = 0;
		while i < bytes.len() {
			let c = bytes[i] as char;
			if i + 1 < bytes.len() && c == '%' {
				let next = bytes[i + 1] as char;
				if next == '%' {
					// Double percent, append a single percent.
					output.push('%');
					i += 1;
				} else if next.is_ascii_digit() {
					// Find the length of the number (QString::toInt()
					// semantics: out-of-int-range parses fail and yield 0,
					// making the index -1).
					let mut num = String::new();
					i += 1;
					while i < bytes.len() && bytes[i].is_ascii_digit() {
						num.push(bytes[i] as char);
						i += 1;
					}
					i -= 1;
					let n: i64 = num.parse().unwrap_or(0);
					let index = if n > i32::MAX as i64 || n < i32::MIN as i64 {
						-1
					} else {
						(n as i32) - 1
					};
					if index >= 0 && (index as usize) < args.len() {
						output.push_str(&args[index as usize]);
					}
				} else {
					output.push(c);
				}
			} else {
				output.push(c);
			}
			i += 1;
		}
		output
	}

	/// Gizmo activated callback (C++ `gizmo_activated()`): sets
	/// `use_args_in` to `false` and `dont_emit_valign_ = true`.
	fn gizmo_activated(&mut self, core: &mut NodeCore) {
		core.set_standard_value(USE_ARGS_INPUT, -1, NodeValue::Boolean(false));
		self.dont_emit_valign = true;
	}

	/// Gizmo deactivated callback (C++ `gizmo_deactivated()`): sets
	/// `use_args_in` to `true` and `dont_emit_valign_ = true`.
	fn gizmo_deactivated(&mut self, core: &mut NodeCore) {
		core.set_standard_value(USE_ARGS_INPUT, -1, NodeValue::Boolean(true));
		self.dont_emit_valign = true;
	}

	/// Set the vertical alignment through the undo system (C++
	/// `set_vertical_alignment_undoable()`, formerly a
	/// `NodeParamSetStandardValueCommand` on the undo stack). The undo
	/// command stack is not part of this crate, so only the resulting
	/// standard-value write is performed (`// CPP-PARITY: textv3.cpp`
	/// `set_vertical_alignment_undoable`).
	fn set_vertical_alignment_undoable(&mut self, core: &mut NodeCore, a: i32) {
		core.set_standard_value(
			VERTICAL_ALIGNMENT_INPUT,
			-1,
			NodeValue::Combo(Self::get_our_alignment_from_gizmos(a) as i64),
		);
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
	/// retranslate covers the inherited shape inputs and `base_in`
	/// ("Base").
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		match id {
			TEXT_INPUT => "Text",
			VERTICAL_ALIGNMENT_INPUT => "Vertical Alignment",
			ARGS_INPUT => "Arguments",
			crate::nodes::generatorwithmerge::BASE_INPUT => "Base",
			_ => crate::nodes::shapenodebase::ShapeNodeBase::input_name(id),
		}
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
	///
	/// The Rust model has no generate-job payload and no array value
	/// representation: the job case goes through
	/// [`crate::nodes::generatorwithmerge::GeneratorWithMerge::push_mergable_job`]
	/// with a null handle, and the args array resolves to the single row
	/// value when present (a per-element array model is deferred), so
	/// `%N` expansion is exercised directly via [`Self::format_string`]
	/// (`// CPP-PARITY: textv3.cpp` `value()`).
	fn value(
		&self,
		core: &NodeCore,
		inputs: &NodeValueRow,
		time: Rational,
		table: &mut NodeValueTable,
	) {
		let text_val = inputs
			.get(TEXT_INPUT)
			.cloned()
			.unwrap_or_else(|| core.value_at_time(TEXT_INPUT, -1, time));
		let mut text = to_text(&text_val);

		let use_args_val = inputs
			.get(USE_ARGS_INPUT)
			.cloned()
			.unwrap_or_else(|| core.value_at_time(USE_ARGS_INPUT, -1, time));
		if to_bool(&use_args_val) {
			let args: Vec<String> = match inputs.get(ARGS_INPUT) {
				Some(NodeValue::Text(s)) => vec![s.clone()],
				_ => Vec::new(),
			};
			if !args.is_empty() {
				text = Self::format_string(&text, &args);
			}
		}

		if !text.is_empty() {
			// C++ `push_mergable_job(value, Texture::job(text_params, job),
			// table)` — merged over base_in when connected, else pushed
			// directly. The null handle marks the renderer-deferred
			// generate job.
			crate::nodes::generatorwithmerge::GeneratorWithMerge::push_mergable_job(
				inputs,
				crate::handle::CHandle::null(),
				table,
			);
		} else if let Some(base @ NodeValue::Texture(_)) =
			inputs.get(crate::nodes::generatorwithmerge::BASE_INPUT)
		{
			table.push(base.value_type(), base.clone(), None);
		}
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
	///
	/// The Rust frame is an opaque [`crate::bridge::render::TextureHandle`]
	/// whose pixels cannot be read or written from this crate, so the
	/// body is a documented no-op; the layout/measure/offset control flow
	/// is ported in [`Self::layout_request`], [`Self::base_offset`] and
	/// [`Self::draw_offset`], and exercised by the tests.
	fn generate_frame(
		&self,
		core: &NodeCore,
		frame: &mut crate::bridge::render::TextureHandle,
		time: Rational,
	) {
		let _ = (core, frame, time);
	}

	/// Gizmo position update (C++ `update_gizmo_positions()`): after
	/// the base update, sets the text gizmo rect to the bounding rect
	/// of the polygon gizmo's polygon (empty polygon -> zero rect) and
	/// feeds it the current `text_in` HTML.
	///
	/// The polygon/text gizmos live in the GUI layer with no Rust model
	/// in this crate, so this is a documented no-op
	/// (`// CPP-PARITY: textv3.cpp` `update_gizmo_positions`).
	fn gizmo_update(&self, core: &NodeCore, row: &NodeValueRow) {
		let _ = (core, row);
	}

	/// Input value changed (C++ `InputValueChangedEvent()`): when
	/// `valign_in` changes and `dont_emit_valign_` is not set, forwards
	/// the new alignment to the text gizmo; then defers to the base
	/// implementation.
	///
	/// The text gizmo has no Rust model in this crate, so only the
	/// flag check is represented (`// CPP-PARITY: textv3.cpp`
	/// `InputValueChangedEvent`).
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		let _ = (core, element);
		if input == VERTICAL_ALIGNMENT_INPUT && !self.dont_emit_valign {
			// The C++ forwards the new alignment to the text gizmo here.
		}
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(TextGeneratorV3 {
			dont_emit_valign: self.dont_emit_valign,
		}))
	}

	/// Downcast to [`Self`] (gizmo-state access).
	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	/// Mutable downcast (see [`NodeBehavior::as_any`]).
	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
	}
}

impl TextGeneratorV3 {
	/// Build the C++ `TextLayoutRequest` (textv3.cpp `generate_frame()`):
	/// Olive-HTML text, 96 DPI (3780 dots/meter), wrapped to the shape
	/// size X. Font family/size come from the markup; the backend defaults
	/// are used when absent.
	pub fn layout_request(row: &NodeValueRow) -> TextLayoutRequest {
		let size = row
			.get(crate::nodes::shapenodebase::SIZE_INPUT)
			.map(to_vec2)
			.unwrap_or([0.0, 0.0]);
		TextLayoutRequest {
			text: row.get(TEXT_INPUT).map(to_text).unwrap_or_else(String::new),
			mode: TextLayoutMode::OliveHtml,
			font_family: String::new(),
			font_size_pt: 0.0,
			dots_per_meter: 3780,
			wrap_width: size[0],
			center_horizontally: false,
		}
	}

	/// The C++ base offset (textv3.cpp `generate_frame()`): the shape
	/// position re-centered into frame space — `pos - size/2 + frame/2`
	/// (the frame halves are integer division in C++).
	pub fn base_offset(
		pos: [f64; 2],
		size: [f64; 2],
		frame_width: i32,
		frame_height: i32,
	) -> (f64, f64) {
		(
			pos[0] - size[0] / 2.0 + (frame_width / 2) as f64,
			pos[1] - size[1] / 2.0 + (frame_height / 2) as f64,
		)
	}

	/// The C++ draw offset (textv3.cpp `generate_frame()`): the base
	/// offset plus the vertical-alignment delta — top: none; middle:
	/// `size.y/2 - doc.height/2`; bottom: `size.y - doc.height` (all
	/// double math, unlike the integer halving in v2).
	pub fn draw_offset(
		align: VerticalAlignment,
		base: (f64, f64),
		size: [f64; 2],
		doc_height: f64,
	) -> (f64, f64) {
		let (dx, mut dy) = base;
		match align {
			VerticalAlignment::Top => {}
			VerticalAlignment::Middle => dy += size[1] / 2.0 - doc_height / 2.0,
			VerticalAlignment::Bottom => dy += size[1] - doc_height,
		}
		(dx, dy)
	}

	/// The C++ `TextRenderTransform` (textv3.cpp `generate_frame()`):
	/// scale, the draw offset, and the clip rect at the base offset
	/// covering the shape size (set before the vertical-alignment
	/// translate in the C++).
	pub fn render_transform(
		scale: f64,
		draw: (f64, f64),
		base: (f64, f64),
		size: [f64; 2],
	) -> TextRenderTransform {
		TextRenderTransform {
			scale,
			draw_offset_x: draw.0,
			draw_offset_y: draw.1,
			clip_enabled: true,
			clip_offset_x: base.0,
			clip_offset_y: base.1,
			clip_width: size[0],
			clip_height: size[1],
		}
	}

	/// The layout/measure control flow of the C++ `generate_frame()` with
	/// the backend hooks: build the request and measure via the installed
	/// measure backend (zero size when none is installed — the documented
	/// no-backend fallback; the frame is left cleared).
	///
	/// The render step needs the frame's pixel buffer, which the Rust
	/// frame handle does not expose; it is not representable here
	/// (`// CPP-PARITY: textv3.cpp` `generate_frame`).
	pub fn measure_and_layout(row: &NodeValueRow) -> (TextLayoutRequest, TextLayoutSize) {
		let req = Self::layout_request(row);
		let doc = match super::textbackend::text_measure_backend() {
			Some(measure) => measure(&req),
			None => TextLayoutSize::default(),
		};
		(req, doc)
	}
}

/// Constructor (C++ `TextGeneratorV3::TextGeneratorV3()`): builds the
/// shape base without its own gizmo behavior (`ShapeNodeBase(false)`),
/// adds `text_in`, `valign_in`, `use_args_in` and `args_in` with the
/// defaults, flags and properties documented on the constants, sets the
/// inherited `size_in` standard value to `(400, 300)`, creates the
/// `TextGizmo` bound to `text_in`, and initializes
/// `dont_emit_valign_ = false`.
///
/// The `TextGizmo` is a GUI-layer gizmo with no Rust model (see the
/// struct doc); the inherited inputs (`base_in` from the merge base,
/// `pos_in`/`size_in` from the shape base without its color input) are
/// wired here, mirroring the C++ constructor chain `Node ->
/// GeneratorWithMerge -> ShapeNodeBase(false) -> TextGeneratorV3`.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();

	// GeneratorWithMerge base: base_in texture effect input.
	let mut base = crate::input::Input::new(
		crate::nodes::generatorwithmerge::BASE_INPUT,
		crate::value::ValueType::Texture,
		NodeValue::None,
	);
	base.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(base);
	core.effect_input = crate::nodes::generatorwithmerge::BASE_INPUT.to_string();
	core.flags |= crate::node::flags::VIDEO_EFFECT;

	// ShapeNodeBase(false): pos/size, no color input.
	core.add_input(crate::input::Input::new(
		crate::nodes::shapenodebase::POSITION_INPUT,
		crate::value::ValueType::Vec2,
		NodeValue::Vec2([0.0, 0.0]),
	));
	let mut size = crate::input::Input::new(
		crate::nodes::shapenodebase::SIZE_INPUT,
		crate::value::ValueType::Vec2,
		NodeValue::Vec2([100.0, 100.0]),
	);
	size.properties = vec![("min".to_string(), NodeValue::Vec2([0.0, 0.0]))];
	core.add_input(size);

	// Own inputs.
	let mut text = crate::input::Input::new(
		TEXT_INPUT,
		crate::value::ValueType::Text,
		NodeValue::Text("<p style='font-size: 72pt; color: white;'>Sample Text</p>".to_string()),
	);
	text.properties = vec![("vieweronly".to_string(), NodeValue::Boolean(true))];
	core.add_input(text);

	let mut valign = crate::input::Input::new(
		VERTICAL_ALIGNMENT_INPUT,
		crate::value::ValueType::Combo,
		NodeValue::Combo(0),
	);
	valign.flags |= STATIC_FLAGS | crate::input::flags::HIDDEN;
	core.add_input(valign);

	let mut use_args = crate::input::Input::new(
		USE_ARGS_INPUT,
		crate::value::ValueType::Boolean,
		NodeValue::Boolean(true),
	);
	use_args.flags |= STATIC_FLAGS | crate::input::flags::HIDDEN;
	core.add_input(use_args);

	let mut args = crate::input::Input::new(
		ARGS_INPUT,
		crate::value::ValueType::Text,
		NodeValue::Text(String::new()),
	);
	args.flags |= crate::input::flags::ARRAY;
	args.properties = vec![("arraystart".to_string(), NodeValue::Int(1))];
	core.add_input(args);

	// C++ set_standard_value override.
	core.set_standard_value(
		crate::nodes::shapenodebase::SIZE_INPUT,
		-1,
		NodeValue::Vec2([400.0, 300.0]),
	);

	(
		core,
		Box::new(TextGeneratorV3 {
			dont_emit_valign: false,
		}),
	)
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

#[cfg(test)]
mod tests {
	use super::*;
	use crate::node::NodeBehavior;
	use crate::value::{NodeValueTable, ValueType};
	use oakcore_rs::Rational;

	#[test]
	fn input_names() {
		let n = TextGeneratorV3 {
			dont_emit_valign: false,
		};
		assert_eq!(n.input_name(TEXT_INPUT), "Text");
		assert_eq!(n.input_name(VERTICAL_ALIGNMENT_INPUT), "Vertical Alignment");
		assert_eq!(n.input_name(ARGS_INPUT), "Arguments");
		assert_eq!(
			n.input_name(crate::nodes::generatorwithmerge::BASE_INPUT),
			"Base"
		);
		assert_eq!(
			n.input_name(crate::nodes::shapenodebase::POSITION_INPUT),
			"Position"
		);
		assert_eq!(
			n.input_name(crate::nodes::shapenodebase::SIZE_INPUT),
			"Size"
		);
		// The hidden use_args_in input has no display name override.
		assert_eq!(n.input_name(USE_ARGS_INPUT), USE_ARGS_INPUT);
	}

	#[test]
	fn create_wires_inherited_and_own_inputs() {
		let (core, behavior) = create();
		assert_eq!(behavior.type_id(), "org.olivevideoeditor.Olive.text3");
		assert_eq!(
			core.get_input(TEXT_INPUT).unwrap().value_type,
			ValueType::Text
		);
		assert!(core
			.get_input(TEXT_INPUT)
			.unwrap()
			.properties
			.iter()
			.any(|(k, v)| k == "vieweronly" && v == &NodeValue::Boolean(true)));
		let valign = core.get_input(VERTICAL_ALIGNMENT_INPUT).unwrap();
		assert_ne!(valign.flags & crate::input::flags::HIDDEN, 0);
		assert_ne!(valign.flags & crate::input::flags::NOT_CONNECTABLE, 0);
		assert_ne!(valign.flags & crate::input::flags::NOT_KEYFRAMABLE, 0);
		let use_args = core.get_input(USE_ARGS_INPUT).unwrap();
		assert_eq!(use_args.default, NodeValue::Boolean(true));
		let args = core.get_input(ARGS_INPUT).unwrap();
		assert_ne!(args.flags & crate::input::flags::ARRAY, 0);
		assert!(args
			.properties
			.iter()
			.any(|(k, v)| k == "arraystart" && v == &NodeValue::Int(1)));
		// No color input (ShapeNodeBase(false)).
		assert!(core
			.get_input(crate::nodes::shapenodebase::COLOR_INPUT)
			.is_none());
		assert_eq!(
			core.standard_value(crate::nodes::shapenodebase::SIZE_INPUT, -1),
			NodeValue::Vec2([400.0, 300.0])
		);
		assert_eq!(
			core.effect_input,
			crate::nodes::generatorwithmerge::BASE_INPUT
		);
		// v3 is shown in the create menu (no DONT_SHOW_IN_CREATE_MENU flag).
		assert_eq!(core.flags & crate::node::flags::DONT_SHOW_IN_CREATE_MENU, 0);
	}

	#[test]
	fn alignment_round_trip() {
		for v in [
			VerticalAlignment::Top,
			VerticalAlignment::Middle,
			VerticalAlignment::Bottom,
		] {
			let gizmo = TextGeneratorV3::get_gizmo_alignment_from_ours(v);
			assert_eq!(TextGeneratorV3::get_our_alignment_from_gizmos(gizmo), v);
		}
		assert_eq!(
			TextGeneratorV3::get_gizmo_alignment_from_ours(VerticalAlignment::Top),
			0
		);
		assert_eq!(
			TextGeneratorV3::get_gizmo_alignment_from_ours(VerticalAlignment::Middle),
			2
		);
		assert_eq!(
			TextGeneratorV3::get_gizmo_alignment_from_ours(VerticalAlignment::Bottom),
			1
		);
		// Unknown gizmo values map to Top.
		assert_eq!(
			TextGeneratorV3::get_our_alignment_from_gizmos(99),
			VerticalAlignment::Top
		);
	}

	#[test]
	fn format_string_expands_args() {
		let args = vec!["foo".to_string(), "bar".to_string()];
		assert_eq!(
			TextGeneratorV3::format_string("hello %1", &args),
			"hello foo"
		);
		assert_eq!(TextGeneratorV3::format_string("%2 %1", &args), "bar foo");
		// Out of range expands to nothing.
		assert_eq!(TextGeneratorV3::format_string("[%3]", &args), "[]");
		assert_eq!(TextGeneratorV3::format_string("[%0]", &args), "[]");
	}

	#[test]
	fn format_string_percent_escapes() {
		let args = vec!["foo".to_string()];
		assert_eq!(TextGeneratorV3::format_string("100%%", &args), "100%");
		assert_eq!(TextGeneratorV3::format_string("%%1", &args), "%1");
		// Lone % before non-digit/non-% is copied verbatim.
		assert_eq!(TextGeneratorV3::format_string("%x %", &args), "%x %");
		// Trailing % is copied verbatim.
		assert_eq!(TextGeneratorV3::format_string("end%", &args), "end%");
	}

	#[test]
	fn format_string_out_of_int_range_fails_to_zero() {
		let args = vec!["foo".to_string()];
		assert_eq!(
			TextGeneratorV3::format_string("%99999999999999999999", &args),
			""
		);
		assert_eq!(TextGeneratorV3::format_string("%2147483648", &args), "");
		assert_eq!(TextGeneratorV3::format_string("%2147483647", &args), "");
	}

	#[test]
	fn format_string_multidigit_and_reuse() {
		let args = vec!["a".to_string(), "b".to_string(), "c".to_string()];
		// %10 parses as index 10 (out of range with 3 args) -> empty.
		assert_eq!(TextGeneratorV3::format_string("%10", &args), "");
		assert_eq!(TextGeneratorV3::format_string("%2%2%2", &args), "bbb");
	}

	#[test]
	fn layout_request_uses_olive_html_and_96dpi() {
		let mut row = NodeValueRow::default();
		row.insert(
			TEXT_INPUT.to_string(),
			NodeValue::Text("<p>Hi</p>".to_string()),
		);
		row.insert(
			crate::nodes::shapenodebase::SIZE_INPUT.to_string(),
			NodeValue::Vec2([400.0, 300.0]),
		);
		let req = TextGeneratorV3::layout_request(&row);
		assert_eq!(req.text, "<p>Hi</p>");
		assert_eq!(req.mode, TextLayoutMode::OliveHtml);
		assert_eq!(req.dots_per_meter, 3780);
		assert_eq!(req.wrap_width, 400.0);
	}

	#[test]
	fn base_and_draw_offsets() {
		let size = [400.0, 300.0];
		let base = TextGeneratorV3::base_offset([0.0, 0.0], size, 1920, 1080);
		assert_eq!(base, (760.0, 390.0));
		// Top: no delta; middle/bottom use double math on doc.height.
		assert_eq!(
			TextGeneratorV3::draw_offset(VerticalAlignment::Top, base, size, 100.0),
			base
		);
		assert_eq!(
			TextGeneratorV3::draw_offset(VerticalAlignment::Middle, base, size, 100.0),
			(base.0, base.1 + 150.0 - 50.0)
		);
		assert_eq!(
			TextGeneratorV3::draw_offset(VerticalAlignment::Bottom, base, size, 100.0),
			(base.0, base.1 + 300.0 - 100.0)
		);
	}

	#[test]
	fn measure_without_backend_returns_zero_size() {
		crate::nodes::textbackend::set_text_backends(None, None);
		let mut row = NodeValueRow::default();
		row.insert(
			TEXT_INPUT.to_string(),
			NodeValue::Text("<p>Hi</p>".to_string()),
		);
		let (_req, doc) = TextGeneratorV3::measure_and_layout(&row);
		assert_eq!(doc.width, 0.0);
		assert_eq!(doc.height, 0.0);
	}

	#[test]
	fn value_pushes_job_when_text_nonempty() {
		let (core, behavior) = create();
		let mut row = NodeValueRow::default();
		row.insert(
			TEXT_INPUT.to_string(),
			NodeValue::Text("<p>Hi</p>".to_string()),
		);
		row.insert(USE_ARGS_INPUT.to_string(), NodeValue::Boolean(false));
		let mut table = NodeValueTable::default();
		behavior.value(&core, &row, Rational::new(0, 1), &mut table);
		assert!(matches!(
			table.get(ValueType::Texture),
			Some(NodeValue::Texture(h)) if h.is_null()
		));
	}

	#[test]
	fn value_expands_args_from_row() {
		let (core, behavior) = create();
		let mut row = NodeValueRow::default();
		row.insert(
			TEXT_INPUT.to_string(),
			NodeValue::Text("Hello %1".to_string()),
		);
		row.insert(USE_ARGS_INPUT.to_string(), NodeValue::Boolean(true));
		row.insert(ARGS_INPUT.to_string(), NodeValue::Text("World".to_string()));
		let mut table = NodeValueTable::default();
		behavior.value(&core, &row, Rational::new(0, 1), &mut table);
		assert!(matches!(
			table.get(ValueType::Texture),
			Some(NodeValue::Texture(h)) if h.is_null()
		));
		// The expanded text is carried by the (deferred) job, which has no
		// payload here; the expansion math itself is covered by
		// format_string tests.
	}

	#[test]
	fn value_passes_base_through_when_text_empty() {
		let (core, behavior) = create();
		let mut row = NodeValueRow::default();
		row.insert(TEXT_INPUT.to_string(), NodeValue::Text(String::new()));
		row.insert(
			crate::nodes::generatorwithmerge::BASE_INPUT.to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		);
		let mut table = NodeValueTable::default();
		behavior.value(&core, &row, Rational::new(0, 1), &mut table);
		assert!(matches!(
			table.get(ValueType::Texture),
			Some(NodeValue::Texture(_))
		));
	}

	#[test]
	fn value_pushes_nothing_when_text_empty_and_no_base() {
		let (core, behavior) = create();
		let mut row = NodeValueRow::default();
		row.insert(TEXT_INPUT.to_string(), NodeValue::Text(String::new()));
		let mut table = NodeValueTable::default();
		behavior.value(&core, &row, Rational::new(0, 1), &mut table);
		assert!(table.is_empty());
	}

	#[test]
	fn gizmo_activation_toggles_use_args() {
		let (mut core, mut behavior) = create();
		let node = behavior
			.as_any_mut()
			.unwrap()
			.downcast_mut::<TextGeneratorV3>()
			.unwrap();
		node.gizmo_activated(&mut core);
		assert_eq!(
			core.standard_value(USE_ARGS_INPUT, -1),
			NodeValue::Boolean(false)
		);
		assert!(node.dont_emit_valign);
		node.gizmo_deactivated(&mut core);
		assert_eq!(
			core.standard_value(USE_ARGS_INPUT, -1),
			NodeValue::Boolean(true)
		);
		assert!(node.dont_emit_valign);
	}

	#[test]
	fn set_vertical_alignment_undoable_maps_through_gizmo_alignment() {
		let (mut core, mut behavior) = create();
		let node = behavior
			.as_any_mut()
			.unwrap()
			.downcast_mut::<TextGeneratorV3>()
			.unwrap();
		// Gizmo vcenter (2) maps back to Middle (1).
		node.set_vertical_alignment_undoable(&mut core, 2);
		assert_eq!(
			core.standard_value(VERTICAL_ALIGNMENT_INPUT, -1),
			NodeValue::Combo(1)
		);
	}

	#[test]
	fn generate_frame_is_documented_noop() {
		let (core, behavior) = create();
		let mut frame = crate::handle::CHandle::null();
		behavior.generate_frame(&core, &mut frame, Rational::new(0, 1));
		assert!(frame.is_null());
	}

	#[test]
	fn duplicate_copies_node() {
		let (_core, behavior) = create();
		let copy = behavior.duplicate(&_core).unwrap();
		assert_eq!(copy.type_id(), "org.olivevideoeditor.Olive.text3");
		assert_eq!(copy.name(), "Text");
	}
}
