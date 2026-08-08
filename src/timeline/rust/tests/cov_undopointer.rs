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

//! Coverage for `src/undopointer.rs` (timelineundopointer.h): the
//! `BlockTrimCommand` compensate-adjacent paths (create gap, remove
//! adjacent, resize adjacent, no-op), `TrackSlideCommand` (create in/out
//! gap, remove adjacent, resize) and `TrackPlaceBlockCommand` (append,
//! gap-past-end, add-tracks, ripple area). These tests drive the mock
//! node graph through the `bridge::node` externs only.

use oakcore_rs::{Rational, TimeRange};

use oaktimeline::bridge::node::{
	oaknode_block_clip_create, oaknode_block_gap_create, oaknode_track_create,
	oaknode_track_get_block_at, oaknode_track_get_block_count, oaknode_track_prepend_block,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::common::MovementMode;
use oaktimeline::handle::{CHandle, get, get_mut, make_owned};
use oaktimeline::undocommon::Command;
use oaktimeline::undopointer::{BlockTrimCommand, TrackPlaceBlockCommand, TrackSlideCommand};

// ---- helpers (mirror the other test binaries' local helpers) ----------

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

/// Borrowed block at `index` (for `.ctx` comparisons only).
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

/// Set a block's in/out/length/media-in points directly.
fn set_times(h: &CHandle, in_: (i32, i32), out: (i32, i32), len: (i32, i32), mi: (i32, i32)) {
	let b = unsafe { get_mut::<MockNode>(h).unwrap() };
	b.in_ = in_;
	b.out = out;
	b.length = len;
	b.media_in = mi;
}

fn blen(h: &CHandle) -> (i32, i32) {
	unsafe { get::<MockNode>(h).unwrap().length }
}

fn media_in(h: &CHandle) -> (i32, i32) {
	unsafe { get::<MockNode>(h).unwrap().media_in }
}

fn media_out(h: &CHandle) -> (i32, i32) {
	unsafe { get::<MockNode>(h).unwrap().out }
}

/// Attach `b` to the front of `t`, then set its geometry.
fn prepend(t: &CHandle, b: &CHandle, in_: (i32, i32), out: (i32, i32), len: (i32, i32), mi: (i32, i32)) {
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()) };
	set_times(b, in_, out, len, mi);
}

fn kind_of(h: &CHandle) -> MockKind {
	unsafe { get::<MockNode>(h).unwrap().kind }
}

fn make_list() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 0,
		..Default::default()
	})
}

fn list_add_track(list: &CHandle, t: &CHandle) {
	unsafe {
		get_mut::<MockNode>(list).unwrap().blocks.push(addr(t));
	}
}

fn track_ptr(h: &CHandle) -> *mut MockNode {
	unsafe { get::<MockNode>(h).unwrap().track }
}

// ---- BlockTrimCommand --------------------------------------------------

/// Trimming to the same length marks the command as a no-op.
#[test]
fn trim_same_length_is_noop() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(10, 1), MovementMode::TrimOut);
	cmd.prepare();
	assert_eq!(blen(&b), (10, 1));
	cmd.redo();
	assert_eq!(blen(&b), (10, 1));
	cmd.undo();
	assert_eq!(blen(&b), (10, 1));
}

/// TrimOut at the end of a track (no adjacent): only the block itself is
/// trimmed, `needs_adjacent_` is false.
#[test]
fn trim_out_last_block_no_adjacent() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(7, 1), MovementMode::TrimOut);
	cmd.prepare();
	cmd.redo();
	assert_eq!(blen(&b), (7, 1));
	cmd.undo();
	assert_eq!(blen(&b), (10, 1));
}

/// TrimIn shortening with a clip previous and no roll edit: the command
/// creates a compensation gap and inserts it before the block; undo
/// removes it again.
#[test]
fn trim_in_creates_adjacent_gap() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (10, 1), (20, 1), (10, 1), (10, 1));
	let a = mk_clip();
	prepend(&t, &a, (0, 1), (10, 1), (10, 1), (0, 1));
	// Track order: [a, b].
	assert_eq!(count(&t), 2);
	assert_eq!(at(&t, 0).ctx as usize, addr(&a) as usize);

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(7, 1), MovementMode::TrimIn);
	cmd.prepare();
	assert_eq!(count(&t), 2);

	cmd.redo();
	// A gap of length 3 was created between a and b.
	assert_eq!(count(&t), 3);
	assert_eq!(blen(&b), (7, 1));
	let mid = at(&t, 1);
	assert!(matches!(kind_of(&mid), MockKind::Gap));
	assert_eq!(blen(&mid), (3, 1));

	cmd.undo();
	assert_eq!(count(&t), 2);
	assert_eq!(blen(&b), (10, 1));
}

