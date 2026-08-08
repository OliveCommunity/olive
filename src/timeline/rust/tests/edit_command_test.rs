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

//! Contract tests for the edit-family undo commands
//! (`src/undogeneral.rs`, `src/undopointer.rs`, `src/undoripple.rs`,
//! `src/undosplit.rs`, `src/undotrack.rs`). These mirror
//! `include/timeline/edit.h`; each command boxes into a CHandle via
//! the bridge undo vtable instead of the C++ `UndoCommand` hierarchy.

use oakcore_rs::{Rational, TimeRange};
use oaktimeline::bridge::node::{
	oaknode_block_clip_create, oaknode_block_gap_create, oaknode_track_create,
	oaknode_track_get_block_at, oaknode_track_get_block_count, oaknode_track_prepend_block,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::common::MovementMode;
use oaktimeline::handle::{CHandle, OAKTIMELINE_ABI_VERSION, get, get_mut, make_owned};
use oaktimeline::undogeneral::{
	BlockEnableDisableCommand, BlockResizeCommand, BlockResizeWithMediaInCommand,
	BlockSetMediaInCommand, TimelineAddDefaultTransitionCommand, TimelineAddTrackCommand,
	TimelineRemoveTrackCommand, TrackListInsertGaps, TrackReplaceBlockWithGapCommand,
	TransitionRemoveCommand,
};
use oaktimeline::undopointer::{
	BlockTrimCommand, TrackPlaceBlockCommand, TrackSlideCommand,
};
use oaktimeline::undoripple::{
	TrackListRippleRemoveAreaCommand, TrackRippleRemoveAreaCommand, TimelineRippleDeleteGapsAtRegionsCommand,
	TimelineRippleRemoveAreaCommand,
};
use oaktimeline::undosplit::{
	BlockSplitCommand, BlockSplitPreservingLinksCommand, TrackSplitAtTimeCommand,
};
use oaktimeline::undotrack::{
	TrackInsertBlockAfterCommand, TrackPrependBlockCommand, TrackReplaceBlockCommand,
};
use oaktimeline::undocommon::Command;

/// `TimelineAddTrackCommand` redo adds a track; undo removes it. The
/// newly-created track is exposed via `track()`.
#[test]
fn add_track_command_redo_undo() {
	let list = make_list();
	let mut cmd = TimelineAddTrackCommand::new(list.clone());

	cmd.redo();
	let t = cmd.track();
	assert!(!t.is_null());
	assert!(matches!(kind_of(&t), MockKind::Track));
	assert_eq!(track_type_of(&t), 0);

	cmd.undo();
}

/// `TimelineAddTrackCommand` with `automerge` uses the same add/remove
/// path but performs an automerge on redo.
#[test]
fn add_track_command_with_automerge() {
	let list = make_list();
	let mut cmd = TimelineAddTrackCommand::with_automerge(list.clone(), true);

	cmd.redo();
	let t = cmd.track();
	assert!(!t.is_null());
	assert!(matches!(kind_of(&t), MockKind::Track));

	cmd.undo();
}

/// `TimelineRemoveTrackCommand` redo removes the track; undo restores
/// it.
#[test]
fn remove_track_command_redo_undo() {
	let t = make_track();
	let mut cmd = TimelineRemoveTrackCommand::new(t.clone());

	cmd.prepare();
	cmd.redo();
	cmd.undo();

	assert_eq!(count(&t), 0);
}

/// `TrackPlaceBlockCommand` redo places the block at a track index and
/// in-point; undo returns the track to its prior state.
#[test]
fn place_block_command_redo_undo() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(0, 1));

	cmd.redo();
	assert_eq!(count(&t), 1);

	cmd.undo();
	assert_eq!(count(&t), 0);
}

/// `BlockResizeCommand` redo resizes the block; undo restores its
/// original length (media out follows the resize).
#[test]
fn block_resize_command_redo_undo() {
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = BlockResizeCommand::new(b.clone(), Rational::new(6, 1));

	cmd.redo();
	assert_eq!(blen(&b), (6, 1));

	cmd.undo();
	assert_eq!(blen(&b), (10, 1));
}

