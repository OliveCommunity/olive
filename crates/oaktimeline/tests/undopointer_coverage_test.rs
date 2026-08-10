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

//! Coverage for `src/undopointer.rs`: the pointer-edit command branches that
//! the other edit-command tests do not reach. These exercise
//! `BlockTrimCommand` (do-nothing / roll-edit flags, TrimIn, removing and
//! resizing the adjacent block), `TrackSlideCommand` (creating/removing the
//! in/out adjacent blocks) and `TrackPlaceBlockCommand` (out-of-range track
//! index, gap insertion past the end of the track, ripple-removal placement),
//! plus the `Command` trait dispatch for each family.
#![cfg(feature = "test-stubs")]

use oakcore_rs::Rational;
use oaktimeline::bridge::node::{
    oaknode_block_clip_create, oaknode_block_gap_create, oaknode_track_create,
    oaknode_track_get_block_at, oaknode_track_get_block_count, oaknode_track_prepend_block,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::common::MovementMode;
use oaktimeline::handle::{CHandle, get, get_mut, make_owned};
use oaktimeline::undocommon::Command;
use oaktimeline::undopointer::{BlockTrimCommand, TrackPlaceBlockCommand, TrackSlideCommand};

// ---- helpers (mirror edit_command_test.rs) --------------------------------

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
fn set_times(h: &CHandle, in_: (i32, i32), out: (i32, i32), len: (i32, i32), mi: (i32, i32)) {
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

/// A block's owning-track raw pointer.
fn track_ptr(h: &CHandle) -> *mut MockNode {
    unsafe { get::<MockNode>(h).unwrap().track }
}

/// A new empty track list of video type (0).
fn make_list() -> CHandle {
    make_owned(MockNode {
        kind: MockKind::TrackList,
        track_type: 0,
        ..Default::default()
    })
}

// ---- BlockTrimCommand -----------------------------------------------------

/// A trim whose new length equals the old length marks the command as
/// "doing nothing"; the setters are idempotent; the Command trait forwards.
#[test]
fn block_trim_doing_nothing_and_setters() {
    let t = make_track();
    let b = mk_clip();
    set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

    let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(10, 1), MovementMode::TrimOut);
    cmd.set_trim_is_a_roll_edit(true);
    cmd.set_remove_zero_length_from_graph(false);
    cmd.prepare();

    // Trait dispatch: doing_nothing_ short-circuits redo and undo.
    Command::redo(&mut cmd);
    Command::undo(&mut cmd);
    assert_eq!(blen(&b), (10, 1));
}

/// A TrimIn that shortens the block with a live (non-gap) previous block
/// creates an adjacent gap and inserts it before the block; undo removes it.
#[test]
fn block_trim_trim_in_creates_adjacent() {
    let t = make_track();
    let prev = mk_clip();
    let b = mk_clip();
    set_times(&prev, (0, 1), (10, 1), (10, 1), (0, 1));
    set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), prev.clone()); }

    let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(6, 1), MovementMode::TrimIn);
    cmd.prepare();
    cmd.redo();
    // The created gap was inserted between `prev` and `b` ([prev, gap, b]).
    assert_eq!(count(&t), 3);
    assert_eq!(blen(&b), (6, 1));
    assert_eq!(at(&t, 0).ctx as usize, addr(&prev) as usize);

    cmd.undo();
    assert_eq!(count(&t), 2);
    assert_eq!(blen(&b), (10, 1));
}

/// A TrimOut that lengthens the block removes a zero-length adjacent gap
/// from the graph; a second redo reuses the stored removal command, and undo
/// restores the gap.
#[test]
fn block_trim_trim_out_removes_adjacent() {
    let t = make_track();
    let b = mk_clip();
    let g = mk_gap();
    set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
    set_times(&g, (10, 1), (15, 1), (5, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), g.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

    let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(15, 1), MovementMode::TrimOut);
    cmd.prepare();
    cmd.redo();
    assert_eq!(count(&t), 1);
    // Reuse of `deleted_adjacent_command_` (idempotent redo).
    cmd.redo();
    assert_eq!(count(&t), 1);

    cmd.undo();
    assert_eq!(count(&t), 2);
    assert_eq!(blen(&b), (10, 1));
}

/// A TrimOut that lengthens the block against a large enough adjacent gap
/// resizes the adjacent instead of removing it (undo restores it).
#[test]
fn block_trim_trim_out_resizes_adjacent() {
    let t = make_track();
    let b = mk_clip();
    let g = mk_gap();
    set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
    set_times(&g, (10, 1), (20, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), g.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

    let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(15, 1), MovementMode::TrimOut);
    cmd.prepare();
    cmd.redo();
    assert_eq!(blen(&g), (5, 1));

    cmd.undo();
    assert_eq!(blen(&g), (10, 1));
}

/// A TrimIn that lengthens the block against a large enough adjacent gap
/// resizes the adjacent on the in side (undo restores it).
#[test]
fn block_trim_trim_in_resizes_adjacent() {
    let t = make_track();
    let g = mk_gap();
    let b = mk_clip();
    set_times(&g, (0, 1), (10, 1), (10, 1), (0, 1));
    set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), g.clone()); }

    let mut cmd = BlockTrimCommand::new(t.clone(), b.clone(), Rational::new(15, 1), MovementMode::TrimIn);
    cmd.prepare();
    cmd.redo();
    assert_eq!(blen(&g), (5, 1));

    cmd.undo();
    assert_eq!(blen(&g), (10, 1));
    assert_eq!(blen(&b), (10, 1));
}

// ---- TrackSlideCommand ----------------------------------------------------