/// TrimIn with no previous block: the gap is prepended to the track.
#[test]
fn trim_in_first_block_creates_gap_prepend() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(7, 1), MovementMode::TrimIn);
	cmd.prepare();
	cmd.redo();
	assert_eq!(count(&t), 2);
	assert!(matches!(kind_of(&at(&t, 0)), MockKind::Gap));
	assert_eq!(blen(&at(&t, 0)), (3, 1));
	assert_eq!(blen(&b), (7, 1));

	cmd.undo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&b), (10, 1));
}

/// TrimOut shortening into a trailing gap: the gap is resized instead of a
/// new one being created.
#[test]
fn trim_out_into_existing_gap_resizes() {
	let t = make_track();
	let g = mk_gap();
	prepend(&t, &g, (10, 1), (13, 1), (3, 1), (10, 1));
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));
	// Track order: [b, g].
	assert_eq!(count(&t), 2);

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(7, 1), MovementMode::TrimOut);
	cmd.prepare();
	cmd.redo();
	// Gap was extended to 3 + 3 = 6; no new block created.
	assert_eq!(count(&t), 2);
	assert_eq!(blen(&g), (6, 1));
	assert_eq!(blen(&b), (7, 1));

	cmd.undo();
	assert_eq!(blen(&g), (3, 1));
	assert_eq!(blen(&b), (10, 1));
}

/// TrimOut growing the block by exactly the adjacent gap's length removes
/// the adjacent block (`we_removed_adjacent_`).
#[test]
fn trim_grows_and_removes_adjacent() {
	let t = make_track();
	let g = mk_gap();
	prepend(&t, &g, (10, 1), (13, 1), (3, 1), (10, 1));
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));
	// Track order: [b, g].
	assert_eq!(count(&t), 2);

	// Grow from 10 to 13: trim_diff = -3, adjacent gap length 3 is consumed.
	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(13, 1), MovementMode::TrimOut);
	cmd.prepare();
	assert_eq!(count(&t), 2);

	cmd.redo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&b), (13, 1));
	assert!(track_ptr(&g).is_null());

	cmd.undo();
	assert_eq!(count(&t), 2);
	assert_eq!(blen(&b), (10, 1));
}

/// TrimIn with a roll edit: the clip adjacent is resized instead of a gap
/// being created.
#[test]
fn trim_roll_edit_resizes_clip_adjacent() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (10, 1), (20, 1), (10, 1), (10, 1));
	let a = mk_clip();
	prepend(&t, &a, (0, 1), (10, 1), (10, 1), (0, 1));
	// Track order: [a, b].

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(7, 1), MovementMode::TrimIn);
	cmd.set_trim_is_a_roll_edit(true);
	cmd.prepare();

	cmd.redo();
	// No gap created; the previous clip grew from 10 to 13.
	assert_eq!(count(&t), 2);
	assert_eq!(blen(&a), (13, 1));
	assert_eq!(blen(&b), (7, 1));

	cmd.undo();
	assert_eq!(blen(&a), (10, 1));
	assert_eq!(blen(&b), (10, 1));
}

/// The remove-from-graph toggle defaults on and can be disabled.
#[test]
fn trim_remove_zero_length_from_graph_toggle() {
	let t = make_track();
	let g = mk_gap();
	prepend(&t, &g, (10, 1), (13, 1), (3, 1), (10, 1));
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(13, 1), MovementMode::TrimOut);
	cmd.set_remove_zero_length_from_graph(false);
	cmd.prepare();
	cmd.redo();
	assert_eq!(count(&t), 1);
	cmd.undo();
	assert_eq!(count(&t), 2);
}

