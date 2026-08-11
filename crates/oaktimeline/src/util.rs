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

//! Inline helpers from `src/timeline/src/timelineutil.h`: Rational ↔
//! num/den pairs, value-handle identity, and block/track/project queries.
//!
//! All graph access goes through the oaknode C ABI (`bridge::node`). The
//! C++ `oaknode_c_api::to_native` internals are NOT replicated — handles
//! are opaque here; identity (`same_*`) is decided by `ctx` equality, not
//! by `to_native` (borrowed accessors box a fresh handle per call).

use std::cmp::Ordering;

use oakcore_rs::Rational;

use crate::bridge::node::{
	oaknode_block_get_in, oaknode_block_get_length, oaknode_block_get_next, oaknode_block_get_out,
	oaknode_block_get_previous, oaknode_block_get_track, oaknode_block_set_in,
	oaknode_block_set_length_and_media_in, oaknode_block_set_length_and_media_out,
	oaknode_node_get_project, oaknode_project_add_node, oaknode_project_remove_node,
	oaknode_sequence_as_node, oaknode_track_get_block_at, oaknode_track_get_block_count,
	oaknode_track_get_length, oaknode_track_get_sequence, oaknode_track_insert_block_after,
	oaknode_track_prepend_block,
};
use crate::handle::CHandle;

/// Split a `Rational` into numerator/denominator pairs for the C ABI
/// (timelineutil.h `rat_nd`).
pub fn rat_nd(r: Rational, n: &mut i32, d: &mut i32) {
	*n = r.numerator() as i32;
	*d = r.denominator() as i32;
}

/// Identity of two handles by `ctx` pointer equality.
fn same_handle(a: CHandle, b: CHandle) -> bool {
	a.ctx == b.ctx
}

/// `same_block`: two block handles wrap the same object.
pub fn same_block(a: CHandle, b: CHandle) -> bool {
	same_handle(a, b)
}

/// `same_track`: two track handles wrap the same object.
pub fn same_track(a: CHandle, b: CHandle) -> bool {
	same_handle(a, b)
}

/// `same_node`: two node handles wrap the same object.
pub fn same_node(a: CHandle, b: CHandle) -> bool {
	same_handle(a, b)
}

/// Total order over handles by `ctx`, so maps work across freshly boxed
/// handles of the same origin.
fn handle_less(a: CHandle, b: CHandle) -> Ordering {
	a.ctx.cmp(&b.ctx)
}

/// `BlockHandleLess`: total order over block handles by wrapped native
/// pointer (identity), so maps work across freshly-boxed borrowed handles.
pub fn block_handle_less(a: CHandle, b: CHandle) -> std::cmp::Ordering {
	handle_less(a, b)
}

/// `TrackHandleLess`: total order over track handles by wrapped native
/// pointer.
pub fn track_handle_less(a: CHandle, b: CHandle) -> std::cmp::Ordering {
	handle_less(a, b)
}

/// `free_detached_handle`: re-take ownership of a detached object's handle
/// then release, destroying it (timelineutil.h).
///
/// In this crate's model a detached block/track handle is an owned handle
/// (it owns its box), so releasing it destroys the object. NULL/empty no-op.
pub fn free_detached_handle(h: *mut CHandle) {
	if h.is_null() {
		return;
	}
	// SAFETY: caller passes a valid handle pointer.
	let handle = unsafe { &mut *h };
	if handle.ctx.is_null() {
		return;
	}
	if let Some(release) = handle.release {
		// SAFETY: `release` is the boxed type's release callback.
		unsafe { release(handle.ctx) };
	}
	handle.ctx = std::ptr::null_mut();
	handle.addref = None;
	handle.release = None;
	handle.abi_version = 0;
}

/// Read a block time field as a `Rational`.
fn block_pair(b: CHandle, f: fn(CHandle, *mut i32, *mut i32) -> i32) -> Rational {
	let mut n = 0;
	let mut d = 0;
	// SAFETY: `n`/`d` are valid out pointers.
	let _ = unsafe { f(b, &mut n, &mut d) };
	Rational::new(n as i64, d as i64)
}

/// `block_in`: in point as a `Rational`.
pub fn block_in(b: CHandle) -> Rational {
	block_pair(b, oaknode_block_get_in)
}

/// `block_out`: out point as a `Rational`.
pub fn block_out(b: CHandle) -> Rational {
	block_pair(b, oaknode_block_get_out)
}

/// `block_length`: block length as a `Rational`.
pub fn block_length(b: CHandle) -> Rational {
	block_pair(b, oaknode_block_get_length)
}

/// `block_set_length_and_media_out`.
pub fn block_set_length_and_media_out(b: CHandle, len: Rational) {
	let (mut n, mut d) = (0, 0);
	rat_nd(len, &mut n, &mut d);
	// SAFETY: `n`/`d` are valid ints.
	let _ = unsafe { oaknode_block_set_length_and_media_out(b, n, d) };
}

/// `block_set_length_and_media_in`.
pub fn block_set_length_and_media_in(b: CHandle, len: Rational) {
	let (mut n, mut d) = (0, 0);
	rat_nd(len, &mut n, &mut d);
	// SAFETY: `n`/`d` are valid ints.
	let _ = unsafe { oaknode_block_set_length_and_media_in(b, n, d) };
}

