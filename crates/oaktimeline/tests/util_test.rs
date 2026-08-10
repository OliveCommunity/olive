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

//! Coverage for the `util` helpers (`src/util.rs`) and the `error` mapping
//! (`src/error.rs`): identity/ordering helpers, `free_detached_handle`,
//! and the project-graph helpers (`track_project`, `block_add_to_graph`,
//! `block_remove_from_graph`).
#![cfg(feature = "test-stubs")]

use std::cmp::Ordering;

use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::error::{Error, OAKTIMELINE_E_FAILED, OAKTIMELINE_E_INVALID, OAKTIMELINE_E_NOMEM, OAKTIMELINE_E_NOT_FOUND, OAKTIMELINE_E_STATE};
use oaktimeline::handle::{CHandle, get, get_mut, make_owned};
use oaktimeline::util::{
	block_add_to_graph, block_handle_less, block_remove_from_graph, free_detached_handle,
	same_block, same_node, same_track, track_handle_less, track_project,
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

/// `same_track` compares by `ctx` identity.
#[test]
fn same_track_identity() {
	let t = make_track();
	assert!(same_track(t.clone(), t.clone()));
	assert!(same_track(t.clone(), t.clone()));
	let t2 = make_track();
	assert!(!same_track(t.clone(), t2));
}

/// `same_node` compares by `ctx` identity.
#[test]
fn same_node_identity() {
	let a = make_owned(MockNode {
		kind: MockKind::Node,
		..Default::default()
	});
	assert!(same_node(a.clone(), a.clone()));
	let b = make_owned(MockNode {
		kind: MockKind::Node,
		..Default::default()
	});
	assert!(!same_node(a, b));
}

/// `same_block` compares by `ctx` identity.
#[test]
fn same_block_identity() {
	let a = make_owned(MockNode {
		kind: MockKind::Clip,
		..Default::default()
	});
	assert!(same_block(a.clone(), a.clone()));
	let b = make_owned(MockNode {
		kind: MockKind::Clip,
		..Default::default()
	});
	assert!(!same_block(a, b));
}

/// `block_handle_less` / `track_handle_less` give a total order by `ctx`.
#[test]
fn handle_less_total_order() {
	let t1 = make_track();
	let t2 = make_track();

	// Ordering matches the raw `ctx` comparison.
	assert_eq!(
		track_handle_less(t1.clone(), t2.clone()),
		t1.ctx.cmp(&t2.ctx)
	);
	assert_eq!(
		track_handle_less(t2.clone(), t1.clone()),
		t2.ctx.cmp(&t1.ctx)
	);
	assert_eq!(track_handle_less(t1.clone(), t1.clone()), Ordering::Equal);

	assert_eq!(
		block_handle_less(t1.clone(), t2.clone()),
		t1.ctx.cmp(&t2.ctx)
	);
	assert_eq!(block_handle_less(t1.clone(), t1.clone()), Ordering::Equal);
}

/// `free_detached_handle` is a no-op for a null pointer.
#[test]
fn free_detached_handle_null_noop() {
	free_detached_handle(std::ptr::null_mut());
}

/// `free_detached_handle` on an owned handle releases its box and clears
/// it, leaving a null/empty handle.
#[test]
fn free_detached_handle_releases_owned() {
	let t = make_track();
	let mut h = t.clone();
	free_detached_handle(&mut h);
	assert!(h.ctx.is_null());
	assert!(h.addref.is_none());
	assert!(h.release.is_none());
	assert_eq!(h.abi_version, 0);
}

/// `error::Error::code` maps every variant to its frozen code.
#[test]
fn error_code_mapping() {
	assert_eq!(Error::Invalid.code(), OAKTIMELINE_E_INVALID);
	assert_eq!(Error::State.code(), OAKTIMELINE_E_STATE);
	assert_eq!(Error::Failed("x".to_string()).code(), OAKTIMELINE_E_FAILED);
	assert_eq!(Error::NotFound.code(), OAKTIMELINE_E_NOT_FOUND);
	assert_eq!(Error::NoMem.code(), OAKTIMELINE_E_NOMEM);
}

/// `track_project` returns the project owning a track's sequence, via the
/// sequence node view.
#[test]
fn track_project_follows_chain() {
	let proj = make_owned(MockNode {
		kind: MockKind::Project,
		..Default::default()
	});
	let seq = make_owned(MockNode {
		kind: MockKind::Sequence,
		..Default::default()
	});
	let t = make_track();

	// Wire the graph: track -> sequence -> project.
	unsafe {
		get_mut::<MockNode>(&t).unwrap().sequence = get_mut::<MockNode>(&seq).unwrap();
		get_mut::<MockNode>(&seq).unwrap().project = get_mut::<MockNode>(&proj).unwrap();
	}

	let p = track_project(t.clone());
	assert!(!p.is_null());
	assert_eq!(p.ctx as usize, addr(&proj) as usize);
}

/// `track_project` is a null handle when the track has no sequence.
#[test]
fn track_project_empty() {
	let t = make_track();
	assert!(track_project(t.clone()).is_null());
}

/// `block_add_to_graph` / `block_remove_from_graph` adopt and detach a
/// block from the project owning its track.
#[test]
fn block_graph_add_remove() {
	let proj = make_owned(MockNode {
		kind: MockKind::Project,
		..Default::default()
	});
	let seq = make_owned(MockNode {
		kind: MockKind::Sequence,
		..Default::default()
	});
	let t = make_track();
	let b = make_owned(MockNode {
		kind: MockKind::Clip,
		..Default::default()
	});
	unsafe {
		get_mut::<MockNode>(&t).unwrap().sequence = get_mut::<MockNode>(&seq).unwrap();
		get_mut::<MockNode>(&seq).unwrap().project = get_mut::<MockNode>(&proj).unwrap();
	}

	block_add_to_graph(b.clone(), t.clone());
	assert_eq!(
		unsafe { get::<MockNode>(&b) }.unwrap().project as usize,
		addr(&proj) as usize
	);

	block_remove_from_graph(b.clone(), t.clone());
	assert!(unsafe { get::<MockNode>(&b) }.unwrap().project.is_null());
}

/// Graph helpers are no-ops when the track owns no project.
#[test]
fn block_graph_noop_without_project() {
	let t = make_track();
	let b = make_owned(MockNode {
		kind: MockKind::Clip,
		..Default::default()
	});
	block_add_to_graph(b.clone(), t.clone());
	assert!(unsafe { get::<MockNode>(&b) }.unwrap().project.is_null());
	block_remove_from_graph(b.clone(), t.clone());
}
