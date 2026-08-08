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

//! oaknode C ABI imports. The task module drives node-graph work through
//! `include/node/project.h` (project shell), `include/node/folder.h`,
//! `include/node/footage.h`, `include/node/sequence.h`,
//! `include/node/colormanager.h` and `include/node/serializer.h`. Signatures
//! mirror the headers verbatim; node handles are `CHandle` values whose
//! underlying objects live inside the node library.

use std::ffi::{c_char, c_int, c_void};

use crate::bridge::common::OakVideoParams;
use crate::bridge::render::OakCancelAtom;
use crate::handle::CHandle;

/// `OakNodeProject` (`include/node/project.h`).
pub type OakNodeProject = CHandle;
/// `OakNodeNode` (`include/node/node.h`).
pub type OakNodeNode = CHandle;
/// `OakNodeFolder` (`include/node/folder.h`).
pub type OakNodeFolder = CHandle;
/// `OakNodeFootage` (`include/node/footage.h`).
pub type OakNodeFootage = CHandle;
/// `OakNodeSequence` (`include/node/sequence.h`).
pub type OakNodeSequence = CHandle;
/// `OakNodeTrack` (`include/node/track.h`).
pub type OakNodeTrack = CHandle;
/// `OakNodeTrackList` (`include/node/track.h`).
pub type OakNodeTrackList = CHandle;
/// `OakNodeBlock` (`include/node/block.h`).
pub type OakNodeBlock = CHandle;
/// `OakNodeColorManager` (`include/node/colormanager.h`).
pub type OakNodeColorManager = CHandle;
/// `OakNodeSerializerSaveData` (`include/node/serializer.h`).
pub type OakNodeSerializerSaveData = CHandle;
/// `OakNodeSerializerLoadData` (`include/node/serializer.h`).
pub type OakNodeSerializerLoadData = CHandle;

/// `OAKNODE_SEQUENCE_TEXTURE_INPUT` (`include/node/sequence.h`).
pub const OAKNODE_SEQUENCE_TEXTURE_INPUT: &str = "tex_in";
/// `OAKNODE_SEQUENCE_SAMPLES_INPUT` (`include/node/sequence.h`).
pub const OAKNODE_SEQUENCE_SAMPLES_INPUT: &str = "samples_in";
/// `OAKNODE_TRACK_TYPE_VIDEO` (`include/node/track.h`).
pub const OAKNODE_TRACK_TYPE_VIDEO: c_int = 0;
/// `OAKNODE_TRACK_TYPE_AUDIO` (`include/node/track.h`).
pub const OAKNODE_TRACK_TYPE_AUDIO: c_int = 1;
/// `OAKNODE_TRACK_TYPE_NONE` (`include/node/track.h`).
pub const OAKNODE_TRACK_TYPE_NONE: c_int = -1;
/// `OAKNODE_BLOCK_OTHER` (`include/node/block.h`).
pub const OAKNODE_BLOCK_OTHER: c_int = 0;
/// `OAKNODE_BLOCK_CLIP` (`include/node/block.h`).
pub const OAKNODE_BLOCK_CLIP: c_int = 1;
/// `OAKNODE_BLOCK_GAP` (`include/node/block.h`).
pub const OAKNODE_BLOCK_GAP: c_int = 2;
/// `OAKNODE_BLOCK_TRANSITION` (`include/node/block.h`).
pub const OAKNODE_BLOCK_TRANSITION: c_int = 3;
/// `OAKNODE_TRANSITION_CROSS_DISSOLVE` (`include/node/block.h`).
pub const OAKNODE_TRANSITION_CROSS_DISSOLVE: c_int = 0;
/// `OAKNODE_TRANSITION_OUT_BLOCK_INPUT` (`include/node/block.h`).
pub const OAKNODE_TRANSITION_OUT_BLOCK_INPUT: &str = "out_block_in";
/// `OAKNODE_TRANSITION_IN_BLOCK_INPUT` (`include/node/block.h`).
pub const OAKNODE_TRANSITION_IN_BLOCK_INPUT: &str = "in_block_in";

