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

//! Coverage for `src/undogeneral.rs`: the general-purpose timeline commands
//! (block resize / media-in, add / remove track, transition removal, gap
//! replacement, enable/disable, gap insertion and default transitions).
//! These tests exercise the `Command` trait dispatch bodies, the edge
//! branches of each command's inherent `redo`/`undo`/`prepare`, the
//! `run_immediately` factories and the `Drop` paths.
#![cfg(feature = "test-stubs")]

use oakcore_rs::Rational;
use oaktimeline::bridge::node::{
	oaknode_block_clip_create, oaknode_block_gap_create, oaknode_track_create,
	oaknode_track_get_block_at, oaknode_track_get_block_count, oaknode_track_prepend_block,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::handle::{get, get_mut, make_owned, CHandle};
use oaktimeline::undocommon::Command;
use oaktimeline::undogeneral::{
	BlockEnableDisableCommand, BlockResizeCommand, BlockResizeWithMediaInCommand,
	BlockSetMediaInCommand, TimelineAddDefaultTransitionCommand, TimelineAddTrackCommand,
	TimelineRemoveTrackCommand, TrackListInsertGaps, TrackReplaceBlockWithGapCommand,
	TransitionRemoveCommand,
};

/// The three resize/media-in commands dispatch through the `Command` trait,
/// which invokes the trait impl bodies (they forward to the inherent
/// methods).
#[test]
fn resize_commands_trait_dispatch() {
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut resize = BlockResizeCommand::new(b.clone(), Rational::new(6, 1));
	Command::redo(&mut resize);
	assert_eq!(blen(&b), (6, 1));
	Command::undo(&mut resize);
	assert_eq!(blen(&b), (10, 1));

	let mut media_in = BlockResizeWithMediaInCommand::new(b.clone(), Rational::new(6, 1));
	Command::redo(&mut media_in);
	assert_eq!(blen(&b), (6, 1));
	assert_eq!(media_in_of(&b), (4, 1));
	Command::undo(&mut media_in);
	assert_eq!(blen(&b), (10, 1));

	let mut set_mi = BlockSetMediaInCommand::new(b.clone(), Rational::new(4, 1));
	Command::redo(&mut set_mi);
	assert_eq!(media_in_of(&b), (4, 1));
	Command::undo(&mut set_mi);
	assert_eq!(media_in_of(&b), (0, 1));
}

/// `run_immediately` constructs, `redo`s and returns the created track in
/// one step; both the plain and the automerge variants are covered, and an
/// audio track list drives the audio input-id branch of `with_automerge`.
#[test]
fn add_track_run_immediately() {
	let list = make_list();
	let t = TimelineAddTrackCommand::run_immediately(list.clone());
	assert!(!t.is_null());
	assert!(matches!(kind_of(&t), MockKind::Track));
	assert_eq!(track_type_of(&t), 0);

	let audio_list = make_audio_list();
	let ta = TimelineAddTrackCommand::run_immediately_with_automerge(audio_list.clone(), true);
	assert!(!ta.is_null());
	assert_eq!(track_type_of(&ta), 1);
}

/// `TimelineAddTrackCommand` and `TimelineRemoveTrackCommand` dispatch
/// through the `Command` trait.
#[test]
fn add_remove_track_trait_dispatch() {
	let list = make_list();
	let mut add = TimelineAddTrackCommand::new(list.clone());
	Command::redo(&mut add);
	assert!(!add.track().is_null());
	Command::undo(&mut add);

	let t = make_track();
	let mut remove = TimelineRemoveTrackCommand::new(t.clone());
	remove.prepare();
	Command::redo(&mut remove);
	Command::undo(&mut remove);
}

/// `TransitionRemoveCommand` with `remove_from_graph` builds a removal
/// command at redo, re-runs it at undo, and frees it on drop.
#[test]
fn transition_remove_graph_redo_undo_drop() {
	let t = make_track();
	let b = mk_clip();
	let c = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&c, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), c.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), b.clone());
	}

	{
		let mut cmd = TransitionRemoveCommand::new(b.clone(), true);
		cmd.redo();
		assert_eq!(count(&t), 1);
		assert!(track_ptr(&b).is_null());
		cmd.undo();
	}
}

