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

//! App-side multicam resolution: the bridge between the
//! [`oak_timeline::multicam`](oak_timeline::multicam) commands and the
//! UI's detection / menu needs.
//!
//! The C++ `MulticamWidget` detects a multicam by asking the viewer, then
//! walks the clip's texture chain for a `MultiCamNode` (`viewer.cpp`'s
//! `detect_multicam_node`). The Rust engine mirrors that here:
//!
//! * [`clip_connected_sequence`] is the C++ `ClipBlock::connected_viewer()`
//!   (a `ViewerOutput` = sequence feeding the clip's `buffer_in`), the
//!   timeline menu's enable condition;
//! * [`multicam_state_for_clip`] resolves a clip to its multicam node, the
//!   source sequence, the source count and the current source — the panel's
//!   grid state;
//! * [`clip_at_playhead_with_multicam`] is the third detection level (the
//!   clip under the playhead on the video tracks), used when nothing is
//!   selected.

use oak_core::Rational;
use oak_node::block::clip_input;
use oak_node::graph::Graph;
use oak_node::id::NodeId;
use oak_node::nodes::multicamnode::{SEQUENCE_INPUT, SEQUENCE_TYPE_INPUT};
use oak_node::sequence::SequenceBehavior;
use oak_node::track::TrackType;

use super::engine::MulticamState;
use super::graphops::{self, lock, ProjectRef};

/// Whether `id` names a sequence node (C++ `dynamic_cast<Sequence*>` /
/// the facade's `oakengine_node_is_sequence`).
pub fn is_sequence(g: &Graph, id: NodeId) -> bool {
	g.get(id)
		.and_then(|e| e.behavior.as_any())
		.and_then(|a| a.downcast_ref::<SequenceBehavior>())
		.is_some()
}

/// The node feeding `node.input[input][element]`, if any.
fn connected_output(g: &Graph, node: NodeId, input: &str, element: i32) -> Option<NodeId> {
	g.connected_output(node, input, element)
}

/// The C++ `find_input_node_internal` walk: check `node`'s input
/// connections for a match, recursing into each source. Collects the first
/// sequence found (stopping at `maximum` matches, `0` = unlimited).
fn find_sequence_internal(
	g: &Graph,
	node: NodeId,
	maximum: usize,
	list: &mut Vec<NodeId>,
) {
	for (from, _input, _element) in g.input_connections(node) {
		if is_sequence(g, from) {
			list.push(from);
			if maximum != 0 && list.len() == maximum {
				return;
			}
		}
		find_sequence_internal(g, from, maximum, list);
		if maximum != 0 && list.len() == maximum {
			return;
		}
	}
}

/// The C++ `find_input_nodes_connected_to_input<ViewerOutput>(input, 1)`:
/// the first sequence feeding the clip's texture input (`buffer_in`), depth
/// 1 and then along the dependency chain — the clip's "connected viewer".
/// This is the timeline Multi-Cam menu's enable condition (a clip whose
/// source is a sequence can host a multicam).
pub fn clip_connected_sequence(g: &Graph, clip: NodeId) -> Option<NodeId> {
	let source = connected_output(g, clip, clip_input::TEXTURE_INPUT, -1)?;
	if is_sequence(g, source) {
		return Some(source);
	}
	let mut list = Vec::new();
	find_sequence_internal(g, source, 1, &mut list);
	list.first().copied()
}

/// The sequence a multicam node pulls its angles from (the `sequence_in`
/// edge target).
pub fn multicam_sequence(p: &ProjectRef, mc: NodeId) -> Option<NodeId> {
	let g = lock(p);
	connected_output(&g.graph, mc, SEQUENCE_INPUT, -1)
}

/// The source sequence's track for angle `source` (the track whose clip
/// makes up that angle), or `None` when the source is out of range.
pub fn multicam_source_track(p: &ProjectRef, mc: NodeId, source: i32) -> Option<NodeId> {
	let g = lock(p);
	let seq = connected_output(&g.graph, mc, SEQUENCE_INPUT, -1)?;
	let kind = multicam_sequence_type(&g.graph, mc);
	graphops::track_ids(&g.graph, seq, kind).get(source as usize).copied()
}