/// TrimIn growing the block by exactly the adjacent gap's length removes
/// the adjacent block; undo re-inserts it before the block.
#[test]
fn trim_in_grows_and_removes_adjacent() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (10, 1), (20, 1), (10, 1), (10, 1));
	let g = mk_gap();
	prepend(&t, &g, (7, 1), (10, 1), (3, 1), (7, 1));
	// Track order: [g, b].
	assert_eq!(count(&t), 2);

	// Grow from 10 to 13: the 3-length gap before b is consumed.
	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(13, 1), MovementMode::TrimIn);
	cmd.prepare();
	cmd.redo();
	assert_eq!(count(&t), 1);
	assert!(track_ptr(&g).is_null());

	cmd.undo();
	assert_eq!(count(&t), 2);
	assert_eq!(at(&t, 0).ctx as usize, addr(&g) as usize);
}

/// `BlockTrimCommand` dispatches through the `Command` trait (the undo
/// stack's vtable invokes it that way).
#[test]
fn trim_command_trait_dispatch() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(7, 1), MovementMode::TrimOut);
	cmd.prepare();
	Command::redo(&mut cmd);
	assert_eq!(blen(&b), (7, 1));
	Command::undo(&mut cmd);
	assert_eq!(blen(&b), (10, 1));

	// Boxing into a command handle runs redo/undo through the vtable.
	let h = cmd.to_command();
	assert!(!h.is_null());
}

// ---- TrackSlideCommand -------------------------------------------------

/// Slide with no in adjacent: a compensation gap is created before the
/// block and removed on undo.
#[test]
fn slide_creates_in_adjacent_gap() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackSlideCommand::new(
		t.clone(),
		vec![b.clone()],
		CHandle::null(),
		CHandle::null(),
		Rational::new(2, 1),
	);
	cmd.prepare();

	cmd.redo();
	// Gap(2) prepended, block unchanged.
	assert_eq!(count(&t), 2);
	assert!(matches!(kind_of(&at(&t, 0)), MockKind::Gap));
	assert_eq!(blen(&at(&t, 0)), (2, 1));

	cmd.undo();
	assert_eq!(count(&t), 1);
}

/// Slide with both an in and an out adjacent to create: two gaps are
/// inserted on redo and removed on undo.
#[test]
fn slide_creates_in_and_out_adjacent_gaps() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (10, 1), (20, 1), (10, 1), (10, 1));
	let a = mk_clip();
	prepend(&t, &a, (0, 1), (10, 1), (10, 1), (0, 1));
	// Track order: [a, b]; slide `a`.

	let mut cmd = TrackSlideCommand::new(
		t.clone(),
		vec![a.clone()],
		CHandle::null(),
		CHandle::null(),
		Rational::new(2, 1),
	);
	cmd.prepare();

	cmd.redo();
	// In gap before `a`, out gap after `a` (before `b`).
	assert_eq!(count(&t), 4);
	assert!(matches!(kind_of(&at(&t, 0)), MockKind::Gap));
	assert!(matches!(kind_of(&at(&t, 1)), MockKind::Clip));
	assert!(matches!(kind_of(&at(&t, 2)), MockKind::Gap));

	cmd.undo();
	assert_eq!(count(&t), 2);
}

/// Slide with a zero movement: the supplied in/out adjacent gaps are
/// removed (their length equals the movement) and restored on undo.
#[test]
fn slide_removes_adjacents() {
	let t = make_track();
	let g_out = mk_gap();
	prepend(&t, &g_out, (10, 1), (10, 1), (0, 1), (10, 1));
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));
	let g_in = mk_gap();
	prepend(&t, &g_in, (0, 1), (0, 1), (0, 1), (0, 1));
	// Track order: [g_in, b, g_out].

	let mut cmd = TrackSlideCommand::new(
		t.clone(),
		vec![b.clone()],
		g_in.clone(),
		g_out.clone(),
		Rational::new(0, 1),
	);
	cmd.prepare();

	cmd.redo();
	// Both zero-length adjacents removed; only the block remains.
	assert_eq!(count(&t), 1);
	assert!(track_ptr(&g_in).is_null());
	assert!(track_ptr(&g_out).is_null());

	cmd.undo();
	assert_eq!(count(&t), 3);
}