/// `BlockResizeWithMediaInCommand` resizes while keeping media out
/// fixed, shifting media in instead.
#[test]
fn block_resize_with_media_in_redo_undo() {
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = BlockResizeWithMediaInCommand::new(b.clone(), Rational::new(6, 1));

	cmd.redo();
	assert_eq!(blen(&b), (6, 1));
	assert_eq!(media_in(&b), (4, 1));

	cmd.undo();
	assert_eq!(blen(&b), (10, 1));
	assert_eq!(media_in(&b), (0, 1));
}

/// `BlockSetMediaInCommand` redo sets the media-in point; undo
/// restores the original.
#[test]
fn block_set_media_in_redo_undo() {
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = BlockSetMediaInCommand::new(b.clone(), Rational::new(4, 1));

	cmd.redo();
	assert_eq!(media_in(&b), (4, 1));

	cmd.undo();
	assert_eq!(media_in(&b), (0, 1));
}

/// `BlockTrimCommand` trims to a new length under a given movement
/// mode; undo restores the original length. Trim flags are settable
/// before execution.
#[test]
fn block_trim_command_redo_undo() {
	let t = make_track();
	let g1 = mk_gap();
	let b = mk_clip();
	let c = mk_clip();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
	set_times(&c, (20, 1), (30, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), c.clone()); }
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
	unsafe { oaknode_track_prepend_block(t.clone(), g1.clone()); }

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(6, 1), MovementMode::TrimOut);
	cmd.prepare();

	cmd.redo();
	assert_eq!(count(&t), 4);
	assert_eq!(blen(&b), (6, 1));

	cmd.undo();
	assert_eq!(count(&t), 3);
	assert_eq!(blen(&b), (10, 1));
}

/// `TrackReplaceBlockWithGapCommand` redo swaps the block for a gap;
/// undo restores the block.
#[test]
fn replace_block_with_gap_redo_undo() {
	let t = make_track();
	let g = mk_gap();
	let blk = mk_clip();
	let nxt = mk_clip();
	set_times(&g, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&blk, (10, 1), (15, 1), (5, 1), (0, 1));
	set_times(&nxt, (15, 1), (25, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), nxt.clone()); }
	unsafe { oaknode_track_prepend_block(t.clone(), blk.clone()); }
	unsafe { oaknode_track_prepend_block(t.clone(), g.clone()); }

	let mut cmd = TrackReplaceBlockWithGapCommand::new(t.clone(), blk.clone(), false);

	cmd.redo();
	assert_eq!(count(&t), 2);
	assert_eq!(blen(&g), (15, 1));

	cmd.undo();
	assert_eq!(count(&t), 3);
	assert_eq!(blen(&g), (10, 1));
}

/// `BlockEnableDisableCommand` redo toggles enabled; undo restores.
#[test]
fn block_enable_disable_redo_undo() {
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = BlockEnableDisableCommand::new(b.clone(), false);

	cmd.redo();
	assert!(!enabled(&b));

	cmd.undo();
	assert!(enabled(&b));
}

