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

//! Coverage for `src/undoripple.rs` (timelineundoripple.h): the
//! per-track ripple-remove command (trim/splice/removal paths),
//! the track-list and timeline ripples, the ripple tool (gap creation,
//! gap removal, resize, trim-in/out) and gap deletion at regions.
#![cfg(feature = "test-stubs")]

use oakcore_rs::{Rational, TimeRange};

use oaktimeline::bridge::node::{
	oaknode_block_clip_create, oaknode_block_gap_create, oaknode_track_create,
	oaknode_track_get_block_at, oaknode_track_get_block_count, oaknode_track_prepend_block,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::common::MovementMode;
use oaktimeline::handle::{CHandle, get, get_mut, make_owned};
use oaktimeline::undocommon::Command;
use oaktimeline::undoripple::{
	RippleInfo, TimelineRippleDeleteGapsAtRegionsCommand, TimelineRippleRemoveAreaCommand,
	TrackListRippleRemoveAreaCommand, TrackListRippleToolCommand, TrackRippleRemoveAreaCommand,
};

// ---- helpers -----------------------------------------------------------

fn make_track() -> CHandle {
	unsafe { oaknode_track_create(0) }
}

fn addr(h: &CHandle) -> *mut MockNode {
	unsafe { get_mut::<MockNode>(h).unwrap() as *mut MockNode }
}

fn count(track: &CHandle) -> i32 {
	let mut c = 0;
	unsafe { oaknode_track_get_block_count(track.clone(), &mut c) };
	c
}

fn at(track: &CHandle, idx: i32) -> CHandle {
	let mut o = CHandle::null();
	unsafe { oaknode_track_get_block_at(track.clone(), idx, &mut o) };
	o
}

fn mk_clip() -> CHandle {
	unsafe { oaknode_block_clip_create() }
}

fn mk_gap() -> CHandle {
	unsafe { oaknode_block_gap_create() }
}

fn set_times(h: &CHandle, in_: (i32, i32), out: (i32, i32), len: (i32, i32), mi: (i32, i32)) {
	let b = unsafe { get_mut::<MockNode>(h).unwrap() };
	b.in_ = in_;
	b.out = out;
	b.length = len;
	b.media_in = mi;
}

fn prepend(t: &CHandle, b: &CHandle, in_: (i32, i32), out: (i32, i32), len: (i32, i32), mi: (i32, i32)) {
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()) };
	set_times(b, in_, out, len, mi);
}

fn blen(h: &CHandle) -> (i32, i32) {
	unsafe { get::<MockNode>(h).unwrap().length }
}

fn track_ptr(h: &CHandle) -> *mut MockNode {
	unsafe { get::<MockNode>(h).unwrap().track }
}

fn make_list(track_type: i32) -> CHandle {
	make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type,
		..Default::default()
	})
}

fn list_add_track(list: &CHandle, t: &CHandle) {
	unsafe {
		get_mut::<MockNode>(list).unwrap().blocks.push(addr(t));
	}
}

fn make_sequence() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::Sequence,
		..Default::default()
	})
}

fn seq_add_list(seq: &CHandle, list: &CHandle) {
	unsafe {
		get_mut::<MockNode>(seq).unwrap().blocks.push(addr(list));
	}
}

// ---- TrackRippleRemoveAreaCommand --------------------------------------

/// No block before/at the range in point: prepare no-ops.
#[test]
fn ripple_area_no_block_at_in() {
	let t = make_track();
	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)),
	);
	cmd.prepare();
	cmd.redo();
	cmd.undo();
	assert_eq!(count(&t), 0);
}

/// The first block starts exactly at the range in point: it is in-trimmed
/// and `insert_previous` falls back to its (null) predecessor. A command
/// without a splice reports a null spliced block.
#[test]
fn ripple_area_in_trims_first_block() {
	let t = make_track();
	let b1 = mk_clip();
	prepend(&t, &b1, (2, 1), (10, 1), (8, 1), (2, 1));

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(8, 1)),
	);
	cmd.prepare();
	assert!(cmd.get_insertion_index().is_null());
	assert!(cmd.get_spliced_block().is_null());

	cmd.redo();
	assert_eq!(blen(&b1), (2, 1)); // 8 - (8-2)

	cmd.undo();
	assert_eq!(blen(&b1), (8, 1));
}

/// A clip straddling the whole range is split; the second half is trimmed
/// and exposed through `get_spliced_block`.
#[test]
fn ripple_area_splits_clip() {
	let t = make_track();
	let b1 = mk_clip();
	prepend(&t, &b1, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(8, 1)),
	);
	cmd.prepare();
	assert!(cmd.get_spliced_block().is_null());

	cmd.redo();
	// Split at 2: [b1(2), split(2)]; the split half is the spliced block.
	assert_eq!(count(&t), 2);
	let split = cmd.get_spliced_block();
	assert!(!split.is_null());
	assert_eq!(blen(&b1), (2, 1));

	cmd.undo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&b1), (10, 1));
}

