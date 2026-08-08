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

//! Pan audio effect (C++ `src/node/src/audio/pan/pan.{h,cpp}`,
//! `olive::PanNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Samples input id (C++ `k_samples_input`). Type: samples; flags:
/// not-keyframable; this is the node's effect input
/// (`set_effect_input(k_samples_input)`).
pub const SAMPLES_INPUT: &str = "samples_in";

/// Panning input id (C++ `k_panning_input`). Type: float; default `0.0`;
/// properties: `min = -1.0`, `max = 1.0`, `view = percentage`.
pub const PANNING_INPUT: &str = "panning_in";

/// Pan effect node. Attenuates the left or right channel of a stereo
/// sample buffer according to a -1..1 panning value.
///
/// The C++ class keeps `NodeInput *samples_input_` /
/// `NodeInput *panning_input_` back-pointers to its own inputs; in Rust
/// inputs live in [`NodeCore`], so there are no own fields.
pub struct PanNode;

impl NodeBehavior for PanNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Pan"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.pan"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Filter]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Adjust the stereo panning of an audio source."
	}

	/// Localized input names (C++ `retranslate()`): `samples_in` ->
	/// "Samples", `panning_in` -> "Pan".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): unallocated samples input ->
	/// push nothing; non-stereo (channel count != 2) samples -> push the
	/// input through unchanged; stereo with a static panning input ->
	/// apply the attenuation immediately (pan > 0 scales channel 0 by
	/// `1.0 - pan`, pan < 0 scales channel 1 by `1.0 + pan`, pan == 0
	/// leaves the buffer untouched) and push the transformed samples;
	/// stereo with a non-static panning input -> build a `SampleJob`
	/// over `samples_in` + `panning_in` and push it as a samples value.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Process a span of samples (C++ `process_samples()`): copies every
	/// input channel to the output at the current index, then applies the
	/// pan — pan > 0 attenuates output channel 0 by `1.0 - pan`, pan < 0
	/// attenuates output channel 1 by `1.0 - abs(pan)`, pan == 0 is a
	/// straight copy. The C++ signature receives the input buffer and a
	/// sample index; the Rust trait instead hands over a time `range` and
	/// the destination buffer.
	fn process_samples(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		range: oakcore_rs::TimeRange,
		output: &mut crate::value::SampleBuffer,
	) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `PanNode::PanNode()`): adds `samples_in` (samples,
/// not-keyframable) and `panning_in` (float, default 0.0, min/max/view
/// properties documented on the constant), sets the audio-effect flag
/// and makes `samples_in` the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_audio_panning` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.pan",
		name: "Pan",
		categories: &[Category::Filter],
		create,
	});
}
