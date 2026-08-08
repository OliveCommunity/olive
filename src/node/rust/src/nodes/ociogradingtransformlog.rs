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

//! OCIO log color grading node (C++
//! `src/node/src/color/ociogradingtransformlog/ociogradingtransformlog.{h,cpp}`,
//! `olive::OCIOGradingTransformLogNode`).
//!
//! Lift/gamma/gain grading built on `ocio::GRADING_LOG`; mirrors the
//! linear grading node for the log style. OCIO's log-style GPU uniforms
//! map to the classic wheels as: brightness = lift, contrast = gain,
//! gamma = gamma.
//!
//! Note: OpenColorIO itself is never linked here; it is reached through
//! the color manager (`crate::colormanager`) and the oakrender bridge
//! (`crate::bridge::render`), like the C++ node's
//! `oakrender_color_processor_create_grading_primary` call.

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

use super::ociobase::OcioBase;

/// Lift input id (C++ `k_lift_input`). Type: vec4 (x = master); default
/// `{0, 0, 0, 0}`; properties: `base = 0.01`, component colors `color0
/// = #c0c0c0`, `color1 = #ff0000`, `color2 = #00ff00`, `color3 =
/// #0000ff`. NOTE: the grading input ids double as the OCIO GPU uniform
/// names for the dynamic GradingPrimaryTransform; do not rename them.
/// The C++ literals carry the literal text `OCIO_NAMESPACE` because the
/// sources were ported without expanding OCIO's namespace macro.
pub const LIFT_INPUT: &str = "OCIO_NAMESPACE_grading_primary_brightness";

/// Gain input id (C++ `k_gain_input`; OCIO's log-style "contrast"
/// uniform). Type: vec4 (x = master); default `{1, 1, 1, 1}`;
/// properties: `base = 0.01` and the component colors documented on
/// [`LIFT_INPUT`]. Do not rename (GPU uniform name).
pub const GAIN_INPUT: &str = "OCIO_NAMESPACE_grading_primary_contrast";

/// Gamma input id (C++ `k_gamma_input`). Type: vec4 (x = master);
/// default `{1, 1, 1, 1}`; properties: `base = 0.01` and the component
/// colors documented on [`LIFT_INPUT`]. Do not rename (GPU uniform
/// name).
pub const GAMMA_INPUT: &str = "OCIO_NAMESPACE_grading_primary_gamma";

/// Saturation input id (C++ `k_saturation_input`). Type: float; default
/// `1.0`; properties: `view = percentage`, `min = 0.0`. Do not rename
/// (GPU uniform name).
pub const SATURATION_INPUT: &str = "OCIO_NAMESPACE_grading_primary_saturation";

/// Pivot input id (C++ `k_pivot_input`). Type: float; default `-0.2`
/// (default for `GRADING_LOG` listed in `ocio::GradingPrimary`);
/// properties: `base = 0.01`. Do not rename (GPU uniform name).
pub const PIVOT_INPUT: &str = "OCIO_NAMESPACE_grading_primary_pivot";

/// Black-clamp enable input id (C++ `k_clamp_black_enable_input`).
/// Type: boolean; default `false`.
pub const CLAMP_BLACK_ENABLE_INPUT: &str = "clamp_black_enable_in";

/// Black clamp input id (C++ `k_clamp_black_input`). Type: float;
/// default `0.0`; properties: `enabled` = current value of
/// [`CLAMP_BLACK_ENABLE_INPUT`], `base = 0.01`. Do not rename (GPU
/// uniform name).
pub const CLAMP_BLACK_INPUT: &str = "OCIO_NAMESPACE_grading_primary_clampBlack";

/// White-clamp enable input id (C++ `k_clamp_white_enable_input`).
/// Type: boolean; default `false`.
pub const CLAMP_WHITE_ENABLE_INPUT: &str = "clamp_white_enable_in";

/// White clamp input id (C++ `k_clamp_white_input`). Type: float;
/// default `1.0`; properties: `enabled` = current value of
/// [`CLAMP_WHITE_ENABLE_INPUT`], `base = 0.01`, and `min` = black clamp
/// + 0.000001 while the black clamp is static. Do not rename (GPU
/// uniform name).
pub const CLAMP_WHITE_INPUT: &str = "OCIO_NAMESPACE_grading_primary_clampWhite";

/// OCIO log grading node. Lift/gamma/gain color grading using
/// OpenColorIO (`ocio::GRADING_LOG`). Owns no members beyond the
/// embedded OCIO base state (C++ has no own private members).
pub struct OCIOGradingTransformLogNode {
	/// Shared OCIO base state (C++ base class `OCIOBaseNode`).
	base: OcioBase,
}