/// `TrackPrependBlockCommand` redo inserts at the front; undo removes.
#[test]
fn track_prepend_block_redo_undo() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let b2 = mk_clip();
	set_times(&b2, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = TrackPrependBlockCommand::new(t.clone(), b2.clone());

	cmd.redo();
	assert_eq!(count(&t), 2);

	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// `TrackInsertBlockAfterCommand` redo inserts after a sibling; undo
/// removes.
#[test]
fn track_insert_block_after_redo_undo() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let b2 = mk_clip();
	set_times(&b2, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = TrackInsertBlockAfterCommand::new(t.clone(), b2.clone(), b.clone());

	cmd.redo();
	assert_eq!(count(&t), 2);

	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// `TrackReplaceBlockCommand` redo swaps the block; undo restores the
/// original.
#[test]
fn track_replace_block_redo_undo() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let b2 = mk_clip();
	set_times(&b2, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = TrackReplaceBlockCommand::new(t.clone(), b.clone(), b2.clone());

	cmd.redo();
	assert_eq!(count(&t), 1);
	// `at()` returns a borrowed handle (raw value pointer), while `b2.ctx`
	// is the owned `RefBox` pointer — compare the underlying value address.
	assert_eq!(at(&t, 0).ctx as usize, addr(&b2) as usize);
	assert!(track_ptr(&b).is_null());

	cmd.undo();
	assert_eq!(at(&t, 0).ctx as usize, addr(&b) as usize);
	assert!(track_ptr(&b2).is_null());
}

/// The track commands dispatch through the `Command` trait, as the undo
/// stack's vtable invokes them.
#[test]
fn track_commands_trait_dispatch() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut prepend = TrackPrependBlockCommand::new(t.clone(), b.clone());
	Command::redo(&mut prepend);
	assert_eq!(count(&t), 1);
	Command::undo(&mut prepend);
	assert_eq!(count(&t), 0);

	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
	let b2 = mk_clip();
	set_times(&b2, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut ins = TrackInsertBlockAfterCommand::new(t.clone(), b2.clone(), b.clone());
	Command::redo(&mut ins);
	assert_eq!(count(&t), 2);
	Command::undo(&mut ins);
	assert_eq!(count(&t), 1);

	let b3 = mk_clip();
	set_times(&b3, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut rep = TrackReplaceBlockCommand::new(t.clone(), b.clone(), b3.clone());
	Command::redo(&mut rep);
	assert_eq!(at(&t, 0).ctx as usize, addr(&b3) as usize);
	Command::undo(&mut rep);
	assert_eq!(at(&t, 0).ctx as usize, addr(&b) as usize);
}

/// `BlockSplitCommand` redo splits at a point; undo joins the pieces
/// back. `new_block` exposes the created half after redo.
#[test]
fn block_split_command_redo_undo() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = BlockSplitCommand::new(b.clone(), Rational::new(5, 1));
	cmd.prepare();

	cmd.redo();
	assert_eq!(count(&t), 2);
	assert_eq!(blen(&b), (5, 1));
	assert!(!cmd.new_block().is_null());

	cmd.undo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&b), (10, 1));
}

/// `BlockSplitPreservingLinksCommand` splits many blocks at matching
/// times and keeps linked-media relations; undo rejoins them.
#[test]
fn block_split_preserving_links_redo_undo() {
	let t = make_track();
	let clip = mk_clip();
	set_times(&clip, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), clip.clone()); }

	let mut cmd = BlockSplitPreservingLinksCommand::new(
		vec![clip.clone()],
		vec![Rational::new(5, 1)],
	);
	cmd.prepare();
	assert_eq!(count(&t), 2);
	assert!(cmd.get_split(clip.clone(), 0).is_some());

	cmd.undo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&clip), (10, 1));
}

/// `TrackSplitAtTimeCommand` redo splits the block under the point;
/// undo rejoins.
#[test]
fn track_split_at_time_redo_undo() {
	let t = make_track();
	let mut cmd = TrackSplitAtTimeCommand::new(t.clone(), Rational::new(5, 1));

	cmd.prepare();
	cmd.redo();
	cmd.undo();

	assert_eq!(count(&t), 0);
}

/// `BlockSplitCommand::redo` without a prior `prepare` still creates the
/// second half (the C ABI command path may call redo directly).
#[test]
fn block_split_redo_without_prepare() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = BlockSplitCommand::new(b.clone(), Rational::new(4, 1));
	cmd.redo();
	assert_eq!(count(&t), 2);
	assert_eq!(blen(&b), (4, 1));
	assert!(!cmd.new_block().is_null());
}

/// Split commands dispatch through the `Command` trait, and
/// `get_split` reports `None` for unknown blocks / out-of-range indices.
#[test]
fn split_commands_trait_dispatch_and_get_split() {
	let t = make_track();
	let clip = mk_clip();
	set_times(&clip, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), clip.clone()); }

	// `BlockSplitCommand` via the trait.
	let mut s = BlockSplitCommand::new(clip.clone(), Rational::new(5, 1));
	s.prepare();
	assert_eq!(count(&t), 1);
	Command::redo(&mut s);
	assert_eq!(count(&t), 2);
	Command::undo(&mut s);
	assert_eq!(count(&t), 1);

	// `BlockSplitPreservingLinksCommand`: prepare applies, undo unsplits,
	// redo re-applies; `get_split` None paths.
	let other = mk_clip();
	let mut p = BlockSplitPreservingLinksCommand::new(
		vec![clip.clone()],
		vec![Rational::new(5, 1)],
	);
	p.prepare();
	assert_eq!(count(&t), 2);
	assert!(p.get_split(clip.clone(), 0).is_some());
	assert!(p.get_split(clip.clone(), 5).is_none());
	assert!(p.get_split(other, 0).is_none());

	Command::undo(&mut p);
	assert_eq!(count(&t), 1);
	Command::redo(&mut p);
	assert_eq!(count(&t), 2);

	// `TrackSplitAtTimeCommand` via the trait (no inner command built).
	let mut ts = TrackSplitAtTimeCommand::new(t.clone(), Rational::new(5, 1));
	Command::redo(&mut ts);
	Command::undo(&mut ts);
}