/// The track-type selector of a multicam node (`sequence_type_in`):
/// [`TrackType::Video`] (0) or [`TrackType::Audio`] (1); defaults to video
/// when the node is stale.
pub fn multicam_sequence_type(g: &Graph, mc: NodeId) -> TrackType {
	g.get(mc)
		.map(|e| {
			let v = e.core.standard_value(SEQUENCE_TYPE_INPUT, -1).to_double() as i32;
			TrackType::from_c(v).unwrap_or(TrackType::Video)
		})
		.unwrap_or(TrackType::Video)
}

/// The number of angle sources of a multicam node: the connected source
/// sequence's track count of the node's `sequence_type_in` kind (the C++
/// `get_source_count()` resolves the same way when a sequence is set).
pub fn multicam_source_count(p: &ProjectRef, mc: NodeId) -> i32 {
	let g = lock(p);
	let Some(seq) = connected_output(&g.graph, mc, SEQUENCE_INPUT, -1) else {
		return 0;
	};
	let kind = multicam_sequence_type(&g.graph, mc);
	graphops::track_list_of(&g.graph, seq, kind)
		.and_then(|list| graphops::track_list_behavior(&g.graph, list))
		.map(|l| l.tracks.len() as i32)
		.unwrap_or(0)
}

/// The currently selected source of a multicam node (`current_in` as int),
/// `-1` when stale.
pub fn multicam_current_source(p: &ProjectRef, mc: NodeId) -> i32 {
	let g = lock(p);
	g.graph
		.get(mc)
		.map(|e| e.core.standard_value(oak_node::nodes::multicamnode::CURRENT_INPUT, -1).to_double() as i32)
		.unwrap_or(-1)
}

/// Resolve a clip to the full multicam state the panel displays: its
/// multicam node, the source sequence, the source count and the current
/// source. `None` when the clip has no multicam or the multicam has no
/// connected sequence.
pub fn multicam_state_for_clip(p: &ProjectRef, clip: NodeId) -> Option<MulticamState> {
	let clip_ref = oak_timeline::util::NodeRef::new(p.clone(), clip);
	let mc = oak_timeline::multicam::clip_find_multicam(&clip_ref)?;
	let sequence_id = multicam_sequence(p, mc.id)?;
	let source_count = multicam_source_count(p, mc.id);
	Some(MulticamState {
		sequence_id: sequence_id.identity(),
		node_id: mc.id.identity(),
		clip_id: clip.identity(),
		source_count,
		current_source: multicam_current_source(p, mc.id),
	})
}

/// The clip covering `time` on the sequence's video tracks whose texture
/// chain contains a multicam node — the C++ detection's third level (the
/// playhead's nearest clip). `None` when no such clip exists.
pub fn clip_at_playhead_with_multicam(p: &ProjectRef, seq: NodeId, time: Rational) -> Option<NodeId> {
	// Collect the candidates under the lock, then re-lock per clip via
	// `clip_find_multicam` (which takes the project lock itself) — holding
	// the guard across it would deadlock.
	let candidates: Vec<NodeId> = {
		let g = lock(p);
		let mut out = Vec::new();
		for track_id in graphops::track_ids(&g.graph, seq, TrackType::Video) {
			let Some(track) = graphops::track_behavior(&g.graph, track_id) else {
				continue;
			};
			for &block_id in &track.blocks {
				let Some(clip) = graphops::clip_behavior(&g.graph, block_id) else {
					continue;
				};
				if time < clip.core.in_() || time >= clip.core.out() {
					continue;
				}
				out.push(block_id);
			}
		}
		out
	};
	candidates.into_iter().find(|block_id| {
		let clip_ref = oak_timeline::util::NodeRef::new(p.clone(), *block_id);
		oak_timeline::multicam::clip_find_multicam(&clip_ref).is_some()
	})
}

