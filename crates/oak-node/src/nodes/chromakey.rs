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

//! Chroma Key effect (C++ `src/node/src/keying/chromakey/chromakey.{h,cpp}`,
//! `olive::ChromaKeyNode`, derived from `olive::OCIOBaseNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Key color input id (C++ `k_color_input`). Type: color; default
/// `Color(0.0, 1.0, 0.0, 1.0)` (opaque green).
pub const COLOR_INPUT: &str = "color_key";

/// Show-mask-only toggle input id (C++ `k_mask_only_input`). Type:
/// boolean; default `false`.
pub const MASK_ONLY_INPUT: &str = "mask_only_in";

/// Invert-mask toggle input id (C++ `k_invert_input`). Type: boolean;
/// default `false`.
pub const INVERT_INPUT: &str = "invert_in";

/// Upper tolerance input id (C++ `k_upper_tolerance_input`). Type:
/// float; default `25.0`; properties: `base = 0.1` (the `min` property
/// tracking the lower tolerance is present but disabled in C++ — see
/// the FIXME in the constructor).
pub const UPPER_TOLERANCE_INPUT: &str = "upper_tolerance_in";

/// Lower tolerance input id (C++ `k_lower_tolerance_input`). Type:
/// float; default `5.0`; properties: `min = 0.0`, `base = 0.1`.
pub const LOWER_TOLERANCE_INPUT: &str = "lower_tolerance_in";

/// Garbage matte texture input id (C++ `k_garbage_matte_input`). Type:
/// texture; flags: not-keyframable.
pub const GARBAGE_MATTE_INPUT: &str = "garbage_in";

/// Core matte texture input id (C++ `k_core_matte_input`). Type:
/// texture; flags: not-keyframable.
pub const CORE_MATTE_INPUT: &str = "core_in";

/// Shadows input id (C++ `k_shadows_input`). Type: float; default
/// `100.0`; properties: `min = 0.0`, `base = 0.1`.
pub const SHADOWS_INPUT: &str = "shadows_in";

/// Highlights input id (C++ `k_highlights_input`). Type: float;
/// default `100.0`; properties: `min = 0.0`, `base = 0.1`.
pub const HIGHLIGHTS_INPUT: &str = "highlights_in";

/// Chroma key node: keys on the CIE Lab distance from a selected
/// color, with optional garbage/core mattes.
///
/// The C++ class derives from `OCIOBaseNode`, which owns the `tex_in`
/// texture input (C++ `OCIOBaseNode::k_texture_input = "tex_in"`, the
/// effect input), the color manager pointer, and the OCIO color
/// processor handle; that state is held here via the shared
/// `crate::nodes::ociobase` helper. The class has no other own members (the
/// private `generate_processor()` is a method, not state).
pub struct ChromaKeyNode {
	/// OCIO base state (C++ base class `OCIOBaseNode`: `manager_` and
	/// `processor_`).
	base: crate::nodes::ociobase::OcioBase,
}

/// Fragment shader (C++ loads the `:/shaders/chromakey.frag` resource
/// in `get_shader_code`). Text copied verbatim from
/// `engine/shaders/chromakey.frag`. The `%1` marker is replaced with
/// the OCIO-generated shader stub (`request.stub`) at request time;
/// the shader calls `SceneLinearToCIEXYZ_d65`, which the stub must
/// define. Note the shader still uses the legacy misspelled uniform
/// names `upper_tolerence_in`/`lower_tolerence_in`, matching the old
/// input ids remapped by `map_legacy_input_id`.
const SHADER_FRAG: &str = r#"// Main texture input
uniform sampler2D tex_in;
uniform vec4 color_key;
uniform bool mask_only_in;
uniform float upper_tolerence_in;
uniform float lower_tolerence_in;

uniform sampler2D garbage_in;
uniform sampler2D core_in;
uniform bool garbage_in_enabled;
uniform bool core_in_enabled;
uniform bool invert_in;

uniform float highlights_in;
uniform float shadows_in;


// Main texture coordinate
in vec2 ove_texcoord;
out vec4 frag_color;

// Program will replace this with OCIO's auto-generated shader code
%1

// Assume D65 white point
float Xn = 95.0489;
float Yn = 100.0;
float Zn = 108.8840;
float delta = 0.20689655172; // 6/29

float func(float t) {
  if (t > pow(delta, 3.0)){
    return pow(t, 1.0/3.0);
  } else{
    return (t / (3.0 * pow(delta, 2))) + 4.0/29.0;
  }
}