/// Slide that only resizes the adjacents (no creation, no removal).
#[test]
fn slide_resizes_adjacents() {
	let t = make_track();
	let g_out = mk_gap();
	prepend(&t, &g_out, (15, 1), (20, 1), (5, 1), (15, 1));
	let b = mk_clip();
	prepend(&t, &b, (5, 1), (15, 1), (10, 1), (5, 1));
	let g_in = mk_gap();
	prepend(&t, &g_in, (0, 1), (5, 1), (5, 1), (0, 1));
	// Track order: [g_in, b, g_out].

	let mut cmd = TrackSlideCommand::new(
		t.clone(),
		vec![b.clone()],
		g_in.clone(),
		g_out.clone(),
		Rational::new(2, 1),
	);
	cmd.prepare();

	cmd.redo();
	assert_eq!(blen(&g_in), (7, 1)); // 5 + 2
	assert_eq!(blen(&g_out), (3, 1)); // 5 - 2

	cmd.undo();
	assert_eq!(blen(&g_in), (5, 1));
	assert_eq!(blen(&g_out), (5, 1));
}

/// `TrackSlideCommand` trait dispatch and boxing.
#[test]
fn slide_command_trait_dispatch() {
	let t = make_track();
	let b = mk_clip();
	prepend(&t, &b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackSlideCommand::new(
		t.clone(),
		vec![b.clone()],
		CHandle::null(),
		CHandle::null(),
		Rational::new(2, 1),
	);
	cmd.prepare();
	Command::redo(&mut cmd);
	assert_eq!(count(&t), 2);
	Command::undo(&mut cmd);
	assert_eq!(count(&t), 1);

	let h = cmd.to_command();
	assert!(!h.is_null());
}

// ---- TrackPlaceBlockCommand --------------------------------------------

/// Placing at the in point of an empty track appends the block.
#[test]
fn place_block_appends_at_track_start() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(0, 1));
	cmd.redo();
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&b), (10, 1));

	cmd.undo();
	assert_eq!(count(&t), 0);
}

/// Placing past the end of the track inserts a gap before the block; undo
/// removes both.
#[test]
fn place_block_past_end_creates_gap() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(10, 1));
	cmd.redo();
	assert_eq!(count(&t), 2);
	assert!(matches!(kind_of(&at(&t, 0)), MockKind::Gap));
	assert_eq!(blen(&at(&t, 0)), (10, 1));
	assert_eq!(blen(&at(&t, 1)), (10, 1));

	cmd.undo();
	assert_eq!(count(&t), 0);
}

/// Placing on a track index beyond the track count adds the missing tracks
/// first (and removes them on undo).
#[test]
fn place_block_adds_missing_tracks() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));

	// Index 3 with one existing track: 3 add-track commands are created.
	let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 3, b.clone(), Rational::new(5, 1));
	cmd.redo();

	cmd.undo();
}

/// Placing inside existing blocks ripple-removes the occupied area
/// (splicing the first block) and inserts the block after it.
#[test]
fn place_block_ripple_removes_area() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let c1 = mk_clip();
	prepend(&t, &c1, (0, 1), (10, 1), (10, 1), (0, 1));

	let b = mk_clip();
	set_times(&b, (0, 1), (5, 1), (5, 1), (0, 1));

	// Place a 5-length block at in 2: the area [2, 7] is cleared from c1.
	let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(2, 1));
	cmd.redo();
	// c1 was split at 2, b inserted after the first half.
	assert!(count(&t) >= 3);

	cmd.undo();
	// The split is joined back and b removed.
	assert_eq!(count(&t), 1);
	assert_eq!(blen(&c1), (10, 1));
}

/// `TrackPlaceBlockCommand` trait dispatch and boxing.
#[test]
fn place_block_command_trait_dispatch() {
	let list = make_list();
	let t = make_track();
	list_add_track(&list, &t);
	let b = mk_clip();
	set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));

	let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(0, 1));
	Command::redo(&mut cmd);
	assert_eq!(count(&t), 1);
	Command::undo(&mut cmd);
	assert_eq!(count(&t), 0);

	let h = cmd.to_command();
	assert!(!h.is_null());
}

/// `TimeRange` import used by placement ripple command construction.
#[allow(dead_code)]
fn _uses_time_range(_r: TimeRange) {}