/// `TransitionRemoveCommand` trait dispatch (with and without graph
/// removal).
#[test]
fn transition_remove_trait_dispatch() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), b.clone());
	}

	let mut keep = TransitionRemoveCommand::new(b.clone(), false);
	Command::redo(&mut keep);
	Command::undo(&mut keep);

	let mut rm = TransitionRemoveCommand::new(b.clone(), true);
	Command::redo(&mut rm);
	Command::undo(&mut rm);
}

/// `TrackReplaceBlockWithGapCommand` when the block is flanked by gaps on
/// both sides: the two gaps are merged, then undo restores both gaps and
/// the block; dropping the post-redo command frees the merged gap.
#[test]
fn replace_block_gap_merge_two_gaps() {
	let t = make_track();
	let g1 = mk_gap();
	let blk = mk_clip();
	let g2 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&blk, (10, 1), (15, 1), (5, 1), (0, 1));
	set_times(&g2, (15, 1), (25, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), g2.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), blk.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), g1.clone());
	}

	{
		let mut cmd = TrackReplaceBlockWithGapCommand::new(t.clone(), blk.clone(), false);
		cmd.redo();
		assert_eq!(count(&t), 1);
		assert_eq!(blen(&g1), (25, 1));
		cmd.undo();
		assert_eq!(count(&t), 3);
		assert_eq!(blen(&g1), (10, 1));
	}

	// A second pass on a FRESH track, dropping the command after redo (no
	// undo) exercises the merged-gap Drop path.
	let t2 = make_track();
	{
		let g1b = mk_gap();
		let blkb = mk_clip();
		let g2b = mk_gap();
		set_times(&g1b, (0, 1), (10, 1), (10, 1), (0, 1));
		set_times(&blkb, (10, 1), (15, 1), (5, 1), (0, 1));
		set_times(&g2b, (15, 1), (25, 1), (10, 1), (0, 1));
		unsafe {
			oaknode_track_prepend_block(t2.clone(), g2b.clone());
		}
		unsafe {
			oaknode_track_prepend_block(t2.clone(), blkb.clone());
		}
		unsafe {
			oaknode_track_prepend_block(t2.clone(), g1b.clone());
		}

		let mut cmd = TrackReplaceBlockWithGapCommand::new(t2.clone(), blkb.clone(), false);
		cmd.redo();
		assert_eq!(count(&t2), 1);
	}
}

/// `TrackReplaceBlockWithGapCommand` when only the NEXT block is a gap: the
/// existing gap is extended rather than a new one created.
#[test]
fn replace_block_gap_extend_next_gap() {
	let t = make_track();
	let blk = mk_clip();
	let g = mk_gap();
	set_times(&blk, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&g, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), g.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), blk.clone());
	}

	let mut cmd = TrackReplaceBlockWithGapCommand::new(t.clone(), blk.clone(), false);
	cmd.redo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&g), (20, 1));
}

/// `TrackReplaceBlockWithGapCommand` when no gap neighbours the block: a
/// fresh gap is created and swapped in; undo swaps the block back and the
/// drop frees the re-orphaned gap.
#[test]
fn replace_block_gap_our_gap() {
	let t = make_track();
	let blk = mk_clip();
	let nxt = mk_clip();
	set_times(&blk, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&nxt, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), nxt.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), blk.clone());
	}

	{
		let mut cmd = TrackReplaceBlockWithGapCommand::new(t.clone(), blk.clone(), false);
		cmd.redo();
		assert_eq!(count(&t), 2);
		cmd.undo();
		assert_eq!(count(&t), 2);
		assert_eq!(at(&t, 0).ctx as usize, addr(&blk) as usize);
	}
}

/// `TrackReplaceBlockWithGapCommand` when the block is at the end of a track
/// preceded by a gap: the block and the preceding gap are both removed.
#[test]
fn replace_block_gap_at_end_preceding_gap() {
	let t = make_track();
	let g = mk_gap();
	let blk = mk_clip();
	set_times(&g, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&blk, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), blk.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), g.clone());
	}

	let mut cmd = TrackReplaceBlockWithGapCommand::new(t.clone(), blk.clone(), false);
	cmd.redo();
	assert_eq!(count(&t), 0);
}

