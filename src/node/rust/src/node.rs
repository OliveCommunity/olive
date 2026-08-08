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

//! Node core data and the behavior trait — the C++ `Node` class,
//! restructured for Rust. COVERAGE.md maps every C++ method to its
//! Rust home; this file carries the virtual surface ([`NodeBehavior`])
//! and the shared data ([`NodeCore`]).

use oakcore_rs::{Rational, TimeRange};

use crate::id::NodeId;
use crate::input::{Input, ValueHint};
use crate::keyframe::KeyframeTrack;
use crate::value::{NodeValue, NodeValueRow, NodeValueTable};

/// Node category (mirrors C++ `Node::CategoryID` order).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Category {
	/// Output nodes.
	Output,
	/// Effects.
	Effect,
	/// Generators.
	Generator,
	/// Inputs (footage).
	Input,
	/// Math/combine.
	Math,
	/// Color.
	Color,
	/// Distort.
	Distort,
	/// Filter.
	Filter,
	/// Keying.
	Keying,
	/// OpenFX plugins.
	OpenFx,
	/// Timeline structural (tracks/blocks).
	Timeline,
	/// Groups.
	Group,
}

/// A gizmo: viewport-interaction data object (C++ `NodeGizmo`). Data
/// only — drawing and mouse handling live in facade/app.
pub struct Gizmo {
	/// Keyframed position inputs (track references).
	pub position_inputs: Vec<(String, i32, i32)>,
	/// Current drag position.
	pub drag_point: (f64, f64),
}

/// Shared per-node data (the C++ `Node` member fields). Behavior lives
/// in [`NodeBehavior`].
pub struct NodeCore {
	/// Inputs by id (array inputs hold multiple elements).
	pub inputs: Vec<Input>,
	/// Keyframe tracks per (input, element).
	pub keyframes: Vec<(String, i32, KeyframeTrack)>,
	/// Caches as oakrender handles (created via bridge::render).
	pub caches: NodeCaches,
	/// Node flags bitmask (hidden, dont-show-in-param-view, ...).
	pub flags: u64,
	/// Editor position (serialization only).
	pub position: (f64, f64),
	/// User label (C++ label_).
	pub label: String,
	/// Color override index (-1 = none).
	pub override_color: i32,
	/// Effect input id (C++ effect_input_).
	pub effect_input: String,
	/// Value hints per (input, element).
	pub hints: Vec<((String, i32), ValueHint)>,
	/// Group-context positions (C++ context_positions_).
	pub context_positions: Vec<(NodeId, (f64, f64), bool)>,
	/// Linked nodes (C++ links_).
	pub links: Vec<NodeId>,
	/// Bin folder membership (None = not in the bin).
	pub bin_folder: Option<NodeId>,
	/// Caches master toggle (C++ caches_enabled_).
	pub caches_enabled: bool,
	/// Gizmos owned by this node.
	pub gizmos: Vec<Gizmo>,
	/// Currently dragged gizmo index.
	pub current_gizmo: Option<usize>,
}

/// The node's oakrender caches (frame/thumbnail/audio/waveform),
/// owned handles released with the node.
pub struct NodeCaches {
	/// Video frame hash cache.
	pub video: crate::bridge::render::CacheHandle,
	/// Thumbnail cache.
	pub thumbnail: crate::bridge::render::CacheHandle,
	/// Audio playback cache.
	pub audio: crate::bridge::render::CacheHandle,
	/// Waveform cache.
	pub waveform: crate::bridge::render::CacheHandle,
}

/// The polymorphic surface of a node — every C++ virtual on `Node`
/// becomes a method here (see COVERAGE.md §1/§7/§8/§10).
pub trait NodeBehavior: Send {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str;

	/// Short menu name (C++ `short_name()`; defaults to [`Self::name`]).
	fn short_name(&self) -> &str {
		self.name()
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str;

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[]
	}

	/// Sub-category (C++ `sub_category()`).
	fn sub_category(&self) -> &str {
		""
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		""
	}

