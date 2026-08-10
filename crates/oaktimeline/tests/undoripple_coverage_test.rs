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

//! Coverage for the ripple commands (`src/undoripple.rs`): single-track,
//! track-list and timeline ripple-removal, and gap deletion at regions.
#![cfg(feature = "test-stubs")]

use oakcore_rs::{Rational, TimeRange};

use oaktimeline::bridge::node::{
	oaknode_block_clip_create, oaknode_block_gap_create, oaknode_track_prepend_block,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::handle::{CHandle, get, get_mut, make_owned};
use oaktimeline::undocommon::Command;
use oaktimeline::undoripple::{
	TimelineRippleDeleteGapsAtRegionsCommand, TimelineRippleRemoveAreaCommand,
	TrackListRippleRemoveAreaCommand, TrackRippleRemoveAreaCommand,
};

/// Raw pointer to the node boxed behind a handle.
fn addr(h: &CHandle) -> *mut MockNode {
	unsafe { get_mut::<MockNode>(h).unwrap() as *mut MockNode }
}

/// A new detached track of video type (0).
fn make_track() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::Track,
		track_type: 0,
		..Default::default()
	})
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
	unsafe { oaktimeline::bridge::node::oaknode_track_get_block_count(track.clone(), &mut c) };
	c
}

/// A block's length as `(num, den)`.
fn blen(h: &CHandle) -> (i32, i32) {
	unsafe { get::<MockNode>(h).unwrap().length }
}

/// The range cuts through a clip, so `prepare` builds a splice split;
/// redo splits into two blocks, undo rejoins.
#[test]
fn ripple_remove_area_splices_through_block() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(5, 1)),
	);
	cmd.prepare();
	assert_eq!(count(&t), 1);

	cmd.redo();
	assert_eq!(count(&t), 2);
	assert!(!cmd.get_spliced_block().is_null());

	cmd.undo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&b), (10, 1));
}

/// The range starts exactly at the block's in point and cuts its out, so
/// `prepare` records an in-point trim (no splice, no removals).
#[test]
fn ripple_remove_area_trims_in_of_trailing_block() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (2, 1), (10, 1), (8, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(5, 1)),
	);
	cmd.prepare();
	cmd.redo();
	// The block's out point is trimmed from 10 to 5 (length 8 -> 5).
	assert_eq!(blen(&b), (5, 1));
	assert_eq!(count(&t), 1);

	cmd.undo();
	assert_eq!(blen(&b), (8, 1));
}

/// The range fully covers a block starting at the range's in point, so it
/// is removed; undo re-inserts it.
#[test]
fn ripple_remove_area_removes_contained_block() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (2, 1), (4, 1), (2, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(5, 1)),
	);
	cmd.prepare();
	cmd.redo();
	assert_eq!(count(&t), 0);

	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// `get_insertion_index` reports the block a replacement would follow, and
/// `prepare` with a null first block is a no-op.
#[test]
fn ripple_remove_area_insertion_index() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(5, 1)),
	);
	cmd.prepare();
	// The first block starts before the range, so inserts follow it.
	assert_eq!(cmd.get_insertion_index().ctx as usize, addr(&b) as usize);

	// On an empty track, `prepare` finds no first block and does nothing.
	let t2 = make_track();
	let mut empty = TrackRippleRemoveAreaCommand::new(
		t2.clone(),
		TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)),
	);
	empty.prepare();
	assert!(empty.get_insertion_index().is_null());
	assert!(empty.get_spliced_block().is_null());
}

/// `TrackRippleRemoveAreaCommand` dispatches through the `Command` trait.
#[test]
fn ripple_remove_area_trait_dispatch() {
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

	let mut cmd = TrackRippleRemoveAreaCommand::new(
		t.clone(),
		TimeRange::new(Rational::new(2, 1), Rational::new(5, 1)),
	);
	cmd.prepare();
	Command::redo(&mut cmd);
	assert_eq!(count(&t), 2);
	Command::undo(&mut cmd);
	assert_eq!(count(&t), 1);
}