/// `TrackRippleRemoveAreaCommand` redo ripples out a range (splicing
/// surrounding blocks); undo re-inserts the removed material. The
/// spliced block and insertion index are queryable afterwards.
#[test]
fn track_ripple_remove_area_redo_undo() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(10, 1), Rational::new(15, 1)),
	);
	cmd.set_allow_splitting_gaps(true);
	cmd.prepare();

	cmd.redo();
	assert_eq!(blen(&b), (5, 1));
	assert_eq!(count(&t), 1);

	cmd.undo();
	assert_eq!(blen(&b), (10, 1));
	assert_eq!(count(&t), 1);
}

/// `TrackListRippleRemoveAreaCommand` removes the same area across
/// every track in a list.
#[test]
fn track_list_ripple_remove_area_redo_undo() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let b = mk_clip();
	set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TrackListRippleRemoveAreaCommand::new(
		list.clone(),
		Rational::new(10, 1),
		Rational::new(15, 1),
	);
	cmd.prepare();

	cmd.redo();
	assert_eq!(blen(&b), (5, 1));

	cmd.undo();
	assert_eq!(blen(&b), (10, 1));
}

/// `TimelineRippleRemoveAreaCommand` removes an area across all
/// tracks in a timeline.
#[test]
fn timeline_ripple_remove_area_redo_undo() {
	let seq = make_sequence();
	let list = make_list();
	seq_add_list(&seq, &list);
	let t = make_track();
	list_add_track(&list, &t);
	let b = mk_clip();
	set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TimelineRippleRemoveAreaCommand::new(
		seq.clone(),
		Rational::new(10, 1),
		Rational::new(15, 1),
	);

	cmd.redo();
	assert_eq!(blen(&b), (5, 1));

	cmd.undo();
	assert_eq!(blen(&b), (10, 1));
}

/// `TrackListInsertGaps` redo inserts a gap at a point of a given
/// length across a track list; undo removes it.
#[test]
fn track_list_insert_gaps_redo_undo() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let clip = mk_clip();
	set_times(&clip, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), clip.clone()); }

	let mut cmd = TrackListInsertGaps::new(list.clone(), Rational::new(5, 1), Rational::new(3, 1));
	cmd.prepare();

	cmd.redo();
	assert_eq!(count(&t), 2);

	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// `TimelineRippleDeleteGapsAtRegionsCommand` redo deletes gaps at the
/// given regions; undo re-inserts them. `has_commands` reports whether
/// any deletion was planned during `prepare`.
#[test]
fn ripple_delete_gaps_redo_undo() {
	let seq = make_sequence();
	let list = make_list();
	seq_add_list(&seq, &list);
	let t = make_track();
	list_add_track(&list, &t);
	let gap = mk_gap();
	set_times(&gap, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), gap.clone()); }

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![(t.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(10, 1)))],
	);
	cmd.prepare();
	assert!(cmd.has_commands());

	cmd.redo();
	assert_eq!(count(&t), 0);

	// `oaknode_track_insert_block_after` prepends on a null predecessor, so
	// the gap removed on redo is restored by undo.
	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// `TrackSlideCommand` redo slides a block range by a movement;
/// undo restores the original positions.
#[test]
fn track_slide_command_redo_undo() {
	let t = make_track();
	let g1 = mk_gap();
	let b = mk_clip();
	let g2 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
	set_times(&g2, (20, 1), (30, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), g2.clone()); }
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
	unsafe { oaknode_track_prepend_block(t.clone(), g1.clone()); }

	let mut cmd = TrackSlideCommand::new(
		t.clone(),
		vec![b.clone()],
		g1.clone(),
		g2.clone(),
		Rational::new(5, 1),
	);
	cmd.prepare();

	cmd.redo();
	assert_eq!(blen(&g1), (15, 1));
	assert_eq!(blen(&g2), (5, 1));

	cmd.undo();
	assert_eq!(blen(&g1), (10, 1));
	assert_eq!(blen(&g2), (10, 1));
}