/// A gap straddling the range is not split (default): it is trimmed out
/// instead.
#[test]
fn ripple_area_trims_gap_without_splitting() {
	let t = make_track();
	let g = mk_gap();
	prepend(&t, &g, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(8, 1)),
	);
	cmd.prepare();

	cmd.redo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&g), (4, 1)); // 10 - 6

	cmd.undo();
	assert_eq!(blen(&g), (10, 1));
}

/// A gap straddling the range MAY be split when allowed.
#[test]
fn ripple_area_splits_gap_when_allowed() {
	let t = make_track();
	let g = mk_gap();
	prepend(&t, &g, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(8, 1)),
	);
	cmd.set_allow_splitting_gaps(true);
	cmd.prepare();
	cmd.redo();
	assert_eq!(count(&t), 2);
	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// The first block is out-trimmed but not in-trimmed: it is trimmed at
/// its out edge and nothing follows.
#[test]
fn ripple_area_trims_out_first_block() {
	let t = make_track();
	let b1 = mk_clip();
	prepend(&t, &b1, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(20, 1)),
	);
	cmd.prepare();
	assert!(!cmd.get_insertion_index().is_null());

	cmd.redo();
	assert_eq!(blen(&b1), (2, 1)); // 10 - (10-2)

	cmd.undo();
	assert_eq!(blen(&b1), (10, 1));
}

/// A range covering whole blocks removes them and in-trims the first
/// block that sticks out past the range. A leading gap keeps the first
/// removal's predecessor non-null.
#[test]
fn ripple_area_removes_blocks_and_trims() {
	let t = make_track();
	let b3 = mk_clip();
	prepend(&t, &b3, (8, 1), (12, 1), (4, 1), (8, 1));
	let b2 = mk_clip();
	prepend(&t, &b2, (4, 1), (8, 1), (4, 1), (4, 1));
	let b1 = mk_clip();
	prepend(&t, &b1, (0, 1), (4, 1), (4, 1), (0, 1));
	let g = mk_gap();
	prepend(&t, &g, (-1, 1), (0, 1), (1, 1), (-1, 1));
	// Track order: [g, b1, b2, b3].

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(0, 1), Rational::new(10, 1)),
	);
	cmd.prepare();

	cmd.redo();
	assert_eq!(count(&t), 2);
	assert_eq!(blen(&b3), (2, 1)); // 4 - (10-8)

	cmd.undo();
	assert_eq!(count(&t), 4);
	assert_eq!(blen(&b1), (4, 1));
	assert_eq!(blen(&b2), (4, 1));
	assert_eq!(blen(&b3), (4, 1));
}

/// A range ending exactly at a block's out point stops the removal loop
/// there. A leading gap keeps every removed block's predecessor non-null
/// (the ABI rejects a null `before`).
#[test]
fn ripple_area_stops_at_exact_out() {
	let t = make_track();
	let b2 = mk_clip();
	prepend(&t, &b2, (4, 1), (10, 1), (6, 1), (4, 1));
	let b1 = mk_clip();
	prepend(&t, &b1, (0, 1), (4, 1), (4, 1), (0, 1));
	let g = mk_gap();
	prepend(&t, &g, (-1, 1), (0, 1), (1, 1), (-1, 1));
	// Track order: [g, b1, b2].

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(0, 1), Rational::new(10, 1)),
	);
	cmd.prepare();
	cmd.redo();
	assert_eq!(count(&t), 1);
	cmd.undo();
	assert_eq!(count(&t), 3);
}

/// Trait dispatch on the per-track ripple command.
#[test]
fn ripple_area_trait_dispatch() {
	let t = make_track();
	let b1 = mk_clip();
	prepend(&t, &b1, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(8, 1)),
	);
	cmd.prepare();
	Command::redo(&mut cmd);
	assert_eq!(count(&t), 2);
	Command::undo(&mut cmd);
	assert_eq!(count(&t), 1);
}

// ---- TrackListRippleRemoveAreaCommand ----------------------------------