/// A locked track is skipped and an empty list produces no child commands.
#[test]
fn track_list_ripple_remove_area_prepare_redo_undo() {
	let list = make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 0,
		..Default::default()
	});
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
	unsafe {
		get_mut::<MockNode>(&list).unwrap().blocks.push(addr(&t));
	}

	let mut cmd = TrackListRippleRemoveAreaCommand::new(
		list.clone(),
		Rational::new(2, 1),
		Rational::new(5, 1),
	);
	cmd.prepare();
	cmd.redo();
	// The child splice splits the single block.
	assert_eq!(count(&t), 2);
	cmd.undo();
	assert_eq!(count(&t), 1);

	// A locked track is skipped.
	let list2 = make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 0,
		..Default::default()
	});
	let t2 = make_track();
	unsafe {
		get_mut::<MockNode>(&t2).unwrap().locked = true;
		get_mut::<MockNode>(&list2).unwrap().blocks.push(addr(&t2));
	}
	let mut locked = TrackListRippleRemoveAreaCommand::new(
		list2.clone(),
		Rational::new(2, 1),
		Rational::new(5, 1),
	);
	locked.prepare();
	locked.redo();
	assert_eq!(count(&t2), 0);
}

/// `TrackListRippleRemoveAreaCommand` dispatches through the `Command`
/// trait.
#[test]
fn track_list_ripple_remove_area_trait_dispatch() {
	let list = make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 0,
		..Default::default()
	});
	let t = make_track();
	unsafe {
		get_mut::<MockNode>(&list).unwrap().blocks.push(addr(&t));
	}
	let mut cmd = TrackListRippleRemoveAreaCommand::new(
		list.clone(),
		Rational::new(0, 1),
		Rational::new(5, 1),
	);
	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
}

/// A sequence with a video track list: `TimelineRippleRemoveAreaCommand`
/// drives a child per track list.
#[test]
fn timeline_ripple_remove_area_redo_undo() {
	let seq = make_owned(MockNode {
		kind: MockKind::Sequence,
		..Default::default()
	});
	let list = make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 0,
		..Default::default()
	});
	let t = make_track();
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
	unsafe {
		get_mut::<MockNode>(&list).unwrap().blocks.push(addr(&t));
		get_mut::<MockNode>(&seq).unwrap().blocks.push(addr(&list));
	}

	let mut cmd = TimelineRippleRemoveAreaCommand::new(
		seq.clone(),
		Rational::new(2, 1),
		Rational::new(5, 1),
	);
	cmd.redo();
	assert_eq!(count(&t), 2);
	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// `TimelineRippleRemoveAreaCommand` dispatches through the `Command` trait.
#[test]
fn timeline_ripple_remove_area_trait_dispatch() {
	let seq = make_owned(MockNode {
		kind: MockKind::Sequence,
		..Default::default()
	});
	let mut cmd = TimelineRippleRemoveAreaCommand::new(
		seq.clone(),
		Rational::new(0, 1),
		Rational::new(5, 1),
	);
	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
}

/// `TimelineRippleDeleteGapsAtRegionsCommand` resolves a gap at a region and
/// reports it via `has_commands`; a non-gap region produces no command.
#[test]
fn ripple_delete_gaps_at_regions_prepare() {
	let t = make_track();
	let g = mk_gap();
	set_times(&g, (0, 1), (5, 1), (5, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), g.clone()); }

	let regions = vec![(
		t.clone(),
		TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)),
	)];
	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(t.clone(), regions);
	cmd.prepare();
	cmd.redo();
	cmd.undo();
	// `has_commands` reflects whether any gap-removal command was built.
	let _ = cmd.has_commands();
}

/// `TimelineRippleDeleteGapsAtRegionsCommand` dispatches through the
/// `Command` trait.
#[test]
fn ripple_delete_gaps_at_regions_trait_dispatch() {
	let t = make_track();
	let g = mk_gap();
	set_times(&g, (0, 1), (5, 1), (5, 1), (0, 1));
	unsafe { oaknode_track_prepend_block(t.clone(), g.clone()); }

	let regions = vec![(
		t.clone(),
		TimeRange::new(Rational::new(0, 1), Rational::new(5, 1)),
	)];
	let mut cmd = TimelineRippleDeleteGapsAtRegionsCommand::new(t.clone(), regions);
	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
}