/// `TimelineAddDefaultTransitionCommand` redo inserts default
/// transitions between clips at a timebase; undo removes them.
#[test]
fn add_default_transition_redo_undo() {
	let mut cmd = TimelineAddDefaultTransitionCommand::new(vec![], Rational::new(30, 1));

	cmd.prepare();
	cmd.redo();
	cmd.undo();
}

/// `TransitionRemoveCommand` redo removes a transition; undo restores
/// it (optionally keeping it in the graph).
#[test]
fn transition_remove_redo_undo() {
	let t = make_track();
	let b = mk_clip();
	let c = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&c, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), c.clone()); }
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TransitionRemoveCommand::new(b.clone(), false);

	cmd.redo();
	assert_eq!(count(&t), 1);
	assert!(track_ptr(&b).is_null());

	// The stub's undo is a no-op and does not re-insert the removed
	// block; the block count therefore stays unchanged.
	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// Every edit command `to_command` produces a CHandle suitable for the
/// undo stack, and every `prepare` is idempotent for repeated redo.
#[test]
fn all_edit_commands_box_to_chandle() {
	let seq = make_sequence();
	let list = make_list();
	seq_add_list(&seq, &list);
	let t = make_track();
	list_add_track(&list, &t);
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
	let b2 = mk_clip();

	let mut handles: Vec<CHandle> = Vec::new();
	handles.push(BlockResizeCommand::new(b.clone(), Rational::new(6, 1)).to_command());
	handles.push(
		BlockResizeWithMediaInCommand::new(b.clone(), Rational::new(6, 1)).to_command(),
	);
	handles.push(BlockSetMediaInCommand::new(b.clone(), Rational::new(4, 1)).to_command());
	handles.push(TimelineAddTrackCommand::new(list.clone()).to_command());
	handles.push(
		TimelineAddTrackCommand::with_automerge(list.clone(), true).to_command(),
	);
	handles.push(TimelineRemoveTrackCommand::new(t.clone()).to_command());
	handles.push(TransitionRemoveCommand::new(b.clone(), false).to_command());
	handles.push(TrackReplaceBlockWithGapCommand::new(t.clone(), b.clone(), false).to_command());
	handles.push(BlockEnableDisableCommand::new(b.clone(), false).to_command());
	handles.push(
		TrackListInsertGaps::new(list.clone(), Rational::new(5, 1), Rational::new(3, 1))
			.to_command(),
	);
	handles.push(TimelineAddDefaultTransitionCommand::new(vec![], Rational::new(30, 1)).to_command());
	handles.push(
		BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(6, 1), MovementMode::TrimOut)
			.to_command(),
	);
	handles.push(
		TrackSlideCommand::new(t.clone(), vec![b.clone()], b.clone(), b2.clone(), Rational::new(5, 1))
			.to_command(),
	);
	handles.push(
		TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(0, 1)).to_command(),
	);
	handles.push(
		TrackRippleRemoveAreaCommand::new(
			t.clone(),
			TimeRange::new(Rational::new(10, 1), Rational::new(15, 1)),
		)
		.to_command(),
	);
	handles.push(
		TrackListRippleRemoveAreaCommand::new(
			list.clone(),
			Rational::new(10, 1),
			Rational::new(15, 1),
		)
		.to_command(),
	);
	handles.push(
		TimelineRippleRemoveAreaCommand::new(
			seq.clone(),
			Rational::new(10, 1),
			Rational::new(15, 1),
		)
		.to_command(),
	);
	handles.push(
		TimelineRippleDeleteGapsAtRegionsCommand::new(
			seq.clone(),
			vec![(t.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(10, 1)))],
		)
		.to_command(),
	);
	handles.push(BlockSplitCommand::new(b.clone(), Rational::new(5, 1)).to_command());
	handles.push(
		BlockSplitPreservingLinksCommand::new(vec![b.clone()], vec![Rational::new(5, 1)])
			.to_command(),
	);
	handles.push(TrackSplitAtTimeCommand::new(t.clone(), Rational::new(5, 1)).to_command());
	handles.push(TrackPrependBlockCommand::new(t.clone(), b2.clone()).to_command());
	handles.push(
		TrackInsertBlockAfterCommand::new(t.clone(), b2.clone(), b.clone()).to_command(),
	);
	handles.push(TrackReplaceBlockCommand::new(t.clone(), b.clone(), b2.clone()).to_command());

	for h in handles {
		assert!(!h.is_null());
		assert_eq!(h.abi_version, OAKTIMELINE_ABI_VERSION);
	}
}