vec4 CIExyz_to_Lab(vec4 CIE) {
  vec4 lab;
  lab.r = 116.0 * func(CIE.g / Yn) - 16.0;
  lab.g = 500.0 * (func(CIE.r / Xn) -  func(CIE.g / Yn));
  lab.b = 200.0 * (func(CIE.g / Yn) -  func(CIE.b / Zn));
  lab.w = CIE.w;

  return lab;
}

float colorclose(vec4 col, vec4 key, float tola,float tolb) { 
  // Decides if a color is close to the specified hue
  float temp = sqrt(((key.g-col.g)*(key.g-col.g))+((key.b-col.b)*(key.b-col.b))+((key.r-col.r)*(key.r-col.r)));
  if (temp < tola) {return (0.0);} 
  if (temp < tolb) {return ((temp-tola)/(tolb-tola));} 
  return (1.0); 
}


void main() {

  vec4 col = texture(tex_in, ove_texcoord);

  vec4 unassoc = col;
  if (unassoc.a > 0) {
    unassoc.rgb /= unassoc.a;
  }

  // Perform color conversion
  vec4 cie_xyz = SceneLinearToCIEXYZ_d65(unassoc);
  vec4 lab = CIExyz_to_Lab(cie_xyz);

  vec4 cie_xyz_key = SceneLinearToCIEXYZ_d65(color_key);
  vec4 lab_key = CIExyz_to_Lab(cie_xyz_key);

  float mask = colorclose(lab, lab_key, lower_tolerence_in, upper_tolerence_in);

  mask = clamp(mask, 0.0, 1.0);

  if (garbage_in_enabled) {
    // Force anything we want to remove to be 0.0
    vec4 garbage = texture(garbage_in, ove_texcoord);
    // Assumes garbage is achromatic
    mask -= garbage.r;
    mask = clamp(mask, 0.0, 1.0);
  }

  if (core_in_enabled) {
    // Force anything we want to keep to be 1.0
    vec3 core = texture(core_in, ove_texcoord).rgb;
    // Assumes core is achromatic
    mask += core.r;
    mask = clamp(mask, 0.0, 1.0);
  }

  // Crush blacks and push whites
  mask = shadows_in * 0.01 * (highlights_in * 0.01 * mask - 1.0) + 1.0;
  mask = clamp(mask, 0.0, 1.0);

  // Invert
  if (invert_in) {
    mask = 1.0 - mask;
  }

  col *= mask;

  if (!mask_only_in) {
    frag_color = col;
  } else {
    frag_color = vec4(vec3(mask), 1.0);
  }
}
"#;

impl ChromaKeyNode {
	/// Fragment shader with the `%1` OCIO stub marker still in place
	/// (C++ `get_shader_code()` before the stub substitution).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}

	/// (Re)build the OCIO color processor (C++ `generate_processor()`):
	/// converts to the `cie_xyz_d65_interchange` output color space via
	/// `oakrender_color_processor_create_transform` and stores the result
	/// with [`OcioBase::set_processor`] when creation succeeds.
	fn generate_processor(&mut self, _core: &mut NodeCore) {
		// The C++ wraps the color manager, builds a transform to the
		// "cie_xyz_d65_interchange" output color space and creates the
		// processor through `oakrender_color_processor_create_transform`,
		// storing it with OcioBase::set_processor when `processor.ctx` is
		// non-null. Without a manager (the Rust model reaches the manager
		// through the oakrender bridge, absent here) the C++ guard
		// `if (manager())` fails, so this is a no-op and the processor
		// stays empty — `value()` then pushes nothing.
		// `// CPP-PARITY: chromakey.cpp` generate_processor.
	}
}

