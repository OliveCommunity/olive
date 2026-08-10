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

//! Coverage for the remaining reachable paths of `src/undogeneral.rs`
//! (timelineundogeneral.h): the default-transition `prepare` neighbour
//! classification (null previous, dual out transition) and the
//! `TimelineAddTrackCommand` factories, plus the audio track-list variant.
#![cfg(feature = "test-stubs")]

use oakcore_rs::Rational;

use oaktimeline::bridge::node::{
	oaknode_block_clip_create, oaknode_block_gap_create, oaknode_track_create,
	oaknode_track_get_block_at, oaknode_track_get_block_count, oaknode_track_prepend_block,
	oaknode_tracklist_get_type,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::handle::{CHandle, get, get_mut, make_owned};
use oaktimeline::undocommon::Command;
use oaktimeline::undogeneral::{
	TimelineAddDefaultTransitionCommand, TimelineAddTrackCommand,
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

fn kind_of(h: &CHandle) -> MockKind {
	unsafe { get::<MockNode>(h).unwrap().kind }
}

fn make_list(track_type: i32) -> CHandle {
	make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type,
		..Default::default()
	})
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

// ---- TimelineAddDefaultTransitionCommand -------------------------------

/// `prepare` on adjacent selected clips: the first clip has a null
/// previous (in transition), and the out side sees the selected next clip
/// (dual transition).
#[test]
fn default_transition_prepare_neighbours() {
	let t = make_track();
	let c2 = mk_clip();
	prepend(&t, &c2, (10, 1), (20, 1), (10, 1), (10, 1));
	let c1 = mk_clip();
	prepend(&t, &c1, (0, 1), (10, 1), (10, 1), (0, 1));
	// Track order: [c1, c2].

	let mut cmd = TimelineAddDefaultTransitionCommand::new(
		vec![c1.clone(), c2.clone()],
		Rational::new(1, 1),
	);
	cmd.prepare();
	cmd.redo();
	cmd.undo();
	Command::redo(&mut cmd);
	Command::undo(&mut cmd);
	assert_eq!(count(&t), 2);
	assert_eq!(at(&t, 0).ctx as usize, addr(&c1) as usize);
}

/// `prepare` with a gap before/after a single selected clip creates in and
/// out transitions.
#[test]
fn default_transition_prepare_gap_neighbours() {
	let t = make_track();
	let g2 = mk_gap();
	prepend(&t, &g2, (10, 1), (13, 1), (3, 1), (10, 1));
	let c1 = mk_clip();
	prepend(&t, &c1, (0, 1), (10, 1), (10, 1), (0, 1));
	let g0 = mk_gap();
	prepend(&t, &g0, (0, 1), (0, 1), (0, 1), (0, 1));
	// Track order: [g0, c1, g2].

	let mut cmd = TimelineAddDefaultTransitionCommand::new(vec![c1.clone()], Rational::new(1, 1));
	cmd.prepare();
	cmd.redo();
	cmd.undo();
	assert_eq!(count(&t), 3);
}

/// Boxing the default-transition command into a handle works and keeps
/// the command alive across redo/undo.
#[test]
fn default_transition_boxes_to_chandle() {
	let t = make_track();
	let c1 = mk_clip();
	prepend(&t, &c1, (0, 1), (10, 1), (10, 1), (0, 1));

	let cmd = TimelineAddDefaultTransitionCommand::new(vec![c1.clone()], Rational::new(1, 1));
	let h = cmd.to_command();
	assert!(!h.is_null());
}

// ---- TimelineAddTrackCommand factories ---------------------------------

/// `run_immediately` and `run_immediately_with_automerge` construct, redo
/// and hand back the created track in one step.
#[test]
fn add_track_run_immediately() {
	let list = make_list(0);

	let t = TimelineAddTrackCommand::run_immediately(list.clone());
	assert!(!t.is_null());
	assert!(matches!(kind_of(&t), MockKind::Track));

	let t2 = TimelineAddTrackCommand::run_immediately_with_automerge(list.clone(), true);
	assert!(!t2.is_null());
}

/// An audio track list produces the samples input id and an audio track.
#[test]
fn add_track_audio_list() {
	let list = make_list(1);
	let mut cmd = TimelineAddTrackCommand::new(list.clone());
	cmd.redo();
	let t = cmd.track();
	assert!(!t.is_null());
	let mut kind = 0;
	unsafe { oaknode_tracklist_get_type(list.clone(), &mut kind) };
	assert_eq!(kind, 1);
	cmd.undo();

	// A none-type list uses the empty input id.
	let none = make_list(-1);
	let mut cmd2 = TimelineAddTrackCommand::new(none.clone());
	cmd2.redo();
	cmd2.undo();
}

/// `TimelineAddTrackCommand` trait dispatch.
#[test]
fn add_track_trait_dispatch() {
	let list = make_list(0);
	let mut cmd = TimelineAddTrackCommand::new(list.clone());
	Command::redo(&mut cmd);
	assert!(!cmd.track().is_null());
	Command::undo(&mut cmd);
}