impl OCIOGradingTransformLogNode {
	/// Set the per-component widget colors of a vec4 input (C++
	/// `set_vec4_input_colors()`): master `#c0c0c0`, R `#ff0000`, G
	/// `#00ff00`, B `#0000ff`.
	fn set_vec4_input_colors(core: &mut NodeCore, input: &str) {
		todo!()
	}

	/// Constrain the white clamp UI minimum to just above the black
	/// clamp (C++ `update_clamp_white_minimum()`), as required by
	/// `ocio::GradingPrimary::validate`. No-op while the black clamp is
	/// keyframed or connected — a static UI minimum cannot follow an
	/// animated value, so the invariant is enforced per frame in
	/// `value()` instead.
	fn update_clamp_white_minimum(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// (Re)build the color processor (C++ `generate_processor()`):
	/// creates a grading-primary processor of style
	/// `OAKRENDER_GRADING_PRIMARY_LOG` through the color manager and
	/// stores it with [`OcioBase::set_processor`] when creation
	/// succeeds.
	fn generate_processor(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// OCIO config change hook (C++ `config_changed()` override):
	/// regenerates the processor.
	fn config_changed(&mut self, core: &mut NodeCore) {
		todo!()
	}
}

impl NodeBehavior for OCIOGradingTransformLogNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"OCIO Color Grading (Log)"
	}

	/// Stable type id (C++ `id()`). The literal `OCIO_NAMESPACE` text is
	/// in the C++ string (the namespace macro was never expanded), so the
	/// id is kept verbatim for project compatibility.
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.OCIO_NAMESPACEgradingtransformlog"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Color]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Lift/gamma/gain color grading using OpenColorIO."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` -> "Input",
	/// lift -> "Lift", gain -> "Gain", gamma -> "Gamma", saturation ->
	/// "Saturation", pivot -> "Pivot", clamp enables -> "Enable
	/// Black/White Clamp", clamps -> "Black Clamp"/"White Clamp".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Input value changed (C++ `InputValueChangedEvent`): toggling a
	/// clamp-enable input mirrors it into the clamp input's `enabled`
	/// property; a black-clamp change re-constrains the white clamp
	/// minimum; any change regenerates the processor.
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		todo!()
	}

	/// Edge connected (C++ `InputConnectedEvent`): forwards to the base
	/// class and, for the black clamp input, re-constrains the white
	/// clamp minimum.
	fn input_connected(&mut self, core: &mut NodeCore, input: &str, element: i32, source: crate::id::NodeId) {
		todo!()
	}

	/// Edge disconnected (C++ `InputDisconnectedEvent`): forwards to the
	/// base class and, for the black clamp input, re-constrains the
	/// white clamp minimum.
	fn input_disconnected(&mut self, core: &mut NodeCore, input: &str, element: i32, source: crate::id::NodeId) {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// processor not ready -> push nothing (no pass-through branch).
	/// Otherwise builds a `ColorTransformJob` from the whole input row
	/// and rewrites the vec4 (RGBM: x = master) inputs into the vec3
	/// form the GPU uniforms expect: lift RGB = channel + master
	/// (additive); gain and gamma RGB = channel * master
	/// (multiplicative). Disabled clamps are pushed as
	/// `GradingPrimary::NoClampBlack()/NoClampWhite()`, and when both
	/// clamps are enabled the white clamp is raised to black + 0.000001
	/// per frame if keyframed/connected values violate white > black.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Added to a graph (C++ base `AddedToGraphEvent`): captures the
	/// project's color manager and runs `config_changed()` via
	/// [`OcioBase::added_to_graph`].
	fn added_to_graph(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// Removed from a graph (C++ base `RemovedFromGraphEvent`): clears
	/// the color manager pointer via [`OcioBase::removed_from_graph`].
	fn removed_from_graph(&mut self, core: &mut NodeCore) {
		todo!()
	}

	/// Deep copy (C++ `copy()` via `NODE_DEFAULT_FUNCTIONS`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++
/// `OCIOGradingTransformLogNode::OCIOGradingTransformLogNode()`):
/// builds the base (`tex_in` texture input, effect input, video-effect
/// flag), adds the lift/gain/gamma/saturation/pivot and
/// clamp-enable/clamp inputs with the defaults, flags and properties
/// documented on the constants, and applies the initial white-clamp
/// minimum constraint.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.OCIO_NAMESPACEgradingtransformlog`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.OCIO_NAMESPACEgradingtransformlog",
		name: "OCIO Color Grading (Log)",
		categories: &[Category::Color],
		create,
	});
}
