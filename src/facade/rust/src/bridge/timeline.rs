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
extern "C" {
	/// `oaktimeline_marker_list_create` — new owning list, refcount 1.
	pub fn oaktimeline_marker_list_create() -> CHandle;
	/// `oaktimeline_marker_list_of` — borrowed list of a viewer node.
	pub fn oaktimeline_marker_list_of(owner: CHandle) -> CHandle;
	/// `oaktimeline_marker_list_free` — NULL/empty no-op; clears `list->ctx`.
	pub fn oaktimeline_marker_list_free(list: *mut CHandle);
	/// `oaktimeline_marker_add` — append a marker directly (no command).
	pub fn oaktimeline_marker_add(
		list: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		name: *const c_char,
		color: c_int,
	) -> c_int;
	/// `oaktimeline_marker_count` — number of markers.
	pub fn oaktimeline_marker_count(list: CHandle, out_count: *mut c_int) -> c_int;
	/// `oaktimeline_marker_at` — marker at index; name two-stage.
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
	) -> c_int;
	/// `oaktimeline_marker_add_command` — owned `MarkerAddCommand`.
	pub fn oaktimeline_marker_add_command(
		list: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		name: *const c_char,
		color: c_int,
	) -> CHandle;
	/// `oaktimeline_marker_remove_at_command` — owned `MarkerRemoveCommand`.
	pub fn oaktimeline_marker_remove_at_command(list: CHandle, index: c_int) -> CHandle;
	/// `oaktimeline_marker_set_time_command` — owned `MarkerChangeTimeCommand`.
	pub fn oaktimeline_marker_set_time_command(
		list: CHandle,
		index: c_int,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> CHandle;
	/// `oaktimeline_marker_set_props_command` — owned color/name command.
	pub fn oaktimeline_marker_set_props_command(
		list: CHandle,
		index: c_int,
		color: c_int,
		name: *const c_char,
	) -> CHandle;
	/// `oaktimeline_marker_list_load` — read from an oakcommon reader.
	pub fn oaktimeline_marker_list_load(list: CHandle, reader: CHandle) -> c_int;
	/// `oaktimeline_marker_list_save` — write to an oakcommon writer.
	pub fn oaktimeline_marker_list_save(list: CHandle, writer: CHandle) -> c_int;

	// ---- include/timeline/workarea.h ----------------------------------------
	/// `oaktimeline_workarea_create` — new owning work area, refcount 1.
	pub fn oaktimeline_workarea_create() -> CHandle;
	/// `oaktimeline_workarea_of` — borrowed work area of a viewer node.
	pub fn oaktimeline_workarea_of(owner: CHandle) -> CHandle;
	/// `oaktimeline_workarea_free` — NULL/empty no-op; clears `w->ctx`.
	pub fn oaktimeline_workarea_free(w: *mut CHandle);
	/// `oaktimeline_workarea_set_enabled` — live.
	pub fn oaktimeline_workarea_set_enabled(w: CHandle, enabled: c_int) -> c_int;
	/// `oaktimeline_workarea_get` — state out; params may be NULL.
	pub fn oaktimeline_workarea_get(
		w: CHandle,
		in_num: *mut c_int,
		in_den: *mut c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
		enabled: *mut c_int,
	) -> c_int;
	/// `oaktimeline_workarea_set_range` — live.
	pub fn oaktimeline_workarea_set_range(
		w: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> c_int;
	/// `oaktimeline_workarea_set_range_command` — owned command, old range from caller.
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
	) -> CHandle;
	/// `oaktimeline_workarea_set_enabled_command` — owned command.
	pub fn oaktimeline_workarea_set_enabled_command(w: CHandle, enabled: c_int) -> CHandle;
	/// `oaktimeline_workarea_reset` — the reset sentinel range.
	pub fn oaktimeline_workarea_reset(
		in_num: *mut c_int,
		in_den: *mut c_int,
		out_num: *mut c_int,
		out_den: *mut c_int,
	) -> c_int;
	/// `oaktimeline_workarea_load` — read from an oakcommon reader.
	pub fn oaktimeline_workarea_load(w: CHandle, reader: CHandle) -> c_int;
	/// `oaktimeline_workarea_save` — write to an oakcommon writer.
	pub fn oaktimeline_workarea_save(w: CHandle, writer: CHandle) -> c_int;

	// ---- include/timeline/edit.h --------------------------------------------
	/// `oaktimeline_add_track_command` — owned `TimelineAddTrackCommand`.
	pub fn oaktimeline_add_track_command(list: CHandle) -> CHandle;
	/// `oaktimeline_remove_track_command` — owned `TimelineRemoveTrackCommand`.
	pub fn oaktimeline_remove_track_command(track: CHandle) -> CHandle;
	/// `oaktimeline_place_block_command` — owned `TrackPlaceBlockCommand`.
	pub fn oaktimeline_place_block_command(
		list: CHandle,
		track_index: c_int,
		block: CHandle,
		in_num: i64,
		in_den: i64,
	) -> CHandle;
	/// `oaktimeline_replace_block_with_gap_command` — owned command.
	pub fn oaktimeline_replace_block_with_gap_command(track: CHandle, block: CHandle) -> CHandle;
	/// `oaktimeline_trim_command` — owned `BlockTrimCommand`; `mode` is an
	/// `OakTimelineMovementMode` value.
	pub fn oaktimeline_trim_command(
		track: CHandle,
		block: CHandle,
		new_length_num: i64,
		new_length_den: i64,
		mode: c_int,
	) -> CHandle;
	/// `oaktimeline_split_command` — owned `BlockSplitCommand` (one point).
	pub fn oaktimeline_split_command(blocks: *const CHandle, count: c_int, point_num: i64, point_den: i64) -> CHandle;
	/// `oaktimeline_split_preserving_links_command` — owned command.
	pub fn oaktimeline_split_preserving_links_command(
		blocks: *const CHandle,
		count: c_int,
		point_nums: *const i64,
		point_dens: *const i64,
		time_count: c_int,
	) -> CHandle;
	/// `oaktimeline_ripple_delete_gaps_command` — owned command.
	pub fn oaktimeline_ripple_delete_gaps_command(
		sequence: CHandle,
		in_nums: *const i64,
		in_dens: *const i64,
		out_nums: *const i64,
		out_dens: *const i64,
		tracks: *const CHandle,
		range_count: c_int,
	) -> CHandle;
	/// `oaktimeline_slide_command` — owned `TrackSlideCommand`.
	pub fn oaktimeline_slide_command(
		track: CHandle,
		blocks: *const CHandle,
		block_count: c_int,
		in_adjacent: CHandle,
		out_adjacent: CHandle,
		movement_num: i64,
		movement_den: i64,
	) -> CHandle;
	/// `oaktimeline_ripple_remove_area_command` — owned
	/// `TrackRippleRemoveAreaCommand`.
	pub fn oaktimeline_ripple_remove_area_command(
		track: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
	) -> CHandle;
	/// `oaktimeline_insert_gaps_command` — owned `TrackListInsertGaps`.
	pub fn oaktimeline_insert_gaps_command(
		list: CHandle,
		point_num: i64,
		point_den: i64,
		length_num: i64,
		length_den: i64,
	) -> CHandle;
}