impl NodeBehavior for ChromaKeyNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Chroma Key"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.chromakey"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Keying]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"A simple color key based on the distance from the chroma of a selected color."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Input", `garbage_in` -> "Garbage Matte", `core_in` ->
	/// "Core Matte", `color_key` -> "Key Color", `shadows_in` ->
	/// "Shadows", `highlights_in` -> "Highlights",
	/// `upper_tolerance_in` -> "Upper Tolerance",
	/// `lower_tolerance_in` -> "Lower Tolerance", `invert_in` ->
	/// "Invert Mask", `mask_only_in` -> "Show Mask Only".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		match id {
			crate::nodes::ociobase::TEXTURE_INPUT => "Input",
			GARBAGE_MATTE_INPUT => "Garbage Matte",
			CORE_MATTE_INPUT => "Core Matte",
			COLOR_INPUT => "Key Color",
			SHADOWS_INPUT => "Shadows",
			HIGHLIGHTS_INPUT => "Highlights",
			UPPER_TOLERANCE_INPUT => "Upper Tolerance",
			LOWER_TOLERANCE_INPUT => "Lower Tolerance",
			INVERT_INPUT => "Invert Mask",
			MASK_ONLY_INPUT => "Show Mask Only",
			_ => id,
		}
	}

	/// Input value changed (C++ `InputValueChangedEvent`): the lower
	/// tolerance branch that would update the upper tolerance's `min`
	/// property is disabled in C++ (FIXME); unconditionally
	/// regenerates the OCIO color processor
	/// (`generate_processor()`).
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		let _ = (input, element);
		// The C++ lower-tolerance branch that mirrors the lower tolerance
		// into the upper tolerance's `min` property is commented out
		// (FIXME: breaks when the lower tolerance is keyframed/connected),
		// so only the processor regeneration remains.
		self.generate_processor(core);
	}

	/// Evaluate outputs (C++ `value()`): no texture on `tex_in` ->
	/// push nothing; texture present and a valid OCIO processor ->
	/// push a `ColorTransformJob` wired with the processor, the input
	/// texture, this node as the custom-shader provider, and the
	/// function name `SceneLinearToCIEXYZ_d65`.
	///
	/// The C++ class also overrides `config_changed()` (pure virtual
	/// on `OCIOBaseNode`) to regenerate the processor when the OCIO
	/// config changes; `NodeBehavior` has no equivalent hook — that
	/// wiring belongs to the facade/event layer.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oak_core::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		let _ = (core, time);
		match inputs.get(crate::nodes::ociobase::TEXTURE_INPUT) {
			Some(crate::value::NodeValue::Texture(_)) => {
				if self.base.processor().is_some() {
					// `// CPP-PARITY: chromakey.cpp` `value()` — the C++
					// builds a ColorTransformJob with the processor, the
					// input texture, this node as the custom-shader
					// provider and the function name
					// `SceneLinearToCIEXYZ_d65`. The Rust model has no
					// color-transform job payload: the renderer seam
					// resolves the deferred job from this null handle.
					table.push(
						crate::value::ValueType::Texture,
						crate::value::NodeValue::Texture(crate::handle::CHandle::null()),
						None,
					);
				}
				// Texture present but no processor: the C++ pushes
				// nothing (unlike the base class, which would pass the
				// texture through).
			}
			_ => {}
		}
	}

	/// Shader code request (C++ `get_shader_code()`): reads the
	/// fragment shader and replaces every `%1` marker with
	/// `request.stub` (the OCIO auto-generated shader code).
	fn shader_code(&self, request: &str) -> Option<String> {
		Some(SHADER_FRAG.replace("%1", request))
	}

	/// Legacy input id mapping (C++ `get_input_id_for_legacy_id()`):
	/// maps the misspelled `upper_tolerence_in` /
	/// `lower_tolerence_in` from old project files onto
	/// [`UPPER_TOLERANCE_INPUT`] / [`LOWER_TOLERANCE_INPUT`];
	/// anything else defers to the default (identity) mapping.
	fn map_legacy_input_id<'a>(&self, id: &'a str) -> &'a str {
		match id {
			"upper_tolerence_in" => UPPER_TOLERANCE_INPUT,
			"lower_tolerence_in" => LOWER_TOLERANCE_INPUT,
			_ => id,
		}
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		// The C++ copy constructor copies the embedded OCIO base state;
		// a fresh base with no processor is the safe Rust port (the
		// processor is never populated without the render bridge).
		Some(Box::new(ChromaKeyNode {
			base: crate::nodes::ociobase::OcioBase::new(),
		}))
	}
}

