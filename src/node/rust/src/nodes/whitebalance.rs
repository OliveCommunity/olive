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

//! White balance node (C++
//! `src/node/src/color/whitebalance/whitebalance.{h,cpp}`,
//! `olive::WhiteBalanceNode`).
//!
//! White balance correction by color temperature and tint: converts a
//! scene illuminant temperature (Kelvin) into per-channel RGB gains
//! using the Tanner Helland blackbody approximation, normalized so the
//! green channel is preserved (no exposure shift); tint shifts along
//! the green-magenta axis.

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Temperature input id (C++ `k_temperature_input`). Type: float;
/// default `6500.0` (Kelvin); properties: `min = 1000.0`, `max =
/// 40000.0`, `view = normal slider`.
pub const TEMPERATURE_INPUT: &str = "temperature_in";

/// Tint input id (C++ `k_tint_input`). Type: float; default `0.0`;
/// properties: `min = -1.0`, `max = 1.0`, `base = 0.01`.
pub const TINT_INPUT: &str = "tint_in";

/// Gain uniform id (C++ `k_gain_input`). Not a declared node input —
/// the C++ never calls `add_input` for it; it is the shader uniform
/// name fed per frame in `value()` with the RGB gain computed by
/// [`WhiteBalanceNode::gain_for_temperature`]. Type: vec3.
pub const GAIN_INPUT: &str = "wb_gain_in";

/// White balance node. Adjusts white balance by color temperature and
/// tint. The C++ class has no own private members, so this is a
/// unit-like struct (caches/inputs live in `NodeCore`).
pub struct WhiteBalanceNode;

/// Fragment shader (C++ `get_shader_code` loads the
/// `:/shaders/whitebalance.frag` resource). Text copied verbatim from
/// `engine/shaders/whitebalance.frag`.
const SHADER_FRAG: &str = r#"uniform sampler2D tex_in;

uniform vec3 wb_gain_in;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void)
{
    vec4 source = texture(tex_in, ove_texcoord);

    // Deliberately not clamped: white balance must also work on HDR/linear
    // footage with values above 1.0
    frag_color = vec4(source.rgb * wb_gain_in, source.a);
}
"#;

impl WhiteBalanceNode {
	/// Fragment shader for any request (C++ `get_shader_code()` ignores
	/// the request id and always returns this shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}

	/// RGB gains for a given illuminant temperature and tint (C++
	/// `get_gain_for_temperature()`, extracted for testability). Kelvin
	/// is clamped to [1000, 40000]; the Tanner Helland blackbody
	/// approximation gives 0-255 per channel (red: 255 below 6600K, else
	/// `329.698727446 * (t - 60)^-0.1332047592`; green: logarithmic
	/// below 6600K, power-law above; blue: 255 above 6600K, 0 below
	/// 1900K, logarithmic between). The result is normalized so the
	/// green channel gain is 1.0 at tint 0, then tint scales the green
	/// channel by `clamp(1.0 + tint, 0.0, 2.0)` (green-magenta axis).
	pub fn gain_for_temperature(kelvin: f64, tint: f64) -> [f64; 3] {
		todo!()
	}
}

impl NodeBehavior for WhiteBalanceNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"White Balance"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.whitebalance"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Color]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Adjust white balance by color temperature and tint."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` -> "Input",
	/// `temperature_in` -> "Temperature (K)", `tint_in` -> "Tint".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): the request id is
	/// ignored; always returns [`SHADER_FRAG`].
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// otherwise builds a `ShaderJob` from the whole input row, inserts
	/// `wb_gain_in` as a vec3 computed by
	/// [`Self::gain_for_temperature`] from the temperature and tint
	/// inputs, and pushes the texture as that job.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `WhiteBalanceNode::WhiteBalanceNode()`): adds
/// `tex_in` (texture, effect input), `temperature_in` and `tint_in`
/// with the defaults and properties documented on the constants, and
/// sets the video-effect flag.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.whitebalance`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.whitebalance",
		name: "White Balance",
		categories: &[Category::Color],
		create,
	});
}
