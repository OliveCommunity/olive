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
//! (opaque oakrender handles); no OFX types (`OFX::Host::ImageEffect::Instance`,
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
use crate::value::{NodeValue, NodeValueRow, NodeValueTable};
use oakcore_rs::{Rational, TimeRange};

/// Texture input id (C++ `plugin::k_texture_input`). Type: texture;
/// no default. Only synthesized when the plugin declares clip inputs
/// but none of them is the simple-source clip (see [`create`]); then
/// it becomes the node's effect input with display name "Texture".
pub const TEXTURE_INPUT: &str = "tex_in";

/// OFX simple-source clip name (C++ `kOfxImageEffectSimpleSourceClipName`):
/// the canonical first texture clip of a filter plugin.
pub const SOURCE_CLIP: &str = "Source";

/// Opaque handle to an OFX plugin instance owned by the `oakplugin`
/// crate C ABI bridge (C++ `OFX::Host::ImageEffect::Instance *`,
/// member `plugin_instance_`). Placeholder type for this draft — the
/// bridge's real handle type replaces it.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct PluginInstanceHandle(pub u64);

impl PluginInstanceHandle {
	/// The null/absent instance handle (no plugin instance behind the
	/// node).
	pub fn null() -> Self {
		PluginInstanceHandle(0)
	}