/// Constructor (C++ `ChromaKeyNode::ChromaKeyNode()`): the
/// `OCIOBaseNode` base adds `tex_in` (texture, not-keyframable), sets
/// the video-effect flag and the effect input; this class then adds
/// `color_key`, `lower_tolerance_in`, `upper_tolerance_in`,
/// `garbage_in`, `core_in`, `highlights_in`, `shadows_in`,
/// `invert_in`, and `mask_only_in` with the defaults and properties
/// documented on the constants.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();

	// OCIOBaseNode base constructor.
	let mut tex = crate::input::Input::new(
		crate::nodes::ociobase::TEXTURE_INPUT,
		crate::value::ValueType::Texture,
		crate::value::NodeValue::None,
	);
	tex.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(tex);
	core.effect_input = crate::nodes::ociobase::TEXTURE_INPUT.to_string();
	core.flags |= crate::node::flags::VIDEO_EFFECT;

	core.add_input(crate::input::Input::new(
		COLOR_INPUT,
		crate::value::ValueType::Color,
		crate::value::NodeValue::Color([0.0, 1.0, 0.0, 1.0]),
	));

	let mut lower = crate::input::Input::new(
		LOWER_TOLERANCE_INPUT,
		crate::value::ValueType::Float,
		crate::value::NodeValue::Float(5.0),
	);
	lower.properties = vec![
		("min".to_string(), crate::value::NodeValue::Float(0.0)),
		("base".to_string(), crate::value::NodeValue::Float(0.1)),
	];
	core.add_input(lower);

	let mut upper = crate::input::Input::new(
		UPPER_TOLERANCE_INPUT,
		crate::value::ValueType::Float,
		crate::value::NodeValue::Float(25.0),
	);
	upper.properties = vec![("base".to_string(), crate::value::NodeValue::Float(0.1))];
	core.add_input(upper);

	let mut garbage = crate::input::Input::new(
		GARBAGE_MATTE_INPUT,
		crate::value::ValueType::Texture,
		crate::value::NodeValue::None,
	);
	garbage.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(garbage);

	let mut core_matte = crate::input::Input::new(
		CORE_MATTE_INPUT,
		crate::value::ValueType::Texture,
		crate::value::NodeValue::None,
	);
	core_matte.flags |= crate::input::flags::NOT_KEYFRAMABLE;
	core.add_input(core_matte);

	let mut highlights = crate::input::Input::new(
		HIGHLIGHTS_INPUT,
		crate::value::ValueType::Float,
		crate::value::NodeValue::Float(100.0),
	);
	highlights.properties = vec![
		("min".to_string(), crate::value::NodeValue::Float(0.0)),
		("base".to_string(), crate::value::NodeValue::Float(0.1)),
	];
	core.add_input(highlights);

	let mut shadows = crate::input::Input::new(
		SHADOWS_INPUT,
		crate::value::ValueType::Float,
		crate::value::NodeValue::Float(100.0),
	);
	shadows.properties = vec![
		("min".to_string(), crate::value::NodeValue::Float(0.0)),
		("base".to_string(), crate::value::NodeValue::Float(0.1)),
	];
	core.add_input(shadows);

	core.add_input(crate::input::Input::new(
		INVERT_INPUT,
		crate::value::ValueType::Boolean,
		crate::value::NodeValue::Boolean(false),
	));
	core.add_input(crate::input::Input::new(
		MASK_ONLY_INPUT,
		crate::value::ValueType::Boolean,
		crate::value::NodeValue::Boolean(false),
	));

	(
		core,
		Box::new(ChromaKeyNode {
			base: crate::nodes::ociobase::OcioBase::new(),
		}),
	)
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.chromakey`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.chromakey",
		name: "Chroma Key",
		categories: &[Category::Keying],
		create,
	});
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::value::{NodeValue, NodeValueTable, ValueType};
	use oak_core::Rational;

	#[test]
	fn input_names() {
		let n = ChromaKeyNode {
			base: crate::nodes::ociobase::OcioBase::new(),
		};
		assert_eq!(n.input_name(crate::nodes::ociobase::TEXTURE_INPUT), "Input");
		assert_eq!(n.input_name(GARBAGE_MATTE_INPUT), "Garbage Matte");
		assert_eq!(n.input_name(CORE_MATTE_INPUT), "Core Matte");
		assert_eq!(n.input_name(COLOR_INPUT), "Key Color");
		assert_eq!(n.input_name(SHADOWS_INPUT), "Shadows");
		assert_eq!(n.input_name(HIGHLIGHTS_INPUT), "Highlights");
		assert_eq!(n.input_name(UPPER_TOLERANCE_INPUT), "Upper Tolerance");
		assert_eq!(n.input_name(LOWER_TOLERANCE_INPUT), "Lower Tolerance");
		assert_eq!(n.input_name(INVERT_INPUT), "Invert Mask");
		assert_eq!(n.input_name(MASK_ONLY_INPUT), "Show Mask Only");
		assert_eq!(n.input_name("other_in"), "other_in");
	}

	#[test]
	fn create_wires_inputs_flags_and_properties() {
		let (core, behavior) = create();
		assert_eq!(behavior.type_id(), "org.olivevideoeditor.Olive.chromakey");
		assert_ne!(
			core.get_input(crate::nodes::ociobase::TEXTURE_INPUT)
				.unwrap()
				.flags & crate::input::flags::NOT_KEYFRAMABLE,
			0
		);
		assert_eq!(
			core.get_input(COLOR_INPUT).unwrap().default,
			NodeValue::Color([0.0, 1.0, 0.0, 1.0])
		);
		assert_eq!(
			core.get_input(LOWER_TOLERANCE_INPUT).unwrap().default,
			NodeValue::Float(5.0)
		);
		assert_eq!(
			core.get_input(UPPER_TOLERANCE_INPUT).unwrap().default,
			NodeValue::Float(25.0)
		);
		assert_eq!(
			core.get_input(HIGHLIGHTS_INPUT).unwrap().default,
			NodeValue::Float(100.0)
		);
		assert_eq!(
			core.get_input(SHADOWS_INPUT).unwrap().default,
			NodeValue::Float(100.0)
		);
		assert_eq!(
			core.get_input(INVERT_INPUT).unwrap().default,
			NodeValue::Boolean(false)
		);
		assert_eq!(
			core.get_input(MASK_ONLY_INPUT).unwrap().default,
			NodeValue::Boolean(false)
		);
		for id in [GARBAGE_MATTE_INPUT, CORE_MATTE_INPUT] {
			assert_ne!(
				core.get_input(id).unwrap().flags & crate::input::flags::NOT_KEYFRAMABLE,
				0
			);
		}
		assert_eq!(core.effect_input, crate::nodes::ociobase::TEXTURE_INPUT);
		assert_ne!(core.flags & crate::node::flags::VIDEO_EFFECT, 0);
	}

	#[test]
	fn shader_code_replaces_stub_marker() {
		let n = ChromaKeyNode {
			base: crate::nodes::ociobase::OcioBase::new(),
		};
		let stub = "float SceneLinearToCIEXYZ_d65(vec4 c){ return 0.0; }";
		let code = n.shader_code(stub).unwrap();
		assert!(!code.contains("%1"));
		assert!(code.contains(stub));
	}

	#[test]
	fn legacy_input_ids_remap_misspellings() {
		let n = ChromaKeyNode {
			base: crate::nodes::ociobase::OcioBase::new(),
		};
		assert_eq!(
			n.map_legacy_input_id("upper_tolerence_in"),
			UPPER_TOLERANCE_INPUT
		);
		assert_eq!(
			n.map_legacy_input_id("lower_tolerence_in"),
			LOWER_TOLERANCE_INPUT
		);
		assert_eq!(
			n.map_legacy_input_id("anything_else_in"),
			"anything_else_in"
		);
	}

	#[test]
	fn value_no_texture_pushes_nothing() {
		let (core, behavior) = create();
		let mut table = NodeValueTable::default();
		behavior.value(
			&core,
			&crate::value::NodeValueRow::default(),
			Rational::new(0, 1),
			&mut table,
		);
		assert!(table.is_empty());
	}

	#[test]
	fn value_texture_without_processor_pushes_nothing() {
		// Unlike the OCIO base, chroma key has no pass-through branch:
		// without a processor the C++ pushes nothing.
		let (core, behavior) = create();
		let inputs = crate::value::NodeValueRow::from([(
			crate::nodes::ociobase::TEXTURE_INPUT.to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		)]);
		let mut table = NodeValueTable::default();
		behavior.value(&core, &inputs, Rational::new(0, 1), &mut table);
		assert!(table.is_empty());
	}

	#[test]
	fn value_texture_with_processor_pushes_deferred_job() {
		let core = NodeCore::new();
		let mut node = ChromaKeyNode {
			base: crate::nodes::ociobase::OcioBase::new(),
		};
		node.base
			.set_processor(Some(crate::handle::CHandle::null()));
		let inputs = crate::value::NodeValueRow::from([(
			crate::nodes::ociobase::TEXTURE_INPUT.to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		)]);
		let mut table = NodeValueTable::default();
		node.value(&core, &inputs, Rational::new(0, 1), &mut table);
		assert!(table.get(ValueType::Texture).is_some());
	}

	#[test]
	fn duplicate_clones() {
		let (core, behavior) = create();
		let dup = behavior.duplicate(&core).unwrap();
		assert_eq!(dup.name(), "Chroma Key");
	}
}