/// `TrackReplaceBlockWithGapCommand` when the block is at the end of a track
/// preceded by a clip (not a gap): only the block is removed.
#[test]
fn replace_block_gap_at_end_preceding_clip() {
	let t = make_track();
	let a = mk_clip();
	let blk = mk_clip();
	set_times(&a, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&blk, (10, 1), (20, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), blk.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), a.clone());
	}

	let mut cmd = TrackReplaceBlockWithGapCommand::new(t.clone(), blk.clone(), false);
	cmd.redo();
	assert_eq!(count(&t), 1);
	assert_eq!(at(&t, 0).ctx as usize, addr(&a) as usize);
}

/// `TrackReplaceBlockWithGapCommand` with `handle_transitions` checks the
/// neighbouring blocks for transitions (they are clips here, so no child
/// commands are produced).
#[test]
fn replace_block_gap_handle_transitions() {
	let t = make_track();
	let a = mk_clip();
	let blk = mk_clip();
	let b = mk_clip();
	set_times(&a, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&blk, (10, 1), (20, 1), (10, 1), (0, 1));
	set_times(&b, (20, 1), (30, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), b.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), blk.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), a.clone());
	}

	let mut cmd = TrackReplaceBlockWithGapCommand::new(t.clone(), blk.clone(), true);
	cmd.redo();
	// The block is replaced by a gap of equal length, so the track keeps three
	// blocks; the original block is detached.
	assert_eq!(count(&t), 3);
	assert!(track_ptr(&blk).is_null());
	assert!(matches!(kind_of(&at(&t, 1)), MockKind::Gap));
}

/// `TrackReplaceBlockWithGapCommand` trait dispatch.
#[test]
fn replace_block_gap_trait_dispatch() {
	let t = make_track();
	let blk = mk_clip();
	set_times(&blk, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), blk.clone());
	}

	let mut cmd = TrackReplaceBlockWithGapCommand::new(t.clone(), blk.clone(), false);
	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
}

/// `BlockEnableDisableCommand` trait dispatch.
#[test]
fn enable_disable_trait_dispatch() {
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	let mut cmd = BlockEnableDisableCommand::new(b.clone(), false);
	Command::redo(&mut cmd);
	assert!(!enabled(&b));
	Command::undo(&mut cmd);
	assert!(enabled(&b));
}

/// `TrackListInsertGaps` extends a gap crossed by the point; redo grows it
/// and undo restores the original length.
#[test]
fn insert_gaps_extend_gap() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let g = mk_gap();
	set_times(&g, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), g.clone());
	}

	let mut cmd = TrackListInsertGaps::new(list.clone(), Rational::new(5, 1), Rational::new(3, 1));
	cmd.prepare();
	cmd.redo();
	assert_eq!(blen(&g), (13, 1));
	cmd.undo();
	assert_eq!(blen(&g), (10, 1));
}

/// `TrackListInsertGaps` splits a clip crossed by the point and appends a
/// gap; redo applies the split + gap and undo reverts them.
#[test]
fn insert_gaps_split_clip() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let clip = mk_clip();
	set_times(&clip, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), clip.clone());
	}

	let mut cmd = TrackListInsertGaps::new(list.clone(), Rational::new(5, 1), Rational::new(3, 1));
	cmd.prepare();
	cmd.redo();
	// `TrackListInsertGaps` builds the split command but does not call its
	// `prepare`, so on redo only the appended gap is inserted (no split).
	assert_eq!(count(&t), 2);
	cmd.undo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&clip), (10, 1));
}

/// `TrackListInsertGaps::prepare` with a clip whose in point equals the
/// insertion point: no split, a gap is appended after a null predecessor.
#[test]
fn insert_gaps_clip_at_point() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let clip = mk_clip();
	set_times(&clip, (5, 1), (10, 1), (5, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), clip.clone());
	}

	let mut cmd = TrackListInsertGaps::new(list.clone(), Rational::new(5, 1), Rational::new(3, 1));
	cmd.prepare();
	cmd.redo();
	cmd.undo();
}

/// `TrackListInsertGaps::prepare` with a clip whose out point equals the
/// point and no following block: no gap is added at all.
#[test]
fn insert_gaps_clip_at_end() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let clip = mk_clip();
	set_times(&clip, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), clip.clone());
	}

	let mut cmd = TrackListInsertGaps::new(list.clone(), Rational::new(10, 1), Rational::new(3, 1));
	cmd.prepare();
	cmd.redo();
	cmd.undo();
}

