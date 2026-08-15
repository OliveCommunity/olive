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

//! Contract tests for the single-lib domain commands: the undo commands
//! drive a real oaknode project graph through `oaktimeline::util::NodeRef`
//! (the CHandle-based bridge signatures left with the deleted C ABI).
//!
//! Each test builds a project with a sequence/track-list/track, applies a
//! command through `redo`, checks the graph state, and `undo`s back to the
//! original state.

use std::sync::{Arc, Mutex};

use oakcore_rs::{Rational, TimeRange};
use oaknode::block::{ClipBlockBehavior, GapBlockBehavior};
use oaknode::project::Project;
use oaknode::sequence::SequenceBehavior;
use oaknode::track::{TrackBehavior, TrackListBehavior, TrackType};
use oaktimeline::undocommon::Command;
use oaktimeline::undogeneral::{
	BlockResizeCommand, TimelineAddTrackCommand, TimelineRemoveTrackCommand,
	TrackReplaceBlockWithGapCommand,
};
use oaktimeline::undopointer::TrackMoveBlockCommand;
use oaktimeline::undoripple::TrackRippleRemoveAreaCommand;
use oaktimeline::undosplit::BlockSplitCommand;
use oaktimeline::util::{
	block_clip_create, block_gap_create, block_in, block_length, block_out, block_track,
	track_append_block, track_block_at, track_block_count, track_length, track_ripple_remove_block,
	tracklist_track_count, NodeRef,
};

/// A project with one sequence owning one (video) track list.
fn make_project() -> Arc<Mutex<Project>> {
	let project = Project::new();
	let (seq_id, list_id) = {
		let mut p = project.lock().unwrap();
		let (core, behavior) = SequenceBehavior::create();
		let seq_id = p.graph.add_node(core, behavior);
		let (core, behavior) = TrackListBehavior::create();
		let list_id = p.graph.add_node(core, behavior);
		if let Some(e) = p.graph.get_mut(seq_id) {
			if let Some(a) = e.behavior.as_any_mut() {
				if let Some(s) = a.downcast_mut::<SequenceBehavior>() {
					s.track_lists.push(list_id);
				}
			}
		}
		if let Some(e) = p.graph.get_mut(list_id) {
			if let Some(a) = e.behavior.as_any_mut() {
				if let Some(l) = a.downcast_mut::<TrackListBehavior>() {
					l.sequence = Some(seq_id);
				}
			}
		}
		(seq_id, list_id)
	};
	let _ = (seq_id, list_id);
	project
}

/// The sequence and its video track list of `project`.
fn sequence_and_list(
	project: &Arc<Mutex<Project>>,
) -> (NodeRef, NodeRef) {
	let p = project.lock().unwrap();
	let mut seq = None;
	let mut list = None;
	for id in p.graph.node_ids() {
		if let Some(e) = p.graph.get(id) {
			if e.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<SequenceBehavior>())
				.is_some()
			{
				seq = Some(NodeRef::new(project.clone(), id));
			} else if e
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<TrackListBehavior>())
				.is_some()
			{
				list = Some(NodeRef::new(project.clone(), id));
			}
		}
	}
	(seq.unwrap(), list.unwrap())
}

/// Add a clip block spanning `[in, out)` to `track`.
fn add_clip(track: &NodeRef, in_: Rational, out: Rational) -> NodeRef {
	let clip = block_clip_create(&track.project);
	{
		let mut p = track.project.lock().unwrap();
		if let Some(e) = p.graph.get_mut(clip.id) {
			if let Some(a) = e.behavior.as_any_mut() {
				if let Some(c) = a.downcast_mut::<ClipBlockBehavior>() {
					c.core.range = TimeRange::new(in_, out);
				}
			}
		}
	}
	track_append_block(track, &clip);
	clip
}

/// Add a gap block spanning `[in, out)` to `track`.
fn add_gap(track: &NodeRef, in_: Rational, out: Rational) -> NodeRef {
	let gap = block_gap_create(&track.project);
	{
		let mut p = track.project.lock().unwrap();
		if let Some(e) = p.graph.get_mut(gap.id) {
			if let Some(a) = e.behavior.as_any_mut() {
				if let Some(g) = a.downcast_mut::<GapBlockBehavior>() {
					g.core.range = TimeRange::new(in_, out);
				}
			}
		}
	}
	track_append_block(track, &gap);
	gap
}

