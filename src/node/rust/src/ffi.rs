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

//! C ABI export layer: implements `include/node/*.h` verbatim.
//!
//! Organization: one submodule per public header. The authoritative
//! function list is the header itself; each submodule below carries a
//! complete inventory comment plus the export stubs. Bodies only
//! unwrap handles, call safe Rust, and map results through
//! [`crate::handle::guard*`].

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

/// `include/node/project.h` exports (complete inventory):
/// oaknode_project_init / free / initialize / clear / root / name /
/// filename / pretty_filename / set_filename / is_modified /
/// set_modified / is_new / cache_path / copy_settings / load / save /
/// load_from_data / save_to_data / folder_add / footage_import /
/// get_project_from_object / debug_alive_count.
pub mod project {
	use super::*;

	/// `oaknode_project_init`: new project, refcount 1.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_init() -> CHandle {
		todo!()
	}

	/// `oaknode_project_free`: NULL/empty no-op.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_free(project: *mut CHandle) {
		todo!()
	}

	/// `oaknode_project_initialize`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_initialize(project: CHandle) -> c_int {
		todo!()
	}

	/// `oaknode_project_clear`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_clear(project: CHandle) -> c_int {
		todo!()
	}

	/// `oaknode_project_load` (path on disk).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_load(path: *const c_char) -> CHandle {
		todo!()
	}

	/// `oaknode_project_save`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_save(project: CHandle) -> c_int {
		todo!()
	}

	// … every other declaration in include/node/project.h follows the
	// same shape; see the header for the authoritative list.
}

/// `include/node/node.h` exports (complete inventory):
/// node_type_name/id/category/description, add/remove_input,
/// set_input_name/flag/property, set_standard_value,
/// get_standard_value, set_input_at_time(_undoable/_into),
/// connect_edge / disconnect_edge, input_get_connected_node,
/// input_array_size/append/remove, copy_inputs, node_get_project,
/// node_identity, node_from_identity, sequence_from_node,
/// sequence_set_default_parameters, find_input_footage,
/// node_get_markers / get_work_area / get_video_frame_cache,
/// command_create_* family, debug_alive_count.
pub mod node {
	use super::*;

	/// `oaknode_node_identity`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_identity(node: CHandle) -> usize {
		todo!()
	}

	/// `oaknode_node_from_identity` (registry lookup).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_from_identity(id: usize) -> CHandle {
		todo!()
	}

	/// `oaknode_node_connect_edge`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_connect_edge(
		from: CHandle,
		to: CHandle,
		input: *const c_char,
		element: c_int,
	) -> c_int {
		todo!()
	}

	// … remainder per include/node/node.h.
}

/// `include/node/sequence.h` exports (complete inventory):
/// sequence_create / as_node / from_node / set_default_parameters /
/// add_default_nodes / get_length / track_list / playhead …
pub mod sequence {
	use super::*;

	/// `oaknode_sequence_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_create() -> CHandle {
		todo!()
	}

	/// `oaknode_sequence_as_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_as_node(sequence: CHandle) -> CHandle {
		todo!()
	}

	// … remainder per include/node/sequence.h.
}

/// `include/node/keyframe.h` — keyframe helpers (the handle-based
/// keyframe API itself is declared once the keyframe module lands; these
/// four are the paste/bezier/sibling helpers of the 2026-08-09 gap list).
pub mod keyframe {
	use super::*;

	/// `oaknode_keyframe_get_valid_bezier_control`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_valid_bezier_control(
		keyframe: CHandle,
		handle: c_int,
		out_x: *mut f64,
		out_y: *mut f64,
	) -> c_int {
		todo!()
	}

	/// `oaknode_keyframe_opposing_bezier_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_opposing_bezier_type(
		type_: c_int,
	) -> c_int {
		todo!()
	}

	/// `oaknode_keyframe_compute_paste_value`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_compute_paste_value(
		target_node: CHandle,
		keyframe: CHandle,
		out: *mut crate::value::OakNodeValue,
	) -> c_int {
		todo!()
	}

	/// `oaknode_keyframe_has_sibling_at_time`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_has_sibling_at_time(
		keyframe: CHandle,
		time_num: i64,
		time_den: i64,
		out_value: *mut c_int,
	) -> c_int {
		todo!()
	}
}

/// `include/node/dragger.h` exports (complete inventory):
/// oaknode_dragger_create / start / drag / end / is_started / free.
pub mod dragger {
	use super::*;

	/// `oaknode_dragger_create`: new dragger, refcount 1.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_create(
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		track: c_int,
	) -> CHandle {
		todo!()
	}

	/// `oaknode_dragger_start`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_start(
		dragger: CHandle,
		time_num: i64,
		time_den: i64,
		track: c_int,
		insert_on_all_tracks: c_int,
	) -> c_int {
		todo!()
	}

	/// `oaknode_dragger_drag`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_drag(
		dragger: CHandle,
		value: *const crate::value::OakNodeValue,
	) -> c_int {
		todo!()
	}

	/// `oaknode_dragger_end` (out_command mirrors the OakUndoCommand
	/// handle layout, identical to CHandle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_end(
		dragger: CHandle,
		out_command: *mut CHandle,
	) -> c_int {
		todo!()
	}

	/// `oaknode_dragger_is_started`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_is_started(
		dragger: CHandle,
		out_started: *mut c_int,
	) -> c_int {
		todo!()
	}

	/// `oaknode_dragger_free`: NULL/empty no-op.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_free(dragger: *mut CHandle) {
		todo!()
	}
}

/// `include/node/multicam.h` exports (complete inventory): the four
/// input-id getters plus source-count / grid-math / current-source.
pub mod multicam {
	use super::*;

	/// `oaknode_multicam_input_current`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_input_current() -> *const c_char {
		todo!()
	}

	/// `oaknode_multicam_input_sources`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_input_sources() -> *const c_char {
		todo!()
	}

	/// `oaknode_multicam_input_sequence`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_input_sequence() -> *const c_char {
		todo!()
	}

	/// `oaknode_multicam_input_sequence_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_input_sequence_type(
	) -> *const c_char {
		todo!()
	}

	/// `oaknode_multicam_get_source_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_get_source_count(
		node: CHandle,
		out_count: *mut c_int,
	) -> c_int {
		todo!()
	}

	/// `oaknode_multicam_get_rows_and_columns`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_get_rows_and_columns(
		source_count: c_int,
		rows: *mut c_int,
		cols: *mut c_int,
	) -> c_int {
		todo!()
	}

	/// `oaknode_multicam_index_to_row_cols`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_index_to_row_cols(
		index: c_int,
		rows: c_int,
		cols: c_int,
		out_row: *mut c_int,
		out_col: *mut c_int,
	) -> c_int {
		todo!()
	}

	/// `oaknode_multicam_rows_cols_to_index`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_rows_cols_to_index(
		row: c_int,
		col: c_int,
		rows: c_int,
		cols: c_int,
	) -> c_int {
		todo!()
	}

	/// `oaknode_multicam_get_current_source`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_get_current_source(
		node: CHandle,
		out_source: *mut c_int,
	) -> c_int {
		todo!()
	}
}

/// `include/node/block.h`, `folder.h`, `footage.h`, `group.h`,
/// `colormanager.h`, `factory.h`, `serializer.h`, `traverser.h`: same
/// one-submodule-per-header pattern (create them as `block`/`folder`/
/// `footage`/`group`/`colormanager`/`factory`/`serializer`/`traverser`).
pub mod remaining_headers {
	// Stubs are added per header when the corresponding internal module
	// is implemented; keep header order and naming identical to the C
	// side so bindgen/diff audits stay mechanical.
}
