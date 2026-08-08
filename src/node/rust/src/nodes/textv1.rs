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

//! Legacy text generator v1 (C++
//! `src/node/src/generator/text/textv1.{h,cpp}`,
//! `olive::TextGeneratorV1`).
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

/// Text color input id (C++ `k_color_input`). Type: color; default
/// `Color(1.0, 1.0, 1.0)` (white).
pub const COLOR_INPUT: &str = "color_in";

/// Vertical alignment input id (C++ `k_v_align_input`). Type: combo;
/// default `1` (center); combo strings: "Top", "Center", "Bottom".
pub const V_ALIGN_INPUT: &str = "valign_in";

/// Font family input id (C++ `k_font_input`). Type: font (C++
/// `NodeValue::k_font` — no dedicated [`crate::value::ValueType`]
/// variant, travels as text); no default.
pub const FONT_INPUT: &str = "font_in";

/// Font size input id (C++ `k_font_size_input`). Type: float; default
/// `72.0`.
pub const FONT_SIZE_INPUT: &str = "font_size_in";

/// Legacy text generator v1. The C++ class has no own private members
/// (inputs/caches live in [`NodeCore`]), so this is a unit struct.
pub struct TextGeneratorV1;

impl NodeBehavior for TextGeneratorV1 {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Text (Legacy)"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.textgenerator"
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
	/// `font_size_in` -> "Font Size", `color_in` -> "Color",
	/// `valign_in` -> "Vertical Align" (combo strings Top/Center/
	/// Bottom).
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): if the text input is
	/// non-empty, push a texture generate job at the global video
	/// params; otherwise push nothing.
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
	/// the text into a grayscale coverage buffer (32-bit-aligned
	/// scanlines) via the installed text backend — HTML mode replaces
	/// newlines with `<br>`, wrap width is 80% of frame width ("title
	/// safe"), horizontal centering is on, vertical offset depends on
	/// the valign combo (top: 10% margin; center: frame center; bottom:
	/// 10% bottom margin) — then transplants the coverage as alpha
	/// into the float frame multiplied by the color input. With no
	/// backend installed, warns once and leaves the frame empty.
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

/// Constructor (C++ `TextGeneratorV1::TextGeneratorV1()`): adds
/// `text_in`, `html_in`, `color_in`, `valign_in`, `font_in` and
/// `font_size_in` with the defaults documented on the constants, and
/// sets the `dont_show_in_create_menu` flag.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_text_generator_v1` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.textgenerator",
		name: "Text (Legacy)",
		categories: &[Category::Generator],
		create,
	});
}