	/// Localized input name (C++ `get_input_name()` virtual).
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		id
	}

	/// Inputs excluded from rendering (C++ `ignore_inputs_for_rendering()`).
	fn ignore_inputs_for_rendering(&self) -> &[String] {
		&[]
	}

	/// Array elements active at `time` (C++ `get_active_elements_at_time()`).
	fn active_elements_at_time(&self, input: &str, time: Rational) -> Vec<i32> {
		let _ = (input, time);
		Vec::new()
	}

	/// Cache ranges (C++ `get_video_cache_range()` /
	/// `get_audio_cache_range()`).
	fn video_cache_range(&self, core: &NodeCore) -> TimeRange {
		let _ = core;
		TimeRange::default()
	}

	/// Audio cache range.
	fn audio_cache_range(&self, core: &NodeCore) -> TimeRange {
		let _ = core;
		TimeRange::default()
	}

	/// Value hint for an input (C++ `get_value_hint_for_input()` virtual).
	fn value_hint_for_input(&self, input: &str) -> Option<ValueHint> {
		let _ = input;
		None
	}

	/// Render-time connection resolution (C++
	/// `get_connected_render_output()`; Group overrides).
	fn connected_render_output(&self, core: &NodeCore, input: &str, element: i32) -> Option<NodeId> {
		let _ = (core, input, element);
		None
	}

	/// Time adjustment through this node (C++
	/// `input_time_adjustment()`/`output_time_adjustment()`; clips
	/// override for speed/reverse).
	fn input_time_adjustment(&self, input: &str, element: i32, time: TimeRange, traverse: bool) -> TimeRange {
		let _ = (input, element, traverse);
		time
	}

	/// Output-side time adjustment.
	fn output_time_adjustment(&self, input: &str, element: i32, time: TimeRange, traverse: bool) -> TimeRange {
		let _ = (input, element, traverse);
		time
	}

	/// Evaluate outputs (C++ `value()`).
	fn value(&self, core: &NodeCore, inputs: &NodeValueRow, time: Rational, table: &mut NodeValueTable) {
		let _ = (core, inputs, time, table);
	}

	/// Process a span of samples (C++ `process_samples()`).
	fn process_samples(
		&self,
		core: &NodeCore,
		inputs: &NodeValueRow,
		range: TimeRange,
		output: &mut crate::value::SampleBuffer,
	) {
		let _ = (core, inputs, range, output);
	}

	/// Direct frame generation (C++ `generate_frame()`; CPU-render
	/// nodes).
	fn generate_frame(&self, core: &NodeCore, frame: &mut crate::bridge::render::TextureHandle, time: Rational) {
		let _ = (core, frame, time);
	}

	/// Shader code request (C++ `get_shader_code()`; GPU nodes).
	fn shader_code(&self, request: &str) -> Option<String> {
		let _ = request;
		None
	}

	/// Gizmo transform/positions (C++ `gizmo_transformation()` /
	/// `update_gizmo_positions()`).
	fn gizmo_update(&self, core: &NodeCore, row: &NodeValueRow) {
		let _ = (core, row);
	}

	/// Gizmo drag callbacks (C++ `gizmo_drag_start/move`).
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		let _ = (core, start, x, y, modifiers);
	}

	/// Input value changed (C++ `InputValueChangedEvent`).
	fn input_value_changed(&mut self, core: &mut NodeCore, input: &str, element: i32) {
		let _ = (core, input, element);
	}

	/// Edge connected to an input (C++ `InputConnectedEvent`).
	fn input_connected(&mut self, core: &mut NodeCore, input: &str, element: i32, source: NodeId) {
		let _ = (core, input, element, source);
	}

	/// Edge disconnected from an input (C++ `InputDisconnectedEvent`).
	fn input_disconnected(&mut self, core: &mut NodeCore, input: &str, element: i32, source: NodeId) {
		let _ = (core, input, element, source);
	}

	/// Someone connected to this node's output (C++
	/// `OutputConnectedEvent`).
	fn output_connected(&mut self, core: &mut NodeCore, target: NodeId, input: &str, element: i32) {
		let _ = (core, target, input, element);
	}

	/// Output disconnected (C++ `OutputDisconnectedEvent`).
	fn output_disconnected(&mut self, core: &mut NodeCore, target: NodeId, input: &str, element: i32) {
		let _ = (core, target, input, element);
	}

	/// Attached to a preview/viewer (C++ `ConnectedToPreviewEvent`).
	fn connected_to_preview(&mut self, core: &mut NodeCore) {
		let _ = core;
	}

	/// Inserted into / removed from a project graph (C++
	/// `AddedToGraphEvent` / `RemovedFromGraphEvent`).
	fn added_to_graph(&mut self, core: &mut NodeCore) {
		let _ = core;
	}

	/// See [`NodeBehavior::added_to_graph`].
	fn removed_from_graph(&mut self, core: &mut NodeCore) {
		let _ = core;
	}

	/// Node links changed (C++ `LinkChangeEvent`).
	fn link_changed(&mut self, core: &mut NodeCore) {
		let _ = core;
	}

	/// Deep copy (C++ `copy()`); None = not copiable.
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>>;

	/// Custom load/save (C++ `load_custom()`/`save_custom()`).
	fn load_custom(&mut self, core: &mut NodeCore, reader: &mut dyn crate::serializer::XmlRead) -> bool {
		let _ = (core, reader);
		true
	}

	/// See [`NodeBehavior::load_custom`].
	fn save_custom(&self, core: &NodeCore, writer: &mut dyn crate::serializer::XmlWrite) {
		let _ = (core, writer);
	}

	/// Post-load fixups (C++ `PostLoadEvent` / `LoadFinishedEvent`).
	fn post_load(&mut self, core: &mut NodeCore) {
		let _ = core;
	}

	/// Legacy input id mapping for old project versions (C++
	/// `get_input_id_for_legacy_id()`; default identity).
	fn map_legacy_input_id<'a>(&self, id: &'a str) -> &'a str {
		id
	}
}