#[cfg(test)]
mod tests {
	use super::*;
	use oak_node::block::ClipBlockBehavior;
	use oak_node::node::NodeCore;
	use oak_node::project::Project;
	use oak_node::sequence::SequenceBehavior;
	use oak_node::track::{TrackBehavior, TrackListBehavior};
	use std::sync::{Arc, Mutex};

	use oak_timeline::util::{
		block_clip_create, block_in, track_append_block, NodeRef,
	};

	/// Project fixture: a sequence owning one video track list with one
	/// video track.
	struct Fixture {
		project: Arc<Mutex<oak_node::project::Project>>,
		seq: NodeRef,
		track: NodeRef,
	}

	fn fixture() -> Fixture {
		let project = Project::new();
		let (seq_id, _list_id, track_id) = {
			let mut p = project.lock().unwrap();
			let (core, behavior) = SequenceBehavior::create();
			let seq_id = p.graph.add_node(core, behavior);
			let (core, behavior) = TrackListBehavior::create();
			let list_id = p.graph.add_node(core, behavior);
			let (core, behavior) = (NodeCore::new(), Box::new(TrackBehavior::new(TrackType::Video)));
			let track_id = p.graph.add_node(core, behavior);
			{
				let seq = p
					.graph
					.get_mut(seq_id)
					.unwrap()
					.behavior
					.as_any_mut()
					.unwrap()
					.downcast_mut::<SequenceBehavior>()
					.unwrap();
				seq.track_lists.push(list_id);
			}
			let list = p.graph.get_mut(list_id).unwrap();
			let l = list
				.behavior
				.as_any_mut()
				.unwrap()
				.downcast_mut::<TrackListBehavior>()
				.unwrap();
			l.sequence = Some(seq_id);
			l.tracks.push(track_id);
			let track = p.graph.get_mut(track_id).unwrap();
			let t = track
				.behavior
				.as_any_mut()
				.unwrap()
				.downcast_mut::<TrackBehavior>()
				.unwrap();
			t.kind = TrackType::Video;
			t.track_list = Some(list_id);
			(seq_id, list_id, track_id)
		};
		let _ = _list_id;
		Fixture {
			project: project.clone(),
			seq: NodeRef::new(project.clone(), seq_id),
			track: NodeRef::new(project, track_id),
		}
	}

	/// A clip spanning `[0, 50)` on the fixture's video track.
	fn add_clip(fx: &Fixture) -> NodeRef {
		let clip = block_clip_create(&fx.project);
		{
			let mut p = fx.project.lock().unwrap();
			let c = p
				.graph
				.get_mut(clip.id)
				.unwrap()
				.behavior
				.as_any_mut()
				.unwrap()
				.downcast_mut::<ClipBlockBehavior>()
				.unwrap();
			c.core.range = oak_core::TimeRange::new(Rational::new(0, 1), Rational::new(50, 1));
		}
		track_append_block(&fx.track, &clip);
		clip
	}

	/// A plain footage-fed clip has no connected sequence (the C++ viewer
	/// check); a sequence-fed clip resolves its source.
	#[test]
	fn connected_sequence_resolves_the_source() {
		let fx = fixture();
		let clip = add_clip(&fx);
		let g = lock(&fx.project);
		// No source at all: no connected sequence.
		assert!(clip_connected_sequence(&g.graph, clip.id).is_none());
		drop(g);

		// Feed the clip from a sequence (a nested-sequence clip).
		{
			let mut p = fx.project.lock().unwrap();
			p.graph
				.connect(fx.seq.id, clip.id, clip_input::TEXTURE_INPUT, -1)
				.unwrap();
		}
		let g = lock(&fx.project);
		assert_eq!(clip_connected_sequence(&g.graph, clip.id), Some(fx.seq.id));
	}

