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

//! Noise generator (C++ `src/node/src/generator/noise/noise.{h,cpp}`,
//! `olive::NoiseGeneratorNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Base texture input id (C++ `k_base_in`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const BASE_INPUT: &str = "base_in";

/// Color noise toggle input id (C++ `k_color_input`). Type: bool;
/// default `false`.
pub const COLOR_INPUT: &str = "color_in";

/// Noise strength input id (C++ `k_strength_input`). Type: float;
/// default `0.2`; properties: `view = percentage`, `min = 0`.
pub const STRENGTH_INPUT: &str = "strength_in";

/// Noise generator node. Adds gold-noise to an optional base texture
/// (or generates it standalone). Has no own member fields in C++.
pub struct NoiseGeneratorNode;

/// Fragment shader (C++ loads the `:/shaders/noise.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/noise.frag`.
const SHADER_FRAG: &str = r#"uniform float time_in;

uniform float strength_in;
uniform bool color_in;

uniform sampler2D base_in;
uniform bool base_in_enabled;

in vec2 ove_texcoord;
out vec4 frag_color;

float PHI = 1.61803398874989484820459 * 00000.1; // Golden Ratio
float PI  = 3.14159265358979323846264 *  00000.1; // PI
float SQ2 = 1.41421356237309504880169 * 10000.0; // Square Root of Two

bool isNan( float val )
{
  return ( val < 0.0 || 0.0 < val || val == 0.0 ) ? false : true;
  // important: some nVidias failed to cope with version below.
  // Probably wrong optimization.
  /*return ( val <= 0.0 || 0.0 <= val ) ? false : true;*/

  // Taken from: https://stackoverflow.com/questions/11810158/how-to-deal-with-nan-or-inf-in-opengl-es-2-0-shaders
}

float gold_noise(vec2 coordinate, float seed){
  float value = fract(tan(distance(coordinate*(seed+PHI), vec2(PHI, PI)))*SQ2)*(strength_in);
  return isNan(value) ? 0.0 : value;
}

void main(void) {
  vec3 noise;
  if (color_in) {
    noise = vec3(gold_noise(ove_texcoord, time_in + 42069.0), gold_noise(ove_texcoord, time_in + 69220.0), gold_noise(ove_texcoord, time_in + 1337.0));
  } else {
    noise = vec3(gold_noise(ove_texcoord, time_in + 69420.0));
  }

  if (base_in_enabled) {
    vec4 base = texture(base_in, ove_texcoord);
    base.rgb += noise;
    frag_color = base;
  } else {
    frag_color = vec4(noise, 1.0);
  }
}
"#;

impl NoiseGeneratorNode {
	/// Fragment shader for all shader requests (C++
	/// `get_shader_code()` ignores the request id).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for NoiseGeneratorNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Noise"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.noise"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Generator]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Generates noise patterns"
	}

	/// Localized input names (C++ `retranslate()`): `base_in` ->
	/// "Base", `strength_in` -> "Strength", `color_in` -> "Color".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): builds a shader job from the
	/// input row, additionally inserting `time_in` (current time in
	/// seconds as a float), then pushes a texture job using the base
	/// texture's params when connected, else the sequence video params.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): returns the
	/// noise fragment shader for any request id.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `NoiseGeneratorNode::NoiseGeneratorNode()`): adds
/// `base_in`, `strength_in` and `color_in` with the defaults, flags and
/// properties documented on the constants, sets the video-effect flag
/// and makes `base_in` the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.noise`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.noise",
		name: "Noise",
		categories: &[Category::Generator],
		create,
	});
}