/// Node type ids used by the OTIO tasks
/// (`src/task/src/project/loadotio/loadotio.cpp`).
/// `k_sequence_id` — the sequence node type.
pub const OAKNODE_TYPE_SEQUENCE: &str = "org.olivevideoeditor.Olive.sequence";
/// `k_transform_id` — the transform node type.
pub const OAKNODE_TYPE_TRANSFORM: &str = "org.olivevideoeditor.Olive.transform";
/// `k_volume_id` — the volume node type.
pub const OAKNODE_TYPE_VOLUME: &str = "org.olivevideoeditor.Olive.volume";

/// C string helper: build a NUL-terminated copy of a Rust string for the
/// C ABI. The buffer is leaked for the duration of the process; callers
/// pass short-lived literals.
pub fn cstr(s: &str) -> *const c_char {
	let mut bytes = s.as_bytes().to_vec();
	bytes.push(0);
	bytes.leak().as_ptr() as *const c_char
}

extern "C" {
	// --- node.h ---
	/// `oaknode_node_get_label` (two-stage string getter).
	pub fn oaknode_node_get_label(node: OakNodeNode, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_node_set_label`.
	pub fn oaknode_node_set_label(node: OakNodeNode, label: *const c_char) -> c_int;
	/// `oaknode_node_get_id` (two-stage string getter).
	pub fn oaknode_node_get_id(node: OakNodeNode, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_node_get_project` (borrowed project handle out).
	pub fn oaknode_node_get_project(node: OakNodeNode, out: *mut OakNodeProject) -> c_int;
	/// `oaknode_node_input_get_connected_node` (borrowed handle out).
	pub fn oaknode_node_input_get_connected_node(
		node: OakNodeNode,
		input_id: *const c_char,
		out_node: *mut OakNodeNode,
	) -> c_int;
	/// `oaknode_node_set_context_position` (live, non-undoable).
	pub fn oaknode_node_set_context_position(
		node: OakNodeNode,
		context: OakNodeNode,
		x: f64,
		y: f64,
		expanded: c_int,
	) -> c_int;
	/// `oaknode_node_find_input_footage` (borrowed handle out).
	pub fn oaknode_node_find_input_footage(node: OakNodeNode, out: *mut OakNodeFootage) -> c_int;
	/// `oaknode_node_get_input_string` (two-stage string getter).
	pub fn oaknode_node_get_input_string(
		node: OakNodeNode,
		input_id: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oaknode_node_create_copy` — orphaned deep copy of a node.
	pub fn oaknode_node_create_copy(node: OakNodeNode) -> OakNodeNode;
	/// `oaknode_node_copy_inputs`.
	pub fn oaknode_node_copy_inputs(dst: OakNodeNode, src: OakNodeNode, include_connections: c_int) -> c_int;
	/// `oaknode_node_connect`.
	pub fn oaknode_node_connect(output_node: OakNodeNode, input_node: OakNodeNode, input_id: *const c_char) -> c_int;
	/// `oaknode_node_set_value_hint_track`.
	pub fn oaknode_node_set_value_hint_track(
		node: OakNodeNode,
		input_id: *const c_char,
		track_type: c_int,
		track_index: c_int,
	) -> c_int;
	/// `oaknode_node_get_video_frame_cache` (borrowed cache out).
	pub fn oaknode_node_get_video_frame_cache(
		node: OakNodeNode,
		out: *mut crate::bridge::render::OakRenderCache,
	) -> c_int;
	/// `oaknode_node_free`.
	pub fn oaknode_node_free(node: *mut OakNodeNode);
	/// `oaknode_command_create_remove_node` — undoable node removal.
	pub fn oaknode_command_create_remove_node(node: OakNodeNode) -> crate::bridge::undo::OakUndoCommand;

	// --- factory.h ---
	/// `oaknode_factory_create_from_id`.
	pub fn oaknode_factory_create_from_id(type_id: *const c_char) -> OakNodeNode;

	// --- node.h (viewer) ---
	/// `oaknode_viewer_set_video_params`.
	pub fn oaknode_viewer_set_video_params(viewer: OakNodeNode, params: *const OakVideoParams) -> c_int;
	/// `oaknode_viewer_set_audio_params`.
	pub fn oaknode_viewer_set_audio_params(viewer: OakNodeNode, params: *const std::ffi::c_void) -> c_int;

	// --- block.h ---
	/// `oaknode_block_clip_create`.
	pub fn oaknode_block_clip_create() -> OakNodeBlock;
	/// `oaknode_block_gap_create`.
	pub fn oaknode_block_gap_create() -> OakNodeBlock;
	/// `oaknode_block_transition_create` (kind: `OakNodeTransitionKind`).
	pub fn oaknode_block_transition_create(kind: c_int) -> OakNodeBlock;
	/// `oaknode_block_as_node` (borrowed cast).
	pub fn oaknode_block_as_node(block: OakNodeBlock) -> OakNodeNode;
	/// `oaknode_block_get_kind`.
	pub fn oaknode_block_get_kind(block: OakNodeBlock, out_kind: *mut c_int) -> c_int;
	/// `oaknode_block_get_in`.
	pub fn oaknode_block_get_in(block: OakNodeBlock, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_block_get_length`.
	pub fn oaknode_block_get_length(block: OakNodeBlock, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_block_set_length_and_media_out`.
	pub fn oaknode_block_set_length_and_media_out(block: OakNodeBlock, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_clip_set_media_in`.
	pub fn oaknode_clip_set_media_in(clip: OakNodeBlock, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_transition_get_in_offset`.
	pub fn oaknode_transition_get_in_offset(transition: OakNodeBlock, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_transition_get_out_offset`.
	pub fn oaknode_transition_get_out_offset(transition: OakNodeBlock, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_transition_set_offsets_and_length`.
	pub fn oaknode_transition_set_offsets_and_length(
		transition: OakNodeBlock,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> c_int;

	// --- project.h ---
	/// `oaknode_project_init`.
	pub fn oaknode_project_init() -> OakNodeProject;
	/// `oaknode_project_free`.
	pub fn oaknode_project_free(project: *mut OakNodeProject);
	/// `oaknode_project_initialize`.
	pub fn oaknode_project_initialize(project: OakNodeProject) -> c_int;
	/// `oaknode_project_clear`.
	pub fn oaknode_project_clear(project: OakNodeProject) -> c_int;
	/// `oaknode_project_root`.
	pub fn oaknode_project_root(project: OakNodeProject) -> OakNodeFolder;
	/// `oaknode_project_name`.
	pub fn oaknode_project_name(project: OakNodeProject, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_filename`.
	pub fn oaknode_project_filename(project: OakNodeProject, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_pretty_filename`.
	pub fn oaknode_project_pretty_filename(project: OakNodeProject, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_set_filename`.
	pub fn oaknode_project_set_filename(project: OakNodeProject, filename: *const c_char) -> c_int;
	/// `oaknode_project_is_modified`.
	pub fn oaknode_project_is_modified(project: OakNodeProject) -> c_int;
	/// `oaknode_project_set_modified`.
	pub fn oaknode_project_set_modified(project: OakNodeProject, modified: c_int) -> c_int;
	/// `oaknode_project_is_new`.
	pub fn oaknode_project_is_new(project: OakNodeProject) -> c_int;
	/// `oaknode_project_cache_path`.
	pub fn oaknode_project_cache_path(project: OakNodeProject, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_copy_settings`.
	pub fn oaknode_project_copy_settings(dst: OakNodeProject, src: OakNodeProject) -> c_int;
	/// `oaknode_project_get_cache_location_setting`.
	pub fn oaknode_project_get_cache_location_setting(project: OakNodeProject) -> c_int;
	/// `oaknode_project_set_cache_location_setting`.
	pub fn oaknode_project_set_cache_location_setting(project: OakNodeProject, setting: c_int) -> c_int;
	/// `oaknode_project_get_custom_cache_path`.
	pub fn oaknode_project_get_custom_cache_path(project: OakNodeProject, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_set_custom_cache_path`.
	pub fn oaknode_project_set_custom_cache_path(project: OakNodeProject, path: *const c_char) -> c_int;
	/// `oaknode_project_get_uuid`.
	pub fn oaknode_project_get_uuid(project: OakNodeProject, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_add_node`.
	pub fn oaknode_project_add_node(project: OakNodeProject, node: OakNodeNode) -> c_int;
	/// `oaknode_project_remove_node`.
	pub fn oaknode_project_remove_node(project: OakNodeProject, node: OakNodeNode) -> c_int;
	/// `oaknode_project_node_count`.
	pub fn oaknode_project_node_count(project: OakNodeProject) -> c_int;
	/// `oaknode_project_node_at`.
	pub fn oaknode_project_node_at(project: OakNodeProject, index: c_int) -> OakNodeNode;

	// --- folder.h ---
	/// `oaknode_folder_create`.
	pub fn oaknode_folder_create(project: OakNodeProject) -> OakNodeFolder;
	/// `oaknode_folder_child_count`.
	pub fn oaknode_folder_child_count(folder: OakNodeFolder) -> c_int;
	/// `oaknode_folder_child_at`.
	pub fn oaknode_folder_child_at(folder: OakNodeFolder, index: c_int) -> OakNodeNode;
	/// `oaknode_folder_add_child`.
	pub fn oaknode_folder_add_child(folder: OakNodeFolder, child: OakNodeNode) -> c_int;
	/// `oaknode_folder_as_node`.
	pub fn oaknode_folder_as_node(folder: OakNodeFolder) -> OakNodeNode;
	/// `oaknode_command_create_folder_add_child`.
	pub fn oaknode_command_create_folder_add_child(folder: OakNodeFolder, child: OakNodeNode) -> crate::bridge::undo::OakUndoCommand;
	/// `oaknode_folder_remove_child`.
	pub fn oaknode_folder_remove_child(folder: OakNodeFolder, child: OakNodeNode) -> c_int;
	/// `oaknode_folder_move_children`.
	pub fn oaknode_folder_move_children(nodes: *const OakNodeNode, count: c_int, dest_folder: OakNodeFolder) -> c_int;
	/// `oaknode_folder_has_child_recursive`.
	pub fn oaknode_folder_has_child_recursive(folder: OakNodeFolder, child: OakNodeNode) -> c_int;
	/// `oaknode_folder_index_of_child`.
	pub fn oaknode_folder_index_of_child(folder: OakNodeFolder, child: OakNodeNode) -> c_int;
	/// `oaknode_folder_parent_of`.
	pub fn oaknode_folder_parent_of(node: OakNodeNode) -> OakNodeFolder;

	// --- footage.h ---
	/// `oaknode_footage_create`.
	pub fn oaknode_footage_create(project: OakNodeProject, filename: *const c_char) -> OakNodeFootage;
	/// `oaknode_footage_as_node`.
	pub fn oaknode_footage_as_node(footage: OakNodeFootage) -> OakNodeNode;
	/// `oaknode_footage_filename`.
	pub fn oaknode_footage_filename(footage: OakNodeFootage, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_footage_set_filename`.
	pub fn oaknode_footage_set_filename(footage: OakNodeFootage, filename: *const c_char) -> c_int;
	/// `oaknode_footage_is_valid`.
	pub fn oaknode_footage_is_valid(footage: OakNodeFootage) -> c_int;
	/// `oaknode_footage_timestamp`.
	pub fn oaknode_footage_timestamp(footage: OakNodeFootage, out_timestamp: *mut i64) -> c_int;
	/// `oaknode_footage_set_timestamp`.
	pub fn oaknode_footage_set_timestamp(footage: OakNodeFootage, timestamp: i64) -> c_int;
	/// `oaknode_footage_decoder`.
	pub fn oaknode_footage_decoder(footage: OakNodeFootage, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_footage_total_stream_count`.
	pub fn oaknode_footage_total_stream_count(footage: OakNodeFootage) -> c_int;
	/// `oaknode_footage_video_stream_count`.
	pub fn oaknode_footage_video_stream_count(footage: OakNodeFootage) -> c_int;
	/// `oaknode_footage_audio_stream_count`.
	pub fn oaknode_footage_audio_stream_count(footage: OakNodeFootage) -> c_int;
	/// `oaknode_footage_subtitle_stream_count`.
	pub fn oaknode_footage_subtitle_stream_count(footage: OakNodeFootage) -> c_int;
	/// `oaknode_footage_duration`.
	pub fn oaknode_footage_duration(footage: OakNodeFootage, out_numerator: *mut c_int, out_denominator: *mut c_int) -> c_int;
	/// `oaknode_footage_proxy_enabled`.
	pub fn oaknode_footage_proxy_enabled(footage: OakNodeFootage) -> c_int;
	/// `oaknode_footage_set_proxy_enabled`.
	pub fn oaknode_footage_set_proxy_enabled(footage: OakNodeFootage, enabled: c_int) -> c_int;
	/// `oaknode_footage_proxy_path`.
	pub fn oaknode_footage_proxy_path(footage: OakNodeFootage, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_footage_proxy_state`.
	pub fn oaknode_footage_proxy_state(footage: OakNodeFootage) -> c_int;
	/// `oaknode_footage_set_proxy`.
	pub fn oaknode_footage_set_proxy(footage: OakNodeFootage, path: *const c_char, state: c_int, video_stream_index: c_int, preset_version: c_int, enabled: c_int) -> c_int;
	/// `oaknode_footage_clear_proxy`.
	pub fn oaknode_footage_clear_proxy(footage: OakNodeFootage) -> c_int;
	/// `oaknode_footage_get_video_params`.
	pub fn oaknode_footage_get_video_params(footage: OakNodeFootage, index: c_int, out: *mut OakVideoParams) -> c_int;
	/// `oaknode_footage_set_video_params`.
	pub fn oaknode_footage_set_video_params(footage: OakNodeFootage, index: c_int, params: *const OakVideoParams) -> c_int;
	/// `oaknode_footage_get_video_length`.
	pub fn oaknode_footage_get_video_length(footage: OakNodeFootage, out_num: *mut i64, out_den: *mut i64) -> c_int;
	/// `oaknode_footage_set_cancel_atom`.
	pub fn oaknode_footage_set_cancel_atom(footage: OakNodeFootage, atom: OakCancelAtom) -> c_int;

	// --- sequence.h ---
	/// `oaknode_sequence_create`.
	pub fn oaknode_sequence_create() -> OakNodeSequence;
	/// `oaknode_sequence_free`.
	pub fn oaknode_sequence_free(sequence: *mut OakNodeSequence);
	/// `oaknode_sequence_set_default_parameters`.
	pub fn oaknode_sequence_set_default_parameters(sequence: OakNodeSequence) -> c_int;
	/// `oaknode_sequence_as_node`.
	pub fn oaknode_sequence_as_node(sequence: OakNodeSequence) -> OakNodeNode;
	/// `oaknode_sequence_from_node`.
	pub fn oaknode_sequence_from_node(node: OakNodeNode) -> OakNodeSequence;
	/// `oaknode_sequence_get_track_list`.
	pub fn oaknode_sequence_get_track_list(sequence: OakNodeSequence, r#type: c_int, out: *mut OakNodeTrackList) -> c_int;
	/// `oaknode_sequence_get_track_count`.
	pub fn oaknode_sequence_get_track_count(sequence: OakNodeSequence, r#type: c_int, count: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_track_at`.
	pub fn oaknode_sequence_get_track_at(sequence: OakNodeSequence, r#type: c_int, index: c_int, out: *mut OakNodeTrack) -> c_int;
	/// `oaknode_sequence_get_all_track_count`.
	pub fn oaknode_sequence_get_all_track_count(sequence: OakNodeSequence, count: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_all_track_at`.
	pub fn oaknode_sequence_get_all_track_at(sequence: OakNodeSequence, index: c_int, out: *mut OakNodeTrack) -> c_int;
	/// `oaknode_sequence_get_playhead`.
	pub fn oaknode_sequence_get_playhead(sequence: OakNodeSequence, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_sequence_set_playhead`.
	pub fn oaknode_sequence_set_playhead(sequence: OakNodeSequence, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_sequence_get_length`.
	pub fn oaknode_sequence_get_length(sequence: OakNodeSequence, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_video_length`.
	pub fn oaknode_sequence_get_video_length(sequence: OakNodeSequence, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_audio_length`.
	pub fn oaknode_sequence_get_audio_length(sequence: OakNodeSequence, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_sequence_verify_length`.
	pub fn oaknode_sequence_verify_length(sequence: OakNodeSequence) -> c_int;
	/// `oaknode_sequence_get_video_stream_count`.
	pub fn oaknode_sequence_get_video_stream_count(sequence: OakNodeSequence, count: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_audio_stream_count`.
	pub fn oaknode_sequence_get_audio_stream_count(sequence: OakNodeSequence, count: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_video_params`.
	pub fn oaknode_sequence_get_video_params(sequence: OakNodeSequence, index: c_int, out: *mut OakVideoParams) -> c_int;
	/// `oaknode_sequence_set_video_params`.
	pub fn oaknode_sequence_set_video_params(sequence: OakNodeSequence, index: c_int, params: OakVideoParams) -> c_int;
	/// `oaknode_sequence_get_audio_params`.
	pub fn oaknode_sequence_get_audio_params(sequence: OakNodeSequence, index: c_int, out: *mut *const c_void) -> c_int;
	/// `oaknode_sequence_set_audio_params`.
	pub fn oaknode_sequence_set_audio_params(sequence: OakNodeSequence, index: c_int, params: *const c_void) -> c_int;

	// --- track.h ---
	/// `oaknode_track_as_node`.
	pub fn oaknode_track_as_node(track: OakNodeTrack) -> OakNodeNode;
	/// `oaknode_track_create`.
	pub fn oaknode_track_create(r#type: c_int) -> OakNodeTrack;
	/// `oaknode_track_free`.
	pub fn oaknode_track_free(track: *mut OakNodeTrack);
	/// `oaknode_track_get_type`.
	pub fn oaknode_track_get_type(track: OakNodeTrack, r#type: *mut c_int) -> c_int;
	/// `oaknode_track_get_length`.
	pub fn oaknode_track_get_length(track: OakNodeTrack, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_track_get_block_count`.
	pub fn oaknode_track_get_block_count(track: OakNodeTrack, count: *mut c_int) -> c_int;
	/// `oaknode_track_get_block_at` (borrowed handle out).
	pub fn oaknode_track_get_block_at(track: OakNodeTrack, index: c_int, out: *mut OakNodeBlock) -> c_int;
	/// `oaknode_track_append_block`.
	pub fn oaknode_track_append_block(track: OakNodeTrack, block: OakNodeBlock) -> c_int;
	/// `oaknode_track_set_type`.
	pub fn oaknode_track_set_type(track: OakNodeTrack, r#type: c_int) -> c_int;
	/// `oaknode_track_get_index`.
	pub fn oaknode_track_get_index(track: OakNodeTrack, index: *mut c_int) -> c_int;
	/// `oaknode_track_set_index`.
	pub fn oaknode_track_set_index(track: OakNodeTrack, index: c_int) -> c_int;
	/// `oaknode_track_get_sequence`.
	pub fn oaknode_track_get_sequence(track: OakNodeTrack, out: *mut OakNodeSequence) -> c_int;
	/// `oaknode_tracklist_get_sequence`.
	pub fn oaknode_tracklist_get_sequence(list: OakNodeTrackList, out: *mut OakNodeSequence) -> c_int;
	/// `oaknode_tracklist_get_track_input_id`.
	pub fn oaknode_tracklist_get_track_input_id(list: OakNodeTrackList, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_tracklist_array_append`.
	pub fn oaknode_tracklist_array_append(list: OakNodeTrackList) -> c_int;
	/// `oaknode_tracklist_array_remove_last`.
	pub fn oaknode_tracklist_array_remove_last(list: OakNodeTrackList) -> c_int;
	/// `oaknode_tracklist_get_array_index_from_cache_index`.
	pub fn oaknode_tracklist_get_array_index_from_cache_index(list: OakNodeTrackList, cache_index: c_int, out_index: *mut c_int) -> c_int;
	/// `oaknode_tracklist_get_type`.
	pub fn oaknode_tracklist_get_type(list: OakNodeTrackList, r#type: *mut c_int) -> c_int;
	/// `oaknode_tracklist_get_track_count`.
	pub fn oaknode_tracklist_get_track_count(list: OakNodeTrackList, count: *mut c_int) -> c_int;
	/// `oaknode_tracklist_get_track_at`.
	pub fn oaknode_tracklist_get_track_at(list: OakNodeTrackList, index: c_int, out: *mut OakNodeTrack) -> c_int;
	/// `oaknode_tracklist_get_total_length`.
	pub fn oaknode_tracklist_get_total_length(list: OakNodeTrackList, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_tracklist_get_array_size`.
	pub fn oaknode_tracklist_get_array_size(list: OakNodeTrackList, size: *mut c_int) -> c_int;
	/// `oaknode_tracklist_add_track`.
	pub fn oaknode_tracklist_add_track(list: OakNodeTrackList, track: OakNodeTrack) -> c_int;
	/// `oaknode_tracklist_remove_track`.
	pub fn oaknode_tracklist_remove_track(list: OakNodeTrackList, track: OakNodeTrack) -> c_int;

	// --- colormanager.h ---
	/// `oaknode_colormanager_init`.
	pub fn oaknode_colormanager_init(project: OakNodeProject) -> OakNodeColorManager;
	/// `oaknode_colormanager_free`.
	pub fn oaknode_colormanager_free(manager: *mut OakNodeColorManager);
	/// `oaknode_colormanager_wrap_borrowed`.
	pub fn oaknode_colormanager_wrap_borrowed(native_manager: *mut c_void) -> OakNodeColorManager;
	/// `oaknode_colormanager_initialize`.
	pub fn oaknode_colormanager_initialize(manager: OakNodeColorManager) -> c_int;
	/// `oaknode_colormanager_set_up_default_config`.
	pub fn oaknode_colormanager_set_up_default_config() -> c_int;

	// --- serializer.h ---
	/// `oaknode_serializer_initialize`.
	pub fn oaknode_serializer_initialize() -> c_int;
	/// `oaknode_serializer_shutdown`.
	pub fn oaknode_serializer_shutdown();
	/// `oaknode_serializer_savedata_create`.
	pub fn oaknode_serializer_savedata_create(load_type: c_int, project: OakNodeProject) -> OakNodeSerializerSaveData;
	/// `oaknode_serializer_savedata_free`.
	pub fn oaknode_serializer_savedata_free(save_data: *mut OakNodeSerializerSaveData);
	/// `oaknode_serializer_savedata_set_nodes`.
	pub fn oaknode_serializer_savedata_set_nodes(save_data: OakNodeSerializerSaveData, nodes: *const OakNodeNode, count: c_int) -> c_int;
	/// `oaknode_serializer_savedata_set_property`.
	pub fn oaknode_serializer_savedata_set_property(save_data: OakNodeSerializerSaveData, node: OakNodeNode, key: *const c_char, value: *const c_char) -> c_int;
	/// `oaknode_serializer_save_to_xml`.
	pub fn oaknode_serializer_save_to_xml(save_data: OakNodeSerializerSaveData, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_serializer_load_from_xml`.
	pub fn oaknode_serializer_load_from_xml(project: OakNodeProject, xml: *const c_char, load_type: c_int, out_result: *mut c_int, out_load_data: *mut OakNodeSerializerLoadData, details_buf: *mut c_char, details_buf_size: c_int) -> c_int;
	/// `oaknode_serializer_loaddata_free`.
	pub fn oaknode_serializer_loaddata_free(load_data: *mut OakNodeSerializerLoadData);
	/// `oaknode_serializer_loaddata_node_count`.
	pub fn oaknode_serializer_loaddata_node_count(load_data: OakNodeSerializerLoadData) -> c_int;
	/// `oaknode_serializer_loaddata_node_at`.
	pub fn oaknode_serializer_loaddata_node_at(load_data: OakNodeSerializerLoadData, index: c_int) -> OakNodeNode;
	/// `oaknode_serializer_loaddata_get_property`.
	pub fn oaknode_serializer_loaddata_get_property(load_data: OakNodeSerializerLoadData, node: OakNodeNode, key: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_serializer_loaddata_connection_count`.
	pub fn oaknode_serializer_loaddata_connection_count(load_data: OakNodeSerializerLoadData) -> c_int;
	/// `oaknode_serializer_loaddata_connection_at`.
	pub fn oaknode_serializer_loaddata_connection_at(load_data: OakNodeSerializerLoadData, index: c_int, out_output_node: *mut OakNodeNode, out_input_node: *mut OakNodeNode, input_id_buf: *mut c_char, input_id_buf_size: c_int, out_element: *mut c_int) -> c_int;
	/// `oaknode_serializer_save_to_file`.
	pub fn oaknode_serializer_save_to_file(project: OakNodeProject, filename: *const c_char, use_compression: c_int, out_code: *mut c_int, details: *mut c_char, details_size: c_int) -> c_int;
	/// `oaknode_serializer_load_from_file`.
	pub fn oaknode_serializer_load_from_file(project: OakNodeProject, filename: *const c_char, out_code: *mut c_int, details: *mut c_char, details_size: c_int) -> c_int;
}