/// The block span of `block` (`None` for a stale node).
fn span_of(block: &NodeRef) -> Option<(Rational, Rational)> {
	let p = block.project.lock().unwrap();
	p.graph.get(block.id).and_then(|e| e.behavior.as_any()).and_then(|a| {
		[
			a.downcast_ref::<ClipBlockBehavior>().map(|b| &b.core),
			a.downcast_ref::<GapBlockBehavior>().map(|b| &b.core),
		]
		.into_iter()
		.flatten()
		.next()
		.map(|core| (core.in_(), core.out()))
	})
}

/// `TimelineAddTrackCommand` redo appends a track to the list; undo removes
/// it again, and a second redo restores the same track node.
#[test]
fn add_track_command_redo_undo_cycle() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);

	assert_eq!(tracklist_track_count(&list), 0);
	let track = TimelineAddTrackCommand::run_immediately(list.clone());
	assert_eq!(tracklist_track_count(&list), 1);
	assert_eq!(track_length(&track), Rational::new(0, 1));

	let mut cmd = TimelineRemoveTrackCommand::new(track.clone());
	cmd.redo();
	assert_eq!(tracklist_track_count(&list), 0);
	cmd.undo();
	assert_eq!(tracklist_track_count(&list), 1);
	cmd.redo();
	assert_eq!(tracklist_track_count(&list), 0);
}

/// `TimelineAddTrackCommand` creates the track with the list's media type.
#[test]
fn add_track_command_track_type() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);

	let track = TimelineAddTrackCommand::run_immediately(list);
	{
		let p = project.lock().unwrap();
		let entry = p.graph.get(track.id).unwrap();
		let behavior = entry
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<TrackBehavior>())
			.unwrap();
		assert_eq!(behavior.kind, TrackType::Video);
	}
}

/// `BlockSplitCommand` splits a clip at a point: the original becomes the
/// first half `[in, point)` and the clone the second half `[point, out)`;
/// undo restores the single original block.
#[test]
fn split_command_redo_undo() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list.clone());
	let clip = add_clip(&track, Rational::new(0, 1), Rational::new(100, 1));

	let mut split = BlockSplitCommand::new(clip.clone(), Rational::new(40, 1));
	split.prepare();
	split.redo();

	assert_eq!(block_length(&clip), Rational::new(40, 1));
	assert_eq!(block_in(&clip), Rational::new(0, 1));
	let second = split.new_block().unwrap();
	assert_eq!(block_in(&second), Rational::new(40, 1));
	assert_eq!(block_out(&second), Rational::new(100, 1));
	assert_eq!(track_block_count(&track), 2);

	split.undo();
	assert_eq!(block_length(&clip), Rational::new(100, 1));
	assert_eq!(block_out(&clip), Rational::new(100, 1));
	assert_eq!(track_block_count(&track), 1);

	// The undo detached the second half from the graph; the next redo
	// re-attaches it with the same identity.
	split.redo();
	assert_eq!(block_length(&clip), Rational::new(40, 1));
	assert_eq!(split.new_block().unwrap().id, second.id);
	assert_eq!(track_block_count(&track), 2);
}

/// `TrackReplaceBlockWithGapCommand` merges a clip into a following gap on
/// redo and restores both blocks on undo.
#[test]
fn replace_block_with_gap_round_trip() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list);
	let clip = add_clip(&track, Rational::new(0, 1), Rational::new(50, 1));
	let gap = add_gap(&track, Rational::new(50, 1), Rational::new(100, 1));

	let mut cmd = TrackReplaceBlockWithGapCommand::new(track.clone(), clip.clone(), false);
	cmd.redo();
	// The clip is gone; the gap absorbed its length (in-anchored at 50).
	assert_eq!(track_block_count(&track), 1);
	let remaining = track_block_at(&track, 0).unwrap();
	assert_eq!(remaining.id, gap.id);
	assert_eq!(block_in(&remaining), Rational::new(50, 1));
	assert_eq!(block_length(&remaining), Rational::new(100, 1));
	assert!(block_track(&clip).is_none());

	cmd.undo();
	assert_eq!(track_block_count(&track), 2);
	let first = track_block_at(&track, 0).unwrap();
	assert_eq!(first.id, clip.id);
	assert_eq!(block_in(&first), Rational::new(0, 1));
	assert_eq!(block_length(&first), Rational::new(50, 1));
	let second = track_block_at(&track, 1).unwrap();
	assert_eq!(second.id, gap.id);
	assert_eq!(block_in(&second), Rational::new(50, 1));
	assert_eq!(block_length(&second), Rational::new(50, 1));
}