	/// True when no instance is behind this handle.
	pub fn is_null(&self) -> bool {
		self.0 == 0
	}
}

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
	///
	/// The Rust model has no plugin-job payload: the job case pushes a
	/// null texture handle marking a renderer-deferred plugin job
	/// (`// CPP-PARITY: plugin.cpp` `value()`).
	fn value(
		&self,
		core: &NodeCore,
		inputs: &NodeValueRow,
		time: Rational,
		table: &mut NodeValueTable,
	) {
		let _ = (core, time);

		// Re-push every non-texture, non-none input value, tagged with
		// its input id.
		for (id, v) in inputs.iter() {
			if matches!(v, NodeValue::Texture(_) | NodeValue::None) {
				continue;
			}
			table.push(v.value_type(), v.clone(), Some(id.clone()));
		}

		// Resolve the input texture: simple-source clip, then tex_in,
		// then the first texture-typed input.
		let tex = inputs
			.get(SOURCE_CLIP)
			.filter(|v| matches!(v, NodeValue::Texture(_)))
			.or_else(|| {
				inputs
					.get(TEXTURE_INPUT)
					.filter(|v| matches!(v, NodeValue::Texture(_)))
			})
			.or_else(|| inputs.values().find(|v| matches!(v, NodeValue::Texture(_))));

		if tex.is_some() && !self.instance.is_null() {
			// C++ `table->push(NodeValue::k_texture, tex->to_job(job),
			// this)` — a deferred plugin job; no job payload here.
			table.push(
				crate::value::ValueType::Texture,
				NodeValue::Texture(crate::handle::CHandle::null()),
				None,
			);
		}
	}

	/// Process audio samples (C++ `process_samples()`): passthrough —
	/// silences the output when the input is empty/unallocated,
	/// otherwise reallocates the output to the input's params when
	/// mismatched and copies every channel verbatim.
	///
	/// The C++ receives the input buffer from the audio traversal; the
	/// Rust trait hands only the value row, so the input buffer is
	/// located as the first samples value in the row.
	fn process_samples(
		&self,
		core: &NodeCore,
		inputs: &NodeValueRow,
		range: TimeRange,
		output: &mut crate::value::SampleBuffer,
	) {
		let _ = (core, range);

		let input = match inputs.values().find(|v| matches!(v, NodeValue::Samples(_))) {
			Some(NodeValue::Samples(b)) => b,
			_ => {
				// Empty/unallocated input: silence the output (C++
				// `output.silence()`).
				if output.is_allocated() {
					output.data.fill(0);
				}
				return;
			}
		};

		if !input.is_allocated() || input.channels == 0 || input.sample_count == 0 {
			if output.is_allocated() {
				output.data.fill(0);
			}
			return;
		}

		// Reallocate the output to the input's params when mismatched
		// (C++ `set_audio_params` + `set_sample_count` + `allocate`).
		if !output.is_allocated()
			|| output.channels != input.channels
			|| output.sample_count != input.sample_count
		{
			output.format = input.format;
			output.channels = input.channels;
			output.sample_count = input.sample_count;
			let bps = input.format.bytes_per_sample();
			output.data = vec![0u8; input.channels * input.sample_count * bps];
		}

		// Copy every channel verbatim (C++ `output.fast_set(input, c)`).
		for c in 0..input.channels {
			for i in 0..input.sample_count {
				let v = input.sample_value(c, i);
				output.set_sample_value(c, i, v);
			}
		}
	}

	/// Direct frame generation (C++ `generate_frame()`): allocates
	/// the destination frame if needed and zero-fills it (plugins do
	/// their real image work in the plugin job, not here).
	///
	/// The Rust frame is an opaque [`crate::handle::CHandle`]
	/// whose pixels cannot be read or written from this crate, so the
	/// body is a documented no-op (`// CPP-PARITY: plugin.cpp`
	/// `generate_frame`).
	fn generate_frame(
		&self,
		core: &NodeCore,
		frame: &mut crate::handle::CHandle,
		time: Rational,
	) {
		let _ = (core, frame, time);
	}

	/// Deep copy (C++ `copy()`): asks the bridge to create a fresh
	/// plugin instance — filter context when supported, else the
	/// plugin's first declared context — and wraps it in a new node;
	/// `None` when there is no instance or instance creation fails.
	///
	/// This crate's bridge has no instance-creation call, so a plugin
	/// node can never be duplicated here and `None` is always returned
	/// (`// CPP-PARITY: plugin.cpp` `copy()`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		None
	}

	/// Downcast to [`Self`] (instance/metadata access).
	fn as_any(&self) -> Option<&dyn std::any::Any> {
		Some(self)
	}

	/// Mutable downcast (see [`NodeBehavior::as_any`]).
	fn as_any_mut(&mut self) -> Option<&mut dyn std::any::Any> {
		Some(self)
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
///
/// The plugin-instance bridge is not wired in this crate, so the
/// descriptor walk cannot run here: a placeholder node with a null
/// instance and empty cached metadata is returned. Real construction
/// belongs to the oakplugin bridge's discovery pass, which registers
/// one `PluginNode` per discovered plugin (`// CPP-PARITY: plugin.cpp`
/// `PluginNode::PluginNode`).
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let core = NodeCore::new();
	let node = PluginNode {
		instance: PluginInstanceHandle::null(),
		sub_category: "General".to_string(),
		name: String::new(),
		type_id: String::new(),
		description: String::new(),
	};
	(core, Box::new(node))
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

#[cfg(test)]
mod tests {
	use super::*;
	use crate::node::NodeBehavior;
	use crate::value::{NodeValueTable, ValueType};
	use oakcore_rs::{Rational, TimeRange};

	/// A plugin node with a non-null instance and cached metadata.
	fn node() -> PluginNode {
		PluginNode {
			instance: PluginInstanceHandle(1),
			sub_category: "Filter".to_string(),
			name: "Test Plugin".to_string(),
			type_id: "org.example.testplugin".to_string(),
			description: "A test plugin".to_string(),
		}
	}

	#[test]
	fn metadata_served_from_cache() {
		let n = node();
		assert_eq!(n.name(), "Test Plugin");
		assert_eq!(n.type_id(), "org.example.testplugin");
		assert_eq!(n.description(), "A test plugin");
		assert_eq!(n.sub_category(), "Filter");
		assert_eq!(n.categories(), &[Category::OpenFx]);
	}

	#[test]
	fn instance_handle_null_semantics() {
		assert!(PluginInstanceHandle::null().is_null());
		assert!(!PluginInstanceHandle(1).is_null());
	}

	#[test]
	fn value_tags_non_texture_inputs() {
		let n = node();
		let core = NodeCore::new();
		let mut row = NodeValueRow::default();
		row.insert("opacity".to_string(), NodeValue::Float(0.5));
		row.insert("mode".to_string(), NodeValue::Combo(2));
		let mut table = NodeValueTable::default();
		n.value(&core, &row, Rational::new(0, 1), &mut table);
		// Both values pushed, tagged with their input ids.
		let tagged: Vec<(&str, &NodeValue)> = table
			.rows()
			.iter()
			.filter(|(_, _, t)| t.is_some())
			.map(|(_, v, t)| (t.as_deref().unwrap(), v))
			.collect();
		assert_eq!(tagged.len(), 2);
		assert!(tagged
			.iter()
			.any(|(id, v)| *id == "opacity" && *v == &NodeValue::Float(0.5)));
		assert!(tagged
			.iter()
			.any(|(id, v)| *id == "mode" && *v == &NodeValue::Combo(2)));
		// No texture pushed (no texture input in the row).
		assert!(table.get(ValueType::Texture).is_none());
	}

	#[test]
	fn value_skips_texture_and_none_inputs() {
		let n = node();
		let core = NodeCore::new();
		let mut row = NodeValueRow::default();
		row.insert(
			"tex_in".to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		);
		row.insert("none_in".to_string(), NodeValue::None);
		let mut table = NodeValueTable::default();
		n.value(&core, &row, Rational::new(0, 1), &mut table);
		// The texture is consumed as the job source; nothing else is pushed
		// (none values are skipped; the plugin job is the texture push).
		assert_eq!(table.count(), 1);
		assert!(matches!(
			table.get(ValueType::Texture),
			Some(NodeValue::Texture(h)) if h.is_null()
		));
	}

	#[test]
	fn value_resolves_source_clip_first() {
		let n = node();
		let core = NodeCore::new();
		let mut row = NodeValueRow::default();
		row.insert(
			SOURCE_CLIP.to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		);
		row.insert(
			TEXTURE_INPUT.to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		);
		let mut table = NodeValueTable::default();
		n.value(&core, &row, Rational::new(0, 1), &mut table);
		assert!(matches!(
			table.get(ValueType::Texture),
			Some(NodeValue::Texture(h)) if h.is_null()
		));
	}

	#[test]
	fn value_pushes_nothing_without_instance() {
		let n = PluginNode {
			instance: PluginInstanceHandle::null(),
			..node()
		};
		let core = NodeCore::new();
		let mut row = NodeValueRow::default();
		row.insert(
			TEXTURE_INPUT.to_string(),
			NodeValue::Texture(crate::handle::CHandle::null()),
		);
		let mut table = NodeValueTable::default();
		n.value(&core, &row, Rational::new(0, 1), &mut table);
		assert!(table.is_empty());
	}

	#[test]
	fn process_samples_copies_verbatim() {
		let n = node();
		let core = NodeCore::new();
		let input = crate::value::SampleBuffer {
			format: oakcore_rs::SampleFormat::F32Planar,
			channels: 2,
			sample_count: 3,
			// Planar layout: channel 0 plane [1, 2, 3], channel 1 plane
			// [4, 5, 6].
			data: vec![1.0f32, 2.0, 3.0, 4.0, 5.0, 6.0]
				.iter()
				.flat_map(|f| f.to_le_bytes())
				.collect(),
		};
		let mut row = NodeValueRow::default();
		row.insert("samples_in".to_string(), NodeValue::Samples(input));
		let mut output = crate::value::SampleBuffer::default();
		n.process_samples(
			&core,
			&row,
			TimeRange::new(Rational::new(0, 1), Rational::new(1, 1)),
			&mut output,
		);
		assert!(output.is_allocated());
		assert_eq!(output.channels, 2);
		assert_eq!(output.sample_count, 3);
		assert_eq!(output.format, oakcore_rs::SampleFormat::F32Planar);
		for c in 0..2 {
			for i in 0..3 {
				let expected = 1.0 + (c * 3 + i) as f64;
				assert_eq!(output.sample_value(c, i), expected);
			}
		}
	}

	#[test]
	fn process_samples_silences_output_on_missing_input() {
		let n = node();
		let core = NodeCore::new();
		let mut output = crate::value::SampleBuffer {
			format: oakcore_rs::SampleFormat::F32,
			channels: 1,
			sample_count: 2,
			data: vec![1.0f32, 2.0]
				.iter()
				.flat_map(|f| f.to_le_bytes())
				.collect(),
		};
		n.process_samples(
			&core,
			&NodeValueRow::default(),
			TimeRange::new(Rational::new(0, 1), Rational::new(1, 1)),
			&mut output,
		);
		assert_eq!(output.sample_value(0, 0), 0.0);
		assert_eq!(output.sample_value(0, 1), 0.0);
	}

	#[test]
	fn generate_frame_is_documented_noop() {
		let n = node();
		let core = NodeCore::new();
		let mut frame = crate::handle::CHandle::null();
		n.generate_frame(&core, &mut frame, Rational::new(0, 1));
		assert!(frame.is_null());
	}

	#[test]
	fn duplicate_returns_none() {
		let n = node();
		assert!(n.duplicate(&NodeCore::new()).is_none());
	}

	#[test]
	fn push_button_clicked_is_noop() {
		let mut n = node();
		let mut core = NodeCore::new();
		n.push_button_clicked(&mut core, "button".to_string());
	}

	#[test]
	fn create_returns_placeholder_node() {
		let (core, behavior) = create();
		let n = behavior
			.as_any()
			.unwrap()
			.downcast_ref::<PluginNode>()
			.unwrap();
		assert!(n.instance.is_null());
		assert_eq!(n.sub_category(), "General");
		assert_eq!(behavior.name(), "");
		assert_eq!(behavior.type_id(), "");
		// No inputs beyond the standard enabled input.
		assert_eq!(core.inputs.len(), 1);
	}
}
