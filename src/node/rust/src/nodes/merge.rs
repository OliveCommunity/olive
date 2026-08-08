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

//! Merge node (C++ `src/node/src/math/merge/merge.{h,cpp}`,
//! `olive::MergeNode`): alpha-over composites two textures.

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Base (background) texture input id (C++ `k_base_in`). Type:
/// texture; flags: not-keyframable.
pub const BASE_INPUT: &str = "base_in";

/// Blend (foreground) texture input id (C++ `k_blend_in`). Type:
/// texture; flags: not-keyframable.
pub const BLEND_INPUT: &str = "blend_in";

/// Merge node. Unit-like: the C++ class's only members are
/// `base_in_`/`blend_in_` `NodeInput*` back-pointers, which live in
/// [`NodeCore`] here, so there is nothing to duplicate.
pub struct MergeNode;

/// Fragment shader (C++ `get_shader_code()` loads the
/// `:/shaders/alphaover.frag` resource). Text copied verbatim from
/// `engine/shaders/alphaover.frag`.
const SHADER_FRAG: &str = r#"uniform sampler2D base_in;
uniform sampler2D blend_in;
uniform bool base_in_enabled;
uniform bool blend_in_enabled;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void) {
    vec4 base_col = texture(base_in, ove_texcoord);
    vec4 blend_col = texture(blend_in, ove_texcoord);

    if (!base_in_enabled && !blend_in_enabled) {
        frag_color = vec4(0.0);
        return;
    }

    if (!base_in_enabled) {
        frag_color = blend_col;
        return;
    }

    if (!blend_in_enabled) {
        frag_color = base_col;
        return;
    }

    base_col *= 1.0 - blend_col.a;
    base_col += blend_col;

    frag_color = base_col;
}
"#;

impl MergeNode {
	/// Fragment shader (C++ `get_shader_code()`; the request id is
	/// ignored — the same alpha-over shader serves every request).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for MergeNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Merge"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.merge"
	}

	/// Categories (C++ `category()`; filed under math even though it
	/// composites textures).
	fn categories(&self) -> &[Category] {
		&[Category::Math]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Merge two textures together."
	}

	/// Localized input names (C++ `retranslate()`): `base_in` ->
	/// "Base", `blend_in` -> "Blend".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): if only the blend texture is
	/// present, or the blend texture has fewer than RGBA channels (no
	/// alpha to over with), push the blend input as-is; if only the
	/// base texture is present, push the base input as-is; if both are
	/// present, push a shader job over the base texture with the whole
	/// input row as job values; if neither, push nothing.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): always returns
	/// the alpha-over fragment shader regardless of the request id.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `MergeNode::MergeNode()`): adds `base_in` and
/// `blend_in` as not-keyframable texture inputs and sets the
/// dont-show-in-param-view flag.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_merge_node` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.merge",
		name: "Merge",
		categories: &[Category::Math],
		create,
	});
}