/// `TrackReplaceBlockWithGapCommand` creates its own gap when no
/// neighbouring gap exists, and swaps it back out on undo.
#[test]
fn replace_block_with_gap_creates_gap() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list);
	let clip = add_clip(&track, Rational::new(0, 1), Rational::new(50, 1));
	let tail = add_clip(&track, Rational::new(50, 1), Rational::new(100, 1));

	let mut cmd = TrackReplaceBlockWithGapCommand::new(track.clone(), clip.clone(), false);
	cmd.redo();
	// clip replaced by a fresh gap of the same length.
	assert_eq!(track_block_count(&track), 2);
	let first = track_block_at(&track, 0).unwrap();
	assert_ne!(first.id, clip.id);
	assert_eq!(block_in(&first), Rational::new(0, 1));
	assert_eq!(block_length(&first), Rational::new(50, 1));
	assert_eq!(track_block_at(&track, 1).unwrap().id, tail.id);

	cmd.undo();
	assert_eq!(track_block_count(&track), 2);
	let restored = track_block_at(&track, 0).unwrap();
	assert_eq!(restored.id, clip.id);
	assert_eq!(block_length(&restored), Rational::new(50, 1));
}

/// `TrackRippleRemoveAreaCommand` trims the boundary blocks and removes
/// the blocks fully inside the range; undo restores everything.
#[test]
fn ripple_remove_area_round_trip() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list);
	let c1 = add_clip(&track, Rational::new(0, 1), Rational::new(50, 1));
	let c2 = add_clip(&track, Rational::new(50, 1), Rational::new(100, 1));
	let c3 = add_clip(&track, Rational::new(100, 1), Rational::new(150, 1));

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		track.clone(),
		TimeRange::new(Rational::new(25, 1), Rational::new(125, 1)),
	);
	cmd.prepare();
	cmd.redo();

	// c2 removed; c1 trimmed to [0,25); c3 out-anchored to [125,150).
	assert_eq!(track_block_count(&track), 2);
	let first = track_block_at(&track, 0).unwrap();
	assert_eq!(first.id, c1.id);
	assert_eq!(span_of(&first), Some((Rational::new(0, 1), Rational::new(25, 1))));
	let second = track_block_at(&track, 1).unwrap();
	assert_eq!(second.id, c3.id);
	assert_eq!(
		span_of(&second),
		Some((Rational::new(125, 1), Rational::new(150, 1)))
	);

	cmd.undo();
	assert_eq!(track_block_count(&track), 3);
	assert_eq!(track_block_at(&track, 0).unwrap().id, c1.id);
	assert_eq!(track_block_at(&track, 1).unwrap().id, c2.id);
	assert_eq!(track_block_at(&track, 2).unwrap().id, c3.id);
	assert_eq!(span_of(&c1), Some((Rational::new(0, 1), Rational::new(50, 1))));
	assert_eq!(
		span_of(&c2),
		Some((Rational::new(50, 1), Rational::new(100, 1)))
	);
	assert_eq!(
		span_of(&c3),
		Some((Rational::new(100, 1), Rational::new(150, 1)))
	);
}

/// `BlockResizeCommand` changes a block's length out-anchored and restores
/// it on undo.
#[test]
fn block_resize_round_trip() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list);
	let clip = add_clip(&track, Rational::new(0, 1), Rational::new(50, 1));

	let mut cmd = BlockResizeCommand::new(clip.clone(), Rational::new(30, 1));
	cmd.redo();
	assert_eq!(block_length(&clip), Rational::new(30, 1));
	// Out-anchored: the in point shifts so the out stays at 50.
	assert_eq!(block_out(&clip), Rational::new(50, 1));
	cmd.undo();
	assert_eq!(block_length(&clip), Rational::new(50, 1));
	assert_eq!(block_in(&clip), Rational::new(0, 1));
}

