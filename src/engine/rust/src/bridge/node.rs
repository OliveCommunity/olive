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

//! oaknode C ABI imports, mirroring the oaknode crate's exports
//! (`src/node/rust/src/ffi.rs`; headers `include/node/*.h`).
//!
//! Every handle crosses as [`crate::handle::CHandle`] (structurally
//! identical to every `OakNode*` value handle). String getters are
//! two-stage: they report the required size **including** the terminating
//! NUL; the facade converts with [`crate::handle::string_result`]. All
//! module error codes (-30001..) pass through untranslated.

use std::ffi::{c_char, c_int, c_void};

use crate::handle::CHandle;

// `include/node/node.h` — POD parameter value mirror of `oaknode_value`
// (defined in [`crate::node`]). The facade hands the engine's
// `oak_node_value` straight to the module (the two structs are
// layout-identical).
extern "C" {
	// ---- include/node/project.h --------------------------------------------
	/// `oaknode_project_init` — new project, refcount 1.
	pub fn oaknode_project_init() -> CHandle;
	/// `oaknode_project_free` — NULL/empty no-op; clears `project->ctx`.
	pub fn oaknode_project_free(project: *mut CHandle);
	/// `oaknode_project_initialize` — create the root folder.
	pub fn oaknode_project_initialize(project: CHandle) -> c_int;
	/// `oaknode_project_clear` — destroy all nodes, keep the shell.
	pub fn oaknode_project_clear(project: CHandle) -> c_int;
	/// `oaknode_project_root` — borrowed root folder handle.
	pub fn oaknode_project_root(project: CHandle) -> CHandle;
	/// `oaknode_project_name` (two-stage string).
	pub fn oaknode_project_name(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_filename` (two-stage string).
	pub fn oaknode_project_filename(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_pretty_filename` (two-stage string).
	pub fn oaknode_project_pretty_filename(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_set_filename`.
	pub fn oaknode_project_set_filename(project: CHandle, filename: *const c_char) -> c_int;
	/// `oaknode_project_is_modified`.
	pub fn oaknode_project_is_modified(project: CHandle) -> c_int;
	/// `oaknode_project_set_modified`.
	pub fn oaknode_project_set_modified(project: CHandle, modified: c_int) -> c_int;
	/// `oaknode_project_is_new`.
	pub fn oaknode_project_is_new(project: CHandle) -> c_int;
	/// `oaknode_project_cache_path` (two-stage string).
	pub fn oaknode_project_cache_path(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_copy_settings`.
	pub fn oaknode_project_copy_settings(dst: CHandle, src: CHandle) -> c_int;
	/// `oaknode_project_get_cache_location_setting`.
	pub fn oaknode_project_get_cache_location_setting(project: CHandle) -> c_int;
	/// `oaknode_project_set_cache_location_setting`.
	pub fn oaknode_project_set_cache_location_setting(project: CHandle, setting: c_int) -> c_int;
	/// `oaknode_project_get_custom_cache_path` (two-stage string).
	pub fn oaknode_project_get_custom_cache_path(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_set_custom_cache_path`.
	pub fn oaknode_project_set_custom_cache_path(project: CHandle, path: *const c_char) -> c_int;
	/// `oaknode_project_get_uuid` (two-stage string).
	pub fn oaknode_project_get_uuid(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_project_add_node` — graph takes the node's lifetime.
	pub fn oaknode_project_add_node(project: CHandle, node: CHandle) -> c_int;
	/// `oaknode_project_remove_node` — detach without deleting.
	pub fn oaknode_project_remove_node(project: CHandle, node: CHandle) -> c_int;
	/// `oaknode_project_node_count`.
	pub fn oaknode_project_node_count(project: CHandle) -> c_int;
	/// `oaknode_project_node_at` — borrowed handle at index.
	pub fn oaknode_project_node_at(project: CHandle, index: c_int) -> CHandle;

	// ---- include/node/node.h ------------------------------------------------
	/// `oaknode_debug_alive_count`.
	pub fn oaknode_debug_alive_count() -> c_int;
	/// `oaknode_node_get_id` (two-stage string).
	pub fn oaknode_node_get_id(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_node_get_name` (two-stage string).
	pub fn oaknode_node_get_name(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_node_get_label` (two-stage string).
	pub fn oaknode_node_get_label(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_node_set_label` — live.
	pub fn oaknode_node_set_label(node: CHandle, label: *const c_char) -> c_int;
	/// `oaknode_node_set_label_undoable`.
	pub fn oaknode_node_set_label_undoable(node: CHandle, label: *const c_char, out_command: *mut CHandle) -> c_int;
	/// `oaknode_node_get_override_color`.
	pub fn oaknode_node_get_override_color(node: CHandle, out_value: *mut c_int) -> c_int;
	/// `oaknode_node_set_override_color` — live.
	pub fn oaknode_node_set_override_color(node: CHandle, index: c_int) -> c_int;
	/// `oaknode_node_set_override_color_undoable`.
	pub fn oaknode_node_set_override_color_undoable(node: CHandle, index: c_int, out_command: *mut CHandle) -> c_int;
	/// `oaknode_node_is_enabled`.
	pub fn oaknode_node_is_enabled(node: CHandle, out_value: *mut c_int) -> c_int;
	/// `oaknode_node_set_enabled` — live.
	pub fn oaknode_node_set_enabled(node: CHandle, enabled: c_int) -> c_int;
	/// `oaknode_node_set_enabled_undoable`.
	pub fn oaknode_node_set_enabled_undoable(node: CHandle, enabled: c_int, out_command: *mut CHandle) -> c_int;
	/// `oaknode_node_input_count`.
	pub fn oaknode_node_input_count(node: CHandle, out_count: *mut c_int) -> c_int;
	/// `oaknode_node_input_id` (two-stage string).
	pub fn oaknode_node_input_id(node: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_node_input_get_type`.
	pub fn oaknode_node_input_get_type(node: CHandle, input_id: *const c_char, out_type: *mut c_int) -> c_int;
	/// `oaknode_node_input_is_connected`.
	pub fn oaknode_node_input_is_connected(node: CHandle, input_id: *const c_char, out_value: *mut c_int) -> c_int;
	/// `oaknode_node_input_is_connectable`.
	pub fn oaknode_node_input_is_connectable(node: CHandle, input_id: *const c_char, out_value: *mut c_int) -> c_int;
	/// `oaknode_node_get_input_name` (two-stage string).
	pub fn oaknode_node_get_input_name(node: CHandle, input_id: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_node_input_get_connected_node` — borrowed handle out.
	pub fn oaknode_node_input_get_connected_node(node: CHandle, input_id: *const c_char, out_node: *mut CHandle) -> c_int;
	/// `oaknode_node_get_input` — POD value out.
	pub fn oaknode_node_get_input(
		node: CHandle,
		input_id: *const c_char,
		out: *mut crate::node::OakNodeValue,
	) -> c_int;
	/// `oaknode_node_set_input` — live.
	pub fn oaknode_node_set_input(node: CHandle, input_id: *const c_char, v: *const crate::node::OakNodeValue) -> c_int;
	/// `oaknode_node_set_input_undoable`.
	pub fn oaknode_node_set_input_undoable(
		node: CHandle,
		input_id: *const c_char,
		v: *const crate::node::OakNodeValue,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_node_get_input_string` (two-stage string).
	pub fn oaknode_node_get_input_string(node: CHandle, input_id: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_node_set_input_string` — live.
	pub fn oaknode_node_set_input_string(node: CHandle, input_id: *const c_char, value: *const c_char) -> c_int;
	/// `oaknode_node_set_input_string_undoable`.
	pub fn oaknode_node_set_input_string_undoable(
		node: CHandle,
		input_id: *const c_char,
		value: *const c_char,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_node_connect` — live, element -1.
	pub fn oaknode_node_connect(output_node: CHandle, input_node: CHandle, input_id: *const c_char) -> c_int;
	/// `oaknode_node_connect_undoable`.
	pub fn oaknode_node_connect_undoable(
		output_node: CHandle,
		input_node: CHandle,
		input_id: *const c_char,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_node_disconnect` — live, element -1.
	pub fn oaknode_node_disconnect(input_node: CHandle, input_id: *const c_char) -> c_int;
	/// `oaknode_node_disconnect_undoable`.
	pub fn oaknode_node_disconnect_undoable(input_node: CHandle, input_id: *const c_char, out_command: *mut CHandle) -> c_int;
	/// `oaknode_node_output_connection_count`.
	pub fn oaknode_node_output_connection_count(node: CHandle, out_count: *mut c_int) -> c_int;
	/// `oaknode_node_output_connection_node_at` — borrowed handle out.
	pub fn oaknode_node_output_connection_node_at(node: CHandle, index: c_int, out_node: *mut CHandle) -> c_int;
	/// `oaknode_node_output_connection_input_id_at` (two-stage string).
	pub fn oaknode_node_output_connection_input_id_at(
		node: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oaknode_node_output_connection_element_at`.
	pub fn oaknode_node_output_connection_element_at(node: CHandle, index: c_int, out_element: *mut c_int) -> c_int;
	/// `oaknode_node_link` — live; `out_linked` may be NULL.
	pub fn oaknode_node_link(a: CHandle, b: CHandle, out_linked: *mut c_int) -> c_int;
	/// `oaknode_node_unlink` — live; `out_unlinked` may be NULL.
	pub fn oaknode_node_unlink(a: CHandle, b: CHandle, out_unlinked: *mut c_int) -> c_int;
	/// `oaknode_node_link_undoable` — `link` != 0 links, 0 unlinks.
	pub fn oaknode_node_link_undoable(a: CHandle, b: CHandle, link: c_int, out_command: *mut CHandle) -> c_int;
	/// `oaknode_node_are_linked`.
	pub fn oaknode_node_are_linked(a: CHandle, b: CHandle, out_value: *mut c_int) -> c_int;
	/// `oaknode_node_link_count`.
	pub fn oaknode_node_link_count(node: CHandle, out_count: *mut c_int) -> c_int;
	/// `oaknode_node_link_at` — borrowed handle out.
	pub fn oaknode_node_link_at(node: CHandle, index: c_int, out_node: *mut CHandle) -> c_int;
	/// `oaknode_node_context_count`.
	pub fn oaknode_node_context_count(node: CHandle, out_count: *mut c_int) -> c_int;
	/// `oaknode_node_context_node_at` — borrowed handle out.
	pub fn oaknode_node_context_node_at(node: CHandle, index: c_int, out_node: *mut CHandle) -> c_int;
	/// `oaknode_node_get_context_position` — any out pointer may be NULL.
	pub fn oaknode_node_get_context_position(
		node: CHandle,
		context: CHandle,
		out_x: *mut f64,
		out_y: *mut f64,
		out_expanded: *mut c_int,
	) -> c_int;
	/// `oaknode_node_set_context_position` — live.
	pub fn oaknode_node_set_context_position(node: CHandle, context: CHandle, x: f64, y: f64, expanded: c_int) -> c_int;
	/// `oaknode_node_set_context_position_undoable`.
	pub fn oaknode_node_set_context_position_undoable(
		node: CHandle,
		context: CHandle,
		x: f64,
		y: f64,
		expanded: c_int,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_node_remove_from_context` — live.
	pub fn oaknode_node_remove_from_context(node: CHandle, context: CHandle) -> c_int;
	/// `oaknode_node_create_copy` — standalone copy, refcount 1.
	pub fn oaknode_node_create_copy(node: CHandle) -> CHandle;
	/// `oaknode_node_copy_in_graph` — copy + MultiUndoCommand out.
	pub fn oaknode_node_copy_in_graph(node: CHandle, out_command: *mut CHandle) -> CHandle;
	/// `oaknode_node_get_project` — borrowed project handle out.
	pub fn oaknode_node_get_project(node: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_node_input_array_insert` — live.
	pub fn oaknode_node_input_array_insert(node: CHandle, input_id: *const c_char, index: c_int) -> c_int;
	/// `oaknode_node_input_array_remove` — live.
	pub fn oaknode_node_input_array_remove(node: CHandle, input_id: *const c_char, index: c_int) -> c_int;
	/// `oaknode_node_connect_element` — element-aware connect.
	pub fn oaknode_node_connect_element(output_node: CHandle, input_node: CHandle, input_id: *const c_char, element: c_int) -> c_int;
	/// `oaknode_node_disconnect_element` — element-aware disconnect.
	pub fn oaknode_node_disconnect_element(input_node: CHandle, input_id: *const c_char, element: c_int) -> c_int;
	/// `oaknode_command_create_add_node` — owned `NodeAddCommand`.
	pub fn oaknode_command_create_add_node(graph: CHandle, node: CHandle) -> CHandle;
	/// `oaknode_command_create_set_position_recursive` — owned command.
	pub fn oaknode_command_create_set_position_recursive(node: CHandle, context: CHandle, x: f64, y: f64) -> CHandle;
	/// `oaknode_node_get_markers` — addref'd oaktimeline list handle out.
	pub fn oaknode_node_get_markers(node: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_node_get_work_area` — addref'd oaktimeline workarea handle out.
	pub fn oaknode_node_get_work_area(node: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_node_get_video_frame_cache` — addref'd oakrender cache handle out.
	pub fn oaknode_node_get_video_frame_cache(node: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_node_copy_inputs` — `include_connections` != 0 also copies edges.
	pub fn oaknode_node_copy_inputs(dst: CHandle, src: CHandle, include_connections: c_int) -> c_int;
	/// `oaknode_node_set_value_hint_track` — single texture type hint.
	pub fn oaknode_node_set_value_hint_track(node: CHandle, input_id: *const c_char, track_type: c_int, track_index: c_int) -> c_int;
	/// `oaknode_viewer_set_video_params` — `params` is an oakcommon handle.
	pub fn oaknode_viewer_set_video_params(viewer: CHandle, params: *const CHandle) -> c_int;
	/// `oaknode_viewer_set_audio_params` — `params` is a borrowed oakcore handle.
	pub fn oaknode_viewer_set_audio_params(viewer: CHandle, params: *const c_void) -> c_int;
	/// `oaknode_node_find_input_footage` — borrowed footage handle out.
	pub fn oaknode_node_find_input_footage(node: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_node_get_input_at_time` — rational seconds.
	pub fn oaknode_node_get_input_at_time(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		out: *mut crate::node::OakNodeValue,
	) -> c_int;
	/// `oaknode_node_set_input_at_time_undoable` — rational seconds.
	pub fn oaknode_node_set_input_at_time_undoable(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		v: *const crate::node::OakNodeValue,
		track: c_int,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_node_identity` — opaque identity int.
	pub fn oaknode_node_identity(node: CHandle) -> usize;
	/// `oaknode_node_set_input_at_time_into` — batch into a multi command.
	pub fn oaknode_node_set_input_at_time_into(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		v: *const crate::node::OakNodeValue,
		track: c_int,
		multi_command: CHandle,
	) -> c_int;
	/// `oaknode_command_create_remove_node` — owned remove+disconnect command.
	pub fn oaknode_command_create_remove_node(node: CHandle) -> CHandle;
	/// `oaknode_node_free` — NULL/empty no-op; clears `node->ctx`.
	pub fn oaknode_node_free(node: *mut CHandle);

	// ---- include/node/factory.h ---------------------------------------------
	/// `oaknode_factory_initialize`.
	pub fn oaknode_factory_initialize() -> c_int;
	/// `oaknode_factory_destroy`.
	pub fn oaknode_factory_destroy();
	/// `oaknode_factory_id_count`.
	pub fn oaknode_factory_id_count(out_count: *mut c_int) -> c_int;
	/// `oaknode_factory_id_at` (two-stage string).
	pub fn oaknode_factory_id_at(index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_factory_name_from_id` (two-stage string).
	pub fn oaknode_factory_name_from_id(type_id: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_factory_create_from_id` — new node, refcount 1.
	pub fn oaknode_factory_create_from_id(type_id: *const c_char) -> CHandle;
	/// `oaknode_factory_node_at` — borrowed prototype handle out.
	pub fn oaknode_factory_node_at(index: c_int, out_node: *mut CHandle) -> c_int;

	// ---- include/node/folder.h ----------------------------------------------
	/// `oaknode_folder_create` — new folder node in the project.
	pub fn oaknode_folder_create(project: CHandle) -> CHandle;
	/// `oaknode_folder_child_count`.
	pub fn oaknode_folder_child_count(folder: CHandle) -> c_int;
	/// `oaknode_folder_child_at` — borrowed child handle.
	pub fn oaknode_folder_child_at(folder: CHandle, index: c_int) -> CHandle;
	/// `oaknode_folder_add_child` — live.
	pub fn oaknode_folder_add_child(folder: CHandle, child: CHandle) -> c_int;
	/// `oaknode_folder_as_node` — folder viewed as a node.
	pub fn oaknode_folder_as_node(folder: CHandle) -> CHandle;
	/// `oaknode_command_create_folder_add_child` — owned command.
	pub fn oaknode_command_create_folder_add_child(folder: CHandle, child: CHandle) -> CHandle;
	/// `oaknode_folder_remove_child` — live.
	pub fn oaknode_folder_remove_child(folder: CHandle, child: CHandle) -> c_int;
	/// `oaknode_folder_move_children` — move several nodes, one command.
	pub fn oaknode_folder_move_children(nodes: *const CHandle, count: c_int, dest_folder: CHandle) -> c_int;
	/// `oaknode_folder_has_child_recursive`.
	pub fn oaknode_folder_has_child_recursive(folder: CHandle, child: CHandle) -> c_int;
	/// `oaknode_folder_index_of_child`.
	pub fn oaknode_folder_index_of_child(folder: CHandle, child: CHandle) -> c_int;
	/// `oaknode_folder_parent_of` — the folder owning the node.
	pub fn oaknode_folder_parent_of(node: CHandle) -> CHandle;

	// ---- include/node/footage.h ---------------------------------------------
	/// `oaknode_footage_create` — new footage node in the project.
	pub fn oaknode_footage_create(project: CHandle, filename: *const c_char) -> CHandle;
	/// `oaknode_footage_as_node` — footage viewed as a node.
	pub fn oaknode_footage_as_node(footage: CHandle) -> CHandle;
	/// `oaknode_footage_filename` (two-stage string).
	pub fn oaknode_footage_filename(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_footage_set_filename` — triggers the reprobe cascade.
	pub fn oaknode_footage_set_filename(footage: CHandle, filename: *const c_char) -> c_int;
	/// `oaknode_footage_is_valid`.
	pub fn oaknode_footage_is_valid(footage: CHandle) -> c_int;
	/// `oaknode_footage_timestamp`.
	pub fn oaknode_footage_timestamp(footage: CHandle, out_timestamp: *mut i64) -> c_int;
	/// `oaknode_footage_set_timestamp`.
	pub fn oaknode_footage_set_timestamp(footage: CHandle, timestamp: i64) -> c_int;
	/// `oaknode_footage_decoder` (two-stage string).
	pub fn oaknode_footage_decoder(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_footage_total_stream_count`.
	pub fn oaknode_footage_total_stream_count(footage: CHandle) -> c_int;
	/// `oaknode_footage_video_stream_count`.
	pub fn oaknode_footage_video_stream_count(footage: CHandle) -> c_int;
	/// `oaknode_footage_audio_stream_count`.
	pub fn oaknode_footage_audio_stream_count(footage: CHandle) -> c_int;
	/// `oaknode_footage_subtitle_stream_count`.
	pub fn oaknode_footage_subtitle_stream_count(footage: CHandle) -> c_int;
	/// `oaknode_footage_duration` — rational seconds.
	pub fn oaknode_footage_duration(footage: CHandle, out_numerator: *mut c_int, out_denominator: *mut c_int) -> c_int;
	/// `oaknode_footage_proxy_enabled`.
	pub fn oaknode_footage_proxy_enabled(footage: CHandle) -> c_int;
	/// `oaknode_footage_set_proxy_enabled` — live.
	pub fn oaknode_footage_set_proxy_enabled(footage: CHandle, enabled: c_int) -> c_int;
	/// `oaknode_footage_proxy_path` (two-stage string).
	pub fn oaknode_footage_proxy_path(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_footage_proxy_state`.
	pub fn oaknode_footage_proxy_state(footage: CHandle) -> c_int;
	/// `oaknode_footage_set_proxy`.
	pub fn oaknode_footage_set_proxy(
		footage: CHandle,
		path: *const c_char,
		state: c_int,
		video_stream_index: c_int,
		preset_version: c_int,
		enabled: c_int,
	) -> c_int;
	/// `oaknode_footage_clear_proxy`.
	pub fn oaknode_footage_clear_proxy(footage: CHandle) -> c_int;
	/// `oaknode_footage_get_video_params` — oakcommon handle out.
	pub fn oaknode_footage_get_video_params(footage: CHandle, index: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_footage_set_video_params` — oakcommon handle in.
	pub fn oaknode_footage_set_video_params(footage: CHandle, index: c_int, params: *const CHandle) -> c_int;
	/// `oaknode_footage_get_video_length` — rational seconds.
	pub fn oaknode_footage_get_video_length(footage: CHandle, out_num: *mut i64, out_den: *mut i64) -> c_int;
	/// `oaknode_footage_set_cancel_atom`.
	pub fn oaknode_footage_set_cancel_atom(footage: CHandle, atom: CHandle) -> c_int;

	// ---- include/node/group.h -----------------------------------------------
	/// `oaknode_group_create` — detached group node, refcount 1.
	pub fn oaknode_group_create() -> CHandle;
	/// `oaknode_group_cast` — node viewed as a group.
	pub fn oaknode_group_cast(node: CHandle) -> CHandle;
	/// `oaknode_group_free` — NULL/empty no-op.
	pub fn oaknode_group_free(group: *mut CHandle);
	/// `oaknode_group_add_input_passthrough` — direct; id written two-stage.
	pub fn oaknode_group_add_input_passthrough(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oaknode_group_add_input_passthrough_undoable`.
	pub fn oaknode_group_add_input_passthrough_undoable(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_group_remove_input_passthrough` — direct.
	pub fn oaknode_group_remove_input_passthrough(group: CHandle, node: CHandle, input_id: *const c_char, element: c_int) -> c_int;
	/// `oaknode_group_passthrough_count`.
	pub fn oaknode_group_passthrough_count(group: CHandle, out_count: *mut c_int) -> c_int;
	/// `oaknode_group_passthrough_id_at` (two-stage string).
	pub fn oaknode_group_passthrough_id_at(group: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_group_passthrough_input_at` — node/input/element out.
	pub fn oaknode_group_passthrough_input_at(
		group: CHandle,
		index: c_int,
		out_node: *mut CHandle,
		buf: *mut c_char,
		buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int;
	/// `oaknode_group_get_output_passthrough`.
	pub fn oaknode_group_get_output_passthrough(group: CHandle, out_node: *mut CHandle) -> c_int;
	/// `oaknode_group_set_output_passthrough` — direct.
	pub fn oaknode_group_set_output_passthrough(group: CHandle, node: CHandle) -> c_int;
	/// `oaknode_group_set_output_passthrough_undoable`.
	pub fn oaknode_group_set_output_passthrough_undoable(group: CHandle, node: CHandle, out_command: *mut CHandle) -> c_int;
	/// `oaknode_group_resolve_input` — resolve a passthrough id.
	pub fn oaknode_group_resolve_input(
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		out_node: *mut CHandle,
		buf: *mut c_char,
		buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int;

	// ---- include/node/keyframe.h --------------------------------------------
	/// `oaknode_keyframe_create` — detached keyframe, refcount 1.
	pub fn oaknode_keyframe_create(
		time_num: i64,
		time_den: i64,
		value: *const crate::node::OakNodeValue,
		type_: c_int,
		track: c_int,
		element: c_int,
		input_id: *const c_char,
		parent_or_null: CHandle,
	) -> CHandle;
	/// `oaknode_keyframe_free` — NULL/empty no-op.
	pub fn oaknode_keyframe_free(keyframe: *mut CHandle);
	/// `oaknode_keyframe_get_time`.
	pub fn oaknode_keyframe_get_time(keyframe: CHandle, out_num: *mut i64, out_den: *mut i64) -> c_int;
	/// `oaknode_keyframe_set_time` — live.
	pub fn oaknode_keyframe_set_time(keyframe: CHandle, time_num: i64, time_den: i64) -> c_int;
	/// `oaknode_keyframe_set_time_undoable`.
	pub fn oaknode_keyframe_set_time_undoable(
		keyframe: CHandle,
		time_num: i64,
		time_den: i64,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_keyframe_get_value` — POD out.
	pub fn oaknode_keyframe_get_value(keyframe: CHandle, out: *mut crate::node::OakNodeValue) -> c_int;
	/// `oaknode_keyframe_set_value` — live.
	pub fn oaknode_keyframe_set_value(keyframe: CHandle, v: *const crate::node::OakNodeValue) -> c_int;
	/// `oaknode_keyframe_set_value_undoable`.
	pub fn oaknode_keyframe_set_value_undoable(
		keyframe: CHandle,
		v: *const crate::node::OakNodeValue,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_keyframe_get_value_string` (two-stage string).
	pub fn oaknode_keyframe_get_value_string(keyframe: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_keyframe_set_value_string` — live.
	pub fn oaknode_keyframe_set_value_string(keyframe: CHandle, value: *const c_char) -> c_int;
	/// `oaknode_keyframe_set_value_string_undoable`.
	pub fn oaknode_keyframe_set_value_string_undoable(
		keyframe: CHandle,
		value: *const c_char,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_keyframe_get_type`.
	pub fn oaknode_keyframe_get_type(keyframe: CHandle, out_type: *mut c_int) -> c_int;
	/// `oaknode_keyframe_set_type` — live.
	pub fn oaknode_keyframe_set_type(keyframe: CHandle, type_: c_int) -> c_int;
	/// `oaknode_keyframe_set_type_undoable`.
	pub fn oaknode_keyframe_set_type_undoable(keyframe: CHandle, type_: c_int, out_command: *mut CHandle) -> c_int;
	/// `oaknode_keyframe_get_bezier_control` — `handle` 0=in, 1=out.
	pub fn oaknode_keyframe_get_bezier_control(keyframe: CHandle, handle: c_int, out_x: *mut f64, out_y: *mut f64) -> c_int;
	/// `oaknode_keyframe_set_bezier_control` — live.
	pub fn oaknode_keyframe_set_bezier_control(keyframe: CHandle, handle: c_int, x: f64, y: f64) -> c_int;
	/// `oaknode_keyframe_set_bezier_control_undoable`.
	pub fn oaknode_keyframe_set_bezier_control_undoable(
		keyframe: CHandle,
		handle: c_int,
		x: f64,
		y: f64,
		out_command: *mut CHandle,
	) -> c_int;
	/// `oaknode_keyframe_get_track`.
	pub fn oaknode_keyframe_get_track(keyframe: CHandle, out_track: *mut c_int) -> c_int;
	/// `oaknode_keyframe_get_element`.
	pub fn oaknode_keyframe_get_element(keyframe: CHandle, out_element: *mut c_int) -> c_int;
	/// `oaknode_keyframe_get_input` (two-stage string).
	pub fn oaknode_keyframe_get_input(keyframe: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_keyframe_get_parent` — borrowed node handle out.
	pub fn oaknode_keyframe_get_parent(keyframe: CHandle, out_node: *mut CHandle) -> c_int;
	/// `oaknode_keyframe_get_valid_bezier_control` — identity for non-bezier.
	pub fn oaknode_keyframe_get_valid_bezier_control(keyframe: CHandle, handle: c_int, out_x: *mut f64, out_y: *mut f64) -> c_int;
	/// `oaknode_keyframe_opposing_bezier_type`.
	pub fn oaknode_keyframe_opposing_bezier_type(type_: c_int) -> c_int;
	/// `oaknode_keyframe_compute_paste_value` — POD out.
	pub fn oaknode_keyframe_compute_paste_value(
		target_node: CHandle,
		keyframe: CHandle,
		out: *mut crate::node::OakNodeValue,
	) -> c_int;
	/// `oaknode_keyframe_has_sibling_at_time` — relative to own track.
	pub fn oaknode_keyframe_has_sibling_at_time(keyframe: CHandle, time_num: i64, time_den: i64, out_value: *mut c_int) -> c_int;

	// ---- include/node/dragger.h ---------------------------------------------
	/// `oaknode_dragger_create` — new dragger, refcount 1.
	pub fn oaknode_dragger_create(node: CHandle, input_id: *const c_char, element: c_int, track: c_int) -> CHandle;
	/// `oaknode_dragger_start` — rational time.
	pub fn oaknode_dragger_start(
		dragger: CHandle,
		time_num: i64,
		time_den: i64,
		track: c_int,
		insert_on_all_tracks: c_int,
	) -> c_int;
	/// `oaknode_dragger_drag` — live.
	pub fn oaknode_dragger_drag(dragger: CHandle, value: *const crate::node::OakNodeValue) -> c_int;
	/// `oaknode_dragger_end` — owned command out.
	pub fn oaknode_dragger_end(dragger: CHandle, out_command: *mut CHandle) -> c_int;
	/// `oaknode_dragger_is_started`.
	pub fn oaknode_dragger_is_started(dragger: CHandle, out_started: *mut c_int) -> c_int;
	/// `oaknode_dragger_free` — NULL/empty no-op.
	pub fn oaknode_dragger_free(dragger: *mut CHandle);

	// ---- include/node/multicam.h --------------------------------------------
	/// `oaknode_multicam_input_current` — static string, never freed.
	pub fn oaknode_multicam_input_current() -> *const c_char;
	/// `oaknode_multicam_input_sources` — static string, never freed.
	pub fn oaknode_multicam_input_sources() -> *const c_char;
	/// `oaknode_multicam_input_sequence` — static string, never freed.
	pub fn oaknode_multicam_input_sequence() -> *const c_char;
	/// `oaknode_multicam_input_sequence_type` — static string, never freed.
	pub fn oaknode_multicam_input_sequence_type() -> *const c_char;
	/// `oaknode_multicam_get_source_count`.
	pub fn oaknode_multicam_get_source_count(node: CHandle, out_count: *mut c_int) -> c_int;
	/// `oaknode_multicam_get_rows_and_columns`.
	pub fn oaknode_multicam_get_rows_and_columns(source_count: c_int, rows: *mut c_int, cols: *mut c_int) -> c_int;
	/// `oaknode_multicam_index_to_row_cols`.
	pub fn oaknode_multicam_index_to_row_cols(index: c_int, rows: c_int, cols: c_int, out_row: *mut c_int, out_col: *mut c_int) -> c_int;
	/// `oaknode_multicam_rows_cols_to_index`.
	pub fn oaknode_multicam_rows_cols_to_index(row: c_int, col: c_int, rows: c_int, cols: c_int) -> c_int;
	/// `oaknode_multicam_get_current_source`.
	pub fn oaknode_multicam_get_current_source(node: CHandle, out_source: *mut c_int) -> c_int;

	// ---- include/node/sequence.h --------------------------------------------
	/// `oaknode_sequence_create` — detached sequence, refcount 1.
	pub fn oaknode_sequence_create() -> CHandle;
	/// `oaknode_sequence_free` — NULL/empty no-op.
	pub fn oaknode_sequence_free(sequence: *mut CHandle);
	/// `oaknode_sequence_set_default_parameters`.
	pub fn oaknode_sequence_set_default_parameters(sequence: CHandle) -> c_int;
	/// `oaknode_sequence_as_node` — sequence viewed as a node.
	pub fn oaknode_sequence_as_node(sequence: CHandle) -> CHandle;
	/// `oaknode_sequence_from_node` — node viewed as a sequence.
	pub fn oaknode_sequence_from_node(node: CHandle) -> CHandle;
	/// `oaknode_sequence_get_track_list` — borrowed track list out.
	pub fn oaknode_sequence_get_track_list(sequence: CHandle, type_: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_sequence_get_track_count`.
	pub fn oaknode_sequence_get_track_count(sequence: CHandle, type_: c_int, count: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_track_at` — borrowed track handle out.
	pub fn oaknode_sequence_get_track_at(sequence: CHandle, type_: c_int, index: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_sequence_get_all_track_count`.
	pub fn oaknode_sequence_get_all_track_count(sequence: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_all_track_at` — borrowed track handle out.
	pub fn oaknode_sequence_get_all_track_at(sequence: CHandle, index: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_sequence_get_playhead` — rational seconds.
	pub fn oaknode_sequence_get_playhead(sequence: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_sequence_set_playhead` — rational seconds.
	pub fn oaknode_sequence_set_playhead(sequence: CHandle, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_sequence_get_length` — rational seconds.
	pub fn oaknode_sequence_get_length(sequence: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_video_length` — rational seconds.
	pub fn oaknode_sequence_get_video_length(sequence: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_audio_length` — rational seconds.
	pub fn oaknode_sequence_get_audio_length(sequence: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_sequence_verify_length`.
	pub fn oaknode_sequence_verify_length(sequence: CHandle) -> c_int;
	/// `oaknode_sequence_get_video_stream_count`.
	pub fn oaknode_sequence_get_video_stream_count(sequence: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_audio_stream_count`.
	pub fn oaknode_sequence_get_audio_stream_count(sequence: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_sequence_get_video_params` — oakcommon handle out.
	pub fn oaknode_sequence_get_video_params(sequence: CHandle, index: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_sequence_set_video_params` — oakcommon handle by value.
	pub fn oaknode_sequence_set_video_params(sequence: CHandle, index: c_int, params: CHandle) -> c_int;
	/// `oaknode_sequence_get_audio_params` — borrowed oakcore handle out.
	pub fn oaknode_sequence_get_audio_params(sequence: CHandle, index: c_int, out: *mut *mut c_void) -> c_int;
	/// `oaknode_sequence_set_audio_params` — borrowed oakcore handle in.
	pub fn oaknode_sequence_set_audio_params(sequence: CHandle, index: c_int, params: *const c_void) -> c_int;

	// ---- include/node/track.h -----------------------------------------------
	/// `oaknode_track_as_node` — track viewed as a node.
	pub fn oaknode_track_as_node(track: CHandle) -> CHandle;
	/// `oaknode_track_create` — new track, refcount 1.
	pub fn oaknode_track_create(type_: c_int) -> CHandle;
	/// `oaknode_track_free` — NULL/empty no-op.
	pub fn oaknode_track_free(track: *mut CHandle);
	/// `oaknode_track_get_type`.
	pub fn oaknode_track_get_type(track: CHandle, type_: *mut c_int) -> c_int;
	/// `oaknode_track_set_type`.
	pub fn oaknode_track_set_type(track: CHandle, type_: c_int) -> c_int;
	/// `oaknode_track_get_height`.
	pub fn oaknode_track_get_height(track: CHandle, height: *mut f64) -> c_int;
	/// `oaknode_track_set_height`.
	pub fn oaknode_track_set_height(track: CHandle, height: f64) -> c_int;
	/// `oaknode_track_get_height_in_pixels`.
	pub fn oaknode_track_get_height_in_pixels(track: CHandle, height: *mut c_int) -> c_int;
	/// `oaknode_track_set_height_in_pixels`.
	pub fn oaknode_track_set_height_in_pixels(track: CHandle, height: c_int) -> c_int;
	/// `oaknode_track_get_default_height_in_pixels`.
	pub fn oaknode_track_get_default_height_in_pixels() -> c_int;
	/// `oaknode_track_get_minimum_height_in_pixels`.
	pub fn oaknode_track_get_minimum_height_in_pixels() -> c_int;
	/// `oaknode_track_get_index`.
	pub fn oaknode_track_get_index(track: CHandle, index: *mut c_int) -> c_int;
	/// `oaknode_track_set_index`.
	pub fn oaknode_track_set_index(track: CHandle, index: c_int) -> c_int;
	/// `oaknode_track_get_muted`.
	pub fn oaknode_track_get_muted(track: CHandle, muted: *mut c_int) -> c_int;
	/// `oaknode_track_set_muted`.
	pub fn oaknode_track_set_muted(track: CHandle, muted: c_int) -> c_int;
	/// `oaknode_track_get_locked`.
	pub fn oaknode_track_get_locked(track: CHandle, locked: *mut c_int) -> c_int;
	/// `oaknode_track_set_locked`.
	pub fn oaknode_track_set_locked(track: CHandle, locked: c_int) -> c_int;
	/// `oaknode_track_get_reference`.
	pub fn oaknode_track_get_reference(track: CHandle, type_: *mut c_int, index: *mut c_int) -> c_int;
	/// `oaknode_track_get_length` — rational seconds.
	pub fn oaknode_track_get_length(track: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_track_get_sequence` — borrowed sequence handle out.
	pub fn oaknode_track_get_sequence(track: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_track_get_block_count`.
	pub fn oaknode_track_get_block_count(track: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_track_get_block_at` — borrowed block handle out.
	pub fn oaknode_track_get_block_at(track: CHandle, index: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_track_append_block`.
	pub fn oaknode_track_append_block(track: CHandle, block: CHandle) -> c_int;
	/// `oaknode_track_prepend_block`.
	pub fn oaknode_track_prepend_block(track: CHandle, block: CHandle) -> c_int;
	/// `oaknode_track_insert_block_at_index`.
	pub fn oaknode_track_insert_block_at_index(track: CHandle, block: CHandle, index: c_int) -> c_int;
	/// `oaknode_track_insert_block_after`.
	pub fn oaknode_track_insert_block_after(track: CHandle, block: CHandle, before: CHandle) -> c_int;
	/// `oaknode_track_insert_block_before`.
	pub fn oaknode_track_insert_block_before(track: CHandle, block: CHandle, after: CHandle) -> c_int;
	/// `oaknode_track_ripple_remove_block`.
	pub fn oaknode_track_ripple_remove_block(track: CHandle, block: CHandle) -> c_int;
	/// `oaknode_track_replace_block`.
	pub fn oaknode_track_replace_block(track: CHandle, old_block: CHandle, new_block: CHandle) -> c_int;
	/// `oaknode_track_get_block_index`.
	pub fn oaknode_track_get_block_index(track: CHandle, block: CHandle, index: *mut c_int) -> c_int;
	/// `oaknode_track_get_block_containing_time` — rational seconds.
	pub fn oaknode_track_get_block_containing_time(track: CHandle, numerator: c_int, denominator: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_track_get_visible_block_at_time` — rational seconds.
	pub fn oaknode_track_get_visible_block_at_time(track: CHandle, numerator: c_int, denominator: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_track_is_range_free`.
	pub fn oaknode_track_is_range_free(
		track: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		is_free: *mut c_int,
	) -> c_int;
	/// `oaknode_track_get_nearest_block_before_or_at`.
	pub fn oaknode_track_get_nearest_block_before_or_at(track: CHandle, numerator: c_int, denominator: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_track_get_nearest_block_after_or_at`.
	pub fn oaknode_track_get_nearest_block_after_or_at(track: CHandle, numerator: c_int, denominator: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_tracklist_get_sequence` — borrowed sequence handle out.
	pub fn oaknode_tracklist_get_sequence(list: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_tracklist_get_track_input_id` (two-stage string).
	pub fn oaknode_tracklist_get_track_input_id(list: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_tracklist_array_append`.
	pub fn oaknode_tracklist_array_append(list: CHandle) -> c_int;
	/// `oaknode_tracklist_array_remove_last`.
	pub fn oaknode_tracklist_array_remove_last(list: CHandle) -> c_int;
	/// `oaknode_tracklist_get_array_index_from_cache_index`.
	pub fn oaknode_tracklist_get_array_index_from_cache_index(list: CHandle, cache_index: c_int, out_index: *mut c_int) -> c_int;
	/// `oaknode_tracklist_get_type`.
	pub fn oaknode_tracklist_get_type(list: CHandle, type_: *mut c_int) -> c_int;
	/// `oaknode_tracklist_get_track_count`.
	pub fn oaknode_tracklist_get_track_count(list: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_tracklist_get_track_at` — borrowed track handle out.
	pub fn oaknode_tracklist_get_track_at(list: CHandle, index: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_tracklist_get_total_length` — rational seconds.
	pub fn oaknode_tracklist_get_total_length(list: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_tracklist_get_array_size`.
	pub fn oaknode_tracklist_get_array_size(list: CHandle, size: *mut c_int) -> c_int;
	/// `oaknode_tracklist_add_track`.
	pub fn oaknode_tracklist_add_track(list: CHandle, track: CHandle) -> c_int;
	/// `oaknode_tracklist_remove_track`.
	pub fn oaknode_tracklist_remove_track(list: CHandle, track: CHandle) -> c_int;

	// ---- include/node/block.h -----------------------------------------------
	/// `oaknode_block_clip_create` — new clip block, refcount 1.
	pub fn oaknode_block_clip_create() -> CHandle;
	/// `oaknode_block_gap_create` — new gap block, refcount 1.
	pub fn oaknode_block_gap_create() -> CHandle;
	/// `oaknode_block_transition_create` — new transition block, refcount 1.
	pub fn oaknode_block_transition_create(kind: c_int) -> CHandle;
	/// `oaknode_block_free` — NULL/empty no-op.
	pub fn oaknode_block_free(block: *mut CHandle);
	/// `oaknode_block_get_kind`.
	pub fn oaknode_block_get_kind(block: CHandle, out_kind: *mut c_int) -> c_int;
	/// `oaknode_block_as_node` — block viewed as a node.
	pub fn oaknode_block_as_node(block: CHandle) -> CHandle;
	/// `oaknode_block_from_node` — node viewed as a block.
	pub fn oaknode_block_from_node(node: CHandle) -> CHandle;
	/// `oaknode_block_get_in` — rational seconds.
	pub fn oaknode_block_get_in(block: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_block_set_in` — rational seconds.
	pub fn oaknode_block_set_in(block: CHandle, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_block_get_out` — rational seconds.
	pub fn oaknode_block_get_out(block: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_block_set_out` — rational seconds.
	pub fn oaknode_block_set_out(block: CHandle, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_block_get_length` — rational seconds.
	pub fn oaknode_block_get_length(block: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_block_set_length_and_media_out`.
	pub fn oaknode_block_set_length_and_media_out(block: CHandle, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_block_set_length_and_media_in`.
	pub fn oaknode_block_set_length_and_media_in(block: CHandle, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_block_get_enabled`.
	pub fn oaknode_block_get_enabled(block: CHandle, enabled: *mut c_int) -> c_int;
	/// `oaknode_block_set_enabled`.
	pub fn oaknode_block_set_enabled(block: CHandle, enabled: c_int) -> c_int;
	/// `oaknode_block_get_previous` — borrowed block handle out.
	pub fn oaknode_block_get_previous(block: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_block_get_next` — borrowed block handle out.
	pub fn oaknode_block_get_next(block: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_block_get_track` — borrowed track handle out.
	pub fn oaknode_block_get_track(block: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_block_link` — live.
	pub fn oaknode_block_link(a: CHandle, b: CHandle) -> c_int;
	/// `oaknode_block_unlink` — live.
	pub fn oaknode_block_unlink(a: CHandle, b: CHandle) -> c_int;
	/// `oaknode_block_are_linked`.
	pub fn oaknode_block_are_linked(a: CHandle, b: CHandle, linked: *mut c_int) -> c_int;
	/// `oaknode_block_get_link_count`.
	pub fn oaknode_block_get_link_count(block: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_block_get_link_at` — borrowed block handle out.
	pub fn oaknode_block_get_link_at(block: CHandle, index: c_int, out: *mut CHandle) -> c_int;
	/// `oaknode_clip_get_media_in` — rational seconds.
	pub fn oaknode_clip_get_media_in(clip: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_clip_set_media_in` — rational seconds.
	pub fn oaknode_clip_set_media_in(clip: CHandle, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_clip_get_speed`.
	pub fn oaknode_clip_get_speed(clip: CHandle, speed: *mut f64) -> c_int;
	/// `oaknode_clip_set_speed`.
	pub fn oaknode_clip_set_speed(clip: CHandle, speed: f64) -> c_int;
	/// `oaknode_clip_get_reverse`.
	pub fn oaknode_clip_get_reverse(clip: CHandle, reverse: *mut c_int) -> c_int;
	/// `oaknode_clip_set_reverse`.
	pub fn oaknode_clip_set_reverse(clip: CHandle, reverse: c_int) -> c_int;
	/// `oaknode_clip_get_maintain_audio_pitch`.
	pub fn oaknode_clip_get_maintain_audio_pitch(clip: CHandle, maintain: *mut c_int) -> c_int;
	/// `oaknode_clip_set_maintain_audio_pitch`.
	pub fn oaknode_clip_set_maintain_audio_pitch(clip: CHandle, maintain: c_int) -> c_int;
	/// `oaknode_clip_get_loop_mode`.
	pub fn oaknode_clip_get_loop_mode(clip: CHandle, loop_mode: *mut c_int) -> c_int;
	/// `oaknode_clip_set_loop_mode`.
	pub fn oaknode_clip_set_loop_mode(clip: CHandle, loop_mode: c_int) -> c_int;
	/// `oaknode_clip_get_track_type`.
	pub fn oaknode_clip_get_track_type(clip: CHandle, type_: *mut c_int) -> c_int;
	/// `oaknode_transition_get_in_offset` — rational seconds.
	pub fn oaknode_transition_get_in_offset(transition: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_transition_get_out_offset` — rational seconds.
	pub fn oaknode_transition_get_out_offset(transition: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_transition_get_offset_center` — rational seconds.
	pub fn oaknode_transition_get_offset_center(transition: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int;
	/// `oaknode_transition_set_offset_center`.
	pub fn oaknode_transition_set_offset_center(transition: CHandle, numerator: c_int, denominator: c_int) -> c_int;
	/// `oaknode_transition_set_offsets_and_length`.
	pub fn oaknode_transition_set_offsets_and_length(
		transition: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> c_int;
	/// `oaknode_transition_is_dual`.
	pub fn oaknode_transition_is_dual(transition: CHandle, dual: *mut c_int) -> c_int;
	/// `oaknode_transition_get_connected_out_block` — borrowed block handle out.
	pub fn oaknode_transition_get_connected_out_block(transition: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_transition_get_connected_in_block` — borrowed block handle out.
	pub fn oaknode_transition_get_connected_in_block(transition: CHandle, out: *mut CHandle) -> c_int;
	/// `oaknode_clip_add_cache_passthrough_from`.
	pub fn oaknode_clip_add_cache_passthrough_from(clip: CHandle, other: CHandle) -> c_int;

	// ---- include/node/colormanager.h ----------------------------------------
	/// `oaknode_colormanager_init` — manager for a project, refcount 1.
	pub fn oaknode_colormanager_init(project: CHandle) -> CHandle;
	/// `oaknode_colormanager_free` — NULL/empty no-op.
	pub fn oaknode_colormanager_free(manager: *mut CHandle);
	/// `oaknode_colormanager_wrap_borrowed` — wrap a native manager pointer.
	pub fn oaknode_colormanager_wrap_borrowed(native_manager: *mut c_void) -> CHandle;
	/// `oaknode_colormanager_initialize`.
	pub fn oaknode_colormanager_initialize(manager: CHandle) -> c_int;
	/// `oaknode_colormanager_set_up_default_config`.
	pub fn oaknode_colormanager_set_up_default_config() -> c_int;
	/// `oaknode_colormanager_get_config_filename` (two-stage string).
	pub fn oaknode_colormanager_get_config_filename(manager: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_colormanager_set_config_filename`.
	pub fn oaknode_colormanager_set_config_filename(manager: CHandle, filename: *const c_char) -> c_int;
	/// `oaknode_colormanager_update_config_from_filename`.
	pub fn oaknode_colormanager_update_config_from_filename(manager: CHandle) -> c_int;
	/// `oaknode_colormanager_get_default_input_color_space` (two-stage string).
	pub fn oaknode_colormanager_get_default_input_color_space(manager: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_colormanager_set_default_input_color_space`.
	pub fn oaknode_colormanager_set_default_input_color_space(manager: CHandle, colorspace: *const c_char) -> c_int;
	/// `oaknode_colormanager_get_reference_color_space` (two-stage string).
	pub fn oaknode_colormanager_get_reference_color_space(manager: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_colormanager_get_compliant_color_space` (two-stage string).
	pub fn oaknode_colormanager_get_compliant_color_space(
		manager: CHandle,
		colorspace: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oaknode_colormanager_get_colorspace_for_ffmpeg_tags` (two-stage string).
	pub fn oaknode_colormanager_get_colorspace_for_ffmpeg_tags(
		manager: CHandle,
		primaries: c_int,
		trc: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oaknode_colormanager_get_display_count`.
	pub fn oaknode_colormanager_get_display_count(manager: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_colormanager_get_display_at` (two-stage string).
	pub fn oaknode_colormanager_get_display_at(manager: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_colormanager_get_default_display` (two-stage string).
	pub fn oaknode_colormanager_get_default_display(manager: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_colormanager_get_view_count`.
	pub fn oaknode_colormanager_get_view_count(manager: CHandle, display: *const c_char, count: *mut c_int) -> c_int;
	/// `oaknode_colormanager_get_view_at` (two-stage string).
	pub fn oaknode_colormanager_get_view_at(
		manager: CHandle,
		display: *const c_char,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oaknode_colormanager_get_default_view` (two-stage string).
	pub fn oaknode_colormanager_get_default_view(manager: CHandle, display: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_colormanager_get_look_count`.
	pub fn oaknode_colormanager_get_look_count(manager: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_colormanager_get_look_at` (two-stage string).
	pub fn oaknode_colormanager_get_look_at(manager: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_colormanager_get_colorspace_count`.
	pub fn oaknode_colormanager_get_colorspace_count(manager: CHandle, count: *mut c_int) -> c_int;
	/// `oaknode_colormanager_get_colorspace_at` (two-stage string).
	pub fn oaknode_colormanager_get_colorspace_at(manager: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_colormanager_get_default_luma_coefs`.
	pub fn oaknode_colormanager_get_default_luma_coefs(manager: CHandle, rgb: *mut f64) -> c_int;
	/// `oaknode_colormanager_get_compliant_color_transform` — transform handle out.
	pub fn oaknode_colormanager_get_compliant_color_transform(
		manager: CHandle,
		transform: CHandle,
		force_display: c_int,
		out: *mut CHandle,
	) -> c_int;

	// ---- include/node/traverser.h -------------------------------------------
	/// `oaknode_traverser_init` — new traverser, refcount 1.
	pub fn oaknode_traverser_init() -> CHandle;
	/// `oaknode_traverser_free` — NULL/empty no-op.
	pub fn oaknode_traverser_free(traverser: *mut CHandle);
	/// `oaknode_traverser_generate_database` — value database handle out.
	pub fn oaknode_traverser_generate_database(
		traverser: CHandle,
		node: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		out_db: *mut CHandle,
	) -> c_int;
	/// `oaknode_traverser_database_free` — NULL/empty no-op.
	pub fn oaknode_traverser_database_free(db: *mut CHandle);
	/// `oaknode_traverser_database_row_count`.
	pub fn oaknode_traverser_database_row_count(db: CHandle, out_count: *mut c_int) -> c_int;
	/// `oaknode_traverser_database_row_key_at` (two-stage string).
	pub fn oaknode_traverser_database_row_key_at(db: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_traverser_database_row_value_count`.
	pub fn oaknode_traverser_database_row_value_count(db: CHandle, key: *const c_char, out_count: *mut c_int) -> c_int;
	/// `oaknode_traverser_database_value_at` — POD out.
	pub fn oaknode_traverser_database_value_at(
		db: CHandle,
		key: *const c_char,
		index: c_int,
		out: *mut crate::node::OakNodeValue,
	) -> c_int;
	/// `oaknode_traverser_database_value_string_at` (two-stage string).
	pub fn oaknode_traverser_database_value_string_at(
		db: CHandle,
		key: *const c_char,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;

	// ---- include/node/serializer.h ------------------------------------------
	/// `oaknode_serializer_initialize`.
	pub fn oaknode_serializer_initialize() -> c_int;
	/// `oaknode_serializer_shutdown`.
	pub fn oaknode_serializer_shutdown();
	/// `oaknode_serializer_savedata_create` — save-data handle, refcount 1.
	pub fn oaknode_serializer_savedata_create(load_type: c_int, project: CHandle) -> CHandle;
	/// `oaknode_serializer_savedata_free` — NULL/empty no-op.
	pub fn oaknode_serializer_savedata_free(save_data: *mut CHandle);
	/// `oaknode_serializer_savedata_set_nodes`.
	pub fn oaknode_serializer_savedata_set_nodes(save_data: CHandle, nodes: *const CHandle, count: c_int) -> c_int;
	/// `oaknode_serializer_savedata_set_property`.
	pub fn oaknode_serializer_savedata_set_property(
		save_data: CHandle,
		node: CHandle,
		key: *const c_char,
		value: *const c_char,
	) -> c_int;
	/// `oaknode_serializer_save_to_xml` (two-stage string).
	pub fn oaknode_serializer_save_to_xml(save_data: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int;
	/// `oaknode_serializer_load_from_xml` — load-data handle out.
	pub fn oaknode_serializer_load_from_xml(
		project: CHandle,
		xml: *const c_char,
		load_type: c_int,
		out_result: *mut c_int,
		out_load_data: *mut CHandle,
		details_buf: *mut c_char,
		details_buf_size: c_int,
	) -> c_int;
	/// `oaknode_serializer_loaddata_free` — NULL/empty no-op.
	pub fn oaknode_serializer_loaddata_free(load_data: *mut CHandle);
	/// `oaknode_serializer_loaddata_node_count`.
	pub fn oaknode_serializer_loaddata_node_count(load_data: CHandle) -> c_int;
	/// `oaknode_serializer_loaddata_node_at` — borrowed node handle.
	pub fn oaknode_serializer_loaddata_node_at(load_data: CHandle, index: c_int) -> CHandle;
	/// `oaknode_serializer_loaddata_get_property` (two-stage string).
	pub fn oaknode_serializer_loaddata_get_property(
		load_data: CHandle,
		node: CHandle,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int;
	/// `oaknode_serializer_loaddata_connection_count`.
	pub fn oaknode_serializer_loaddata_connection_count(load_data: CHandle) -> c_int;
	/// `oaknode_serializer_loaddata_connection_at`.
	pub fn oaknode_serializer_loaddata_connection_at(
		load_data: CHandle,
		index: c_int,
		out_output_node: *mut CHandle,
		out_input_node: *mut CHandle,
		input_id_buf: *mut c_char,
		input_id_buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int;
	/// `oaknode_serializer_save_to_file` — write a project file.
	pub fn oaknode_serializer_save_to_file(
		project: CHandle,
		filename: *const c_char,
		use_compression: c_int,
		out_code: *mut c_int,
		details: *mut c_char,
		details_size: c_int,
	) -> c_int;
	/// `oaknode_serializer_load_from_file` — read a project file.
	pub fn oaknode_serializer_load_from_file(
		project: CHandle,
		filename: *const c_char,
		out_code: *mut c_int,
		details: *mut c_char,
		details_size: c_int,
	) -> c_int;
}