/// The track-list ripple skips locked tracks and drives its children.
#[test]
fn ripple_list_skips_locked_tracks() {
	let list = make_list(0);
	let locked = make_track();
	unsafe { get_mut::<MockNode>(&locked).unwrap().locked = true };
	list_add_track(&list, &locked);
	let open = make_track();
	list_add_track(&list, &open);
	let b1 = mk_clip();
	prepend(&open, &b1, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackListRippleRemoveAreaCommand::new(
		list.clone(),
		Rational::new(2, 1),
		Rational::new(8, 1),
	);
	cmd.prepare();

	// The unlocked track's block was split by the child.
	cmd.redo();
	assert_eq!(count(&open), 2);
	cmd.undo();
	assert_eq!(count(&open), 1);

	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
	assert_eq!(count(&open), 1);
}

// ---- TimelineRippleRemoveAreaCommand -----------------------------------

/// The timeline ripple walks the sequence's per-type track lists.
#[test]
fn timeline_ripple_remove_area() {
	let seq = make_sequence();
	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t = make_track();
	list_add_track(&list, &t);
	let b1 = mk_clip();
	prepend(&t, &b1, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TimelineRippleRemoveAreaCommand::new(
		seq.clone(),
		Rational::new(2, 1),
		Rational::new(8, 1),
	);
	cmd.redo();
	assert_eq!(count(&t), 2);
	cmd.undo();
	assert_eq!(count(&t), 1);

	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
	assert_eq!(count(&t), 1);
}

// ---- TrackListRippleToolCommand ----------------------------------------

/// Empty info: redo/undo are no-ops. (`RippleInfo` has no public
/// constructor, so the non-empty ripple-tool paths are exercised by the
/// C++ gtest suite through the ABI.)
#[test]
fn ripple_tool_empty_info_noop() {
	let list = make_list(0);
	let mut cmd = TrackListRippleToolCommand::new(
		list.clone(),
		Vec::new(),
		Rational::new(2, 1),
		MovementMode::Move,
	);
	cmd.redo();
	cmd.undo();
	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
}

// ---- TimelineRippleDeleteGapsAtRegionsCommand --------------------------

fn seq_with_video_track(blocks: Vec<CHandle>) -> (CHandle, CHandle) {
	let seq = make_sequence();
	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t = make_track();
	list_add_track(&list, &t);
	for b in blocks {
		unsafe { oaknode_track_prepend_block(t.clone(), b.clone()) };
	}
	(seq, t)
}

/// A single gap region shorter than the gap: the gap is resized.
#[test]
fn ripple_delete_gaps_resizes_single_gap() {
	let g1 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	let (seq, t) = seq_with_video_track(vec![g1.clone()]);

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![(t.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)))],
	);
	cmd.prepare();
	assert!(cmd.has_commands());

	cmd.redo();
	assert_eq!(blen(&g1), (5, 1));

	cmd.undo();
	assert_eq!(blen(&g1), (10, 1));

	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
	assert_eq!(blen(&g1), (10, 1));
}

/// A gap region on an empty track (no block before/at the in point) is
/// skipped (`is_gap` on a null handle).
#[test]
fn ripple_delete_gaps_empty_track_region_skipped() {
	let seq = make_sequence();
	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t1 = make_track();
	list_add_track(&list, &t1);
	let g1 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t1.clone(), g1.clone()) };
	let t_empty = make_track();
	list_add_track(&list, &t_empty);

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![
			(t_empty.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(5, 1))),
			(t1.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(5, 1))),
		],
	);
	cmd.prepare();
	assert!(cmd.has_commands());
	cmd.redo();
	assert_eq!(blen(&g1), (5, 1));
	cmd.undo();
	assert_eq!(blen(&g1), (10, 1));
}

/// Another track whose first block at the earliest point is a clip: the
/// following gap is found for synchronisation.
#[test]
fn ripple_delete_gaps_finds_next_gap_on_other_track() {
	let g1 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	let (seq, t1) = seq_with_video_track(vec![g1.clone()]);

	// Second track: [c(0-5), g2(5-15)].
	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t2 = make_track();
	list_add_track(&list, &t2);
	let g2 = mk_gap();
	set_times(&g2, (5, 1), (15, 1), (10, 1), (5, 1));
	let c = mk_clip();
	set_times(&c, (0, 1), (5, 1), (5, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t2.clone(), g2.clone()) };
	unsafe { oaknode_track_prepend_block(t2.clone(), c.clone()) };

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![(t1.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)))],
	);
	cmd.prepare();
	assert!(cmd.has_commands());

	cmd.redo();
	assert_eq!(blen(&g1), (5, 1));
	assert_eq!(blen(&g2), (5, 1));

	cmd.undo();
	assert_eq!(blen(&g1), (10, 1));
	assert_eq!(blen(&g2), (10, 1));
}

/// A clip followed by another clip (no gap) zeroes the ripple length and
/// produces no commands.
#[test]
fn ripple_delete_gaps_clip_no_gap_breaks() {
	let g1 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	let (seq, t1) = seq_with_video_track(vec![g1.clone()]);

	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t2 = make_track();
	list_add_track(&list, &t2);
	let c2 = mk_clip();
	set_times(&c2, (5, 1), (15, 1), (10, 1), (5, 1));
	let c1 = mk_clip();
	set_times(&c1, (0, 1), (5, 1), (5, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t2.clone(), c2.clone()) };
	unsafe { oaknode_track_prepend_block(t2.clone(), c1.clone()) };

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![(t1.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)))],
	);
	cmd.prepare();
	assert!(!cmd.has_commands());
	cmd.redo();
	cmd.undo();
}

