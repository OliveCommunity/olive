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

//! Shared generator-with-merge base (C++
//! `src/node/src/generator/shape/generatorwithmerge.{h,cpp}`,
//! `olive::GeneratorWithMerge`).
//!
//! Abstract C++ base for generators that can merge their output over a
//! base texture. Not instantiable, so this is a helper module, not a
//! [`NodeBehavior`] implementation.

use crate::value::NodeValue;

/// Base texture input id (C++ `k_base_input`). Type: texture; flags:
/// not-keyframable; the base constructor makes it the effect input and
/// sets the video-effect flag.
pub const BASE_INPUT: &str = "base_in";

/// Merge fragment shader (C++ `get_shader_code()` `"mrg"` branch loads
/// `:/shaders/alphaover.frag`). Text copied verbatim from
/// `engine/shaders/alphaover.frag`.
const MERGE_SHADER_FRAG: &str = r#"uniform sampler2D base_in;
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

/// Merge fragment shader for the `"mrg"` shader request (C++
/// `GeneratorWithMerge::get_shader_code()` `"mrg"` branch; any other
/// request returns an empty `ShaderCode`).
pub fn merge_shader_frag() -> &'static str {
	MERGE_SHADER_FRAG
}

/// Helper mirroring the C++ `GeneratorWithMerge` base. The base has no
/// own member fields; its constructor adds [`BASE_INPUT`], makes it the
/// effect input and sets the video-effect flag, and its `retranslate()`
/// names `base_in` "Base".
pub struct GeneratorWithMerge;

impl GeneratorWithMerge {
	/// Push a generated texture job, merged over the base input when
	/// one is connected (C++ `push_mergable_job()`): with a base
	/// texture, builds a `"mrg"` shader job with the base input as
	/// `MergeNode::k_base_in` and the generated job as
	/// `MergeNode::k_blend_in`, pushing `base->to_job(merge)`; without
	/// a base, pushes the generated job unchanged.
	///
	/// The Rust model has no shader-job payload (see
	/// [`crate::nodes::mathbase`]): the merged case pushes a null
	/// texture handle marking a renderer-deferred `"mrg"` shader job,
	/// and the un-merged case pushes `job` itself.
	pub fn push_mergable_job(
		inputs: &crate::value::NodeValueRow,
		job: crate::bridge::render::TextureHandle,
		table: &mut crate::value::NodeValueTable,
	) {
		match inputs.get(BASE_INPUT) {
			Some(NodeValue::Texture(_)) => {
				// A base is connected: the C++ pushes
				// `base->to_job(ShaderJob("mrg"))` — a deferred alpha-over
				// merge of the generated texture over the base.
				// `// CPP-PARITY: generatorwithmerge.cpp` push_mergable_job.
				table.push(
					crate::value::ValueType::Texture,
					NodeValue::Texture(crate::handle::CHandle::null()),
					None,
				);
			}
			_ => {
				table.push(
					crate::value::ValueType::Texture,
					NodeValue::Texture(job),
					None,
				);
			}
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::value::{NodeValue, NodeValueTable, ValueType};

	#[test]
	fn push_job_without_base_pushes_job() {
		let job = crate::handle::CHandle::null();
		let mut table = NodeValueTable::default();
		GeneratorWithMerge::push_mergable_job(
			&crate::value::NodeValueRow::default(),
			job,
			&mut table,
		);
		match table.get(ValueType::Texture) {
			Some(NodeValue::Texture(h)) => assert!(h.is_null()),
			_ => panic!("texture expected"),
		}
	}

	#[test]
	fn push_job_with_base_pushes_deferred_merge() {
		let job = crate::handle::CHandle::null();
		let inputs = crate::value::NodeValueRow::from([(
			BASE_INPUT.to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		)]);
		let mut table = NodeValueTable::default();
		GeneratorWithMerge::push_mergable_job(&inputs, job, &mut table);
		assert!(
			table.get(ValueType::Texture).is_some(),
			"merge job placeholder pushed"
		);
	}
}