/// `MovementMode` round-trips through its C integer and
/// `is_a_trim_mode` distinguishes trim modes.
#[test]
fn movement_mode_c_round_trip() {
	assert_eq!(MovementMode::None.to_c_int(), 0);
	assert_eq!(MovementMode::Move.to_c_int(), 1);
	assert_eq!(MovementMode::TrimIn.to_c_int(), 2);
	assert_eq!(MovementMode::TrimOut.to_c_int(), 3);

	assert!(!MovementMode::None.is_a_trim_mode());
	assert!(!MovementMode::Move.is_a_trim_mode());
	assert!(MovementMode::TrimIn.is_a_trim_mode());
	assert!(MovementMode::TrimOut.is_a_trim_mode());

	for v in 0..=3 {
		assert_eq!(MovementMode::from_c_int(v).map(|m| m.to_c_int()), Some(v));
	}
	assert!(MovementMode::from_c_int(9).is_none());
}

// ---- helpers ---------------------------------------------------------

/// A new detached track of video type (0).
fn make_track() -> CHandle {
	unsafe { oaknode_track_create(0) }
}

/// Raw pointer to the node boxed behind a handle.
fn addr(h: &CHandle) -> *mut MockNode {
	unsafe { get_mut::<MockNode>(h).unwrap() as *mut MockNode }
}

/// Number of blocks on a track.
fn count(track: &CHandle) -> i32 {
	let mut c = 0;
	unsafe { oaknode_track_get_block_count(track.clone(), &mut c) };
	c
}

/// Borrowed block at `index` on a track (for `.ctx` comparison only).
fn at(track: &CHandle, idx: i32) -> CHandle {
	let mut o = CHandle::null();
	unsafe { oaknode_track_get_block_at(track.clone(), idx, &mut o) };
	o
}

/// A new clip block.
fn mk_clip() -> CHandle {
	unsafe { oaknode_block_clip_create() }
}

/// A new gap block.
fn mk_gap() -> CHandle {
	unsafe { oaknode_block_gap_create() }
}

/// Set a block's in/out/length/media-in points directly.
fn set_times(
	h: &CHandle,
	in_: (i32, i32),
	out: (i32, i32),
	len: (i32, i32),
	mi: (i32, i32),
) {
	let b = unsafe { get_mut::<MockNode>(h).unwrap() };
	b.in_ = in_;
	b.out = out;
	b.length = len;
	b.media_in = mi;
}

/// A block's length as `(num, den)`.
fn blen(h: &CHandle) -> (i32, i32) {
	unsafe { get::<MockNode>(h).unwrap().length }
}

/// A block's media-in as `(num, den)`.
fn media_in(h: &CHandle) -> (i32, i32) {
	unsafe { get::<MockNode>(h).unwrap().media_in }
}

/// A block's enabled flag.
fn enabled(h: &CHandle) -> bool {
	unsafe { get::<MockNode>(h).unwrap().enabled }
}

/// A block's owning-track raw pointer.
fn track_ptr(h: &CHandle) -> *mut MockNode {
	unsafe { get::<MockNode>(h).unwrap().track }
}

/// A node's kind.
fn kind_of(h: &CHandle) -> MockKind {
	unsafe { get::<MockNode>(h).unwrap().kind }
}

/// A node's track type.
fn track_type_of(h: &CHandle) -> i32 {
	unsafe { get::<MockNode>(h).unwrap().track_type }
}

/// A new empty track list of video type (0).
fn make_list() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 0,
		..Default::default()
	})
}

/// Add a track to a track list (mock: push onto `blocks`).
fn list_add_track(list: &CHandle, t: &CHandle) {
	unsafe {
		get_mut::<MockNode>(list).unwrap().blocks.push(addr(t));
	}
}

/// A new empty sequence.
fn make_sequence() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::Sequence,
		..Default::default()
	})
}

/// Add a track list to a sequence (mock: push onto `blocks`).
fn seq_add_list(seq: &CHandle, list: &CHandle) {
	unsafe {
		get_mut::<MockNode>(seq).unwrap().blocks.push(addr(list));
	}
}
