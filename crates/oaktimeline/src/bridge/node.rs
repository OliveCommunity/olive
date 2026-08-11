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

//! oaknode C ABI imports (`include/node/{block,node,sequence,track,project}.h`).
//! Timeline reads and mutates the block/track/sequence graph exclusively
//! through these frozen functions — it never reimplements node internals.
//!
//! All time quantities cross the C ABI as `int` numerator/denominator pairs
//! (the ABI is `int`; the crate's internal `Rational` is `i64`). Converting
//! between the two is the bridge's responsibility.

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

/// A timeline block (`OakNodeBlock`) — a clip or a gap. Opaque handle.
pub type OakNodeBlock = CHandle;
/// A track (`OakNodeTrack`). Opaque handle.
pub type OakNodeTrack = CHandle;
/// A sequence/timeline (`OakNodeSequence`). Opaque handle.
pub type OakNodeSequence = CHandle;
/// A generic node (`OakNodeNode`). Opaque handle.
pub type OakNodeNode = CHandle;
/// A project (`OakNodeProject`). Opaque handle.
pub type OakNodeProject = CHandle;
/// A track list (`OakNodeTrackList`). Opaque handle.
pub type OakNodeTrackList = CHandle;

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_clip_create() -> OakNodeBlock {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_clip_create()
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_clip_create() }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_gap_create() -> OakNodeBlock {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_gap_create()
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_gap_create() }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_as_node(block: OakNodeBlock) -> OakNodeNode {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_as_node(block)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_as_node(block) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_in(
	block: OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_get_in(block, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_get_in(block, numerator, denominator) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_out(
	block: OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_get_out(block, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_get_out(block, numerator, denominator) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_length(
	block: OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_get_length(block, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_get_length(block, numerator, denominator) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_set_length_and_media_out(
	block: OakNodeBlock,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_set_length_and_media_out(block, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe {
			oaknode::ffi::block::oaknode_block_set_length_and_media_out(
				block,
				numerator,
				denominator,
			)
		}
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_set_length_and_media_in(
	block: OakNodeBlock,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_set_length_and_media_in(block, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe {
			oaknode::ffi::block::oaknode_block_set_length_and_media_in(
				block,
				numerator,
				denominator,
			)
		}
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_set_in(block: OakNodeBlock, numerator: c_int, denominator: c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_set_in(block, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_set_in(block, numerator, denominator) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_enabled(block: OakNodeBlock, enabled: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_get_enabled(block, enabled)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_get_enabled(block, enabled) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_set_enabled(block: OakNodeBlock, enabled: c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_set_enabled(block, enabled)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_set_enabled(block, enabled) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_previous(block: OakNodeBlock, out: *mut OakNodeBlock) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_get_previous(block, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_get_previous(block, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_next(block: OakNodeBlock, out: *mut OakNodeBlock) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_get_next(block, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_get_next(block, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_track(block: OakNodeBlock, out: *mut OakNodeTrack) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_get_track(block, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_get_track(block, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_link(a: OakNodeBlock, b: OakNodeBlock) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_link(a, b)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_link(a, b) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_unlink(a: OakNodeBlock, b: OakNodeBlock) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_unlink(a, b)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_unlink(a, b) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_length(
	track: OakNodeTrack,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_get_length(track, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_get_length(track, numerator, denominator) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_sequence(track: OakNodeTrack, out: *mut OakNodeSequence) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_get_sequence(track, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_get_sequence(track, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_prepend_block(track: OakNodeTrack, block: OakNodeBlock) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_prepend_block(track, block)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_prepend_block(track, block) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_insert_block_after(
	track: OakNodeTrack,
	block: OakNodeBlock,
	before: OakNodeBlock,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_insert_block_after(track, block, before)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_insert_block_after(track, block, before) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_ripple_remove_block(track: OakNodeTrack, block: OakNodeBlock) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_ripple_remove_block(track, block)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_ripple_remove_block(track, block) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_replace_block(
	track: OakNodeTrack,
	old_block: OakNodeBlock,
	new_block: OakNodeBlock,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_replace_block(track, old_block, new_block)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_replace_block(track, old_block, new_block) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_as_node(sequence: OakNodeSequence) -> OakNodeNode {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_sequence_as_node(sequence)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::sequence::oaknode_sequence_as_node(sequence) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_from_node(node: OakNodeNode) -> OakNodeSequence {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_sequence_from_node(node)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::sequence::oaknode_sequence_from_node(node) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_get_project(node: OakNodeNode, out: *mut OakNodeProject) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_node_get_project(node, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::node::oaknode_node_get_project(node, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_output_connection_count(node: OakNodeNode, out_count: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_node_output_connection_count(node, out_count)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::node::oaknode_node_output_connection_count(node, out_count) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_get_markers(node: OakNodeNode, out: *mut crate::handle::CHandle) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_node_get_markers(node, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::node::oaknode_node_get_markers(node, out as *mut std::ffi::c_void) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_get_work_area(node: OakNodeNode, out: *mut crate::handle::CHandle) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_node_get_work_area(node, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe {
			oaknode::ffi::node::oaknode_node_get_work_area(node, out as *mut std::ffi::c_void)
		}
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_add_node(project: OakNodeProject, node: OakNodeNode) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_project_add_node(project, node)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::project::oaknode_project_add_node(project, node) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_remove_node(project: OakNodeProject, node: OakNodeNode) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_project_remove_node(project, node)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::project::oaknode_project_remove_node(project, node) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_command_create_remove_node(node: OakNodeNode) -> crate::handle::CHandle {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_command_create_remove_node(node)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::node::oaknode_command_create_remove_node(node) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_kind(block: OakNodeBlock, out_kind: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_get_kind(block, out_kind)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_get_kind(block, out_kind) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_from_node(node: OakNodeNode) -> OakNodeBlock {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_from_node(node)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_from_node(node) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_are_linked(a: OakNodeBlock, b: OakNodeBlock, linked: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_block_are_linked(a, b, linked)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_block_are_linked(a, b, linked) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_clip_add_cache_passthrough_from(clip: OakNodeBlock, other: OakNodeBlock) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_clip_add_cache_passthrough_from(clip, other)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_clip_add_cache_passthrough_from(clip, other) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_clip_get_media_in(
	clip: OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_clip_get_media_in(clip, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_clip_get_media_in(clip, numerator, denominator) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_clip_set_media_in(
	clip: OakNodeBlock,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_clip_set_media_in(clip, numerator, denominator)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::block::oaknode_clip_set_media_in(clip, numerator, denominator) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_create(kind: c_int) -> OakNodeTrack {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_create(kind)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_create(kind) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_locked(track: OakNodeTrack, locked: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_get_locked(track, locked)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_get_locked(track, locked) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_set_locked(track: OakNodeTrack, locked: c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_set_locked(track, locked)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_set_locked(track, locked) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_block_count(track: OakNodeTrack, count: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_get_block_count(track, count)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_get_block_count(track, count) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_block_at(
	track: OakNodeTrack,
	index: c_int,
	out: *mut OakNodeBlock,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_get_block_at(track, index, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_track_get_block_at(track, index, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_block_containing_time(
	track: OakNodeTrack,
	numerator: c_int,
	denominator: c_int,
	out: *mut OakNodeBlock,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_get_block_containing_time(
			track,
			numerator,
			denominator,
			out,
		)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe {
			oaknode::ffi::track::oaknode_track_get_block_containing_time(
				track,
				numerator,
				denominator,
				out,
			)
		}
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_nearest_block_before_or_at(
	track: OakNodeTrack,
	numerator: c_int,
	denominator: c_int,
	out: *mut OakNodeBlock,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_get_nearest_block_before_or_at(
			track,
			numerator,
			denominator,
			out,
		)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe {
			oaknode::ffi::track::oaknode_track_get_nearest_block_before_or_at(
				track,
				numerator,
				denominator,
				out,
			)
		}
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_nearest_block_after_or_at(
	track: OakNodeTrack,
	numerator: c_int,
	denominator: c_int,
	out: *mut OakNodeBlock,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_track_get_nearest_block_after_or_at(
			track,
			numerator,
			denominator,
			out,
		)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe {
			oaknode::ffi::track::oaknode_track_get_nearest_block_after_or_at(
				track,
				numerator,
				denominator,
				out,
			)
		}
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_type(list: OakNodeTrackList, kind: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_tracklist_get_type(list, kind)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_tracklist_get_type(list, kind) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_track_count(list: OakNodeTrackList, count: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_tracklist_get_track_count(list, count)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_tracklist_get_track_count(list, count) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_track_at(
	list: OakNodeTrackList,
	index: c_int,
	out: *mut OakNodeTrack,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_tracklist_get_track_at(list, index, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_tracklist_get_track_at(list, index, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_array_append(list: OakNodeTrackList) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_tracklist_array_append(list)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_tracklist_array_append(list) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_array_remove_last(list: OakNodeTrackList) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_tracklist_array_remove_last(list)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::track::oaknode_tracklist_array_remove_last(list) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_track_list(
	sequence: OakNodeSequence,
	kind: c_int,
	out: *mut OakNodeTrackList,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_sequence_get_track_list(sequence, kind, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::sequence::oaknode_sequence_get_track_list(sequence, kind, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_all_track_count(sequence: OakNodeSequence, count: *mut c_int) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_sequence_get_all_track_count(sequence, count)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::sequence::oaknode_sequence_get_all_track_count(sequence, count) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_all_track_at(
	sequence: OakNodeSequence,
	index: c_int,
	out: *mut OakNodeTrack,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_sequence_get_all_track_at(sequence, index, out)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::sequence::oaknode_sequence_get_all_track_at(sequence, index, out) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_connect(
	output_node: OakNodeNode,
	input_node: OakNodeNode,
	input_id: *const c_char,
) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_node_connect(output_node, input_node, input_id)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::node::oaknode_node_connect(output_node, input_node, input_id) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_disconnect(input_node: OakNodeNode, input_id: *const c_char) -> c_int {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_node_disconnect(input_node, input_id)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::node::oaknode_node_disconnect(input_node, input_id) }
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_copy_in_graph(
	node: OakNodeNode,
	out_command: *mut crate::handle::CHandle,
) -> OakNodeNode {
	#[cfg(any(test, feature = "test-stubs"))]
	{
		super::teststubs::oaknode_node_copy_in_graph(node, out_command)
	}
	#[cfg(not(any(test, feature = "test-stubs")))]
	{
		unsafe { oaknode::ffi::node::oaknode_node_copy_in_graph(node, out_command) }
	}
}
