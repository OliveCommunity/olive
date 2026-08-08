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

//! Legacy text generator v2 (C++
//! `src/node/src/generator/text/textv2.{h,cpp}`,
//! `olive::TextGeneratorV2`, derives from `ShapeNodeBase`).
//!
//! FONT/RASTER BACKEND DEPENDENCY — DELIBERATELY UNDECIDED:
//! The C++ does NOT link any font/raster library directly (no freetype,
//! stb, harfbuzz, etc. anywhere in the tree). Text layout/rasterization
//! was historically Qt's rich text stack (`QTextDocument` +
//! `QAbstractTextDocumentLayout` + `QPainter` into a
//! `QImage::Format_Grayscale8` coverage buffer); it now runs behind the
//! facade-installed hooks in [`super::textbackend`]. No Rust font crate
//! is chosen here on purpose.

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Text input id (C++ `k_text_input`). Type: text; default
/// `"Sample Text"`.
pub const TEXT_INPUT: &str = "text_in";

/// HTML toggle input id (C++ `k_html_input`). Type: boolean; default
/// `false`.
pub const HTML_INPUT: &str = "html_in";

/// Vertical alignment input id (C++ `k_v_align_input`). Type: combo;
/// default `0` (C++ `k_vertical_align_top`); combo strings: "Top",
/// "Center", "Bottom".
pub const V_ALIGN_INPUT: &str = "valign_in";

/// Font family input id (C++ `k_font_input`). Type: font (C++
/// `NodeValue::k_font` — no dedicated [`crate::value::ValueType`]
/// variant, travels as text); no default.
pub const FONT_INPUT: &str = "font_in";

/// Font size input id (C++ `k_font_size_input`). Type: float; default
/// `72.0`.
pub const FONT_SIZE_INPUT: &str = "font_size_in";

/// Legacy text generator v2. The C++ class has no own private members
/// — it inherits position/size/color inputs and the polygon gizmo from
/// the shape base (C++ `ShapeNodeBase`, to be modelled in
/// `super::shapenodebase`) and everything else lives in [`NodeCore`] —
/// so this is a unit struct.
pub struct TextGeneratorV2;

impl NodeBehavior for TextGeneratorV2 {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Text (Legacy)"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.text2"
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
	/// "Text", `html_in` -> "Enable HTML", `font_in` -> "Font",
	/// `font_size_in` -> "Font Size", `valign_in` -> "Vertical Align"
	/// (combo strings Top/Center/Bottom); the base class retranslate
	/// covers the inherited shape inputs.
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): if the text input is
	/// non-empty, push a texture generate job at the global video
	/// params forced to `PixelFormat::f32`; otherwise push nothing.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Direct frame generation (C++ `generate_frame()`): rasterizes
	/// the text into a grayscale coverage buffer via the installed
	/// text backend at 72 DPI (2835 dots/meter) — HTML mode replaces
	/// newlines with `<br>`, wrap width is the shape size X, base
	/// offset is the shape position re-centered into frame space,
	/// vertical draw offset depends on the valign combo (top: none;
	/// center: `size.y/2 - doc.height/2`; bottom: `size.y -
	/// doc.height`), and the clip rect is the shape rect at the base
	/// offset — then writes the coverage multiplied by the RGBA color
	/// into the float frame (SIMD path in C++, scalar fallback
	/// identical). With no backend installed, warns once and leaves
	/// the frame empty.
	fn generate_frame(
		&self,
		core: &NodeCore,
		frame: &mut crate::bridge::render::TextureHandle,
		time: oakcore_rs::Rational,
	) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `TextGeneratorV2::TextGeneratorV2()`): adds
/// `text_in`, `html_in`, `valign_in`, `font_in` and `font_size_in` with
/// the defaults documented on the constants, sets the inherited shape
/// base `color_in` standard value to white and `size_in` to
/// `(400, 300)`, and sets the `dont_show_in_create_menu` flag.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_text_generator_v2` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.text2",
		name: "Text (Legacy)",
		categories: &[Category::Generator],
		create,
	});
}
