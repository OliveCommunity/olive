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

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_get_label(node: OakNodeNode, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_label(node, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_set_label(node: OakNodeNode, label: *const c_char) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_label(node, label) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_get_id(node: OakNodeNode, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_id(node, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_get_project(node: OakNodeNode, out: *mut OakNodeProject) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_project(node, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_input_get_connected_node(
	node: OakNodeNode,
	input_id: *const c_char,
	out_node: *mut OakNodeNode,
) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_get_connected_node(node, input_id, out_node) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_set_context_position(
	node: OakNodeNode,
	context: OakNodeNode,
	x: f64,
	y: f64,
	expanded: c_int,
) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_context_position(node, context, x, y, expanded) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_find_input_footage(node: OakNodeNode, out: *mut OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_find_input_footage(node, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_get_input_string(
	node: OakNodeNode,
	input_id: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_input_string(node, input_id, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_create_copy(node: OakNodeNode) -> OakNodeNode {
	unsafe { oaknode::ffi::node::oaknode_node_create_copy(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_copy_inputs(
	dst: OakNodeNode,
	src: OakNodeNode,
	include_connections: c_int,
) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_copy_inputs(dst, src, include_connections) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_connect(
	output_node: OakNodeNode,
	input_node: OakNodeNode,
	input_id: *const c_char,
) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_connect(output_node, input_node, input_id) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_set_value_hint_track(
	node: OakNodeNode,
	input_id: *const c_char,
	track_type: c_int,
	track_index: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::node::oaknode_node_set_value_hint_track(
			node,
			input_id,
			track_type,
			track_index,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_get_video_frame_cache(
	node: OakNodeNode,
	out: *mut crate::bridge::render::OakRenderCache,
) -> c_int {
	unsafe {
		oaknode::ffi::node::oaknode_node_get_video_frame_cache(node, out as *mut std::ffi::c_void)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_node_free(node: *mut OakNodeNode) {
	unsafe { oaknode::ffi::node::oaknode_node_free(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_command_create_remove_node(
	node: OakNodeNode,
) -> crate::bridge::undo::OakUndoCommand {
	unsafe { oaknode::ffi::node::oaknode_command_create_remove_node(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_factory_create_from_id(type_id: *const c_char) -> OakNodeNode {
	unsafe { oaknode::ffi::factory::oaknode_factory_create_from_id(type_id) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_viewer_set_video_params(
	viewer: OakNodeNode,
	params: *const OakVideoParams,
) -> c_int {
	unsafe {
		oaknode::ffi::node::oaknode_viewer_set_video_params(
			viewer,
			params as *const std::ffi::c_void,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_viewer_set_audio_params(
	viewer: OakNodeNode,
	params: *const std::ffi::c_void,
) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_viewer_set_audio_params(viewer, params) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_clip_create() -> OakNodeBlock {
	unsafe { oaknode::ffi::block::oaknode_block_clip_create() }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_gap_create() -> OakNodeBlock {
	unsafe { oaknode::ffi::block::oaknode_block_gap_create() }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_transition_create(kind: c_int) -> OakNodeBlock {
	unsafe { oaknode::ffi::block::oaknode_block_transition_create(kind) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_as_node(block: OakNodeBlock) -> OakNodeNode {
	unsafe { oaknode::ffi::block::oaknode_block_as_node(block) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_kind(block: OakNodeBlock, out_kind: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_kind(block, out_kind) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_in(
	block: OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_in(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_get_length(
	block: OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_length(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_block_set_length_and_media_out(
	block: OakNodeBlock,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::block::oaknode_block_set_length_and_media_out(block, numerator, denominator)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_clip_set_media_in(
	clip: OakNodeBlock,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_set_media_in(clip, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_transition_get_in_offset(
	transition: OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::block::oaknode_transition_get_in_offset(transition, numerator, denominator)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_transition_get_out_offset(
	transition: OakNodeBlock,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::block::oaknode_transition_get_out_offset(transition, numerator, denominator)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_transition_set_offsets_and_length(
	transition: OakNodeBlock,
	in_num: c_int,
	in_den: c_int,
	out_num: c_int,
	out_den: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::block::oaknode_transition_set_offsets_and_length(
			transition, in_num, in_den, out_num, out_den,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_init() -> OakNodeProject {
	unsafe { oaknode::ffi::project::oaknode_project_init() }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_free(project: *mut OakNodeProject) {
	unsafe { oaknode::ffi::project::oaknode_project_free(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_initialize(project: OakNodeProject) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_initialize(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_clear(project: OakNodeProject) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_clear(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_root(project: OakNodeProject) -> OakNodeFolder {
	unsafe { oaknode::ffi::project::oaknode_project_root(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_name(project: OakNodeProject, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_name(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_filename(
	project: OakNodeProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_filename(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_pretty_filename(
	project: OakNodeProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_pretty_filename(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_set_filename(project: OakNodeProject, filename: *const c_char) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_set_filename(project, filename) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_is_modified(project: OakNodeProject) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_is_modified(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_set_modified(project: OakNodeProject, modified: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_set_modified(project, modified) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_is_new(project: OakNodeProject) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_is_new(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_cache_path(
	project: OakNodeProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_cache_path(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_copy_settings(dst: OakNodeProject, src: OakNodeProject) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_copy_settings(dst, src) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_get_cache_location_setting(project: OakNodeProject) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_get_cache_location_setting(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_set_cache_location_setting(
	project: OakNodeProject,
	setting: c_int,
) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_set_cache_location_setting(project, setting) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_get_custom_cache_path(
	project: OakNodeProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_get_custom_cache_path(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_set_custom_cache_path(
	project: OakNodeProject,
	path: *const c_char,
) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_set_custom_cache_path(project, path) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_get_uuid(
	project: OakNodeProject,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_get_uuid(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_add_node(project: OakNodeProject, node: OakNodeNode) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_add_node(project, node) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_remove_node(project: OakNodeProject, node: OakNodeNode) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_remove_node(project, node) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_node_count(project: OakNodeProject) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_node_count(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_project_node_at(project: OakNodeProject, index: c_int) -> OakNodeNode {
	unsafe { oaknode::ffi::project::oaknode_project_node_at(project, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_create(project: OakNodeProject) -> OakNodeFolder {
	unsafe { oaknode::ffi::folder::oaknode_folder_create(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_child_count(folder: OakNodeFolder) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_child_count(folder) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_child_at(folder: OakNodeFolder, index: c_int) -> OakNodeNode {
	unsafe { oaknode::ffi::folder::oaknode_folder_child_at(folder, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_add_child(folder: OakNodeFolder, child: OakNodeNode) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_add_child(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_as_node(folder: OakNodeFolder) -> OakNodeNode {
	unsafe { oaknode::ffi::folder::oaknode_folder_as_node(folder) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_command_create_folder_add_child(
	folder: OakNodeFolder,
	child: OakNodeNode,
) -> crate::bridge::undo::OakUndoCommand {
	unsafe { oaknode::ffi::folder::oaknode_command_create_folder_add_child(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_remove_child(folder: OakNodeFolder, child: OakNodeNode) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_remove_child(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_move_children(
	nodes: *const OakNodeNode,
	count: c_int,
	dest_folder: OakNodeFolder,
) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_move_children(nodes, count, dest_folder) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_has_child_recursive(folder: OakNodeFolder, child: OakNodeNode) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_has_child_recursive(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_index_of_child(folder: OakNodeFolder, child: OakNodeNode) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_index_of_child(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_folder_parent_of(node: OakNodeNode) -> OakNodeFolder {
	unsafe { oaknode::ffi::folder::oaknode_folder_parent_of(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_create(project: OakNodeProject, filename: *const c_char) -> OakNodeFootage {
	unsafe { oaknode::ffi::footage::oaknode_footage_create(project, filename) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_as_node(footage: OakNodeFootage) -> OakNodeNode {
	unsafe { oaknode::ffi::footage::oaknode_footage_as_node(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_filename(
	footage: OakNodeFootage,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_filename(footage, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_set_filename(footage: OakNodeFootage, filename: *const c_char) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_filename(footage, filename) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_is_valid(footage: OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_is_valid(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_timestamp(footage: OakNodeFootage, out_timestamp: *mut i64) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_timestamp(footage, out_timestamp) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_set_timestamp(footage: OakNodeFootage, timestamp: i64) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_timestamp(footage, timestamp) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_decoder(
	footage: OakNodeFootage,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_decoder(footage, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_total_stream_count(footage: OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_total_stream_count(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_video_stream_count(footage: OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_video_stream_count(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_audio_stream_count(footage: OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_audio_stream_count(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_subtitle_stream_count(footage: OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_subtitle_stream_count(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_duration(
	footage: OakNodeFootage,
	out_numerator: *mut c_int,
	out_denominator: *mut c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::footage::oaknode_footage_duration(footage, out_numerator, out_denominator)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_proxy_enabled(footage: OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_proxy_enabled(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_set_proxy_enabled(footage: OakNodeFootage, enabled: c_int) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_proxy_enabled(footage, enabled) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_proxy_path(
	footage: OakNodeFootage,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_proxy_path(footage, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_proxy_state(footage: OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_proxy_state(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_set_proxy(
	footage: OakNodeFootage,
	path: *const c_char,
	state: c_int,
	video_stream_index: c_int,
	preset_version: c_int,
	enabled: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::footage::oaknode_footage_set_proxy(
			footage,
			path,
			state,
			video_stream_index,
			preset_version,
			enabled,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_clear_proxy(footage: OakNodeFootage) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_clear_proxy(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_get_video_params(
	footage: OakNodeFootage,
	index: c_int,
	out: *mut OakVideoParams,
) -> c_int {
	unsafe {
		oaknode::ffi::footage::oaknode_footage_get_video_params(
			footage,
			index,
			out as *mut std::ffi::c_void,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_set_video_params(
	footage: OakNodeFootage,
	index: c_int,
	params: *const OakVideoParams,
) -> c_int {
	unsafe {
		oaknode::ffi::footage::oaknode_footage_set_video_params(
			footage,
			index,
			params as *const std::ffi::c_void,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_get_video_length(
	footage: OakNodeFootage,
	out_num: *mut i64,
	out_den: *mut i64,
) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_get_video_length(footage, out_num, out_den) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_footage_set_cancel_atom(footage: OakNodeFootage, atom: OakCancelAtom) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_cancel_atom(footage, atom) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_create() -> OakNodeSequence {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_create() }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_free(sequence: *mut OakNodeSequence) {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_free(sequence) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_set_default_parameters(sequence: OakNodeSequence) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_set_default_parameters(sequence) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_as_node(sequence: OakNodeSequence) -> OakNodeNode {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_as_node(sequence) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_from_node(node: OakNodeNode) -> OakNodeSequence {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_from_node(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_track_list(
	sequence: OakNodeSequence,
	r#type: c_int,
	out: *mut OakNodeTrackList,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_track_list(sequence, r#type, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_track_count(
	sequence: OakNodeSequence,
	r#type: c_int,
	count: *mut c_int,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_track_count(sequence, r#type, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_track_at(
	sequence: OakNodeSequence,
	r#type: c_int,
	index: c_int,
	out: *mut OakNodeTrack,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_track_at(sequence, r#type, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_all_track_count(sequence: OakNodeSequence, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_all_track_count(sequence, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_all_track_at(
	sequence: OakNodeSequence,
	index: c_int,
	out: *mut OakNodeTrack,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_all_track_at(sequence, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_playhead(
	sequence: OakNodeSequence,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::sequence::oaknode_sequence_get_playhead(sequence, numerator, denominator)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_set_playhead(
	sequence: OakNodeSequence,
	numerator: c_int,
	denominator: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::sequence::oaknode_sequence_set_playhead(sequence, numerator, denominator)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_length(
	sequence: OakNodeSequence,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_length(sequence, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_video_length(
	sequence: OakNodeSequence,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::sequence::oaknode_sequence_get_video_length(sequence, numerator, denominator)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_audio_length(
	sequence: OakNodeSequence,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::sequence::oaknode_sequence_get_audio_length(sequence, numerator, denominator)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_verify_length(sequence: OakNodeSequence) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_verify_length(sequence) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_video_stream_count(
	sequence: OakNodeSequence,
	count: *mut c_int,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_video_stream_count(sequence, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_audio_stream_count(
	sequence: OakNodeSequence,
	count: *mut c_int,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_audio_stream_count(sequence, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_video_params(
	sequence: OakNodeSequence,
	index: c_int,
	out: *mut OakVideoParams,
) -> c_int {
	unsafe {
		oaknode::ffi::sequence::oaknode_sequence_get_video_params(
			sequence,
			index,
			out as *mut std::ffi::c_void,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_set_video_params(
	sequence: OakNodeSequence,
	index: c_int,
	params: OakVideoParams,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_set_video_params(sequence, index, params) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_get_audio_params(
	sequence: OakNodeSequence,
	index: c_int,
	out: *mut *mut std::ffi::c_void,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_audio_params(sequence, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_sequence_set_audio_params(
	sequence: OakNodeSequence,
	index: c_int,
	params: *const c_void,
) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_set_audio_params(sequence, index, params) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_as_node(track: OakNodeTrack) -> OakNodeNode {
	unsafe { oaknode::ffi::track::oaknode_track_as_node(track) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_create(r#type: c_int) -> OakNodeTrack {
	unsafe { oaknode::ffi::track::oaknode_track_create(r#type) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_free(track: *mut OakNodeTrack) {
	unsafe { oaknode::ffi::track::oaknode_track_free(track) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_type(track: OakNodeTrack, r#type: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_type(track, r#type) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_length(
	track: OakNodeTrack,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_length(track, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_block_count(track: OakNodeTrack, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_block_count(track, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_block_at(
	track: OakNodeTrack,
	index: c_int,
	out: *mut OakNodeBlock,
) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_block_at(track, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_append_block(track: OakNodeTrack, block: OakNodeBlock) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_append_block(track, block) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_set_type(track: OakNodeTrack, r#type: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_set_type(track, r#type) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_index(track: OakNodeTrack, index: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_index(track, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_set_index(track: OakNodeTrack, index: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_set_index(track, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_track_get_sequence(track: OakNodeTrack, out: *mut OakNodeSequence) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_sequence(track, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_sequence(list: OakNodeTrackList, out: *mut OakNodeSequence) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_sequence(list, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_track_input_id(
	list: OakNodeTrackList,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_track_input_id(list, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_array_append(list: OakNodeTrackList) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_array_append(list) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_array_remove_last(list: OakNodeTrackList) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_array_remove_last(list) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_array_index_from_cache_index(
	list: OakNodeTrackList,
	cache_index: c_int,
	out_index: *mut c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::track::oaknode_tracklist_get_array_index_from_cache_index(
			list,
			cache_index,
			out_index,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_type(list: OakNodeTrackList, r#type: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_type(list, r#type) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_track_count(list: OakNodeTrackList, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_track_count(list, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_track_at(
	list: OakNodeTrackList,
	index: c_int,
	out: *mut OakNodeTrack,
) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_track_at(list, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_total_length(
	list: OakNodeTrackList,
	numerator: *mut c_int,
	denominator: *mut c_int,
) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_total_length(list, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_get_array_size(list: OakNodeTrackList, size: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_array_size(list, size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_add_track(list: OakNodeTrackList, track: OakNodeTrack) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_add_track(list, track) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_tracklist_remove_track(list: OakNodeTrackList, track: OakNodeTrack) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_remove_track(list, track) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_colormanager_init(project: OakNodeProject) -> OakNodeColorManager {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_init(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_colormanager_free(manager: *mut OakNodeColorManager) {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_free(manager) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_colormanager_wrap_borrowed(native_manager: *mut c_void) -> OakNodeColorManager {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_wrap_borrowed(native_manager) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_colormanager_initialize(manager: OakNodeColorManager) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_initialize(manager) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_colormanager_set_up_default_config() -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_set_up_default_config() }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_initialize() -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_initialize() }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_shutdown() {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_shutdown() }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_savedata_create(
	load_type: c_int,
	project: OakNodeProject,
) -> OakNodeSerializerSaveData {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_savedata_create(load_type, project) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_savedata_free(save_data: *mut OakNodeSerializerSaveData) {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_savedata_free(save_data) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_savedata_set_nodes(
	save_data: OakNodeSerializerSaveData,
	nodes: *const OakNodeNode,
	count: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::serializer::oaknode_serializer_savedata_set_nodes(save_data, nodes, count)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_savedata_set_property(
	save_data: OakNodeSerializerSaveData,
	node: OakNodeNode,
	key: *const c_char,
	value: *const c_char,
) -> c_int {
	unsafe {
		oaknode::ffi::serializer::oaknode_serializer_savedata_set_property(
			save_data, node, key, value,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_save_to_xml(
	save_data: OakNodeSerializerSaveData,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_save_to_xml(save_data, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_load_from_xml(
	project: OakNodeProject,
	xml: *const c_char,
	load_type: c_int,
	out_result: *mut c_int,
	out_load_data: *mut OakNodeSerializerLoadData,
	details_buf: *mut c_char,
	details_buf_size: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::serializer::oaknode_serializer_load_from_xml(
			project,
			xml,
			load_type,
			out_result,
			out_load_data,
			details_buf,
			details_buf_size,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_loaddata_free(load_data: *mut OakNodeSerializerLoadData) {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_free(load_data) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_loaddata_node_count(load_data: OakNodeSerializerLoadData) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_node_count(load_data) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_loaddata_node_at(
	load_data: OakNodeSerializerLoadData,
	index: c_int,
) -> OakNodeNode {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_node_at(load_data, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_loaddata_get_property(
	load_data: OakNodeSerializerLoadData,
	node: OakNodeNode,
	key: *const c_char,
	buf: *mut c_char,
	buf_size: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::serializer::oaknode_serializer_loaddata_get_property(
			load_data, node, key, buf, buf_size,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_loaddata_connection_count(load_data: OakNodeSerializerLoadData) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_connection_count(load_data) }
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_loaddata_connection_at(
	load_data: OakNodeSerializerLoadData,
	index: c_int,
	out_output_node: *mut OakNodeNode,
	out_input_node: *mut OakNodeNode,
	input_id_buf: *mut c_char,
	input_id_buf_size: c_int,
	out_element: *mut c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::serializer::oaknode_serializer_loaddata_connection_at(
			load_data,
			index,
			out_output_node,
			out_input_node,
			input_id_buf,
			input_id_buf_size,
			out_element,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_save_to_file(
	project: OakNodeProject,
	filename: *const c_char,
	use_compression: c_int,
	out_code: *mut c_int,
	details: *mut c_char,
	details_size: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::serializer::oaknode_serializer_save_to_file(
			project,
			filename,
			use_compression,
			out_code,
			details,
			details_size,
		)
	}
}

/// Direct call into the `oaknode` crate (single-lib unification).
pub fn oaknode_serializer_load_from_file(
	project: OakNodeProject,
	filename: *const c_char,
	out_code: *mut c_int,
	details: *mut c_char,
	details_size: c_int,
) -> c_int {
	unsafe {
		oaknode::ffi::serializer::oaknode_serializer_load_from_file(
			project,
			filename,
			out_code,
			details,
			details_size,
		)
	}
}