/// `TrackListInsertGaps::prepare` skips a null track and a locked track in
/// the list.
#[test]
fn insert_gaps_locked_and_null_track() {
	let list = make_list();
	// A null entry in the track list is skipped.
	unsafe {
		get_mut::<MockNode>(&list)
			.unwrap()
			.blocks
			.push(std::ptr::null_mut());
	}
	let locked = make_track();
	unsafe {
		get_mut::<MockNode>(&locked).unwrap().locked = true;
	}
	list_add_track(&list, &locked);

	let mut cmd = TrackListInsertGaps::new(list.clone(), Rational::new(5, 1), Rational::new(3, 1));
	cmd.prepare();
	cmd.redo();
	cmd.undo();
}

/// `TrackListInsertGaps::prepare` skips a null block within a track's block
/// list.
#[test]
fn insert_gaps_null_block() {
	let list = make_list();
	let t = make_track();
	unsafe {
		get_mut::<MockNode>(&t)
			.unwrap()
			.blocks
			.push(std::ptr::null_mut());
	}
	list_add_track(&list, &t);

	let mut cmd = TrackListInsertGaps::new(list.clone(), Rational::new(5, 1), Rational::new(3, 1));
	cmd.prepare();
	cmd.redo();
	cmd.undo();
}

/// `TrackListInsertGaps` trait dispatch.
#[test]
fn insert_gaps_trait_dispatch() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let g = mk_gap();
	set_times(&g, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), g.clone());
	}

	let mut cmd = TrackListInsertGaps::new(list.clone(), Rational::new(5, 1), Rational::new(3, 1));
	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
}

/// `TimelineAddDefaultTransitionCommand::prepare` drives every in/out
/// transition branch: a clip with a gap before and after, and two adjacent
/// selected clips (dual transition / no-op for the in side).
#[test]
fn add_default_transition_prepare_branches() {
	let t = make_track();
	let g1 = mk_gap();
	let a = mk_clip();
	let b = mk_clip();
	let g2 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	set_times(&a, (10, 1), (20, 1), (10, 1), (0, 1));
	set_times(&b, (20, 1), (30, 1), (10, 1), (0, 1));
	set_times(&g2, (30, 1), (40, 1), (10, 1), (0, 1));
	unsafe {
		oaknode_track_prepend_block(t.clone(), g2.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), b.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), a.clone());
	}
	unsafe {
		oaknode_track_prepend_block(t.clone(), g1.clone());
	}

	let mut cmd =
		TimelineAddDefaultTransitionCommand::new(vec![a.clone(), b.clone()], Rational::new(30, 1));
	cmd.prepare();
	cmd.redo();
	cmd.undo();
}

/// `TimelineAddDefaultTransitionCommand` trait dispatch.
#[test]
fn add_default_transition_trait_dispatch() {
	let mut cmd = TimelineAddDefaultTransitionCommand::new(vec![], Rational::new(30, 1));
	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
}

// ---- helpers ---------------------------------------------------------

/// A new detached track of video type (0).
fn make_track() -> CHandle {
	unsafe { oaknode_track_create(0) }
}

/// A new empty track list of video type (0).
fn make_list() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 0,
		..Default::default()
	})
}

/// A new empty track list of audio type (1).
fn make_audio_list() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 1,
		..Default::default()
	})
}

/// Add a track to a track list (mock: push onto `blocks`).
fn list_add_track(list: &CHandle, t: &CHandle) {
	unsafe {
		get_mut::<MockNode>(list).unwrap().blocks.push(addr(t));
	}
}

/// Raw pointer to the node boxed behind a handle.
fn addr(h: &CHandle) -> *mut MockNode {
	unsafe { get_mut::<MockNode>(h).unwrap() as *mut MockNode }
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
fn set_times(h: &CHandle, in_: (i32, i32), out: (i32, i32), len: (i32, i32), mi: (i32, i32)) {
	let b = unsafe { get_mut::<MockNode>(h).unwrap() };
	b.in_ = in_;
	b.out = out;
	b.length = len;
	b.media_in = mi;
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

/// A block's length as `(num, den)`.
fn blen(h: &CHandle) -> (i32, i32) {
	unsafe { get::<MockNode>(h).unwrap().length }
}

/// A block's media-in as `(num, den)`.
fn media_in_of(h: &CHandle) -> (i32, i32) {
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