/// A slide with no in/out adjacents creates both as gaps and inserts them on
/// redo; undo removes them.
#[test]
fn track_slide_creates_in_and_out_adjacents() {
    let t = make_track();
    let b = mk_clip();
    let tail = mk_clip();
    set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
    set_times(&tail, (10, 1), (20, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), tail.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }

    let mut cmd = TrackSlideCommand::new(
        t.clone(),
        vec![b.clone()],
        CHandle::null(),
        CHandle::null(),
        Rational::new(5, 1),
    );
    cmd.prepare();
    cmd.redo();
    assert_eq!(count(&t), 4);

    cmd.undo();
    assert_eq!(count(&t), 2);
}

/// A slide that removes the in adjacent (moving back by exactly its length)
/// deletes it on redo and re-inserts it on undo.
#[test]
fn track_slide_removes_in_adjacent() {
    let t = make_track();
    let g_in = mk_gap();
    let b = mk_clip();
    let g_out = mk_gap();
    set_times(&g_in, (0, 1), (5, 1), (5, 1), (0, 1));
    set_times(&b, (5, 1), (15, 1), (10, 1), (0, 1));
    set_times(&g_out, (15, 1), (25, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), g_out.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), g_in.clone()); }

    let mut cmd = TrackSlideCommand::new(
        t.clone(),
        vec![b.clone()],
        g_in.clone(),
        g_out.clone(),
        Rational::new(-5, 1),
    );
    cmd.prepare();
    cmd.redo();
    assert_eq!(count(&t), 2);

    cmd.undo();
    assert_eq!(count(&t), 3);
}

/// A slide that removes the out adjacent (moving forward by exactly its
/// length) deletes it on redo and re-inserts it on undo.
#[test]
fn track_slide_removes_out_adjacent() {
    let t = make_track();
    let g_in = mk_gap();
    let b = mk_clip();
    let g_out = mk_gap();
    set_times(&g_in, (0, 1), (10, 1), (10, 1), (0, 1));
    set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
    set_times(&g_out, (20, 1), (25, 1), (5, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), g_out.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), g_in.clone()); }

    let mut cmd = TrackSlideCommand::new(
        t.clone(),
        vec![b.clone()],
        g_in.clone(),
        g_out.clone(),
        Rational::new(5, 1),
    );
    cmd.prepare();
    cmd.redo();
    assert_eq!(count(&t), 2);

    cmd.undo();
    assert_eq!(count(&t), 3);
}

/// `TrackSlideCommand` dispatches through the `Command` trait.
#[test]
fn track_slide_trait_dispatch() {
    let t = make_track();
    let b = mk_clip();
    let g = mk_gap();
    set_times(&g, (0, 1), (10, 1), (10, 1), (0, 1));
    set_times(&b, (10, 1), (20, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), b.clone()); }
    unsafe { oaknode_track_prepend_block(t.clone(), g.clone()); }

    let mut cmd = TrackSlideCommand::new(
        t.clone(),
        vec![b.clone()],
        g.clone(),
        CHandle::null(),
        Rational::new(5, 1),
    );
    cmd.prepare();
    Command::redo(&mut cmd);
    Command::undo(&mut cmd);
    assert_eq!(blen(&g), (10, 1));
}

// ---- TrackPlaceBlockCommand -----------------------------------------------

/// Placing a block at an out-of-range track index creates the missing tracks
/// on redo and removes them on undo.
#[test]
fn track_place_creates_missing_tracks() {
    let list = make_list();
    let b = mk_clip();
    set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));

    let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 5, b.clone(), Rational::new(0, 1));
    cmd.redo();
    cmd.undo();
}

/// Placing a block past the end of a populated track appends a gap and then
/// the block; undo removes both.
#[test]
fn track_place_appends_gap_past_end() {
    let list = make_list();
    let t = make_track();
    unsafe {
        get_mut::<MockNode>(&list).unwrap().blocks.push(addr(&t));
    }
    let existing = mk_clip();
    set_times(&existing, (0, 1), (10, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), existing.clone()); }

    let b = mk_clip();
    set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));
    let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(15, 1));
    cmd.redo();
    // gap + placed block appended after the existing block.
    assert_eq!(count(&t), 3);
    assert_eq!(track_ptr(&b), addr(&t));

    cmd.undo();
    assert_eq!(count(&t), 1);
    assert!(track_ptr(&b).is_null());
}

/// Placing a block in the middle of a populated track ripple-removes the
/// occupied area then inserts the block; undo restores the area.
#[test]
fn track_place_ripples_area_in_middle() {
    let list = make_list();
    let t = make_track();
    unsafe {
        get_mut::<MockNode>(&list).unwrap().blocks.push(addr(&t));
    }
    let existing = mk_clip();
    set_times(&existing, (0, 1), (10, 1), (10, 1), (0, 1));
    unsafe { oaknode_track_prepend_block(t.clone(), existing.clone()); }

    let b = mk_clip();
    set_times(&b, (0, 1), (5, 1), (5, 1), (0, 1));
    let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(5, 1));
    cmd.redo();
    assert_eq!(track_ptr(&b), addr(&t));

    cmd.undo();
    assert!(track_ptr(&b).is_null());
}

/// `TrackPlaceBlockCommand` dispatches through the `Command` trait.
#[test]
fn track_place_trait_dispatch() {
    let list = make_list();
    let t = make_track();
    unsafe {
        get_mut::<MockNode>(&list).unwrap().blocks.push(addr(&t));
    }
    let b = mk_clip();
    set_times(&b, (0, 1), (10, 1), (10, 1), (0, 1));

    let mut cmd = TrackPlaceBlockCommand::new(list.clone(), 0, b.clone(), Rational::new(0, 1));
    Command::redo(&mut cmd);
    assert_eq!(count(&t), 1);
    Command::undo(&mut cmd);
    assert_eq!(count(&t), 0);
}
