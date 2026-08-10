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

//! oaktimeline C ABI bridge: direct Rust calls into the `oaktimeline` crate.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! call below is a compile-time Rust call into `oaktimeline`'s `ffi` (the
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

//! oaktimeline C ABI imports, mirroring the oaktimeline crate's exports
//! (`src/timeline/rust/src/ffi.rs`; headers `include/timeline/*.h`).
//!
//! Every handle crosses as [`crate::handle::CHandle`]. The marker/workarea
//! time quantities cross as `c_int` num/den pairs; the edit commands take
//! `i64` rationals. String getters report the size **including** the NUL;
//! the facade converts with [`crate::handle::string_result`].

use std::ffi::{c_char, c_int};

use crate::handle::CHandle;

// `include/timeline/marker.h` exports (complete inventory):
// oaktimeline_marker_list_create / free / of / add / count / at /
// add_command / remove_at_command / set_time_command /
// set_props_command / list_load / list_save.

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_list_create() -> CHandle {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_list_create() }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_list_of(owner: CHandle) -> CHandle {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_list_of(owner) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_list_free(list: *mut CHandle) {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_list_free(list) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_add(
		list: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		name: *const c_char,
		color: c_int,
	) -> c_int {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_add(list, in_num, in_den, out_num, out_den, name, color) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_count(list: CHandle, out_count: *mut c_int) -> c_int {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_count(list, out_count) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_at(
		list: CHandle,
		index: c_int,
		in_num: *mut c_int,
		in_den: *mut c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
		color: *mut c_int,
		name_buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_at(list, index, in_num, in_den, out_num, out_den, color, name_buf, buf_size) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_add_command(
		list: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		name: *const c_char,
		color: c_int,
	) -> CHandle {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_add_command(list, in_num, in_den, out_num, out_den, name, color) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_remove_at_command(list: CHandle, index: c_int) -> CHandle {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_remove_at_command(list, index) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_set_time_command(
		list: CHandle,
		index: c_int,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> CHandle {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_set_time_command(list, index, in_num, in_den, out_num, out_den) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_set_props_command(
		list: CHandle,
		index: c_int,
		color: c_int,
		name: *const c_char,
	) -> CHandle {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_set_props_command(list, index, color, name) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_list_load(list: CHandle, reader: CHandle) -> c_int {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_list_load(list, reader) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_marker_list_save(list: CHandle, writer: CHandle) -> c_int {
	unsafe { oaktimeline::ffi::marker::oaktimeline_marker_list_save(list, writer) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_create() -> CHandle {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_create() }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_of(owner: CHandle) -> CHandle {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_of(owner) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_free(w: *mut CHandle) {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_free(w) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_set_enabled(w: CHandle, enabled: c_int) -> c_int {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_set_enabled(w, enabled) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_get(
		w: CHandle,
		in_num: *mut c_int,
		in_den: *mut c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
		enabled: *mut c_int,
	) -> c_int {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_get(w, in_num, in_den, out_num, out_den, enabled) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_set_range(
		w: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> c_int {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_set_range(w, in_num, in_den, out_num, out_den) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_set_range_command(
		w: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		old_in_num: c_int,
		old_in_den: c_int,
		old_out_num: c_int,
		old_out_den: c_int,
	) -> CHandle {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_set_range_command(w, in_num, in_den, out_num, out_den, old_in_num, old_in_den, old_out_num, old_out_den) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_set_enabled_command(w: CHandle, enabled: c_int) -> CHandle {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_set_enabled_command(w, enabled) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_reset(
		in_num: *mut c_int,
		in_den: *mut c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
	) -> c_int {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_reset(in_num, in_den, out_num, out_den) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_load(w: CHandle, reader: CHandle) -> c_int {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_load(w, reader) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_workarea_save(w: CHandle, writer: CHandle) -> c_int {
	unsafe { oaktimeline::ffi::workarea::oaktimeline_workarea_save(w, writer) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_add_track_command(list: CHandle) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_add_track_command(list) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_remove_track_command(track: CHandle) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_remove_track_command(track) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_place_block_command(
		list: CHandle,
		track_index: c_int,
		block: CHandle,
		in_num: i64,
		in_den: i64,
	) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_place_block_command(list, track_index, block, in_num, in_den) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_replace_block_with_gap_command(track: CHandle, block: CHandle) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_replace_block_with_gap_command(track, block) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_trim_command(
		track: CHandle,
		block: CHandle,
		new_length_num: i64,
		new_length_den: i64,
		mode: c_int,
	) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_trim_command(track, block, new_length_num, new_length_den, mode) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_split_command(blocks: *const CHandle, count: c_int, point_num: i64, point_den: i64) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_split_command(blocks, count, point_num, point_den) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_split_preserving_links_command(
		blocks: *const CHandle,
		count: c_int,
		point_nums: *const i64,
		point_dens: *const i64,
		time_count: c_int,
	) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_split_preserving_links_command(blocks, count, point_nums, point_dens, time_count) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_ripple_delete_gaps_command(
		sequence: CHandle,
		in_nums: *const i64,
		in_dens: *const i64,
		out_nums: *const i64,
		out_dens: *const i64,
		tracks: *const CHandle,
		range_count: c_int,
	) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_ripple_delete_gaps_command(sequence, in_nums, in_dens, out_nums, out_dens, tracks, range_count) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_slide_command(
		track: CHandle,
		blocks: *const CHandle,
		block_count: c_int,
		in_adjacent: CHandle,
		out_adjacent: CHandle,
		movement_num: i64,
		movement_den: i64,
	) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_slide_command(track, blocks, block_count, in_adjacent, out_adjacent, movement_num, movement_den) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_ripple_remove_area_command(
		track: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
	) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_ripple_remove_area_command(track, in_num, in_den, out_num, out_den) }
}

/// Direct call into the `oaktimeline` crate (single-lib unification; the
/// `#[no_mangle]` export stays for the external C ABI).
pub fn oaktimeline_insert_gaps_command(
		list: CHandle,
		point_num: i64,
		point_den: i64,
		length_num: i64,
		length_den: i64,
	) -> CHandle {
	unsafe { oaktimeline::ffi::edit::oaktimeline_insert_gaps_command(list, point_num, point_den, length_num, length_den) }
}