/// `block_set_in`: set the in point, keeping the length (the out follows).
pub fn block_set_in(b: CHandle, in_: Rational) {
	let (mut n, mut d) = (0, 0);
	rat_nd(in_, &mut n, &mut d);
	// SAFETY: `n`/`d` are valid ints.
	let _ = unsafe { oaknode_block_set_in(b, n, d) };
}

/// `track_length`: track length as a `Rational`.
pub fn track_length(t: CHandle) -> Rational {
	let mut n = 0;
	let mut d = 0;
	// SAFETY: `n`/`d` are valid out pointers.
	let _ = unsafe { oaknode_track_get_length(t, &mut n, &mut d) };
	Rational::new(n as i64, d as i64)
}

/// `block_previous`: the block before `b` on its track.
pub fn block_previous(b: CHandle) -> CHandle {
	let mut out = CHandle::null();
	// SAFETY: `out` is a valid out pointer.
	let _ = unsafe { oaknode_block_get_previous(b, &mut out) };
	out
}

/// `block_next`: the block after `b` on its track.
pub fn block_next(b: CHandle) -> CHandle {
	let mut out = CHandle::null();
	// SAFETY: `out` is a valid out pointer.
	let _ = unsafe { oaknode_block_get_next(b, &mut out) };
	out
}

/// `block_track`: the track owning `b`.
pub fn block_track(b: CHandle) -> CHandle {
	let mut out = CHandle::null();
	// SAFETY: `out` is a valid out pointer.
	let _ = unsafe { oaknode_block_get_track(b, &mut out) };
	out
}

/// `track_project`: the project graph owning the track's sequence, via
/// `oaknode_track_get_sequence` + `oaknode_sequence_as_node` +
/// `oaknode_node_get_project`.
pub fn track_project(track: CHandle) -> CHandle {
	let mut sequence = CHandle::null();
	if unsafe { oaknode_track_get_sequence(track, &mut sequence) } != 0 || sequence.is_null() {
		return CHandle::null();
	}
	let mut project = CHandle::null();
	// SAFETY: `project` is a valid out pointer; the sequence node view is valid.
	let _ = unsafe { oaknode_node_get_project(oaknode_sequence_as_node(sequence), &mut project) };
	project
}

/// C++ `oaknode_track_append_block` — not exposed by the bridge, so append is
/// synthesized as insert-after the last block (prepend on an empty track).
pub fn track_append_block(track: CHandle, block: CHandle) {
	let mut count = 0;
	// SAFETY: `count` is a valid out pointer.
	let _ = unsafe { oaknode_track_get_block_count(track.clone(), &mut count) };
	if count > 0 {
		let mut last = CHandle::null();
		// SAFETY: `last` is a valid out pointer.
		let _ = unsafe { oaknode_track_get_block_at(track.clone(), count - 1, &mut last) };
		// SAFETY: `track`/`last` are valid handles.
		let _ = unsafe { oaknode_track_insert_block_after(track, block, last) };
	} else {
		// SAFETY: valid handles.
		let _ = unsafe { oaknode_track_prepend_block(track, block) };
	}
}

/// C++ `oaknode_track_insert_block_before` — not exposed by the bridge, so
/// insert-before is synthesized as insert-after `next`'s predecessor (prepend
/// when `next` is the first block on the track).
pub fn track_insert_block_before(track: CHandle, block: CHandle, next: CHandle) {
	let prev = block_previous(next.clone());
	if prev.is_null() {
		// SAFETY: valid handles.
		let _ = unsafe { oaknode_track_prepend_block(track, block) };
	} else {
		// SAFETY: valid handles.
		let _ = unsafe { oaknode_track_insert_block_after(track, block, prev) };
	}
}

/// `block_add_to_graph`: attach `b` to the project graph owning `track`.
///
/// The block handle is passed through directly (not `oaknode_block_as_node`):
/// `oaknode_project_add_node` rewrites the shared node box it receives
/// (`write_node_ref`) to the new project, so later track operations on `b`
/// (which adopt blocks by comparing the handle's project) see the up-to-date
/// ownership instead of faulting with `E_NOT_FOUND` on a stale scratch id.
pub fn block_add_to_graph(b: CHandle, track: CHandle) {
	let project = track_project(track);
	if !project.is_null() {
		// SAFETY: `project` is a valid handle; `b` is a node/block handle.
		let _ = unsafe { oaknode_project_add_node(project, b) };
	}
}

/// `block_remove_from_graph`: detach `b` from the project graph owning
/// `track`.
///
/// Like [`block_add_to_graph`], `b` is passed through so `write_node_ref`
/// re-homes the shared handle into the scratch project (the caller's handle
/// becomes the owner again, matching the C++ `setParent(&memory_manager_)`).
pub fn block_remove_from_graph(b: CHandle, track: CHandle) {
	let project = track_project(track);
	if !project.is_null() {
		// SAFETY: `project` is a valid handle; `b` is a node/block handle.
		let _ = unsafe { oaknode_project_remove_node(project, b) };
	}
}