/// A clip strictly after the earliest point whose predecessor is a gap:
/// the gap is found via the previous block and, when its length equals
/// the ripple length, removed outright.
#[test]
fn ripple_delete_gaps_finds_previous_gap_and_removes() {
	let g1 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	let (seq, t1) = seq_with_video_track(vec![g1.clone()]);

	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t2 = make_track();
	list_add_track(&list, &t2);
	let c = mk_clip();
	set_times(&c, (3, 1), (5, 1), (2, 1), (3, 1));
	let g2 = mk_gap();
	set_times(&g2, (-2, 1), (3, 1), (5, 1), (-2, 1));
	let lead = mk_gap();
	set_times(&lead, (-5, 1), (-2, 1), (3, 1), (-5, 1));
	unsafe { oaknode_track_prepend_block(t2.clone(), c.clone()) };
	unsafe { oaknode_track_prepend_block(t2.clone(), g2.clone()) };
	unsafe { oaknode_track_prepend_block(t2.clone(), lead.clone()) };
	// Track order: [lead, g2, c].

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![(t1.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)))],
	);
	cmd.prepare();
	assert!(cmd.has_commands());

	cmd.redo();
	assert_eq!(blen(&g1), (5, 1));
	// g2 was removed outright (its length equals the ripple length).
	assert_eq!(count(&t2), 2);
	assert!(track_ptr(&g2).is_null());

	cmd.undo();
	assert_eq!(blen(&g1), (10, 1));
	assert_eq!(count(&t2), 3);
	assert_eq!(blen(&g2), (5, 1));
}

/// A clip strictly after the earliest point whose predecessor is not a
/// gap zeroes the ripple length and produces no commands.
#[test]
fn ripple_delete_gaps_prev_not_gap_breaks() {
	let g1 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	let (seq, t1) = seq_with_video_track(vec![g1.clone()]);

	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t2 = make_track();
	list_add_track(&list, &t2);
	let c = mk_clip();
	set_times(&c, (3, 1), (5, 1), (2, 1), (3, 1));
	unsafe { oaknode_track_prepend_block(t2.clone(), c.clone()) };

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![(t1.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)))],
	);
	cmd.prepare();
	assert!(!cmd.has_commands());
	cmd.redo();
	cmd.undo();
}

/// A locked track is skipped when synchronising gaps.
#[test]
fn ripple_delete_gaps_skips_locked_tracks() {
	let g1 = mk_gap();
	set_times(&g1, (0, 1), (10, 1), (10, 1), (0, 1));
	let (seq, t1) = seq_with_video_track(vec![g1.clone()]);

	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t_locked = make_track();
	unsafe { get_mut::<MockNode>(&t_locked).unwrap().locked = true };
	list_add_track(&list, &t_locked);
	let g3 = mk_gap();
	set_times(&g3, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t_locked.clone(), g3.clone()) };

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![(t1.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)))],
	);
	cmd.prepare();
	assert!(cmd.has_commands());
	cmd.redo();
	assert_eq!(blen(&g1), (5, 1));
	// The locked track's gap was untouched.
	assert_eq!(blen(&g3), (10, 1));
	cmd.undo();
}

/// Two regions on the same track exercise the insertion-sort ordering.
#[test]
fn ripple_delete_gaps_sorts_regions_on_track() {
	let seq = make_sequence();
	let list = make_list(0);
	seq_add_list(&seq, &list);
	let t = make_track();
	list_add_track(&list, &t);
	let gb = mk_gap();
	set_times(&gb, (5, 1), (8, 1), (3, 1), (5, 1));
	let ga = mk_gap();
	set_times(&ga, (0, 1), (3, 1), (3, 1), (0, 1));
	let lead = mk_gap();
	set_times(&lead, (-3, 1), (0, 1), (3, 1), (-3, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), gb.clone()) };
	unsafe { oaknode_track_prepend_block(t.clone(), ga.clone()) };
	unsafe { oaknode_track_prepend_block(t.clone(), lead.clone()) };
	// Track order: [lead, ga, gb].

	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(
		seq.clone(),
		vec![
			(t.clone(), TimeRange::new(Rational::new(0, 1), Rational::new(3, 1))),
			(t.clone(), TimeRange::new(Rational::new(5, 1), Rational::new(8, 1))),
		],
	);
	cmd.prepare();
	assert!(cmd.has_commands());

	cmd.redo();
	assert_eq!(count(&t), 1);
	cmd.undo();
	assert_eq!(count(&t), 3);
	assert_eq!(blen(&ga), (3, 1));
	assert_eq!(blen(&gb), (3, 1));
}