/// `TrackMoveBlockCommand` replaces the source spot with a gap and places
/// the block at the destination; undo restores the original layout.
#[test]
fn move_block_round_trip() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list.clone());
	let clip = add_clip(&track, Rational::new(0, 1), Rational::new(50, 1));
	let tail = add_clip(&track, Rational::new(50, 1), Rational::new(100, 1));

	let mut cmd = TrackMoveBlockCommand::new(
		list,
		0,
		clip.clone(),
		Rational::new(150, 1),
	);
	cmd.redo();
	// Layout: a gap fills the clip's old spot [0,50), the tail stays at
	// [50,100), a bridging gap covers [100,150) and the moved clip is
	// appended at [150,200).
	assert_eq!(track_block_count(&track), 4);
	assert_eq!(block_in(&clip), Rational::new(150, 1));
	assert_eq!(block_length(&clip), Rational::new(50, 1));
	let first = track_block_at(&track, 0).unwrap();
	assert_eq!(span_of(&first), Some((Rational::new(0, 1), Rational::new(50, 1))));
	assert_eq!(track_block_at(&track, 1).unwrap().id, tail.id);
	let third = track_block_at(&track, 2).unwrap();
	assert_eq!(
		span_of(&third),
		Some((Rational::new(100, 1), Rational::new(150, 1)))
	);
	assert_eq!(track_block_at(&track, 3).unwrap().id, clip.id);

	cmd.undo();
	assert_eq!(track_block_count(&track), 2);
	assert_eq!(track_block_at(&track, 0).unwrap().id, clip.id);
	assert_eq!(track_block_at(&track, 1).unwrap().id, tail.id);
	assert_eq!(span_of(&clip), Some((Rational::new(0, 1), Rational::new(50, 1))));
	assert_eq!(span_of(&tail), Some((Rational::new(50, 1), Rational::new(100, 1))));
}

/// The `Command` trait dispatch (used by the oakundo vtable wrappers)
/// routes through the same redo/undo bodies as the inherent methods.
#[test]
fn commands_trait_dispatch() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list);
	let clip = add_clip(&track, Rational::new(0, 1), Rational::new(50, 1));

	let mut resize = BlockResizeCommand::new(clip.clone(), Rational::new(10, 1));
	Command::redo(&mut resize);
	assert_eq!(block_length(&clip), Rational::new(10, 1));
	Command::undo(&mut resize);
	assert_eq!(block_length(&clip), Rational::new(50, 1));
}

/// `TrackPlaceBlockCommand` splices a clip into the middle of another:
/// the host block is split at the range in, the remainder is trimmed to
/// the range out, and the placed block lands in between; undo restores
/// the single host block.
#[test]
fn place_block_splice_round_trip() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list.clone());
	let host = add_clip(&track, Rational::new(0, 1), Rational::new(100, 1));
	let placed = add_clip(&track, Rational::new(25, 1), Rational::new(35, 1));
	track_ripple_remove_block(&track, &placed);

	let mut cmd = oaktimeline::undopointer::TrackPlaceBlockCommand::new(
		list.clone(),
		0,
		placed.clone(),
		Rational::new(25, 1),
	);
	cmd.redo();
	// host split to [0,25), remainder trimmed to [25,90), placed at [25,35).
	assert_eq!(track_block_count(&track), 3);
	assert_eq!(track_block_at(&track, 0).unwrap().id, host.id);
	assert_eq!(track_block_at(&track, 1).unwrap().id, placed.id);
	assert_eq!(
		span_of(&host),
		Some((Rational::new(0, 1), Rational::new(25, 1)))
	);

	cmd.undo();
	assert_eq!(track_block_count(&track), 1);
	assert_eq!(track_block_at(&track, 0).unwrap().id, host.id);
	assert_eq!(
		span_of(&host),
		Some((Rational::new(0, 1), Rational::new(100, 1)))
	);
}

/// `TrackListInsertGaps` splits a block at the insertion point and
/// inserts a gap of the requested length after it; undo removes the gap
/// and re-joins the block.
#[test]
fn insert_gaps_round_trip() {
	let project = make_project();
	let (_seq, list) = sequence_and_list(&project);
	let track = TimelineAddTrackCommand::run_immediately(list.clone());
	let clip = add_clip(&track, Rational::new(0, 1), Rational::new(100, 1));

	let mut cmd =
		oaktimeline::undogeneral::TrackListInsertGaps::new(
			list.clone(),
			Rational::new(40, 1),
			Rational::new(20, 1),
		);
	cmd.prepare();
	cmd.redo();
	// clip split to [0,40) + [40,100), gap [40,60) inserted after the
	// first half.
	assert_eq!(track_block_count(&track), 3);
	let first = track_block_at(&track, 0).unwrap();
	assert_eq!(first.id, clip.id);
	assert_eq!(span_of(&first), Some((Rational::new(0, 1), Rational::new(40, 1))));
	let gap = track_block_at(&track, 1).unwrap();
	assert_eq!(
		span_of(&gap),
		Some((Rational::new(40, 1), Rational::new(60, 1)))
	);

	cmd.undo();
	assert_eq!(track_block_count(&track), 1);
	assert_eq!(track_block_at(&track, 0).unwrap().id, clip.id);
	assert_eq!(
		span_of(&clip),
		Some((Rational::new(0, 1), Rational::new(100, 1)))
	);
}
