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

//! OpenFX plugin node (C++ `src/node/src/plugins/plugin.{h,cpp}`,
//! `olive::plugin::PluginNode`).
//!
//! DECLARATION ONLY. This node is a thin wrapper over an OFX plugin
//! instance that lives behind the `oakplugin` crate's C ABI bridge
//! (`crate::bridge`); no OFX types (`OFX::Host::ImageEffect::Instance`,
//! `kOfxParam*`, ...) are declared here. The plugin instance is
//! represented as the opaque [`PluginInstanceHandle`] below — the real
//! definition belongs to the oakplugin bridge module and this draft
//! stands in for it.
//!
//! All per-plugin data (inputs, defaults, properties, labels) is
//! discovered at runtime from the plugin descriptor through the
//! bridge, mirroring the C++ constructor.

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};
use oakcore_rs::Rational;

/// Texture input id (C++ `plugin::k_texture_input`). Type: texture;
/// no default. Only synthesized when the plugin declares clip inputs
/// but none of them is the simple-source clip (see [`create`]); then
/// it becomes the node's effect input with display name "Texture".
pub const TEXTURE_INPUT: &str = "tex_in";

/// Opaque handle to an OFX plugin instance owned by the `oakplugin`
/// crate C ABI bridge (C++ `OFX::Host::ImageEffect::Instance *`,
/// member `plugin_instance_`). Placeholder type for this draft — the
/// bridge's real handle type replaces it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct PluginInstanceHandle(pub u64);

/// OFX plugin node. Wraps one plugin instance; inputs mirror the
/// plugin's declared params and clips.
///
/// The C++ class has a single own member besides the instance
/// pointer, `sub_category_`. Because `name()`/`id()`/`description()`
/// read the plugin descriptor at call time in C++ and the Rust trait
/// returns `&str`, this port caches those strings on the struct
/// (populated from the bridge at construction).
pub struct PluginNode {
	/// The wrapped plugin instance (C++ `plugin_instance_`; an
	/// opaque bridge handle here).
	instance: PluginInstanceHandle,
	/// Sub-category derived from the OFX context (C++
	/// `sub_category_`): "Filter", "Generator", "Transition", or
	/// "General".
	sub_category: String,
	/// Cached display name (C++ `name()` reads the descriptor's
	/// `kOfxPropLabel` on every call).
	name: String,
	/// Cached type id (C++ `id()` = the plugin identifier).
	type_id: String,
	/// Cached description (C++ `description()` reads the
	/// descriptor's `kOfxPropPluginDescription`).
	description: String,
}

impl PluginNode {
	/// Forward a push-button param activation to the plugin (C++
	/// `push_button_clicked()`; currently a no-op upstream).
	pub fn push_button_clicked(&mut self, core: &mut NodeCore, name: String) {
		let _ = (core, name);
		todo!()
	}
}

impl NodeBehavior for PluginNode {
	/// Human-readable name (C++ `name()` = the plugin descriptor's
	/// `kOfxPropLabel`; served from the cached copy).
	fn name(&self) -> &str {
		&self.name
	}

	/// Stable type id (C++ `id()` = the plugin identifier; served
	/// from the cached copy).
	fn type_id(&self) -> &str {
		&self.type_id
	}

	/// Categories (C++ `category()`; always OpenFx).
	fn categories(&self) -> &[Category] {
		&[Category::OpenFx]
	}

	/// Sub-category (C++ `sub_category()` = the context string
	/// captured at construction).
	fn sub_category(&self) -> &str {
		&self.sub_category
	}

	/// Description (C++ `description()` = the descriptor's
	/// `kOfxPropPluginDescription`; served from the cached copy).
	fn description(&self) -> &str {
		&self.description
	}

	/// Evaluate outputs (C++ `value()`): re-pushes every non-texture,
	/// non-none input value tagged with its input id (the tag routes
	/// the value to the matching OFX param); then resolves the input
	/// texture — simple-source clip first, then `tex_in`, then the
	/// first texture-typed input — and, when both texture and plugin
	/// instance exist, pushes the texture converted to a plugin job
	/// bound to this node and the request time.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		let _ = (core, inputs, time, table);
		todo!()
	}

	/// Process audio samples (C++ `process_samples()`): passthrough —
	/// silences the output when the input is empty/unallocated,
	/// otherwise reallocates the output to the input's params when
	/// mismatched and copies every channel verbatim.
	fn process_samples(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		range: oakcore_rs::TimeRange,
		output: &mut crate::value::SampleBuffer,
	) {
		let _ = (core, inputs, range, output);
		todo!()
	}

	/// Direct frame generation (C++ `generate_frame()`): allocates
	/// the destination frame if needed and zero-fills it (plugins do
	/// their real image work in the plugin job, not here).
	fn generate_frame(&self, core: &NodeCore, frame: &mut crate::bridge::render::TextureHandle, time: Rational) {
		let _ = (core, frame, time);
		todo!()
	}

	/// Deep copy (C++ `copy()`): asks the bridge to create a fresh
	/// plugin instance — filter context when supported, else the
	/// plugin's first declared context — and wraps it in a new node;
	/// `None` when there is no instance or instance creation fails.
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		let _ = core;
		todo!()
	}
}

/// Constructor (C++ `PluginNode::PluginNode(instance)`): stores the
/// instance handle and derives the sub-category from the OFX context
/// (filter/generator/transition, else "General"). Then walks the
/// plugin's params through the bridge:
///
/// - group params seed a group-label map; page params seed a
///   page-label map and a param->page-label map (skipping
///   skip-row/skip-column sentinels);
/// - each value param becomes an input: int/choice -> int/combo,
///   double -> float, boolean -> boolean, string -> text,
///   RGB/RGBA -> color, 2D/3D double/int -> vec2/vec3,
///   str-choice -> str-combo, bytes/custom -> binary, push-button ->
///   push-button; group/page and unknown types are skipped;
/// - defaults come from a per-plugin-id cache built from the OFX
///   `kOfxParamPropDefault` properties (normalised-coordinate doubles
///   are converted to canonical pixels against the project extent);
///   a non-null default is added as the input default and set as the
///   standard value (except for push-buttons);
/// - secret params get the hidden flag; the param label (or name)
///   becomes the input display name; parent groups set the `ui_group`
///   property, pages the `ui_page` property;
/// - color inputs get a `color_semantic` property ("color"/"scalar",
///   deduced from label/hint/display-range/default/group heuristics),
///   `min`/`max` from the display range, and a `tooltip` from the
///   hint;
/// - combo inputs get combo-box strings from the choice options
///   (ordered by the choice-order property when present), and
///   str-combos additionally a `combo_value_str` property.
///
/// Finally every non-output clip becomes a texture input named from
/// its label ("Source"/"From"/"To" for the well-known clips), and the
/// effect input is set: the simple-source clip if present, else
/// `tex_in` if present, else a synthesized `tex_in` texture input
/// (display name "Texture") when the plugin has any clip input at
/// all.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type. NOTE: unlike built-in nodes, plugin nodes
/// have no static type id — the C++ factory appends one `PluginNode`
/// per discovered OFX plugin at runtime
/// (`factory.cpp::add_plugins_to_library`), keyed by plugin
/// identifier, so there is no fixed `NodeMeta` literal to push. This
/// function is a no-op placeholder; real registration belongs to the
/// oakplugin bridge's discovery pass.
pub fn register(meta: &mut Vec<NodeMeta>) {
	let _ = meta;
}
