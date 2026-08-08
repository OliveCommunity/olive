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

extern "C" {
  /// `oaknode_block_clip_create` — a new clip block (count 1).
  pub fn oaknode_block_clip_create() -> OakNodeBlock;
  /// `oaknode_block_gap_create` — a new gap block (count 1).
  pub fn oaknode_block_gap_create() -> OakNodeBlock;
  /// `oaknode_block_as_node` — the block viewed as a generic node.
  pub fn oaknode_block_as_node(block: OakNodeBlock) -> OakNodeNode;
  /// `oaknode_block_get_in` — the block's in point as an int pair.
  pub fn oaknode_block_get_in(block: OakNodeBlock, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
  /// `oaknode_block_get_out` — the block's out point as an int pair.
  pub fn oaknode_block_get_out(block: OakNodeBlock, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
  /// `oaknode_block_get_length` — the block's length as an int pair.
  pub fn oaknode_block_get_length(block: OakNodeBlock, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
  /// `oaknode_block_set_length_and_media_out` — resize keeping media-in fixed.
  pub fn oaknode_block_set_length_and_media_out(block: OakNodeBlock, numerator: c_int, denominator: c_int) -> c_int;
  /// `oaknode_block_set_length_and_media_in` — resize keeping the out point fixed.
  pub fn oaknode_block_set_length_and_media_in(block: OakNodeBlock, numerator: c_int, denominator: c_int) -> c_int;
  /// `oaknode_block_get_enabled` — the block's enabled flag.
  pub fn oaknode_block_get_enabled(block: OakNodeBlock, enabled: *mut c_int) -> c_int;
  /// `oaknode_block_set_enabled` — set the enabled flag.
  pub fn oaknode_block_set_enabled(block: OakNodeBlock, enabled: c_int) -> c_int;
  /// `oaknode_block_get_previous` — borrowed previous block (empty when none).
  pub fn oaknode_block_get_previous(block: OakNodeBlock, out: *mut OakNodeBlock) -> c_int;
  /// `oaknode_block_get_next` — borrowed next block (empty when none).
  pub fn oaknode_block_get_next(block: OakNodeBlock, out: *mut OakNodeBlock) -> c_int;
  /// `oaknode_block_get_track` — the owning track (empty when detached).
  pub fn oaknode_block_get_track(block: OakNodeBlock, out: *mut OakNodeTrack) -> c_int;
  /// `oaknode_block_link` — link two blocks so they move together.
  pub fn oaknode_block_link(a: OakNodeBlock, b: OakNodeBlock) -> c_int;
  /// `oaknode_block_unlink` — unlink two blocks.
  pub fn oaknode_block_unlink(a: OakNodeBlock, b: OakNodeBlock) -> c_int;
  /// `oaknode_track_get_length` — the track's length as an int pair.
  pub fn oaknode_track_get_length(track: OakNodeTrack, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
  /// `oaknode_track_get_sequence` — the owning sequence (empty when detached).
  pub fn oaknode_track_get_sequence(track: OakNodeTrack, out: *mut OakNodeSequence) -> c_int;
  /// `oaknode_track_prepend_block` — prepend a block.
  pub fn oaknode_track_prepend_block(track: OakNodeTrack, block: OakNodeBlock) -> c_int;
  /// `oaknode_track_insert_block_after` — insert after `before`.
  pub fn oaknode_track_insert_block_after(track: OakNodeTrack, block: OakNodeBlock, before: OakNodeBlock) -> c_int;
  /// `oaknode_track_ripple_remove_block` — remove a block, shifting later ones
  /// earlier; ownership returns to the caller.
  pub fn oaknode_track_ripple_remove_block(track: OakNodeTrack, block: OakNodeBlock) -> c_int;
  /// `oaknode_track_replace_block` — replace `old_block` with `new_block`
  /// (equal lengths required).
  pub fn oaknode_track_replace_block(track: OakNodeTrack, old_block: OakNodeBlock, new_block: OakNodeBlock) -> c_int;
  /// `oaknode_sequence_as_node` — the sequence viewed as a generic node.
  pub fn oaknode_sequence_as_node(sequence: OakNodeSequence) -> OakNodeNode;
  /// `oaknode_sequence_from_node` — a generic node viewed as a sequence.
  pub fn oaknode_sequence_from_node(node: OakNodeNode) -> OakNodeSequence;
  /// `oaknode_node_get_project` — the owning project (empty when detached).
  pub fn oaknode_node_get_project(node: OakNodeNode, out: *mut OakNodeProject) -> c_int;
  /// `oaknode_node_output_connection_count` — number of output connections.
  pub fn oaknode_node_output_connection_count(node: OakNodeNode, out_count: *mut c_int) -> c_int;
  /// `oaknode_node_get_markers` — borrow the node's marker list.
  pub fn oaknode_node_get_markers(node: OakNodeNode, out: *mut crate::handle::CHandle) -> c_int;
  /// `oaknode_node_get_work_area` — borrow the node's work area.
  pub fn oaknode_node_get_work_area(node: OakNodeNode, out: *mut crate::handle::CHandle) -> c_int;
  /// `oaknode_project_add_node` — adopt a node into a project.
  pub fn oaknode_project_add_node(project: OakNodeProject, node: OakNodeNode) -> c_int;
  /// `oaknode_project_remove_node` — remove a node from a project.
  pub fn oaknode_project_remove_node(project: OakNodeProject, node: OakNodeNode) -> c_int;
  /// `oaknode_command_create_remove_node` — a command that removes a node.
  pub fn oaknode_command_create_remove_node(node: OakNodeNode) -> crate::handle::CHandle;

  // --- block.h: querying / building blocks -------------------------------

  /// `oaknode_block_get_kind` — the block's kind (`OAKNODE_BLOCK_*`).
  pub fn oaknode_block_get_kind(block: OakNodeBlock, out_kind: *mut c_int) -> c_int;
  /// `oaknode_block_from_node` — a generic node viewed as a block (empty when not a block).
  pub fn oaknode_block_from_node(node: OakNodeNode) -> OakNodeBlock;
  /// `oaknode_block_are_linked` — whether two blocks are linked (`linked` receives 1/0).
  pub fn oaknode_block_are_linked(a: OakNodeBlock, b: OakNodeBlock, linked: *mut c_int) -> c_int;
  /// `oaknode_clip_add_cache_passthrough_from` — copy render-cache passthroughs from `other`.
  pub fn oaknode_clip_add_cache_passthrough_from(clip: OakNodeBlock, other: OakNodeBlock) -> c_int;
  /// `oaknode_clip_get_media_in` — the clip's media-in as an int pair (clip blocks only).
  pub fn oaknode_clip_get_media_in(clip: OakNodeBlock, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
  /// `oaknode_clip_set_media_in` — set the clip's media-in as an int pair (clip blocks only).
  pub fn oaknode_clip_set_media_in(clip: OakNodeBlock, numerator: c_int, denominator: c_int) -> c_int;

  // --- track.h: tracks and track lists -----------------------------------

  /// `oaknode_track_create` — a new detached track of the given type.
  pub fn oaknode_track_create(kind: c_int) -> OakNodeTrack;
  /// `oaknode_track_get_locked` — whether the track is locked (`locked` receives 1/0).
  pub fn oaknode_track_get_locked(track: OakNodeTrack, locked: *mut c_int) -> c_int;
  /// `oaknode_track_set_locked` — set the locked flag.
  pub fn oaknode_track_set_locked(track: OakNodeTrack, locked: c_int) -> c_int;
  /// `oaknode_track_get_block_count` — number of blocks on the track.
  pub fn oaknode_track_get_block_count(track: OakNodeTrack, count: *mut c_int) -> c_int;
  /// `oaknode_track_get_block_at` — borrowed block at `index`.
  pub fn oaknode_track_get_block_at(track: OakNodeTrack, index: c_int, out: *mut OakNodeBlock) -> c_int;
  /// `oaknode_track_get_block_containing_time` — block strictly containing `time` (in < t < out).
  pub fn oaknode_track_get_block_containing_time(track: OakNodeTrack, numerator: c_int, denominator: c_int, out: *mut OakNodeBlock) -> c_int;
  /// `oaknode_track_get_nearest_block_before_or_at` — block at or before `time`.
  pub fn oaknode_track_get_nearest_block_before_or_at(track: OakNodeTrack, numerator: c_int, denominator: c_int, out: *mut OakNodeBlock) -> c_int;
  /// `oaknode_track_get_nearest_block_after_or_at` — block at or after `time`.
  pub fn oaknode_track_get_nearest_block_after_or_at(track: OakNodeTrack, numerator: c_int, denominator: c_int, out: *mut OakNodeBlock) -> c_int;
  /// `oaknode_tracklist_get_type` — the track list's track type.
  pub fn oaknode_tracklist_get_type(list: OakNodeTrackList, kind: *mut c_int) -> c_int;
  /// `oaknode_tracklist_get_track_count` — number of tracks in the list.
  pub fn oaknode_tracklist_get_track_count(list: OakNodeTrackList, count: *mut c_int) -> c_int;
  /// `oaknode_tracklist_get_track_at` — borrowed track at `index`.
  pub fn oaknode_tracklist_get_track_at(list: OakNodeTrackList, index: c_int, out: *mut OakNodeTrack) -> c_int;
  /// `oaknode_tracklist_array_append` — append a track-array element on the parent sequence.
  pub fn oaknode_tracklist_array_append(list: OakNodeTrackList) -> c_int;
  /// `oaknode_tracklist_array_remove_last` — remove the last track-array element.
  pub fn oaknode_tracklist_array_remove_last(list: OakNodeTrackList) -> c_int;

  // --- sequence.h ---------------------------------------------------------

  /// `oaknode_sequence_get_track_list` — borrowed per-type track list.
  pub fn oaknode_sequence_get_track_list(sequence: OakNodeSequence, kind: c_int, out: *mut OakNodeTrackList) -> c_int;
  /// `oaknode_sequence_get_all_track_count` — total connected tracks across all types.
  pub fn oaknode_sequence_get_all_track_count(sequence: OakNodeSequence, count: *mut c_int) -> c_int;
  /// `oaknode_sequence_get_all_track_at` — borrowed track at `index` (flat cache).
  pub fn oaknode_sequence_get_all_track_at(sequence: OakNodeSequence, index: c_int, out: *mut OakNodeTrack) -> c_int;

  // --- node.h: edges and cloning ------------------------------------------

  /// `oaknode_node_connect` — connect `output_node` to `input_node`'s `input_id` (live).
  pub fn oaknode_node_connect(output_node: OakNodeNode, input_node: OakNodeNode, input_id: *const c_char) -> c_int;
  /// `oaknode_node_disconnect` — remove the edge feeding `input_node`'s `input_id` (live).
  pub fn oaknode_node_disconnect(input_node: OakNodeNode, input_id: *const c_char) -> c_int;
  /// `oaknode_node_copy_in_graph` — clone `node` in its graph; `*out_command` receives an owned undo handle.
  pub fn oaknode_node_copy_in_graph(node: OakNodeNode, out_command: *mut crate::handle::CHandle) -> OakNodeNode;
}