	/// `multicam_state_for_clip` resolves the multicam node, its source
	/// sequence and the source count; `None` without a multicam.
	#[test]
	fn multicam_state_resolves_node_sequence_and_count() {
		let fx = fixture();
		let clip = add_clip(&fx);
		{
			let mut p = fx.project.lock().unwrap();
			p.graph
				.connect(fx.seq.id, clip.id, clip_input::TEXTURE_INPUT, -1)
				.unwrap();
		}
		// No multicam yet.
		assert!(multicam_state_for_clip(&fx.project, clip.id).is_none());

		// Enable multicam through the real command, then resolve.
		let mut cmd = oak_timeline::multicam::MultiCamEnableCommand::new(
			vec![clip.clone()],
			fx.seq.clone(),
		);
		cmd.redo();
		let state = multicam_state_for_clip(&fx.project, clip.id).expect("multicam enabled");
		assert_eq!(state.sequence_id, fx.seq.id.identity());
		assert_eq!(state.source_count, 1, "one video track = one source");
		assert_eq!(state.current_source, 0);

		// The connected sequence still resolves through the multicam.
		let mc = oak_timeline::multicam::clip_find_multicam(&clip).unwrap();
		assert_eq!(multicam_sequence(&fx.project, mc.id), Some(fx.seq.id));
	}

	/// `clip_at_playhead_with_multicam` finds the clip under the playhead on
	/// the video tracks; a non-multicam clip under the playhead is skipped.
	#[test]
	fn playhead_clip_detection_prefers_multicam_clips() {
		let fx = fixture();
		let plain = add_clip(&fx);
		// A second track with a multicam-enabled clip.
		let (core, behavior) = (NodeCore::new(), Box::new(TrackBehavior::new(TrackType::Video)));
		let track2 = {
			let mut p = fx.project.lock().unwrap();
			let id = p.graph.add_node(core, behavior);
			let list = {
				let seq = p
					.graph
					.get(fx.seq.id)
					.unwrap()
					.behavior
					.as_any()
					.unwrap()
					.downcast_ref::<SequenceBehavior>()
					.unwrap();
				seq.track_lists[0]
			};
			let l = p
				.graph
				.get_mut(list)
				.unwrap()
				.behavior
				.as_any_mut()
				.unwrap()
				.downcast_mut::<TrackListBehavior>()
				.unwrap();
			l.tracks.push(id);
			let t = p
				.graph
				.get_mut(id)
				.unwrap()
				.behavior
				.as_any_mut()
				.unwrap()
				.downcast_mut::<TrackBehavior>()
				.unwrap();
			t.kind = TrackType::Video;
			t.track_list = Some(list);
			id
		};
		let track2 = NodeRef::new(fx.project.clone(), track2);
		let mc_clip = block_clip_create(&fx.project);
		{
			let mut p = fx.project.lock().unwrap();
			let c = p
				.graph
				.get_mut(mc_clip.id)
				.unwrap()
				.behavior
				.as_any_mut()
				.unwrap()
				.downcast_mut::<ClipBlockBehavior>()
				.unwrap();
			c.core.range = oak_core::TimeRange::new(Rational::new(0, 1), Rational::new(50, 1));
		}
		track_append_block(&track2, &mc_clip);
		{
			let mut p = fx.project.lock().unwrap();
			p.graph
				.connect(fx.seq.id, mc_clip.id, clip_input::TEXTURE_INPUT, -1)
				.unwrap();
		}
		let mut cmd = oak_timeline::multicam::MultiCamEnableCommand::new(
			vec![mc_clip.clone()],
			fx.seq.clone(),
		);
		cmd.redo();

		// At frame 25 the multicam clip (topmost video track) wins over the
		// plain clip below it.
		let found = clip_at_playhead_with_multicam(&fx.project, fx.seq.id, Rational::new(25, 1));
		assert_eq!(found, Some(mc_clip.id));
		let _ = plain;
		assert_eq!(block_in(&mc_clip), Rational::new(0, 1));
	}
}
