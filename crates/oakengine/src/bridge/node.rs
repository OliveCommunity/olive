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

//! oaknode C ABI bridge: direct Rust calls into the `oaknode` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oaknode`'s `ffi` (the
//! `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them). Handles cross as the shared
//! [`crate::handle::CHandle`]. Exceptions that keep an `extern "C"`
//! declaration (resolved at link time against the sibling crate in the
//! same dylib) are the host `oakcore_*` symbols and the encoding-params
//! C ABI POD crossings (the facade keeps its own POD mirrors there).

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

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_init() -> CHandle {
	unsafe { oaknode::ffi::project::oaknode_project_init() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_free(project: *mut CHandle) {
	unsafe { oaknode::ffi::project::oaknode_project_free(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_initialize(project: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_initialize(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_clear(project: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_clear(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_root(project: CHandle) -> CHandle {
	unsafe { oaknode::ffi::project::oaknode_project_root(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_name(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_name(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_filename(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_filename(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_pretty_filename(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_pretty_filename(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_set_filename(project: CHandle, filename: *const c_char) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_set_filename(project, filename) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_is_modified(project: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_is_modified(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_set_modified(project: CHandle, modified: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_set_modified(project, modified) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_is_new(project: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_is_new(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_cache_path(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_cache_path(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_copy_settings(dst: CHandle, src: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_copy_settings(dst, src) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_get_cache_location_setting(project: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_get_cache_location_setting(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_set_cache_location_setting(project: CHandle, setting: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_set_cache_location_setting(project, setting) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_get_custom_cache_path(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_get_custom_cache_path(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_set_custom_cache_path(project: CHandle, path: *const c_char) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_set_custom_cache_path(project, path) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_get_uuid(project: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_get_uuid(project, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_add_node(project: CHandle, node: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_add_node(project, node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_remove_node(project: CHandle, node: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_remove_node(project, node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_node_count(project: CHandle) -> c_int {
	unsafe { oaknode::ffi::project::oaknode_project_node_count(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_project_node_at(project: CHandle, index: c_int) -> CHandle {
	unsafe { oaknode::ffi::project::oaknode_project_node_at(project, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_debug_alive_count() -> c_int {
	unsafe { oaknode::ffi::node::oaknode_debug_alive_count() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_id(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_id(node, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_name(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_name(node, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_label(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_label(node, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_label(node: CHandle, label: *const c_char) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_label(node, label) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_label_undoable(node: CHandle, label: *const c_char, out_command: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_label_undoable(node, label, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_override_color(node: CHandle, out_value: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_override_color(node, out_value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_override_color(node: CHandle, index: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_override_color(node, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_override_color_undoable(node: CHandle, index: c_int, out_command: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_override_color_undoable(node, index, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_is_enabled(node: CHandle, out_value: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_is_enabled(node, out_value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_enabled(node: CHandle, enabled: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_enabled(node, enabled) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_enabled_undoable(node: CHandle, enabled: c_int, out_command: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_enabled_undoable(node, enabled, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_effect_input(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_effect_input(node, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_flags(node: CHandle) -> u64 {
	unsafe { oaknode::ffi::node::oaknode_node_get_flags(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_input_count(node: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_count(node, out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_input_id(node: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_id(node, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_input_get_type(node: CHandle, input_id: *const c_char, out_type: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_get_type(node, input_id, out_type) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_input_is_connected(node: CHandle, input_id: *const c_char, out_value: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_is_connected(node, input_id, out_value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_input_is_connectable(node: CHandle, input_id: *const c_char, out_value: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_is_connectable(node, input_id, out_value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_input_name(node: CHandle, input_id: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_input_name(node, input_id, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_input_get_connected_node(node: CHandle, input_id: *const c_char, out_node: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_get_connected_node(node, input_id, out_node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_input(
		node: CHandle,
		input_id: *const c_char,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_input(node, input_id, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_input(node: CHandle, input_id: *const c_char, v: *const crate::node::OakNodeValue) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_input(node, input_id, v) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_input_undoable(
		node: CHandle,
		input_id: *const c_char,
		v: *const crate::node::OakNodeValue,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_input_undoable(node, input_id, v, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_input_string(node: CHandle, input_id: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_input_string(node, input_id, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_input_string(node: CHandle, input_id: *const c_char, value: *const c_char) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_input_string(node, input_id, value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_input_string_undoable(
		node: CHandle,
		input_id: *const c_char,
		value: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_input_string_undoable(node, input_id, value, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_connect(output_node: CHandle, input_node: CHandle, input_id: *const c_char) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_connect(output_node, input_node, input_id) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_connect_undoable(
		output_node: CHandle,
		input_node: CHandle,
		input_id: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_connect_undoable(output_node, input_node, input_id, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_disconnect(input_node: CHandle, input_id: *const c_char) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_disconnect(input_node, input_id) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_disconnect_undoable(input_node: CHandle, input_id: *const c_char, out_command: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_disconnect_undoable(input_node, input_id, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_output_connection_count(node: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_output_connection_count(node, out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_output_connection_node_at(node: CHandle, index: c_int, out_node: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_output_connection_node_at(node, index, out_node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_output_connection_input_id_at(
		node: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_output_connection_input_id_at(node, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_output_connection_element_at(node: CHandle, index: c_int, out_element: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_output_connection_element_at(node, index, out_element) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_link(a: CHandle, b: CHandle, out_linked: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_link(a, b, out_linked) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_unlink(a: CHandle, b: CHandle, out_unlinked: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_unlink(a, b, out_unlinked) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_link_undoable(a: CHandle, b: CHandle, link: c_int, out_command: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_link_undoable(a, b, link, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_are_linked(a: CHandle, b: CHandle, out_value: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_are_linked(a, b, out_value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_link_count(node: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_link_count(node, out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_link_at(node: CHandle, index: c_int, out_node: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_link_at(node, index, out_node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_context_count(node: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_context_count(node, out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_context_node_at(node: CHandle, index: c_int, out_node: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_context_node_at(node, index, out_node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_context_position(
		node: CHandle,
		context: CHandle,
		out_x: *mut f64,
		out_y: *mut f64,
		out_expanded: *mut c_int,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_context_position(node, context, out_x, out_y, out_expanded) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_context_position(node: CHandle, context: CHandle, x: f64, y: f64, expanded: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_context_position(node, context, x, y, expanded) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_context_position_undoable(
		node: CHandle,
		context: CHandle,
		x: f64,
		y: f64,
		expanded: c_int,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_context_position_undoable(node, context, x, y, expanded, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_remove_from_context(node: CHandle, context: CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_remove_from_context(node, context) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_create_copy(node: CHandle) -> CHandle {
	unsafe { oaknode::ffi::node::oaknode_node_create_copy(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_copy_in_graph(node: CHandle, out_command: *mut CHandle) -> CHandle {
	unsafe { oaknode::ffi::node::oaknode_node_copy_in_graph(node, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_project(node: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_project(node, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_input_array_insert(node: CHandle, input_id: *const c_char, index: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_array_insert(node, input_id, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_input_array_remove(node: CHandle, input_id: *const c_char, index: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_input_array_remove(node, input_id, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_connect_element(output_node: CHandle, input_node: CHandle, input_id: *const c_char, element: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_connect_element(output_node, input_node, input_id, element) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_disconnect_element(input_node: CHandle, input_id: *const c_char, element: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_disconnect_element(input_node, input_id, element) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_command_create_add_node(graph: CHandle, node: CHandle) -> CHandle {
	unsafe { oaknode::ffi::node::oaknode_command_create_add_node(graph, node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_command_create_set_position_recursive(node: CHandle, context: CHandle, x: f64, y: f64) -> CHandle {
	unsafe { oaknode::ffi::node::oaknode_command_create_set_position_recursive(node, context, x, y) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_markers(node: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_markers(node, out as *mut std::ffi::c_void) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_work_area(node: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_work_area(node, out as *mut std::ffi::c_void) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_video_frame_cache(node: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_video_frame_cache(node, out as *mut std::ffi::c_void) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_copy_inputs(dst: CHandle, src: CHandle, include_connections: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_copy_inputs(dst, src, include_connections) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_value_hint_track(node: CHandle, input_id: *const c_char, track_type: c_int, track_index: c_int) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_value_hint_track(node, input_id, track_type, track_index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_viewer_set_video_params(viewer: CHandle, params: *const CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_viewer_set_video_params(viewer, params as *const std::ffi::c_void) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_viewer_set_audio_params(viewer: CHandle, params: *const c_void) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_viewer_set_audio_params(viewer, params) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_find_input_footage(node: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_find_input_footage(node, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_get_input_at_time(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_get_input_at_time(node, input_id, time_num, time_den, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_input_at_time_undoable(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		v: *const crate::node::OakNodeValue,
		track: c_int,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_input_at_time_undoable(node, input_id, time_num, time_den, v, track, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_identity(node: CHandle) -> usize {
	unsafe { oaknode::ffi::node::oaknode_node_identity(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_set_input_at_time_into(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		v: *const crate::node::OakNodeValue,
		track: c_int,
		multi_command: CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::node::oaknode_node_set_input_at_time_into(node, input_id, time_num, time_den, v, track, multi_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_command_create_remove_node(node: CHandle) -> CHandle {
	unsafe { oaknode::ffi::node::oaknode_command_create_remove_node(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_node_free(node: *mut CHandle) {
	unsafe { oaknode::ffi::node::oaknode_node_free(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_factory_initialize() -> c_int {
	unsafe { oaknode::ffi::factory::oaknode_factory_initialize() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_factory_destroy() {
	unsafe { oaknode::ffi::factory::oaknode_factory_destroy() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_factory_id_count(out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::factory::oaknode_factory_id_count(out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_factory_id_at(index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::factory::oaknode_factory_id_at(index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_factory_name_from_id(type_id: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::factory::oaknode_factory_name_from_id(type_id, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_factory_create_from_id(type_id: *const c_char) -> CHandle {
	unsafe { oaknode::ffi::factory::oaknode_factory_create_from_id(type_id) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_factory_node_at(index: c_int, out_node: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::factory::oaknode_factory_node_at(index, out_node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_create(project: CHandle) -> CHandle {
	unsafe { oaknode::ffi::folder::oaknode_folder_create(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_child_count(folder: CHandle) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_child_count(folder) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_child_at(folder: CHandle, index: c_int) -> CHandle {
	unsafe { oaknode::ffi::folder::oaknode_folder_child_at(folder, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_add_child(folder: CHandle, child: CHandle) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_add_child(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_as_node(folder: CHandle) -> CHandle {
	unsafe { oaknode::ffi::folder::oaknode_folder_as_node(folder) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_command_create_folder_add_child(folder: CHandle, child: CHandle) -> CHandle {
	unsafe { oaknode::ffi::folder::oaknode_command_create_folder_add_child(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_remove_child(folder: CHandle, child: CHandle) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_remove_child(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_move_children(nodes: *const CHandle, count: c_int, dest_folder: CHandle) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_move_children(nodes, count, dest_folder) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_has_child_recursive(folder: CHandle, child: CHandle) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_has_child_recursive(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_index_of_child(folder: CHandle, child: CHandle) -> c_int {
	unsafe { oaknode::ffi::folder::oaknode_folder_index_of_child(folder, child) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_folder_parent_of(node: CHandle) -> CHandle {
	unsafe { oaknode::ffi::folder::oaknode_folder_parent_of(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_create(project: CHandle, filename: *const c_char) -> CHandle {
	unsafe { oaknode::ffi::footage::oaknode_footage_create(project, filename) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_as_node(footage: CHandle) -> CHandle {
	unsafe { oaknode::ffi::footage::oaknode_footage_as_node(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_filename(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_filename(footage, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_set_filename(footage: CHandle, filename: *const c_char) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_filename(footage, filename) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_is_valid(footage: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_is_valid(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_timestamp(footage: CHandle, out_timestamp: *mut i64) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_timestamp(footage, out_timestamp) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_set_timestamp(footage: CHandle, timestamp: i64) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_timestamp(footage, timestamp) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_decoder(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_decoder(footage, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_total_stream_count(footage: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_total_stream_count(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_video_stream_count(footage: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_video_stream_count(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_audio_stream_count(footage: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_audio_stream_count(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_subtitle_stream_count(footage: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_subtitle_stream_count(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_duration(footage: CHandle, out_numerator: *mut c_int, out_denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_duration(footage, out_numerator, out_denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_proxy_enabled(footage: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_proxy_enabled(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_set_proxy_enabled(footage: CHandle, enabled: c_int) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_proxy_enabled(footage, enabled) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_proxy_path(footage: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_proxy_path(footage, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_proxy_state(footage: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_proxy_state(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_set_proxy(
		footage: CHandle,
		path: *const c_char,
		state: c_int,
		video_stream_index: c_int,
		preset_version: c_int,
		enabled: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_proxy(footage, path, state, video_stream_index, preset_version, enabled) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_clear_proxy(footage: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_clear_proxy(footage) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_get_video_params(footage: CHandle, index: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_get_video_params(footage, index, out as *mut std::ffi::c_void) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_set_video_params(footage: CHandle, index: c_int, params: *const CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_video_params(footage, index, params as *const std::ffi::c_void) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_get_video_length(footage: CHandle, out_num: *mut i64, out_den: *mut i64) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_get_video_length(footage, out_num, out_den) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_footage_set_cancel_atom(footage: CHandle, atom: CHandle) -> c_int {
	unsafe { oaknode::ffi::footage::oaknode_footage_set_cancel_atom(footage, atom) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_create() -> CHandle {
	unsafe { oaknode::ffi::group::oaknode_group_create() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_cast(node: CHandle) -> CHandle {
	unsafe { oaknode::ffi::group::oaknode_group_cast(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_free(group: *mut CHandle) {
	unsafe { oaknode::ffi::group::oaknode_group_free(group) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_add_input_passthrough(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_add_input_passthrough(group, node, input_id, element, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_add_input_passthrough_undoable(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_add_input_passthrough_undoable(group, node, input_id, element, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_remove_input_passthrough(group: CHandle, node: CHandle, input_id: *const c_char, element: c_int) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_remove_input_passthrough(group, node, input_id, element) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_passthrough_count(group: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_passthrough_count(group, out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_passthrough_id_at(group: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_passthrough_id_at(group, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_passthrough_input_at(
		group: CHandle,
		index: c_int,
		out_node: *mut CHandle,
		buf: *mut c_char,
		buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_passthrough_input_at(group, index, out_node, buf, buf_size, out_element) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_get_output_passthrough(group: CHandle, out_node: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_get_output_passthrough(group, out_node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_set_output_passthrough(group: CHandle, node: CHandle) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_set_output_passthrough(group, node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_set_output_passthrough_undoable(group: CHandle, node: CHandle, out_command: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_set_output_passthrough_undoable(group, node, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_group_resolve_input(
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		out_node: *mut CHandle,
		buf: *mut c_char,
		buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
	unsafe { oaknode::ffi::group::oaknode_group_resolve_input(node, input_id, element, out_node, buf, buf_size, out_element) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_create(
		time_num: i64,
		time_den: i64,
		value: *const crate::node::OakNodeValue,
		type_: c_int,
		track: c_int,
		element: c_int,
		input_id: *const c_char,
		parent_or_null: CHandle,
	) -> CHandle {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_create(time_num, time_den, value, type_, track, element, input_id, parent_or_null) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_free(keyframe: *mut CHandle) {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_free(keyframe) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_time(keyframe: CHandle, out_num: *mut i64, out_den: *mut i64) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_time(keyframe, out_num, out_den) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_time(keyframe: CHandle, time_num: i64, time_den: i64) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_time(keyframe, time_num, time_den) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_time_undoable(
		keyframe: CHandle,
		time_num: i64,
		time_den: i64,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_time_undoable(keyframe, time_num, time_den, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_value(keyframe: CHandle, out: *mut crate::node::OakNodeValue) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_value(keyframe, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_value(keyframe: CHandle, v: *const crate::node::OakNodeValue) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_value(keyframe, v) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_value_undoable(
		keyframe: CHandle,
		v: *const crate::node::OakNodeValue,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_value_undoable(keyframe, v, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_value_string(keyframe: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_value_string(keyframe, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_value_string(keyframe: CHandle, value: *const c_char) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_value_string(keyframe, value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_value_string_undoable(
		keyframe: CHandle,
		value: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_value_string_undoable(keyframe, value, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_type(keyframe: CHandle, out_type: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_type(keyframe, out_type) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_type(keyframe: CHandle, type_: c_int) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_type(keyframe, type_) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_type_undoable(keyframe: CHandle, type_: c_int, out_command: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_type_undoable(keyframe, type_, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_bezier_control(keyframe: CHandle, handle: c_int, out_x: *mut f64, out_y: *mut f64) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_bezier_control(keyframe, handle, out_x, out_y) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_bezier_control(keyframe: CHandle, handle: c_int, x: f64, y: f64) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_bezier_control(keyframe, handle, x, y) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_set_bezier_control_undoable(
		keyframe: CHandle,
		handle: c_int,
		x: f64,
		y: f64,
		out_command: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_set_bezier_control_undoable(keyframe, handle, x, y, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_track(keyframe: CHandle, out_track: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_track(keyframe, out_track) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_element(keyframe: CHandle, out_element: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_element(keyframe, out_element) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_input(keyframe: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_input(keyframe, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_parent(keyframe: CHandle, out_node: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_parent(keyframe, out_node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_get_valid_bezier_control(keyframe: CHandle, handle: c_int, out_x: *mut f64, out_y: *mut f64) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_get_valid_bezier_control(keyframe, handle, out_x, out_y) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_opposing_bezier_type(type_: c_int) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_opposing_bezier_type(type_) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_compute_paste_value(
		target_node: CHandle,
		keyframe: CHandle,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_compute_paste_value(target_node, keyframe, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_keyframe_has_sibling_at_time(keyframe: CHandle, time_num: i64, time_den: i64, out_value: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::keyframe::oaknode_keyframe_has_sibling_at_time(keyframe, time_num, time_den, out_value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_dragger_create(node: CHandle, input_id: *const c_char, element: c_int, track: c_int) -> CHandle {
	unsafe { oaknode::ffi::dragger::oaknode_dragger_create(node, input_id, element, track) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_dragger_start(
		dragger: CHandle,
		time_num: i64,
		time_den: i64,
		track: c_int,
		insert_on_all_tracks: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::dragger::oaknode_dragger_start(dragger, time_num, time_den, track, insert_on_all_tracks) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_dragger_drag(dragger: CHandle, value: *const crate::node::OakNodeValue) -> c_int {
	unsafe { oaknode::ffi::dragger::oaknode_dragger_drag(dragger, value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_dragger_end(dragger: CHandle, out_command: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::dragger::oaknode_dragger_end(dragger, out_command) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_dragger_is_started(dragger: CHandle, out_started: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::dragger::oaknode_dragger_is_started(dragger, out_started) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_dragger_free(dragger: *mut CHandle) {
	unsafe { oaknode::ffi::dragger::oaknode_dragger_free(dragger) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_input_current() -> *const c_char {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_input_current() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_input_sources() -> *const c_char {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_input_sources() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_input_sequence() -> *const c_char {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_input_sequence() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_input_sequence_type() -> *const c_char {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_input_sequence_type() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_get_source_count(node: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_get_source_count(node, out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_get_rows_and_columns(source_count: c_int, rows: *mut c_int, cols: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_get_rows_and_columns(source_count, rows, cols) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_index_to_row_cols(index: c_int, rows: c_int, cols: c_int, out_row: *mut c_int, out_col: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_index_to_row_cols(index, rows, cols, out_row, out_col) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_rows_cols_to_index(row: c_int, col: c_int, rows: c_int, cols: c_int) -> c_int {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_rows_cols_to_index(row, col, rows, cols) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_multicam_get_current_source(node: CHandle, out_source: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::multicam::oaknode_multicam_get_current_source(node, out_source) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_create() -> CHandle {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_create() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_free(sequence: *mut CHandle) {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_free(sequence) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_set_default_parameters(sequence: CHandle) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_set_default_parameters(sequence) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_as_node(sequence: CHandle) -> CHandle {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_as_node(sequence) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_from_node(node: CHandle) -> CHandle {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_from_node(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_track_list(sequence: CHandle, type_: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_track_list(sequence, type_, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_track_count(sequence: CHandle, type_: c_int, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_track_count(sequence, type_, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_track_at(sequence: CHandle, type_: c_int, index: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_track_at(sequence, type_, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_all_track_count(sequence: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_all_track_count(sequence, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_all_track_at(sequence: CHandle, index: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_all_track_at(sequence, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_playhead(sequence: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_playhead(sequence, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_set_playhead(sequence: CHandle, numerator: c_int, denominator: c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_set_playhead(sequence, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_length(sequence: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_length(sequence, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_video_length(sequence: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_video_length(sequence, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_audio_length(sequence: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_audio_length(sequence, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_verify_length(sequence: CHandle) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_verify_length(sequence) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_video_stream_count(sequence: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_video_stream_count(sequence, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_audio_stream_count(sequence: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_audio_stream_count(sequence, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_video_params(sequence: CHandle, index: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_video_params(sequence, index, out as *mut std::ffi::c_void) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_set_video_params(sequence: CHandle, index: c_int, params: CHandle) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_set_video_params(sequence, index, params) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_get_audio_params(sequence: CHandle, index: c_int, out: *mut *mut c_void) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_get_audio_params(sequence, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_sequence_set_audio_params(sequence: CHandle, index: c_int, params: *const c_void) -> c_int {
	unsafe { oaknode::ffi::sequence::oaknode_sequence_set_audio_params(sequence, index, params) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_as_node(track: CHandle) -> CHandle {
	unsafe { oaknode::ffi::track::oaknode_track_as_node(track) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_create(type_: c_int) -> CHandle {
	unsafe { oaknode::ffi::track::oaknode_track_create(type_) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_free(track: *mut CHandle) {
	unsafe { oaknode::ffi::track::oaknode_track_free(track) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_type(track: CHandle, type_: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_type(track, type_) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_set_type(track: CHandle, type_: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_set_type(track, type_) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_height(track: CHandle, height: *mut f64) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_height(track, height) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_set_height(track: CHandle, height: f64) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_set_height(track, height) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_height_in_pixels(track: CHandle, height: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_height_in_pixels(track, height) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_set_height_in_pixels(track: CHandle, height: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_set_height_in_pixels(track, height) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_default_height_in_pixels() -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_default_height_in_pixels() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_minimum_height_in_pixels() -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_minimum_height_in_pixels() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_index(track: CHandle, index: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_index(track, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_set_index(track: CHandle, index: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_set_index(track, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_muted(track: CHandle, muted: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_muted(track, muted) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_set_muted(track: CHandle, muted: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_set_muted(track, muted) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_locked(track: CHandle, locked: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_locked(track, locked) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_set_locked(track: CHandle, locked: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_set_locked(track, locked) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_reference(track: CHandle, type_: *mut c_int, index: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_reference(track, type_, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_length(track: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_length(track, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_sequence(track: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_sequence(track, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_block_count(track: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_block_count(track, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_block_at(track: CHandle, index: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_block_at(track, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_append_block(track: CHandle, block: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_append_block(track, block) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_prepend_block(track: CHandle, block: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_prepend_block(track, block) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_insert_block_at_index(track: CHandle, block: CHandle, index: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_insert_block_at_index(track, block, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_insert_block_after(track: CHandle, block: CHandle, before: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_insert_block_after(track, block, before) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_insert_block_before(track: CHandle, block: CHandle, after: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_insert_block_before(track, block, after) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_ripple_remove_block(track: CHandle, block: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_ripple_remove_block(track, block) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_replace_block(track: CHandle, old_block: CHandle, new_block: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_replace_block(track, old_block, new_block) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_block_index(track: CHandle, block: CHandle, index: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_block_index(track, block, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_block_containing_time(track: CHandle, numerator: c_int, denominator: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_block_containing_time(track, numerator, denominator, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_visible_block_at_time(track: CHandle, numerator: c_int, denominator: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_visible_block_at_time(track, numerator, denominator, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_is_range_free(
		track: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		is_free: *mut c_int,
	) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_is_range_free(track, in_num, in_den, out_num, out_den, is_free) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_nearest_block_before_or_at(track: CHandle, numerator: c_int, denominator: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_nearest_block_before_or_at(track, numerator, denominator, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_track_get_nearest_block_after_or_at(track: CHandle, numerator: c_int, denominator: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_track_get_nearest_block_after_or_at(track, numerator, denominator, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_get_sequence(list: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_sequence(list, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_get_track_input_id(list: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_track_input_id(list, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_array_append(list: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_array_append(list) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_array_remove_last(list: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_array_remove_last(list) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_get_array_index_from_cache_index(list: CHandle, cache_index: c_int, out_index: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_array_index_from_cache_index(list, cache_index, out_index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_get_type(list: CHandle, type_: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_type(list, type_) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_get_track_count(list: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_track_count(list, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_get_track_at(list: CHandle, index: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_track_at(list, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_get_total_length(list: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_total_length(list, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_get_array_size(list: CHandle, size: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_get_array_size(list, size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_add_track(list: CHandle, track: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_add_track(list, track) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_tracklist_remove_track(list: CHandle, track: CHandle) -> c_int {
	unsafe { oaknode::ffi::track::oaknode_tracklist_remove_track(list, track) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_clip_create() -> CHandle {
	unsafe { oaknode::ffi::block::oaknode_block_clip_create() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_gap_create() -> CHandle {
	unsafe { oaknode::ffi::block::oaknode_block_gap_create() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_transition_create(kind: c_int) -> CHandle {
	unsafe { oaknode::ffi::block::oaknode_block_transition_create(kind) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_free(block: *mut CHandle) {
	unsafe { oaknode::ffi::block::oaknode_block_free(block) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_kind(block: CHandle, out_kind: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_kind(block, out_kind) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_as_node(block: CHandle) -> CHandle {
	unsafe { oaknode::ffi::block::oaknode_block_as_node(block) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_from_node(node: CHandle) -> CHandle {
	unsafe { oaknode::ffi::block::oaknode_block_from_node(node) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_in(block: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_in(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_set_in(block: CHandle, numerator: c_int, denominator: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_set_in(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_out(block: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_out(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_set_out(block: CHandle, numerator: c_int, denominator: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_set_out(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_length(block: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_length(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_set_length_and_media_out(block: CHandle, numerator: c_int, denominator: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_set_length_and_media_out(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_set_length_and_media_in(block: CHandle, numerator: c_int, denominator: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_set_length_and_media_in(block, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_enabled(block: CHandle, enabled: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_enabled(block, enabled) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_set_enabled(block: CHandle, enabled: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_set_enabled(block, enabled) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_previous(block: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_previous(block, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_next(block: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_next(block, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_track(block: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_track(block, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_link(a: CHandle, b: CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_link(a, b) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_unlink(a: CHandle, b: CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_unlink(a, b) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_are_linked(a: CHandle, b: CHandle, linked: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_are_linked(a, b, linked) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_link_count(block: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_link_count(block, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_block_get_link_at(block: CHandle, index: c_int, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_block_get_link_at(block, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_get_media_in(clip: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_get_media_in(clip, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_set_media_in(clip: CHandle, numerator: c_int, denominator: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_set_media_in(clip, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_get_speed(clip: CHandle, speed: *mut f64) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_get_speed(clip, speed) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_set_speed(clip: CHandle, speed: f64) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_set_speed(clip, speed) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_get_reverse(clip: CHandle, reverse: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_get_reverse(clip, reverse) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_set_reverse(clip: CHandle, reverse: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_set_reverse(clip, reverse) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_get_maintain_audio_pitch(clip: CHandle, maintain: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_get_maintain_audio_pitch(clip, maintain) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_set_maintain_audio_pitch(clip: CHandle, maintain: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_set_maintain_audio_pitch(clip, maintain) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_get_loop_mode(clip: CHandle, loop_mode: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_get_loop_mode(clip, loop_mode) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_set_loop_mode(clip: CHandle, loop_mode: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_set_loop_mode(clip, loop_mode) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_get_track_type(clip: CHandle, type_: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_get_track_type(clip, type_) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_transition_get_in_offset(transition: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_transition_get_in_offset(transition, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_transition_get_out_offset(transition: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_transition_get_out_offset(transition, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_transition_get_offset_center(transition: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_transition_get_offset_center(transition, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_transition_set_offset_center(transition: CHandle, numerator: c_int, denominator: c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_transition_set_offset_center(transition, numerator, denominator) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_transition_set_offsets_and_length(
		transition: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_transition_set_offsets_and_length(transition, in_num, in_den, out_num, out_den) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_transition_is_dual(transition: CHandle, dual: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_transition_is_dual(transition, dual) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_transition_get_connected_out_block(transition: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_transition_get_connected_out_block(transition, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_transition_get_connected_in_block(transition: CHandle, out: *mut CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_transition_get_connected_in_block(transition, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_clip_add_cache_passthrough_from(clip: CHandle, other: CHandle) -> c_int {
	unsafe { oaknode::ffi::block::oaknode_clip_add_cache_passthrough_from(clip, other) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_init(project: CHandle) -> CHandle {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_init(project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_free(manager: *mut CHandle) {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_free(manager) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_wrap_borrowed(native_manager: *mut c_void) -> CHandle {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_wrap_borrowed(native_manager) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_initialize(manager: CHandle) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_initialize(manager) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_set_up_default_config() -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_set_up_default_config() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_config_filename(manager: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_config_filename(manager, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_set_config_filename(manager: CHandle, filename: *const c_char) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_set_config_filename(manager, filename) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_update_config_from_filename(manager: CHandle) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_update_config_from_filename(manager) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_default_input_color_space(manager: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_default_input_color_space(manager, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_set_default_input_color_space(manager: CHandle, colorspace: *const c_char) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_set_default_input_color_space(manager, colorspace) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_reference_color_space(manager: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_reference_color_space(manager, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_compliant_color_space(
		manager: CHandle,
		colorspace: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_compliant_color_space(manager, colorspace, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_colorspace_for_ffmpeg_tags(
		manager: CHandle,
		primaries: c_int,
		trc: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_colorspace_for_ffmpeg_tags(manager, primaries, trc, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_display_count(manager: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_display_count(manager, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_display_at(manager: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_display_at(manager, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_default_display(manager: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_default_display(manager, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_view_count(manager: CHandle, display: *const c_char, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_view_count(manager, display, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_view_at(
		manager: CHandle,
		display: *const c_char,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_view_at(manager, display, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_default_view(manager: CHandle, display: *const c_char, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_default_view(manager, display, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_look_count(manager: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_look_count(manager, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_look_at(manager: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_look_at(manager, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_colorspace_count(manager: CHandle, count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_colorspace_count(manager, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_colorspace_at(manager: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_colorspace_at(manager, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_default_luma_coefs(manager: CHandle, rgb: *mut f64) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_default_luma_coefs(manager, rgb) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_colormanager_get_compliant_color_transform(
		manager: CHandle,
		transform: CHandle,
		force_display: c_int,
		out: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::colormanager::oaknode_colormanager_get_compliant_color_transform(manager, transform, force_display, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_init() -> CHandle {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_init() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_free(traverser: *mut CHandle) {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_free(traverser) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_generate_database(
		traverser: CHandle,
		node: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		out_db: *mut CHandle,
	) -> c_int {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_generate_database(traverser, node, in_num, in_den, out_num, out_den, out_db) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_database_free(db: *mut CHandle) {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_database_free(db) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_database_row_count(db: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_database_row_count(db, out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_database_row_key_at(db: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_database_row_key_at(db, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_database_row_value_count(db: CHandle, key: *const c_char, out_count: *mut c_int) -> c_int {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_database_row_value_count(db, key, out_count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_database_value_at(
		db: CHandle,
		key: *const c_char,
		index: c_int,
		out: *mut crate::node::OakNodeValue,
	) -> c_int {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_database_value_at(db, key, index, out) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_traverser_database_value_string_at(
		db: CHandle,
		key: *const c_char,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::traverser::oaknode_traverser_database_value_string_at(db, key, index, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_initialize() -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_initialize() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_shutdown() {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_shutdown() }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_savedata_create(load_type: c_int, project: CHandle) -> CHandle {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_savedata_create(load_type, project) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_savedata_free(save_data: *mut CHandle) {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_savedata_free(save_data) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_savedata_set_nodes(save_data: CHandle, nodes: *const CHandle, count: c_int) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_savedata_set_nodes(save_data, nodes, count) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_savedata_set_property(
		save_data: CHandle,
		node: CHandle,
		key: *const c_char,
		value: *const c_char,
	) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_savedata_set_property(save_data, node, key, value) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_save_to_xml(save_data: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_save_to_xml(save_data, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_load_from_xml(
		project: CHandle,
		xml: *const c_char,
		load_type: c_int,
		out_result: *mut c_int,
		out_load_data: *mut CHandle,
		details_buf: *mut c_char,
		details_buf_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_load_from_xml(project, xml, load_type, out_result, out_load_data, details_buf, details_buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_loaddata_free(load_data: *mut CHandle) {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_free(load_data) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_loaddata_node_count(load_data: CHandle) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_node_count(load_data) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_loaddata_node_at(load_data: CHandle, index: c_int) -> CHandle {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_node_at(load_data, index) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_loaddata_get_property(
		load_data: CHandle,
		node: CHandle,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_get_property(load_data, node, key, buf, buf_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_loaddata_connection_count(load_data: CHandle) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_connection_count(load_data) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_loaddata_connection_at(
		load_data: CHandle,
		index: c_int,
		out_output_node: *mut CHandle,
		out_input_node: *mut CHandle,
		input_id_buf: *mut c_char,
		input_id_buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_loaddata_connection_at(load_data, index, out_output_node, out_input_node, input_id_buf, input_id_buf_size, out_element) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_save_to_file(
		project: CHandle,
		filename: *const c_char,
		use_compression: c_int,
		out_code: *mut c_int,
		details: *mut c_char,
		details_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_save_to_file(project, filename, use_compression, out_code, details, details_size) }
}

/// Direct call into the `oaknode` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaknode_serializer_load_from_file(
		project: CHandle,
		filename: *const c_char,
		out_code: *mut c_int,
		details: *mut c_char,
		details_size: c_int,
	) -> c_int {
	unsafe { oaknode::ffi::serializer::oaknode_serializer_load_from_file(project, filename, out_code, details, details_size) }
}

