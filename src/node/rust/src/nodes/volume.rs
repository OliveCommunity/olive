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

//! Volume audio effect (C++ `src/node/src/audio/volume/volume.{h,cpp}`,
//! `olive::VolumeNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};

/// Samples input id (C++ `k_samples_input`). Type: samples; flags:
/// not-keyframable; this is the node's effect input
/// (`set_effect_input(k_samples_input)`).
pub const SAMPLES_INPUT: &str = "samples_in";

/// Volume input id (C++ `k_volume_input`). Type: float; default `1.0`;
/// properties: `min = 0.0`, `view = decibel`.
pub const VOLUME_INPUT: &str = "volume_in";

/// Volume effect node. Multiplies every channel of a sample buffer by a
/// gain factor.
///
/// The C++ class derives from `MathNodeBase` (for the shared
/// `process_samples_internal` helper used with `k_op_multiply`) but
/// declares no own data members, so this is a unit-like struct; the
/// multiply-by-scalar sample math will be shared with
/// `super::mathbase` when the base module lands.
pub struct VolumeNode;

impl NodeBehavior for VolumeNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Volume"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.volume"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Filter]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Adjusts the volume of an audio source."
	}

	/// Localized input names (C++ `retranslate()`): `samples_in` ->
	/// "Samples", `volume_in` -> "Volume".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): unallocated samples input ->
	/// push nothing; allocated samples with a static volume input ->
	/// apply the gain immediately and push the transformed samples,
	/// skipping the transform when the gain is effectively 1.0 (C++
	/// preserves the `!qFuzzyCompare(volume, 1.0)` double semantics:
	/// `abs(volume - 1.0) * 1e12 > min(abs(volume), 1.0)`); allocated
	/// samples with a non-static volume input -> build a `SampleJob`
	/// over `samples_in` + `volume_in` and push it as a samples value.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Process a span of samples (C++ `process_samples()`): delegates to
	/// `MathNodeBase::process_samples_internal` with `k_op_multiply`,
	/// i.e. each output sample is the input sample multiplied by the
	/// `volume_in` value. The C++ signature receives the input buffer
	/// and a sample index; the Rust trait instead hands over a time
	/// `range` and the destination buffer.
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

/// Constructor (C++ `VolumeNode::VolumeNode()`): adds `samples_in`
/// (samples, not-keyframable) and `volume_in` (float, default 1.0,
/// min/view properties documented on the constant), sets the
/// audio-effect flag and makes `samples_in` the effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ `k_audio_volume` in
/// `factory.cpp::create_from_factory_index`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.volume",
		name: "Volume",
		categories: &[Category::Filter],
		create,
	});
}
