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

//! Phase-2/3 ffi family contract tests: sequence, track, block, folder,
//! footage, colormanager, traverser, serializer, the undoable node
//! exports, the multicam grid family and the deferred bridge exports.
//! One success + one failure path per family.

use std::ffi::{c_char, c_int, CString};

use oaknode::error::{OAKNODE_E_INVALID, OAKNODE_E_NOT_FOUND, OAKNODE_E_STATE, OAKNODE_OK};
use oaknode::ffi::block::{
	oaknode_block_as_node, oaknode_block_clip_create, oaknode_block_free,
	oaknode_block_from_node, oaknode_block_get_enabled, oaknode_block_get_kind,
	oaknode_block_get_length, oaknode_block_get_out, oaknode_block_gap_create,
	oaknode_block_set_enabled, oaknode_block_set_out, oaknode_clip_get_media_in,
	oaknode_clip_get_speed, oaknode_clip_set_media_in, oaknode_clip_set_speed,
	oaknode_transition_get_in_offset, oaknode_transition_set_offsets_and_length,
};
use oaknode::ffi::colormanager::{
	oaknode_colormanager_get_compliant_color_transform, oaknode_colormanager_get_config_filename,
	oaknode_colormanager_get_display_count, oaknode_colormanager_get_native,
	oaknode_colormanager_init, oaknode_colormanager_initialize, oaknode_colormanager_set_config_filename,
};
use oaknode::ffi::dragger::{
	oaknode_dragger_create, oaknode_dragger_drag, oaknode_dragger_end, oaknode_dragger_free,
	oaknode_dragger_is_started, oaknode_dragger_start,
};
use oaknode::ffi::folder::{
	oaknode_folder_add_child, oaknode_folder_as_node, oaknode_folder_child_at,
	oaknode_folder_child_count, oaknode_folder_create, oaknode_folder_has_child_recursive,
	oaknode_folder_index_of_child, oaknode_folder_parent_of, oaknode_folder_remove_child,
};
use oaknode::ffi::footage::{
	oaknode_footage_as_node, oaknode_footage_clear_proxy, oaknode_footage_create,
	oaknode_footage_filename, oaknode_footage_get_video_params, oaknode_footage_is_valid,
	oaknode_footage_proxy_path, oaknode_footage_set_filename, oaknode_footage_set_proxy,
	oaknode_footage_set_video_params,
};
use oaknode::ffi::group::{
	oaknode_group_add_input_passthrough, oaknode_group_add_input_passthrough_undoable,
	oaknode_group_cast, oaknode_group_create, oaknode_group_free, oaknode_group_get_output_passthrough,
	oaknode_group_passthrough_count, oaknode_group_passthrough_id_at,
	oaknode_group_passthrough_input_at, oaknode_group_remove_input_passthrough,
	oaknode_group_resolve_input, oaknode_group_set_output_passthrough,
	oaknode_group_set_output_passthrough_undoable,
};
use oaknode::ffi::keyframe::{
	oaknode_keyframe_compute_paste_value, oaknode_keyframe_create, oaknode_keyframe_free,
	oaknode_keyframe_get_bezier_control, oaknode_keyframe_get_element, oaknode_keyframe_get_input,
	oaknode_keyframe_get_parent, oaknode_keyframe_get_time, oaknode_keyframe_get_track,
	oaknode_keyframe_get_type, oaknode_keyframe_get_valid_bezier_control,
	oaknode_keyframe_get_value, oaknode_keyframe_get_value_string, oaknode_keyframe_has_sibling_at_time,
	oaknode_keyframe_opposing_bezier_type, oaknode_keyframe_set_bezier_control,
	oaknode_keyframe_set_bezier_control_undoable, oaknode_keyframe_set_time,
	oaknode_keyframe_set_time_undoable, oaknode_keyframe_set_type, oaknode_keyframe_set_type_undoable,
	oaknode_keyframe_set_value, oaknode_keyframe_set_value_string,
	oaknode_keyframe_set_value_string_undoable, oaknode_keyframe_set_value_undoable,
};
use oaknode::ffi::multicam::{
	oaknode_multicam_get_current_source, oaknode_multicam_get_rows_and_columns,
	oaknode_multicam_get_source_count, oaknode_multicam_index_to_row_cols,
	oaknode_multicam_input_current, oaknode_multicam_input_sequence,
	oaknode_multicam_input_sequence_type, oaknode_multicam_input_sources,
	oaknode_multicam_rows_cols_to_index,
};
use oaknode::ffi::node::{
	oaknode_node_connect, oaknode_node_connect_undoable, oaknode_node_disconnect,
	oaknode_node_disconnect_undoable, oaknode_node_find_input_footage, oaknode_node_get_input,
	oaknode_node_get_input_at_time, oaknode_node_get_markers, oaknode_node_get_video_frame_cache,
	oaknode_node_get_work_area, oaknode_node_set_enabled_undoable,
	oaknode_node_set_input_at_time_undoable, oaknode_node_set_input_undoable,
	oaknode_node_set_label_undoable, oaknode_viewer_set_audio_params,
	oaknode_viewer_set_video_params,
};
use oaknode::ffi::project::{
	oaknode_project_add_node, oaknode_project_free, oaknode_project_init,
	oaknode_project_initialize, oaknode_project_node_at,
};
use oaknode::ffi::sequence::{
	oaknode_sequence_as_node, oaknode_sequence_create, oaknode_sequence_free,
	oaknode_sequence_from_node, oaknode_sequence_get_audio_params,
	oaknode_sequence_get_audio_stream_count, oaknode_sequence_get_playhead,
	oaknode_sequence_get_track_count, oaknode_sequence_get_track_list,
	oaknode_sequence_get_video_params, oaknode_sequence_get_video_stream_count,
	oaknode_sequence_set_audio_params, oaknode_sequence_set_default_parameters,
	oaknode_sequence_set_playhead, oaknode_sequence_set_video_params,
	oaknode_sequence_verify_length,
};
use oaknode::ffi::serializer::{
	oaknode_serializer_initialize, oaknode_serializer_loaddata_free,
	oaknode_serializer_loaddata_node_at, oaknode_serializer_loaddata_node_count,
	oaknode_serializer_load_from_file, oaknode_serializer_load_from_xml,
	oaknode_serializer_savedata_create, oaknode_serializer_savedata_free,
	oaknode_serializer_save_to_file, oaknode_serializer_save_to_xml,
};
use oaknode::ffi::track::{
	oaknode_track_append_block, oaknode_track_as_node, oaknode_track_create,
	oaknode_track_free, oaknode_track_get_block_at, oaknode_track_get_block_count,
	oaknode_track_get_height, oaknode_track_get_index, oaknode_track_get_length,
	oaknode_track_get_muted, oaknode_track_get_reference, oaknode_track_get_type,
	oaknode_track_prepend_block, oaknode_track_ripple_remove_block,
	oaknode_track_set_height, oaknode_track_set_index, oaknode_track_set_muted,
	oaknode_tracklist_add_track, oaknode_tracklist_array_append,
	oaknode_tracklist_get_array_size, oaknode_tracklist_get_track_at,
	oaknode_tracklist_get_track_count, oaknode_tracklist_get_track_input_id,
	oaknode_tracklist_get_type, oaknode_tracklist_remove_track,
};
use oaknode::ffi::traverser::{
	oaknode_traverser_database_free, oaknode_traverser_database_row_count,
	oaknode_traverser_generate_database, oaknode_traverser_init,
};
use oaknode::handle::CHandle;
use oaknode::value::{oak, OakNodeValue};

static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn lock() -> std::sync::MutexGuard<'static, ()> {
	LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

fn dup(h: &CHandle) -> CHandle {
	h.clone()
}

fn two_stage<F: FnMut(*mut c_char, i32) -> i32>(mut getter: F) -> Result<String, i32> {
	let needed = getter(std::ptr::null_mut(), 0);
	if needed < 0 {
		return Err(needed);
	}
	let mut buf = vec![0u8; needed as usize];
	let rc = getter(buf.as_mut_ptr() as *mut c_char, needed);
	if rc < 0 {
		return Err(rc);
	}
	buf.pop();
	Ok(String::from_utf8(buf).unwrap())
}

fn free(h: &mut CHandle) {
	if !h.ctx.is_null() {
		unsafe { (h.release.unwrap())(h.ctx) };
		h.ctx = std::ptr::null_mut();
	}
}

/// Folder family: create/add/remove/query + failure paths.
#[test]
fn folder_family() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);

	let mut folder = unsafe { oaknode_folder_create(dup(&p)) };
	assert!(!folder.ctx.is_null());
	assert!(unsafe { oaknode_folder_create(CHandle::null()) }.ctx.is_null());

	let mut root = unsafe { oaknode_project_node_at(dup(&p), 0) };
	let mut node = unsafe { oaknode_folder_as_node(dup(&folder)) };
	assert!(!node.ctx.is_null());

	// Add a subfolder as a child of the root, then re-parent it.
	let mut child = unsafe { oaknode_folder_create(dup(&p)) };
	assert_eq!(
		unsafe { oaknode_folder_add_child(dup(&root), dup(&child)) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oaknode_folder_child_count(dup(&root)) }, 1);
	let mut child_at = unsafe { oaknode_folder_child_at(dup(&root), 0) };
	assert!(!child_at.ctx.is_null());
	assert!(unsafe { oaknode_folder_child_at(dup(&root), 5) }.ctx.is_null());
	assert_eq!(unsafe { oaknode_folder_index_of_child(dup(&root), dup(&child)) }, 0);
	assert_eq!(
		unsafe { oaknode_folder_index_of_child(dup(&root), dup(&folder)) },
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(
		unsafe { oaknode_folder_has_child_recursive(dup(&root), dup(&child)) },
		1
	);
	let mut parent = unsafe { oaknode_folder_parent_of(dup(&child)) };
	assert!(!parent.ctx.is_null());
	assert!(unsafe { oaknode_folder_parent_of(dup(&folder)) }.ctx.is_null());

	// Double-add to another folder fails E_STATE.
	let mut other = unsafe { oaknode_folder_create(dup(&p)) };
	assert_eq!(
		unsafe { oaknode_folder_add_child(dup(&other), dup(&child)) },
		OAKNODE_E_STATE
	);
	assert_eq!(
		unsafe { oaknode_folder_remove_child(dup(&root), dup(&child)) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oaknode_folder_child_count(dup(&root)) }, 0);
	assert_eq!(
		unsafe { oaknode_folder_remove_child(dup(&root), dup(&child)) },
		OAKNODE_E_NOT_FOUND
	);

	free(&mut parent);
	free(&mut child_at);
	free(&mut child);
	free(&mut other);
	free(&mut node);
	free(&mut folder);
	free(&mut root);
	unsafe { oaknode_project_free(&mut p) };
}

/// Sequence family: create → track lists, default params, playhead,
/// lengths + failure paths.
#[test]
fn sequence_family() {
	let _g = lock();
	let mut seq = unsafe { oaknode_sequence_create() };
	assert!(!seq.ctx.is_null());
	let mut node = unsafe { oaknode_sequence_as_node(dup(&seq)) };
	assert!(!node.ctx.is_null());
	let back = unsafe { oaknode_sequence_from_node(dup(&node)) };
	assert!(!back.ctx.is_null());
	let _ = back;

	// Default parameters: one video + one audio stream.
	assert_eq!(
		unsafe { oaknode_sequence_set_default_parameters(dup(&seq)) },
		OAKNODE_OK
	);
	let mut vc = 0;
	assert_eq!(unsafe { oaknode_sequence_get_audio_stream_count(dup(&seq), &mut vc) }, OAKNODE_OK);
	assert_eq!(vc, 1);

	// Track lists exist with zero tracks.
	let mut list = CHandle::null();
	assert_eq!(unsafe { oaknode_sequence_get_track_list(dup(&seq), 0, &mut list) }, OAKNODE_OK);
	assert!(!list.ctx.is_null());
	assert_eq!(
		unsafe { oaknode_sequence_get_track_list(dup(&seq), 99, &mut list) },
		OAKNODE_E_NOT_FOUND
	);
	let mut tc = 0;
	assert_eq!(unsafe { oaknode_sequence_get_track_count(dup(&seq), 0, &mut tc) }, OAKNODE_OK);
	assert_eq!(tc, 0);

	// Playhead round-trip.
	assert_eq!(unsafe { oaknode_sequence_set_playhead(dup(&seq), 30, 1) }, OAKNODE_OK);
	let mut num = 0;
	let mut den = 0;
	assert_eq!(unsafe { oaknode_sequence_get_playhead(dup(&seq), &mut num, &mut den) }, OAKNODE_OK);
	assert_eq!((num, den), (30, 1));

	// verify_length on an empty sequence.
	assert_eq!(unsafe { oaknode_sequence_verify_length(dup(&seq)) }, OAKNODE_OK);

	// Failure paths on empty handles.
	assert_eq!(unsafe { oaknode_sequence_get_playhead(CHandle::null(), &mut num, &mut den) }, OAKNODE_E_INVALID);
	assert!(unsafe { oaknode_sequence_from_node(CHandle::null()) }.ctx.is_null());

	free(&mut list);
	free(&mut node);
	unsafe { oaknode_sequence_free(&mut seq) };
}

/// Track + tracklist family: block ordering primitives, track flags,
/// add/remove tracks + failure paths.
#[test]
fn track_family() {
	let _g = lock();
	let mut track = unsafe { oaknode_track_create(0) }; // video
	assert!(!track.ctx.is_null());
	assert!(unsafe { oaknode_track_create(99) }.ctx.is_null());

	let mut ty = -1;
	assert_eq!(unsafe { oaknode_track_get_type(dup(&track), &mut ty) }, OAKNODE_OK);
	assert_eq!(ty, 0);
	let mut node = unsafe { oaknode_track_as_node(dup(&track)) };
	assert!(!node.ctx.is_null());

	// Height (internal <-> pixels).
	let mut h = 0.0;
	assert_eq!(unsafe { oaknode_track_get_height(dup(&track), &mut h) }, OAKNODE_OK);
	assert!((h - 3.0).abs() < 1e-9);
	assert_eq!(unsafe { oaknode_track_set_height(dup(&track), 5.0) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_track_get_height(dup(&track), &mut h) }, OAKNODE_OK);
	assert!((h - 5.0).abs() < 1e-9);

	// Muted/locked/index.
	assert_eq!(unsafe { oaknode_track_set_muted(dup(&track), 1) }, OAKNODE_OK);
	let mut muted = 0;
	assert_eq!(unsafe { oaknode_track_get_muted(dup(&track), &mut muted) }, OAKNODE_OK);
	assert_eq!(muted, 1);
	assert_eq!(unsafe { oaknode_track_set_index(dup(&track), 2) }, OAKNODE_OK);
	let mut idx = 0;
	assert_eq!(unsafe { oaknode_track_get_index(dup(&track), &mut idx) }, OAKNODE_OK);
	assert_eq!(idx, 2);
	let mut rty = 0;
	let mut ridx = 0;
	assert_eq!(unsafe { oaknode_track_get_reference(dup(&track), &mut rty, &mut ridx) }, OAKNODE_OK);
	assert_eq!((rty, ridx), (0, 2));

	// Blocks: append two clips, prepend one, count, remove.
	let mut clip1 = unsafe { oaknode_block_clip_create() };
	let mut clip2 = unsafe { oaknode_block_clip_create() };
	let mut clip3 = unsafe { oaknode_block_clip_create() };
	assert_eq!(unsafe { oaknode_track_append_block(dup(&track), dup(&clip1)) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_track_append_block(dup(&track), dup(&clip2)) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_track_prepend_block(dup(&track), dup(&clip3)) }, OAKNODE_OK);
	let mut bc = 0;
	assert_eq!(unsafe { oaknode_track_get_block_count(dup(&track), &mut bc) }, OAKNODE_OK);
	assert_eq!(bc, 3);
	let mut blk = CHandle::null();
	assert_eq!(unsafe { oaknode_track_get_block_at(dup(&track), 0, &mut blk) }, OAKNODE_OK);
	assert!(!blk.ctx.is_null());
	assert_eq!(unsafe { oaknode_track_get_block_at(dup(&track), 5, &mut blk) }, OAKNODE_E_NOT_FOUND);
	assert_eq!(unsafe { oaknode_track_ripple_remove_block(dup(&track), dup(&clip1)) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_track_get_block_count(dup(&track), &mut bc) }, OAKNODE_OK);
	assert_eq!(bc, 2);

	// Track length via the blocks' ranges.
	let mut len_num = 0;
	let mut len_den = 0;
	assert_eq!(unsafe { oaknode_track_get_length(dup(&track), &mut len_num, &mut len_den) }, OAKNODE_OK);
	assert_eq!(len_num, 1, "blocks default to 1s length");

	// Tracklist: build a sequence to host the list.
	let mut seq = unsafe { oaknode_sequence_create() };
	let mut snode = unsafe { oaknode_sequence_as_node(dup(&seq)) };
	let mut list2 = CHandle::null();
	assert_eq!(unsafe { oaknode_sequence_get_track_list(dup(&seq), 0, &mut list2) }, OAKNODE_OK);
	let mut lt = 0;
	assert_eq!(unsafe { oaknode_tracklist_get_type(dup(&list2), &mut lt) }, OAKNODE_OK);
	assert_eq!(lt, 0);
	let input_id = two_stage(|b, s| unsafe { oaknode_tracklist_get_track_input_id(dup(&list2), b, s) }).unwrap();
	assert_eq!(input_id, "track_in_0");
	assert_eq!(unsafe { oaknode_tracklist_array_append(dup(&list2)) }, OAKNODE_OK);
	let mut asz = 0;
	assert_eq!(unsafe { oaknode_tracklist_get_array_size(dup(&list2), &mut asz) }, OAKNODE_OK);
	assert_eq!(asz, 1);

	// Add the track to the list.
	assert_eq!(unsafe { oaknode_tracklist_add_track(dup(&list2), dup(&track)) }, OAKNODE_OK);
	let mut tcount = 0;
	assert_eq!(unsafe { oaknode_tracklist_get_track_count(dup(&list2), &mut tcount) }, OAKNODE_OK);
	assert_eq!(tcount, 1);
	let mut track_at = CHandle::null();
	assert_eq!(unsafe { oaknode_tracklist_get_track_at(dup(&list2), 0, &mut track_at) }, OAKNODE_OK);
	assert!(!track_at.ctx.is_null());
	assert_eq!(unsafe { oaknode_tracklist_remove_track(dup(&list2), dup(&track)) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_tracklist_remove_track(dup(&list2), dup(&track)) }, OAKNODE_E_NOT_FOUND);

	free(&mut track_at);
	free(&mut list2);
	free(&mut snode);
	unsafe { oaknode_sequence_free(&mut seq) };
	free(&mut blk);
	free(&mut clip1);
	free(&mut clip2);
	free(&mut clip3);
	free(&mut node);
	unsafe { oaknode_track_free(&mut track) };
}

/// Block family: clip/gap creation, kind, in/out/length, media_in,
/// speed + failure paths.
#[test]
fn block_family() {
	let _g = lock();
	let mut clip = unsafe { oaknode_block_clip_create() };
	let mut gap = unsafe { oaknode_block_gap_create() };
	assert!(!clip.ctx.is_null());
	assert!(!gap.ctx.is_null());

	let mut kind = 0;
	assert_eq!(unsafe { oaknode_block_get_kind(dup(&clip), &mut kind) }, OAKNODE_OK);
	assert_eq!(kind, 1, "clip kind");
	assert_eq!(unsafe { oaknode_block_get_kind(dup(&gap), &mut kind) }, OAKNODE_OK);
	assert_eq!(kind, 2, "gap kind");

	let mut node = unsafe { oaknode_block_as_node(dup(&clip)) };
	assert!(!node.ctx.is_null());
	let mut back = unsafe { oaknode_block_from_node(dup(&node)) };
	assert!(!back.ctx.is_null());
	assert!(unsafe { oaknode_block_from_node(CHandle::null()) }.ctx.is_null());

	// Length / out round-trip.
	let mut len_num = 0;
	let mut len_den = 0;
	assert_eq!(unsafe { oaknode_block_get_length(dup(&clip), &mut len_num, &mut len_den) }, OAKNODE_OK);
	assert_eq!((len_num, len_den), (1, 1));
	assert_eq!(unsafe { oaknode_block_set_out(dup(&clip), 5, 1) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_block_get_out(dup(&clip), &mut len_num, &mut len_den) }, OAKNODE_OK);
	assert_eq!((len_num, len_den), (5, 1));

	// Enabled.
	let mut en = 0;
	assert_eq!(unsafe { oaknode_block_get_enabled(dup(&clip), &mut en) }, OAKNODE_OK);
	assert_eq!(en, 1);
	assert_eq!(unsafe { oaknode_block_set_enabled(dup(&clip), 0) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_block_get_enabled(dup(&clip), &mut en) }, OAKNODE_OK);
	assert_eq!(en, 0);

	// Clip media_in/speed.
	let mut min = 0;
	let mut mden = 0;
	assert_eq!(unsafe { oaknode_clip_get_media_in(dup(&clip), &mut min, &mut mden) }, OAKNODE_OK);
	assert_eq!((min, mden), (0, 1));
	assert_eq!(unsafe { oaknode_clip_set_media_in(dup(&clip), 10, 1) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_clip_get_media_in(dup(&clip), &mut min, &mut mden) }, OAKNODE_OK);
	assert_eq!((min, mden), (10, 1));
	let mut speed = 0.0;
	assert_eq!(unsafe { oaknode_clip_get_speed(dup(&clip), &mut speed) }, OAKNODE_OK);
	assert_eq!(speed, 1.0);
	assert_eq!(unsafe { oaknode_clip_set_speed(dup(&clip), 2.0) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_clip_get_speed(dup(&clip), &mut speed) }, OAKNODE_OK);
	assert_eq!(speed, 2.0);

	// Transition offsets (non-transition clip -> E_INVALID).
	assert_eq!(
		unsafe { oaknode_transition_get_in_offset(dup(&clip), &mut min, &mut mden) },
		OAKNODE_E_STATE
	);

	// Failure paths.
	assert_eq!(unsafe { oaknode_block_get_length(CHandle::null(), &mut len_num, &mut len_den) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_block_get_kind(CHandle::null(), &mut kind) }, OAKNODE_E_INVALID);

	free(&mut back);
	free(&mut node);
	free(&mut clip);
	free(&mut gap);
}

/// Footage family: create/filename/proxy + failure paths.
#[test]
fn footage_family() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);

	let mut footage =
		unsafe { oaknode_footage_create(dup(&p), cs("/tmp/media.mov").as_ptr()) };
	assert!(!footage.ctx.is_null());
	assert!(unsafe { oaknode_footage_create(CHandle::null(), cs("x").as_ptr()) }
		.ctx
		.is_null());

	// Filename round-trip (two-stage).
	let name = two_stage(|b, s| unsafe { oaknode_footage_filename(dup(&footage), b, s) }).unwrap();
	assert_eq!(name, "/tmp/media.mov");
	assert_eq!(
		unsafe { oaknode_footage_set_filename(dup(&footage), cs("/new.mov").as_ptr()) },
		OAKNODE_OK
	);
	let name = two_stage(|b, s| unsafe { oaknode_footage_filename(dup(&footage), b, s) }).unwrap();
	assert_eq!(name, "/new.mov");
	assert_eq!(
		unsafe { oaknode_footage_filename(CHandle::null(), std::ptr::null_mut(), 0) },
		OAKNODE_E_INVALID
	);

	// Not probed -> invalid.
	assert_eq!(unsafe { oaknode_footage_is_valid(dup(&footage)) }, 0);

	// as_node cast.
	let mut node = unsafe { oaknode_footage_as_node(dup(&footage)) };
	assert!(!node.ctx.is_null());

	// Proxy fields.
	assert_eq!(
		unsafe {
			oaknode_footage_set_proxy(
				dup(&footage),
				cs("/proxy.mov").as_ptr(),
				2,
				0,
				1,
				1,
			)
		},
		OAKNODE_OK
	);
	let proxy = two_stage(|b, s| unsafe { oaknode_footage_proxy_path(dup(&footage), b, s) }).unwrap();
	assert_eq!(proxy, "/proxy.mov");
	assert_eq!(unsafe { oaknode_footage_clear_proxy(dup(&footage)) }, OAKNODE_OK);
	let proxy = two_stage(|b, s| unsafe { oaknode_footage_proxy_path(dup(&footage), b, s) }).unwrap();
	assert_eq!(proxy, "");

	free(&mut node);
	free(&mut footage);
	unsafe { oaknode_project_free(&mut p) };
}

/// Colormanager family: state + config-dependent E_STATE paths.
#[test]
fn colormanager_family() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	let mut cm = unsafe { oaknode_colormanager_init(dup(&p)) };
	assert!(!cm.ctx.is_null());

	// State without a config: listings return E_STATE.
	let mut count = 0;
	assert_eq!(
		unsafe { oaknode_colormanager_get_display_count(dup(&cm), &mut count) },
		OAKNODE_E_STATE
	);

	// Filename round-trip.
	assert_eq!(
		unsafe { oaknode_colormanager_set_config_filename(dup(&cm), cs("/cfg.ocio").as_ptr()) },
		OAKNODE_OK
	);
	let name =
		two_stage(|b, s| unsafe { oaknode_colormanager_get_config_filename(dup(&cm), b, s) })
			.unwrap();
	assert_eq!(name, "/cfg.ocio");
	assert_eq!(
		unsafe { oaknode_colormanager_get_config_filename(CHandle::null(), std::ptr::null_mut(), 0) },
		OAKNODE_E_INVALID
	);

	// initialize marks the config loaded (built-in defaults without
	// oakrender).
	assert_eq!(unsafe { oaknode_colormanager_initialize(dup(&cm)) }, OAKNODE_OK);
	assert_eq!(
		unsafe { oaknode_colormanager_get_display_count(dup(&cm), &mut count) },
		OAKNODE_OK
	);
	assert_eq!(count, 1, "built-in default display");

	// get_native is a C++-only bridge export: NULL in the Rust port
	// (NULL-safe), for both a live and an empty handle.
	assert!(unsafe { oaknode_colormanager_get_native(dup(&cm)) }.is_null());
	assert!(unsafe { oaknode_colormanager_get_native(CHandle::null()) }.is_null());

	free(&mut cm);
	unsafe { oaknode_project_free(&mut p) };
}

/// Traverser family: database generation + row enumeration + failures.
#[test]
fn traverser_family() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);

	// A math node in the project.
	let mut math = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);

	let mut tr = unsafe { oaknode_traverser_init() };
	assert!(!tr.ctx.is_null());

	let mut db = CHandle::null();
	assert_eq!(
		unsafe { oaknode_traverser_generate_database(dup(&tr), dup(&math), 0, 1, 1, 1, &mut db) },
		OAKNODE_OK
	);
	assert!(!db.ctx.is_null());

	let mut rows = 0;
	assert_eq!(unsafe { oaknode_traverser_database_row_count(dup(&db), &mut rows) }, OAKNODE_OK);
	assert_eq!(rows, 1, "no input edges, so only the root row");
	// Failure: unknown database handle.
	assert_eq!(
		unsafe { oaknode_traverser_database_row_count(CHandle::null(), &mut rows) },
		OAKNODE_E_INVALID
	);

	unsafe { oaknode_traverser_database_free(&mut db) };
	free(&mut tr);
	free(&mut math);
	unsafe { oaknode_project_free(&mut p) };
}

/// Serializer family: initialize/save-to-xml/load round-trip + failures.
///
/// Needs the `test-stubs` feature: `save_to_xml`/`load_from_xml` route
/// through the oakcommon XML bridge, whose symbols only resolve in the
/// test binary when the in-crate stubs are compiled in.
#[cfg(feature = "test-stubs")]
#[test]
fn serializer_family() {
	let _g = lock();
	assert_eq!(unsafe { oaknode_serializer_initialize() }, OAKNODE_OK);

	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);

	let mut sd = unsafe { oaknode_serializer_savedata_create(1, dup(&p)) }; // LOAD_ONLY_NODES
	assert!(!sd.ctx.is_null());

	let xml = two_stage(|b, s| unsafe { oaknode_serializer_save_to_xml(dup(&sd), b, s) }).unwrap();
	assert!(xml.contains("<project"), "serializes a project document");
	assert_eq!(
		unsafe { oaknode_serializer_save_to_xml(CHandle::null(), std::ptr::null_mut(), 0) },
		OAKNODE_E_INVALID
	);

	// Load back into a fresh project.
	let mut p2 = unsafe { oaknode_project_init() };
	let mut result = -1;
	let mut ld = CHandle::null();
	let xml_c = cs(&xml);
	assert_eq!(
		unsafe {
			oaknode_serializer_load_from_xml(
				dup(&p2),
				xml_c.as_ptr(),
				1,
				&mut result,
				&mut ld,
				std::ptr::null_mut(),
				0,
			)
		},
		OAKNODE_OK
	);
	assert_eq!(result, 0, "OAKNODE_SERIALIZER_OK");
	assert!(!ld.ctx.is_null());
	let mut count = 0;
	assert_eq!(unsafe { oaknode_serializer_loaddata_node_count(dup(&ld)) }, 1);
	let _ = count;
	// node_at for the loaded node.
	let mut loaded_node = unsafe { oaknode_serializer_loaddata_node_at(dup(&ld), 0) };
	assert!(!loaded_node.ctx.is_null());
	assert!(unsafe { oaknode_serializer_loaddata_node_at(dup(&ld), 5) }.ctx.is_null());

	// Corrupt XML -> SERIALIZER_XML_ERROR result, empty load data.
	let mut result2 = -1;
	let mut ld2 = CHandle::null();
	let corrupt = cs("<<<not xml");
	assert_eq!(
		unsafe {
			oaknode_serializer_load_from_xml(
				dup(&p2),
				corrupt.as_ptr(),
				1,
				&mut result2,
				&mut ld2,
				std::ptr::null_mut(),
				0,
			)
		},
		OAKNODE_OK
	);
	assert_eq!(result2, 5, "OAKNODE_SERIALIZER_XML_ERROR");
	assert!(ld2.ctx.is_null());

	// save_to_file / load_from_file round-trip into a temp file.
	let path = std::env::temp_dir().join(format!("oaknode-serializer-{}.oakproj", std::process::id()));
	let path_c = cs(path.to_str().unwrap());
	let mut file_code = -1;
	assert_eq!(
		unsafe {
			oaknode_serializer_save_to_file(
				dup(&p),
				path_c.as_ptr(),
				0,
				&mut file_code,
				std::ptr::null_mut(),
				0,
			)
		},
		OAKNODE_OK
	);
	assert_eq!(file_code, 0, "OAKNODE_SERIALIZER_OK");
	assert!(path.exists());

	let mut p3 = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p3)) }, OAKNODE_OK);
	let mut file_code = -1;
	assert_eq!(
		unsafe {
			oaknode_serializer_load_from_file(
				dup(&p3),
				path_c.as_ptr(),
				&mut file_code,
				std::ptr::null_mut(),
				0,
			)
		},
		OAKNODE_OK
	);
	assert_eq!(file_code, 0);

	// File variants fail on a missing file (FILE_ERROR) and on bad args.
	let missing = cs("/nonexistent/oaknode-serializer-missing.oakproj");
	let mut file_code = -1;
	assert_eq!(
		unsafe {
			oaknode_serializer_load_from_file(
				dup(&p3),
				missing.as_ptr(),
				&mut file_code,
				std::ptr::null_mut(),
				0,
			)
		},
		oaknode::error::OAKNODE_E_FAILED
	);
	assert_eq!(file_code, 4, "OAKNODE_SERIALIZER_FILE_ERROR");
	assert_eq!(
		unsafe {
			oaknode_serializer_save_to_file(
				CHandle::null(),
				path_c.as_ptr(),
				0,
				&mut file_code,
				std::ptr::null_mut(),
				0,
			)
		},
		OAKNODE_E_INVALID
	);
	let _ = std::fs::remove_file(&path);

	free(&mut loaded_node);
	unsafe { oaknode_serializer_loaddata_free(&mut ld) };
	free(&mut sd);
	unsafe { oaknode_project_free(&mut p) };
	unsafe { oaknode_project_free(&mut p2) };
	unsafe { oaknode_project_free(&mut p3) };
}

/// Keyframe handle family, live paths: create/get/set +
/// failures. No undo bridge involved.
#[test]
fn keyframe_family_basic() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);
	let mut math = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);

	let f = |x: f64| OakNodeValue { kind: oak::FLOAT, num: 0, den: 0, f: [x, 0.0, 0.0, 0.0] };

	// create with a parent; invalid type and string values are rejected.
	let mut kf = unsafe {
		oaknode_keyframe_create(1, 25, &f(3.5), 0, 0, -1, cs("param_a_in").as_ptr(), dup(&math))
	};
	assert!(!kf.ctx.is_null());
	assert!(
		unsafe { oaknode_keyframe_create(0, 1, &f(1.0), 99, 0, -1, cs("param_a_in").as_ptr(), dup(&math)) }
			.ctx
			.is_null()
	);
	let sv = OakNodeValue { kind: 10, num: 0, den: 0, f: [0.0; 4] };
	assert!(
		unsafe { oaknode_keyframe_create(0, 1, &sv, 0, 0, -1, cs("param_a_in").as_ptr(), dup(&math)) }
			.ctx
			.is_null()
	);

	// get_time / set_time (live).
	let (mut num, mut den) = (0i64, 0i64);
	assert_eq!(unsafe { oaknode_keyframe_get_time(dup(&kf), &mut num, &mut den) }, OAKNODE_OK);
	assert_eq!((num, den), (1, 25));
	assert_eq!(
		unsafe { oaknode_keyframe_get_time(CHandle::null(), &mut num, &mut den) },
		OAKNODE_E_INVALID
	);
	assert_eq!(unsafe { oaknode_keyframe_set_time(dup(&kf), 2, 25) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_keyframe_get_time(dup(&kf), &mut num, &mut den) }, OAKNODE_OK);
	assert_eq!((num, den), (2, 25));

	// get_value / set_value (live).
	let mut out = OakNodeValue::none();
	assert_eq!(unsafe { oaknode_keyframe_get_value(dup(&kf), &mut out) }, OAKNODE_OK);
	assert_eq!(out.kind, oak::FLOAT);
	assert_eq!(out.f[0], 3.5);
	assert_eq!(
		unsafe { oaknode_keyframe_get_value(CHandle::null(), &mut out) },
		OAKNODE_E_INVALID
	);
	assert_eq!(unsafe { oaknode_keyframe_set_value(dup(&kf), &f(7.0)) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_keyframe_get_value(dup(&kf), &mut out) }, OAKNODE_OK);
	assert_eq!(out.f[0], 7.0);

	// type round-trip; switching to bezier mints the default handles.
	let mut ty = -1;
	assert_eq!(unsafe { oaknode_keyframe_get_type(dup(&kf), &mut ty) }, OAKNODE_OK);
	assert_eq!(ty, 0, "linear");
	assert_eq!(unsafe { oaknode_keyframe_set_type(dup(&kf), 2) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_keyframe_get_type(dup(&kf), &mut ty) }, OAKNODE_OK);
	assert_eq!(ty, 2, "bezier");
	let (mut bx, mut by) = (0.0, 0.0);
	assert_eq!(unsafe { oaknode_keyframe_get_bezier_control(dup(&kf), 0, &mut bx, &mut by) }, OAKNODE_OK);
	assert_eq!((bx, by), (-1.0, 0.0), "default in-handle");
	assert_eq!(unsafe { oaknode_keyframe_get_bezier_control(dup(&kf), 1, &mut bx, &mut by) }, OAKNODE_OK);
	assert_eq!((bx, by), (1.0, 0.0), "default out-handle");
	assert_eq!(
		unsafe { oaknode_keyframe_get_bezier_control(dup(&kf), 5, &mut bx, &mut by) },
		OAKNODE_E_INVALID
	);
	assert_eq!(unsafe { oaknode_keyframe_set_bezier_control(dup(&kf), 0, -0.4, 1.2) }, OAKNODE_OK);
	assert_eq!(
		unsafe { oaknode_keyframe_get_valid_bezier_control(dup(&kf), 0, &mut bx, &mut by) },
		OAKNODE_OK
	);
	assert_eq!((bx, by), (-0.4, 1.2), "standalone handles are already valid");

	// track / element / input / parent.
	let mut trk = -1;
	assert_eq!(unsafe { oaknode_keyframe_get_track(dup(&kf), &mut trk) }, OAKNODE_OK);
	assert_eq!(trk, 0);
	let mut el = -9;
	assert_eq!(unsafe { oaknode_keyframe_get_element(dup(&kf), &mut el) }, OAKNODE_OK);
	assert_eq!(el, -1);
	let input = two_stage(|b, s| unsafe { oaknode_keyframe_get_input(dup(&kf), b, s) }).unwrap();
	assert_eq!(input, "param_a_in");
	let mut parent = CHandle::null();
	assert_eq!(unsafe { oaknode_keyframe_get_parent(dup(&kf), &mut parent) }, OAKNODE_OK);
	assert!(!parent.ctx.is_null());
	// An orphaned keyframe reports an empty parent.
	let mut kf2 = unsafe {
		oaknode_keyframe_create(0, 1, &f(1.0), 0, 0, -1, cs("param_a_in").as_ptr(), CHandle::null())
	};
	let mut parent2 = CHandle::null();
	assert_eq!(unsafe { oaknode_keyframe_get_parent(dup(&kf2), &mut parent2) }, OAKNODE_OK);
	assert!(parent2.ctx.is_null());

	// value_string round-trip.
	assert_eq!(unsafe { oaknode_keyframe_set_value_string(dup(&kf), cs("hello").as_ptr()) }, OAKNODE_OK);
	let s = two_stage(|b, sz| unsafe { oaknode_keyframe_get_value_string(dup(&kf), b, sz) }).unwrap();
	assert_eq!(s, "hello");

	// opposing bezier type.
	assert_eq!(unsafe { oaknode_keyframe_opposing_bezier_type(0) }, 1);
	assert_eq!(unsafe { oaknode_keyframe_opposing_bezier_type(1) }, 0);
	assert_eq!(unsafe { oaknode_keyframe_opposing_bezier_type(7) }, OAKNODE_E_INVALID);

	// has_sibling_at_time: the parent's track is empty -> 0.
	let mut sib = -1;
	assert_eq!(unsafe { oaknode_keyframe_has_sibling_at_time(dup(&kf), 1, 25, &mut sib) }, OAKNODE_OK);
	assert_eq!(sib, 0);
	assert_eq!(
		unsafe { oaknode_keyframe_has_sibling_at_time(CHandle::null(), 1, 25, &mut sib) },
		OAKNODE_E_INVALID
	);

	// compute_paste_value against the parent's input (declared float).
	let mut pv = OakNodeValue::none();
	assert_eq!(unsafe { oaknode_keyframe_compute_paste_value(dup(&math), dup(&kf), &mut pv) }, OAKNODE_OK);
	assert_eq!(pv.kind, oak::FLOAT);
	// Unknown input on the target -> NOT_FOUND.
	let mut kf3 = unsafe {
		oaknode_keyframe_create(0, 1, &f(1.0), 0, 0, -1, cs("nope_in").as_ptr(), dup(&math))
	};
	assert_eq!(
		unsafe { oaknode_keyframe_compute_paste_value(dup(&math), dup(&kf3), &mut pv) },
		OAKNODE_E_NOT_FOUND
	);

	free(&mut parent);
	free(&mut parent2);
	unsafe { oaknode_keyframe_free(&mut kf3) };
	unsafe { oaknode_keyframe_free(&mut kf2) };
	unsafe { oaknode_keyframe_free(&mut kf) };
	free(&mut math);
	unsafe { oaknode_project_free(&mut p) };
}

/// Keyframe handle family, undoable paths: setter commands round-trip
/// through the test-stub undo bridge.
#[cfg(feature = "test-stubs")]
#[test]
fn keyframe_undoable() {
	let _g = lock();
	let f = |x: f64| OakNodeValue { kind: oak::FLOAT, num: 0, den: 0, f: [x, 0.0, 0.0, 0.0] };
	let mut kf = unsafe {
		oaknode_keyframe_create(0, 1, &f(1.0), 0, 0, -1, cs("param_a_in").as_ptr(), CHandle::null())
	};

	// set_time_undoable.
	let mut cmd = CHandle::null();
	assert_eq!(unsafe { oaknode_keyframe_set_time_undoable(dup(&kf), 5, 1, &mut cmd) }, OAKNODE_OK);
	assert!(!cmd.ctx.is_null());
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let (mut num, mut den) = (0i64, 0i64);
	assert_eq!(unsafe { oaknode_keyframe_get_time(dup(&kf), &mut num, &mut den) }, OAKNODE_OK);
	assert_eq!((num, den), (5, 1));
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(unsafe { oaknode_keyframe_get_time(dup(&kf), &mut num, &mut den) }, OAKNODE_OK);
	assert_eq!((num, den), (0, 1));
	unsafe { oakundo_command_free(&mut cmd) };

	// set_value_undoable.
	let mut cmd = CHandle::null();
	assert_eq!(unsafe { oaknode_keyframe_set_value_undoable(dup(&kf), &f(9.0), &mut cmd) }, OAKNODE_OK);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let mut out = OakNodeValue::none();
	assert_eq!(unsafe { oaknode_keyframe_get_value(dup(&kf), &mut out) }, OAKNODE_OK);
	assert_eq!(out.f[0], 9.0);
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(unsafe { oaknode_keyframe_get_value(dup(&kf), &mut out) }, OAKNODE_OK);
	assert_eq!(out.f[0], 1.0);
	unsafe { oakundo_command_free(&mut cmd) };

	// set_type_undoable (linear -> hold -> back).
	let mut cmd = CHandle::null();
	assert_eq!(unsafe { oaknode_keyframe_set_type_undoable(dup(&kf), 1, &mut cmd) }, OAKNODE_OK);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let mut ty = -1;
	assert_eq!(unsafe { oaknode_keyframe_get_type(dup(&kf), &mut ty) }, OAKNODE_OK);
	assert_eq!(ty, 1);
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(unsafe { oaknode_keyframe_get_type(dup(&kf), &mut ty) }, OAKNODE_OK);
	assert_eq!(ty, 0);
	unsafe { oakundo_command_free(&mut cmd) };

	// set_bezier_control_undoable.
	let mut cmd = CHandle::null();
	assert_eq!(
		unsafe { oaknode_keyframe_set_bezier_control_undoable(dup(&kf), 0, -0.5, 2.0, &mut cmd) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let (mut bx, mut by) = (0.0, 0.0);
	assert_eq!(unsafe { oaknode_keyframe_get_bezier_control(dup(&kf), 0, &mut bx, &mut by) }, OAKNODE_OK);
	assert_eq!((bx, by), (-0.5, 2.0));
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(unsafe { oaknode_keyframe_get_bezier_control(dup(&kf), 0, &mut bx, &mut by) }, OAKNODE_OK);
	assert_eq!((bx, by), (0.0, 0.0));
	unsafe { oakundo_command_free(&mut cmd) };

	// set_value_string_undoable.
	let mut cmd = CHandle::null();
	assert_eq!(
		unsafe { oaknode_keyframe_set_value_string_undoable(dup(&kf), cs("str").as_ptr(), &mut cmd) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let s = two_stage(|b, sz| unsafe { oaknode_keyframe_get_value_string(dup(&kf), b, sz) }).unwrap();
	assert_eq!(s, "str");
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	let s = two_stage(|b, sz| unsafe { oaknode_keyframe_get_value_string(dup(&kf), b, sz) }).unwrap();
	assert_eq!(s, "1", "float 1.0 after undo");

	unsafe { oaknode_keyframe_free(&mut kf) };
}

/// Group family, live paths: create/cast/passthroughs/output/resolve +
/// failures. No undo bridge involved.
#[test]
fn group_family_basic() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);

	let mut group = unsafe { oaknode_group_create() };
	assert!(!group.ctx.is_null());
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&group)) }, OAKNODE_OK);
	let mut math = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);

	// cast: the group casts; a non-group node and empty handles do not.
	let mut cast = unsafe { oaknode_group_cast(dup(&group)) };
	assert!(!cast.ctx.is_null());
	assert!(unsafe { oaknode_group_cast(dup(&math)) }.ctx.is_null());
	assert!(unsafe { oaknode_group_cast(CHandle::null()) }.ctx.is_null());
	free(&mut cast);

	// Add a passthrough for math's param_a_in; repeat returns the same id.
	let id = two_stage(|b, s| unsafe {
		oaknode_group_add_input_passthrough(dup(&group), dup(&math), cs("param_a_in").as_ptr(), -1, b, s)
	})
	.unwrap();
	assert_eq!(id, "param_a_in");
	let id2 = two_stage(|b, s| unsafe {
		oaknode_group_add_input_passthrough(dup(&group), dup(&math), cs("param_a_in").as_ptr(), -1, b, s)
	})
	.unwrap();
	assert_eq!(id2, "param_a_in");
	// Unknown inner input -> NOT_FOUND.
	assert_eq!(
		unsafe {
			oaknode_group_add_input_passthrough(
				dup(&group),
				dup(&math),
				cs("nope_in").as_ptr(),
				-1,
				std::ptr::null_mut(),
				0,
			)
		},
		OAKNODE_E_NOT_FOUND
	);
	// Cross-project node -> INVALID.
	let mut p2 = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p2)) }, OAKNODE_OK);
	let mut math2 = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p2), dup(&math2)) }, OAKNODE_OK);
	assert_eq!(
		unsafe {
			oaknode_group_add_input_passthrough(
				dup(&group),
				dup(&math2),
				cs("param_a_in").as_ptr(),
				-1,
				std::ptr::null_mut(),
				0,
			)
		},
		OAKNODE_E_INVALID
	);

	// count / id_at / input_at.
	let mut count = 0;
	assert_eq!(unsafe { oaknode_group_passthrough_count(dup(&group), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 1);
	let pid = two_stage(|b, s| unsafe { oaknode_group_passthrough_id_at(dup(&group), 0, b, s) }).unwrap();
	assert_eq!(pid, "param_a_in");
	assert_eq!(
		unsafe { oaknode_group_passthrough_id_at(dup(&group), 5, std::ptr::null_mut(), 0) },
		OAKNODE_E_NOT_FOUND
	);
	let mut inner_node = CHandle::null();
	let mut inner_el = -9;
	let iid = two_stage(|b, s| unsafe {
		oaknode_group_passthrough_input_at(dup(&group), 0, &mut inner_node, b, s, &mut inner_el)
	})
	.unwrap();
	assert_eq!(iid, "param_a_in");
	assert!(!inner_node.ctx.is_null());
	assert_eq!(inner_el, -1);
	free(&mut inner_node);

	// remove_input_passthrough: second removal is NOT_FOUND.
	assert_eq!(
		unsafe {
			oaknode_group_remove_input_passthrough(
				dup(&group),
				dup(&math),
				cs("param_a_in").as_ptr(),
				-1,
			)
		},
		OAKNODE_OK
	);
	assert_eq!(
		unsafe {
			oaknode_group_remove_input_passthrough(
				dup(&group),
				dup(&math),
				cs("param_a_in").as_ptr(),
				-1,
			)
		},
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(unsafe { oaknode_group_passthrough_count(dup(&group), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 0);

	// output passthrough set/get + clear with an empty handle.
	assert_eq!(unsafe { oaknode_group_set_output_passthrough(dup(&group), dup(&math)) }, OAKNODE_OK);
	let mut out = CHandle::null();
	assert_eq!(unsafe { oaknode_group_get_output_passthrough(dup(&group), &mut out) }, OAKNODE_OK);
	assert!(!out.ctx.is_null());
	free(&mut out);
	assert_eq!(
		unsafe { oaknode_group_set_output_passthrough(dup(&group), CHandle::null()) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oaknode_group_get_output_passthrough(dup(&group), &mut out) }, OAKNODE_OK);
	assert!(out.ctx.is_null());

	// resolve_input: a non-group input resolves to itself.
	let mut rn = CHandle::null();
	let mut re = -9;
	let rid = two_stage(|b, s| unsafe {
		oaknode_group_resolve_input(dup(&math), cs("param_a_in").as_ptr(), -1, &mut rn, b, s, &mut re)
	})
	.unwrap();
	assert_eq!(rid, "param_a_in");
	assert!(!rn.ctx.is_null());
	assert_eq!(re, -1);
	free(&mut rn);
	// Unresolvable input -> NOT_FOUND.
	assert_eq!(
		unsafe {
			oaknode_group_resolve_input(
				dup(&math),
				cs("nope_in").as_ptr(),
				-1,
				&mut rn,
				std::ptr::null_mut(),
				0,
				&mut re,
			)
		},
		OAKNODE_E_NOT_FOUND
	);

	unsafe { oaknode_group_free(&mut group) };
	free(&mut math2);
	free(&mut math);
	unsafe { oaknode_project_free(&mut p2) };
	unsafe { oaknode_project_free(&mut p) };
}

/// Group family, undoable paths: passthrough/output commands round-trip
/// through the test-stub undo bridge.
#[cfg(feature = "test-stubs")]
#[test]
fn group_undoable() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);
	let mut group = unsafe { oaknode_group_create() };
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&group)) }, OAKNODE_OK);
	let mut math = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);

	// add_input_passthrough_undoable: redo adds, undo removes.
	let mut cmd = CHandle::null();
	assert_eq!(
		unsafe {
			oaknode_group_add_input_passthrough_undoable(
				dup(&group),
				dup(&math),
				cs("param_a_in").as_ptr(),
				-1,
				&mut cmd,
			)
		},
		OAKNODE_OK
	);
	assert!(!cmd.ctx.is_null());
	let mut count = 0;
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(unsafe { oaknode_group_passthrough_count(dup(&group), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 1);
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(unsafe { oaknode_group_passthrough_count(dup(&group), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 0);
	unsafe { oakundo_command_free(&mut cmd) };

	// set_output_passthrough_undoable: redo sets, undo restores.
	let mut cmd = CHandle::null();
	assert_eq!(
		unsafe { oaknode_group_set_output_passthrough_undoable(dup(&group), dup(&math), &mut cmd) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let mut out = CHandle::null();
	assert_eq!(unsafe { oaknode_group_get_output_passthrough(dup(&group), &mut out) }, OAKNODE_OK);
	assert!(!out.ctx.is_null());
	free(&mut out);
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(unsafe { oaknode_group_get_output_passthrough(dup(&group), &mut out) }, OAKNODE_OK);
	assert!(out.ctx.is_null());
	unsafe { oakundo_command_free(&mut cmd) };

	unsafe { oaknode_group_free(&mut group) };
	free(&mut math);
	unsafe { oaknode_project_free(&mut p) };
}

/// Dragger family: create/start/drag/end/is_started + failures. `end`
/// builds undo commands through the test-stub bridge.
#[cfg(feature = "test-stubs")]
#[test]
fn dragger_family() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);
	let mut math = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);

	let mut dg = unsafe { oaknode_dragger_create(dup(&math), cs("param_a_in").as_ptr(), -1, 0) };
	assert!(!dg.ctx.is_null());
	// Unknown input -> empty handle.
	assert!(unsafe { oaknode_dragger_create(dup(&math), cs("nope_in").as_ptr(), -1, 0) }.ctx.is_null());

	let mut started = -1;
	assert_eq!(unsafe { oaknode_dragger_is_started(dup(&dg), &mut started) }, OAKNODE_OK);
	assert_eq!(started, 0);

	// Negative track -> E_INVALID (before start).
	let mut dg2 = unsafe { oaknode_dragger_create(dup(&math), cs("param_a_in").as_ptr(), -1, 0) };
	assert_eq!(unsafe { oaknode_dragger_start(dup(&dg2), 0, 1, -1, 1) }, OAKNODE_E_INVALID);

	// start at time 0 on track 0; double start -> E_STATE.
	assert_eq!(unsafe { oaknode_dragger_start(dup(&dg), 0, 1, 0, 1) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_dragger_is_started(dup(&dg), &mut started) }, OAKNODE_OK);
	assert_eq!(started, 1);
	assert_eq!(unsafe { oaknode_dragger_start(dup(&dg), 1, 1, 0, 1) }, OAKNODE_E_STATE);

	// drag sets the value live; wrong POD kind -> E_INVALID.
	let f = |x: f64| OakNodeValue { kind: oak::FLOAT, num: 0, den: 0, f: [x, 0.0, 0.0, 0.0] };
	assert_eq!(unsafe { oaknode_dragger_drag(dup(&dg), &f(4.0)) }, OAKNODE_OK);
	let mut out = OakNodeValue::none();
	assert_eq!(
		unsafe { oaknode_node_get_input_at_time(dup(&math), cs("param_a_in").as_ptr(), 0, 1, &mut out) },
		OAKNODE_OK
	);
	assert_eq!(out.kind, oak::FLOAT);
	assert_eq!(out.f[0], 4.0);
	let bad = OakNodeValue { kind: oak::INT, num: 3, den: 0, f: [0.0; 4] };
	assert_eq!(unsafe { oaknode_dragger_drag(dup(&dg), &bad) }, OAKNODE_E_INVALID);
	// drag before start -> E_STATE.
	assert_eq!(unsafe { oaknode_dragger_drag(dup(&dg2), &f(1.0)) }, OAKNODE_E_STATE);

	// end builds a multi command; the command undoes and redoes the drag.
	let mut cmd = CHandle::null();
	assert_eq!(unsafe { oaknode_dragger_end(dup(&dg), &mut cmd) }, OAKNODE_OK);
	assert!(!cmd.ctx.is_null());
	assert_eq!(unsafe { oaknode_dragger_is_started(dup(&dg), &mut started) }, OAKNODE_OK);
	assert_eq!(started, 0, "end resets the dragger");
	assert_eq!(unsafe { oaknode_dragger_end(dup(&dg), &mut cmd) }, OAKNODE_E_STATE);
	// The drag was applied live, so the command round-trips
	// redo -> undo -> redo.
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(
		unsafe { oaknode_node_get_input_at_time(dup(&math), cs("param_a_in").as_ptr(), 0, 1, &mut out) },
		OAKNODE_OK
	);
	assert_eq!(out.f[0], 4.0, "redo re-applies the drag");
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(
		unsafe { oaknode_node_get_input_at_time(dup(&math), cs("param_a_in").as_ptr(), 0, 1, &mut out) },
		OAKNODE_OK
	);
	assert_eq!(out.f[0], 0.0, "undo restores the pre-drag value");
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(
		unsafe { oaknode_node_get_input_at_time(dup(&math), cs("param_a_in").as_ptr(), 0, 1, &mut out) },
		OAKNODE_OK
	);
	assert_eq!(out.f[0], 4.0, "redo after undo re-applies the drag");
	unsafe { oakundo_command_free(&mut cmd) };

	// Null args -> E_INVALID.
	assert_eq!(unsafe { oaknode_dragger_is_started(CHandle::null(), &mut started) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_dragger_drag(CHandle::null(), &f(1.0)) }, OAKNODE_E_INVALID);

	unsafe { oaknode_dragger_free(&mut dg2) };
	unsafe { oaknode_dragger_free(&mut dg) };
	free(&mut math);
	unsafe { oaknode_project_free(&mut p) };
}

/// Undoable node exports: create commands, redo/undo round-trip via the
/// test-stub undo bridge.
///
/// Needs the `test-stubs` feature: the undo commands are created through
/// the oakundo bridge, whose `oakundo_command_init` symbol only resolves
/// in the test binary when the in-crate stubs are compiled in.
#[cfg(feature = "test-stubs")]
#[test]
fn undoable_node_exports() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);
	let mut math = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);

	// set_label_undoable: redo sets, undo restores.
	let mut cmd = CHandle::null();
	assert_eq!(
		unsafe { oaknode_node_set_label_undoable(dup(&math), cs("new label").as_ptr(), &mut cmd) },
		OAKNODE_OK
	);
	assert!(!cmd.ctx.is_null());
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let label = two_stage(|b, s| unsafe { oaknode::ffi::node::oaknode_node_get_label(dup(&math), b, s) })
		.unwrap();
	assert_eq!(label, "new label");
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	let label = two_stage(|b, s| unsafe { oaknode::ffi::node::oaknode_node_get_label(dup(&math), b, s) })
		.unwrap();
	assert_eq!(label, "");
	unsafe { oakundo_command_free(&mut cmd) };

	// set_enabled_undoable.
	let mut cmd = CHandle::null();
	assert_eq!(
		unsafe { oaknode_node_set_enabled_undoable(dup(&math), 0, &mut cmd) },
		OAKNODE_OK
	);
	let mut en = 1;
	assert_eq!(unsafe { oaknode::ffi::node::oaknode_node_is_enabled(dup(&math), &mut en) }, OAKNODE_OK);
	assert_eq!(en, 1);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(unsafe { oaknode::ffi::node::oaknode_node_is_enabled(dup(&math), &mut en) }, OAKNODE_OK);
	assert_eq!(en, 0);
	unsafe { oakundo_command_free(&mut cmd) };

	// set_input_undoable on param_a_in.
	let mut cmd = CHandle::null();
	let v = OakNodeValue {
		kind: oak::FLOAT,
		num: 0,
		den: 0,
		f: [7.5, 0.0, 0.0, 0.0],
	};
	assert_eq!(
		unsafe { oaknode_node_set_input_undoable(dup(&math), cs("param_a_in").as_ptr(), &v, &mut cmd) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let mut pod = OakNodeValue::none();
	assert_eq!(
		unsafe { oaknode_node_get_input(dup(&math), cs("param_a_in").as_ptr(), &mut pod) },
		OAKNODE_OK
	);
	assert_eq!(pod.f[0], 7.5);
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(
		unsafe { oaknode_node_get_input(dup(&math), cs("param_a_in").as_ptr(), &mut pod) },
		OAKNODE_OK
	);
	assert_eq!(pod.f[0], 0.0);
	unsafe { oakundo_command_free(&mut cmd) };

	// set_input_at_time_undoable: inserts a keyframe on redo.
	let mut cmd = CHandle::null();
	let kv = OakNodeValue {
		kind: oak::FLOAT,
		num: 0,
		den: 0,
		f: [3.0, 0.0, 0.0, 0.0],
	};
	assert_eq!(
		unsafe {
			oaknode_node_set_input_at_time_undoable(
				dup(&math),
				cs("param_a_in").as_ptr(),
				10,
				1,
				&kv,
				0,
				&mut cmd,
			)
		},
		OAKNODE_OK
	);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	// At time 10 the value is now keyframed to 3.0.
	let mut at = OakNodeValue::none();
	assert_eq!(
		unsafe {
			oaknode::ffi::node::oaknode_node_get_input_at_time(
				dup(&math),
				cs("param_a_in").as_ptr(),
				10,
				1,
				&mut at,
			)
		},
		OAKNODE_OK
	);
	assert_eq!(at.f[0], 3.0);
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(
		unsafe {
			oaknode::ffi::node::oaknode_node_get_input_at_time(
				dup(&math),
				cs("param_a_in").as_ptr(),
				10,
				1,
				&mut at,
			)
		},
		OAKNODE_OK
	);
	assert_eq!(at.f[0], 0.0, "keyframe removed on undo");
	unsafe { oakundo_command_free(&mut cmd) };

	// connect_undoable: redo connects, undo disconnects.
	let mut other = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&other)) }, OAKNODE_OK);
	let mut cmd = CHandle::null();
	assert_eq!(
		unsafe { oaknode_node_connect_undoable(dup(&math), dup(&other), cs("param_a_in").as_ptr(), &mut cmd) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oakundo_command_redo_now(dup(&cmd)) }.unwrap(), 0);
	let mut conn = 0;
	assert_eq!(
		unsafe { oaknode::ffi::node::oaknode_node_input_is_connected(dup(&other), cs("param_a_in").as_ptr(), &mut conn) },
		OAKNODE_OK
	);
	assert_eq!(conn, 1);
	assert_eq!(unsafe { oakundo_command_undo_now(dup(&cmd)) }.unwrap(), 0);
	assert_eq!(
		unsafe { oaknode::ffi::node::oaknode_node_input_is_connected(dup(&other), cs("param_a_in").as_ptr(), &mut conn) },
		OAKNODE_OK
	);
	assert_eq!(conn, 0);
	unsafe { oakundo_command_free(&mut cmd) };

	// disconnect_undoable failure: unconnected input.
	let mut cmd = CHandle::null();
	assert_eq!(
		unsafe { oaknode_node_disconnect_undoable(dup(&other), cs("param_a_in").as_ptr(), &mut cmd) },
		OAKNODE_E_NOT_FOUND
	);

	free(&mut other);
	free(&mut math);
	unsafe { oaknode_project_free(&mut p) };
}

// The undo stub functions are resolved through the dlsym path (the
// test-stubs feature compiles them into the test binary); only the
// undoable-node test above uses them.
#[cfg(feature = "test-stubs")]
use oaknode::bridge::undo::{
	command_free as oakundo_command_free, command_redo_now as oakundo_command_redo_now,
	command_undo_now as oakundo_command_undo_now,
};

/// A minimal multicam-typed behavior for the node-backed multicam
/// exports (the real `MultiCamNode` behavior is a Phase-3 item).
struct FakeMulticam;
impl oaknode::node::NodeBehavior for FakeMulticam {
	fn name(&self) -> &str {
		"Multi-Cam"
	}
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.multicam"
	}
	fn duplicate(&self, _core: &oaknode::node::NodeCore) -> Option<Box<dyn oaknode::node::NodeBehavior>> {
		None
	}
}

/// A node handle for an arbitrary behavior/core (unit-style construction
/// for exports whose node type has no factory constructor yet).
fn make_raw_node(
	core: oaknode::node::NodeCore,
	behavior: Box<dyn oaknode::node::NodeBehavior>,
) -> CHandle {
	let project = oaknode::project::Project::new();
	let id = {
		let mut p = project.lock().unwrap();
		p.graph.add_node(core, behavior)
	};
	oaknode::handle::make_owned(oaknode::project::NodeRef::new(project, id, false))
}

/// Multicam family: input-id strings, grid math incl. 0/1 source and
/// non-square grids, index<->row/col round-trips, node-backed exports.
#[test]
fn multicam_family() {
	let _g = lock();
	let s = |p: *const c_char| unsafe { std::ffi::CStr::from_ptr(p) }.to_str().unwrap().to_string();
	assert_eq!(s(unsafe { oaknode_multicam_input_current() }), "current_in");
	assert_eq!(s(unsafe { oaknode_multicam_input_sources() }), "sources_in");
	assert_eq!(s(unsafe { oaknode_multicam_input_sequence() }), "sequence_in");
	assert_eq!(s(unsafe { oaknode_multicam_input_sequence_type() }), "sequence_type_in");

	// Grid math (rows/columns): the square-ish cover grid.
	let mut rows = 0;
	let mut cols = 0;
	let grid = |n: c_int, r: &mut c_int, c: &mut c_int| unsafe {
		oaknode_multicam_get_rows_and_columns(n, r, c)
	};
	assert_eq!(grid(0, &mut rows, &mut cols), OAKNODE_OK);
	assert_eq!((rows, cols), (1, 1), "0 sources -> 1x1");
	assert_eq!(grid(1, &mut rows, &mut cols), OAKNODE_OK);
	assert_eq!((rows, cols), (1, 1), "1 source -> 1x1");
	assert_eq!(grid(2, &mut rows, &mut cols), OAKNODE_OK);
	assert_eq!((rows, cols), (1, 2), "2 sources -> 1x2");
	assert_eq!(grid(3, &mut rows, &mut cols), OAKNODE_OK);
	assert_eq!((rows, cols), (2, 2), "3 sources -> 2x2");
	assert_eq!(grid(4, &mut rows, &mut cols), OAKNODE_OK);
	assert_eq!((rows, cols), (2, 2));
	assert_eq!(grid(5, &mut rows, &mut cols), OAKNODE_OK);
	assert_eq!((rows, cols), (2, 3), "5 sources -> non-square 2x3");
	assert_eq!(grid(6, &mut rows, &mut cols), OAKNODE_OK);
	assert_eq!((rows, cols), (2, 3), "6 sources -> 2x3");
	assert_eq!(grid(10, &mut rows, &mut cols), OAKNODE_OK);
	assert_eq!((rows, cols), (3, 4), "10 sources -> 3x4");
	// Failure: negative count / null out.
	assert_eq!(grid(-1, &mut rows, &mut cols), OAKNODE_E_INVALID);
	assert_eq!(
		unsafe { oaknode_multicam_get_rows_and_columns(4, std::ptr::null_mut(), &mut cols) },
		OAKNODE_E_INVALID
	);

	// index -> (row, col) and back.
	let mut row = -1;
	let mut col = -1;
	let to_rc = |i: c_int, r: c_int, c: c_int, or: &mut c_int, oc: &mut c_int| unsafe {
		oaknode_multicam_index_to_row_cols(i, r, c, or, oc)
	};
	assert_eq!(to_rc(0, 2, 3, &mut row, &mut col), OAKNODE_OK);
	assert_eq!((row, col), (0, 0));
	assert_eq!(to_rc(5, 2, 3, &mut row, &mut col), OAKNODE_OK);
	assert_eq!((row, col), (1, 2), "5 in 2x3 -> row 1 col 2");
	assert_eq!(to_rc(2, 1, 2, &mut row, &mut col), OAKNODE_OK);
	assert_eq!((row, col), (1, 0));
	assert_eq!(to_rc(-1, 2, 3, &mut row, &mut col), OAKNODE_E_INVALID);
	assert_eq!(to_rc(0, 0, 3, &mut row, &mut col), OAKNODE_E_INVALID);
	assert_eq!(to_rc(0, 2, 0, &mut row, &mut col), OAKNODE_E_INVALID);
	assert_eq!(
		unsafe { oaknode_multicam_index_to_row_cols(0, 2, 3, std::ptr::null_mut(), &mut col) },
		OAKNODE_E_INVALID
	);
	let to_i = |r: c_int, c: c_int, rr: c_int, cc: c_int| unsafe {
		oaknode_multicam_rows_cols_to_index(r, c, rr, cc)
	};
	assert_eq!(to_i(1, 2, 2, 3), 5, "round-trip");
	assert_eq!(to_i(0, 0, 1, 2), 0);
	assert_eq!(to_i(-1, 0, 2, 3), OAKNODE_E_INVALID);
	assert_eq!(to_i(0, -1, 2, 3), OAKNODE_E_INVALID);
	assert_eq!(to_i(0, 0, 0, 3), OAKNODE_E_INVALID);
	assert_eq!(to_i(2, 0, 2, 3), OAKNODE_E_INVALID, "row out of range");
	assert_eq!(to_i(0, 3, 2, 3), OAKNODE_E_INVALID, "col out of range");

	// Node-backed exports: a fake multicam node with 4 sources, current 2.
	let mut core = oaknode::node::NodeCore::new();
	let mut sources =
		oaknode::input::Input::new("sources_in", oaknode::value::ValueType::None, oaknode::value::NodeValue::None);
	sources.flags = oaknode::input::flags::ARRAY | oaknode::input::flags::NOT_KEYFRAMABLE;
	sources.array_size = 4;
	core.add_input(sources);
	core.add_input(oaknode::input::Input::new(
		"current_in",
		oaknode::value::ValueType::Combo,
		oaknode::value::NodeValue::Combo(2),
	));
	core.set_standard_value("current_in", -1, oaknode::value::NodeValue::Combo(2));
	let mut node = make_raw_node(core, Box::new(FakeMulticam));

	let mut count = -1;
	assert_eq!(unsafe { oaknode_multicam_get_source_count(dup(&node), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 4);
	let mut src = -1;
	assert_eq!(unsafe { oaknode_multicam_get_current_source(dup(&node), &mut src) }, OAKNODE_OK);
	assert_eq!(src, 2);

	// Non-multicam node / empty handle / null outs -> E_INVALID.
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);
	let mut math = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);
	assert_eq!(
		unsafe { oaknode_multicam_get_source_count(dup(&math), &mut count) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_multicam_get_current_source(dup(&math), &mut src) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_multicam_get_source_count(CHandle::null(), &mut count) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_multicam_get_source_count(dup(&node), std::ptr::null_mut()) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_multicam_get_current_source(CHandle::null(), std::ptr::null_mut()) },
		OAKNODE_E_INVALID
	);

	free(&mut node);
	free(&mut math);
	unsafe { oaknode_project_free(&mut p) };
}

/// Timeline/render bridge accessors: markers / work area / video frame
/// cache are addref'd stored handles (empty by default) with the
/// documented error paths; find_input_footage walks the upstream graph.
#[test]
fn bridge_handle_accessors() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);

	// A math node (not a viewer): markers/workarea empty, cache empty.
	let mut math = unsafe {
		oaknode::ffi::factory::oaknode_factory_create_from_id(
			cs("org.olivevideoeditor.Olive.math").as_ptr(),
		)
	};
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);

	let mut h = CHandle::null();
	let hp = &mut h as *mut CHandle as *mut std::ffi::c_void;
	assert_eq!(unsafe { oaknode_node_get_markers(dup(&math), hp) }, OAKNODE_OK);
	assert!(h.ctx.is_null(), "no markers by default");
	assert_eq!(unsafe { oaknode_node_get_work_area(dup(&math), hp) }, OAKNODE_OK);
	assert!(h.ctx.is_null(), "no work area by default");
	assert_eq!(unsafe { oaknode_node_get_video_frame_cache(dup(&math), hp) }, OAKNODE_OK);
	assert!(h.ctx.is_null(), "no frame cache by default");

	// Null args -> E_INVALID.
	assert_eq!(
		unsafe { oaknode_node_get_markers(CHandle::null(), hp) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_node_get_markers(dup(&math), std::ptr::null_mut()) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_node_get_work_area(CHandle::null(), hp) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_node_get_video_frame_cache(CHandle::null(), hp) },
		OAKNODE_E_INVALID
	);

	// find_input_footage: no footage upstream -> empty + OK.
	let mut out = CHandle::null();
	assert_eq!(unsafe { oaknode_node_find_input_footage(dup(&math), &mut out) }, OAKNODE_OK);
	assert!(out.ctx.is_null(), "no footage upstream");
	assert_eq!(
		unsafe { oaknode_node_find_input_footage(CHandle::null(), &mut out) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_node_find_input_footage(dup(&math), std::ptr::null_mut()) },
		OAKNODE_E_INVALID
	);

	// A footage node feeding the math node's input: findable upstream.
	let mut footage = unsafe { oaknode_footage_create(dup(&p), cs("/media/clip.mov").as_ptr()) };
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&footage)) }, OAKNODE_OK);
	assert_eq!(
		unsafe { oaknode_node_connect(dup(&footage), dup(&math), cs("param_a_in").as_ptr()) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oaknode_node_find_input_footage(dup(&math), &mut out) }, OAKNODE_OK);
	assert!(!out.ctx.is_null(), "footage found upstream");
	free(&mut out);

	// The sequence (viewer) node also yields empty markers/workarea.
	let mut seq = unsafe { oaknode_sequence_create() };
	assert_eq!(unsafe { oaknode_node_get_markers(dup(&seq), hp) }, OAKNODE_OK);
	assert!(h.ctx.is_null());

	free(&mut seq);
	free(&mut footage);
	free(&mut math);
	unsafe { oaknode_project_free(&mut p) };
}

/// Viewer params: set video/audio params from oakcommon/oakcore handles
/// (test-stub backed) with the documented degradation errors.
#[cfg(feature = "test-stubs")]
#[test]
fn viewer_stream_params() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);
	let mut seq = unsafe { oaknode_sequence_create() };

	// Build a video-params handle and push it into the viewer.
	let mut vp = oaknode::bridge::common::videoparams_init_basic(640, 480, 4, 4, 1, 1, 0, 1)
		.unwrap();
	assert_eq!(
		oaknode::bridge::common::videoparams_set_frame_rate(dup(&vp), 24, 1).unwrap(),
		0
	);
	assert_eq!(unsafe { oaknode_viewer_set_video_params(dup(&seq), &vp as *const _ as *const std::ffi::c_void) }, OAKNODE_OK);

	// Read it back through the sequence getter and compare.
	let mut vp2 = CHandle::null();
	assert_eq!(
		unsafe {
			oaknode_sequence_get_video_params(dup(&seq), 0, &mut vp2 as *mut CHandle as *mut std::ffi::c_void)
		},
		OAKNODE_OK
	);
	assert!(!vp2.ctx.is_null());
	assert_eq!(oaknode::bridge::common::videoparams_get_width(dup(&vp2)).unwrap(), 640);
	assert_eq!(oaknode::bridge::common::videoparams_get_height(dup(&vp2)).unwrap(), 480);
	assert_eq!(
		oaknode::bridge::common::videoparams_get_frame_rate(dup(&vp2)).unwrap(),
		(24, 1)
	);
	oaknode::bridge::common::videoparams_free(&mut vp2);

	// Audio params via an oakcore handle.
	let ap = oaknode::bridge::core::audioparams_create(48000, 0x3, 4).unwrap();
	assert_eq!(
		unsafe { oaknode_viewer_set_audio_params(dup(&seq), ap as *const std::ffi::c_void) },
		OAKNODE_OK
	);
	let mut ap2: *mut std::ffi::c_void = std::ptr::null_mut();
	assert_eq!(unsafe { oaknode_sequence_get_audio_params(dup(&seq), 0, &mut ap2) }, OAKNODE_OK);
	assert!(!ap2.is_null());
	assert_eq!(oaknode::bridge::core::audioparams_sample_rate(ap2).unwrap(), 48000);
	assert_eq!(oaknode::bridge::core::audioparams_channel_layout(ap2).unwrap(), 0x3);
	assert_eq!(oaknode::bridge::core::audioparams_format(ap2).unwrap(), 4);
	oaknode::bridge::core::audioparams_free(ap2);
	oaknode::bridge::core::audioparams_free(ap);

	// Failure paths: null params / empty handle / non-viewer node.
	assert_eq!(
		unsafe { oaknode_viewer_set_video_params(dup(&seq), std::ptr::null()) },
		OAKNODE_E_INVALID
	);
	let empty = CHandle::null();
	assert_eq!(
		unsafe { oaknode_viewer_set_video_params(dup(&seq), &empty as *const _ as *const std::ffi::c_void) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_viewer_set_audio_params(dup(&seq), std::ptr::null()) },
		OAKNODE_E_INVALID
	);

	oaknode::bridge::common::videoparams_free(&mut vp);
	free(&mut seq);
	unsafe { oaknode_project_free(&mut p) };
}

/// Sequence/footage stream params: get/set video+audio round-trips
/// (test-stub backed) plus the error paths.
#[cfg(feature = "test-stubs")]
#[test]
fn stream_params_roundtrip() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);
	let mut seq = unsafe { oaknode_sequence_create() };
	assert_eq!(unsafe { oaknode_sequence_set_default_parameters(dup(&seq)) }, OAKNODE_OK);

	let mut vc = 0;
	assert_eq!(unsafe { oaknode_sequence_get_video_stream_count(dup(&seq), &mut vc) }, OAKNODE_OK);
	assert_eq!(vc, 1, "default video stream");

	// get_video_params(0) returns an owned handle.
	let mut vp = CHandle::null();
	assert_eq!(
		unsafe { oaknode_sequence_get_video_params(dup(&seq), 0, &mut vp as *mut CHandle as *mut std::ffi::c_void) },
		OAKNODE_OK
	);
	assert!(!vp.ctx.is_null());
	assert_eq!(oaknode::bridge::common::videoparams_get_width(dup(&vp)).unwrap(), 1920);
	// set_video_params(0) round-trips the same handle.
	assert_eq!(unsafe { oaknode_sequence_set_video_params(dup(&seq), 0, dup(&vp)) }, OAKNODE_OK);

	// Error paths.
	assert_eq!(
		unsafe { oaknode_sequence_get_video_params(dup(&seq), 7, &mut vp as *mut CHandle as *mut std::ffi::c_void) },
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(unsafe { oaknode_sequence_set_video_params(dup(&seq), 7, dup(&vp)) }, OAKNODE_E_NOT_FOUND);
	assert_eq!(
		unsafe { oaknode_sequence_get_video_params(dup(&seq), -1, &mut vp as *mut CHandle as *mut std::ffi::c_void) },
		OAKNODE_E_INVALID
	);
	assert_eq!(unsafe { oaknode_sequence_set_video_params(dup(&seq), 0, CHandle::null()) }, OAKNODE_E_INVALID);
	oaknode::bridge::common::videoparams_free(&mut vp);

	// Audio round-trip.
	let mut ac = 0;
	assert_eq!(unsafe { oaknode_sequence_get_audio_stream_count(dup(&seq), &mut ac) }, OAKNODE_OK);
	assert_eq!(ac, 1, "default audio stream");
	let mut ap: *mut std::ffi::c_void = std::ptr::null_mut();
	assert_eq!(unsafe { oaknode_sequence_get_audio_params(dup(&seq), 0, &mut ap) }, OAKNODE_OK);
	assert!(!ap.is_null());
	assert_eq!(oaknode::bridge::core::audioparams_sample_rate(ap).unwrap(), 48000);
	assert_eq!(unsafe { oaknode_sequence_set_audio_params(dup(&seq), 0, ap) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_sequence_get_audio_params(dup(&seq), 9, &mut ap) }, OAKNODE_E_NOT_FOUND);
	assert_eq!(unsafe { oaknode_sequence_set_audio_params(dup(&seq), 9, ap) }, OAKNODE_E_NOT_FOUND);
	assert_eq!(unsafe { oaknode_sequence_get_audio_params(dup(&seq), -1, &mut ap) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_sequence_set_audio_params(dup(&seq), 0, std::ptr::null()) }, OAKNODE_E_INVALID);
	oaknode::bridge::core::audioparams_free(ap);

	// Footage: no video streams -> NOT_FOUND both ways; bad args.
	let mut footage = unsafe { oaknode_footage_create(dup(&p), cs("/media/clip.mov").as_ptr()) };
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&footage)) }, OAKNODE_OK);
	let mut fvp = CHandle::null();
	assert_eq!(
		unsafe { oaknode_footage_get_video_params(dup(&footage), 0, &mut fvp as *mut CHandle as *mut std::ffi::c_void) },
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(
		unsafe { oaknode_footage_set_video_params(dup(&footage), 0, &fvp as *const _ as *const std::ffi::c_void) },
		OAKNODE_E_INVALID,
		"empty handle rejected"
	);
	assert_eq!(
		unsafe { oaknode_footage_get_video_params(dup(&footage), -1, &mut fvp as *mut CHandle as *mut std::ffi::c_void) },
		OAKNODE_E_INVALID
	);

	free(&mut footage);
	free(&mut seq);
	unsafe { oaknode_project_free(&mut p) };
}

/// Colormanager compliant transform: a display transform whose view is
/// clamped to the active config's defaults (test-stub backed).
#[cfg(feature = "test-stubs")]
#[test]
fn colortransform_compliance() {
	let _g = lock();
	let mut p = unsafe { oaknode_project_init() };
	let mut cm = unsafe { oaknode_colormanager_init(dup(&p)) };

	// Config not loaded -> E_STATE.
	let mut t = oaknode::bridge::common::colortransform_init_display("sRGB", "Standard", "").unwrap();
	let mut out = CHandle::null();
	assert_eq!(
		unsafe { oaknode_colormanager_get_compliant_color_transform(dup(&cm), dup(&t), 0, &mut out) },
		OAKNODE_E_STATE
	);

	// Load the config, then a display transform with an unknown view gets
	// clamped to the default view.
	assert_eq!(unsafe { oaknode_colormanager_initialize(dup(&cm)) }, OAKNODE_OK);
	assert_eq!(
		unsafe { oaknode_colormanager_get_compliant_color_transform(dup(&cm), dup(&t), 0, &mut out) },
		OAKNODE_OK
	);
	assert!(!out.ctx.is_null());
	oaknode::bridge::common::colortransform_free(&mut out);

	// An output transform with an unknown colorspace clamps to the
	// default input space.
	let mut t2 = oaknode::bridge::common::colortransform_init_output("no-such-space").unwrap();
	assert_eq!(
		unsafe { oaknode_colormanager_get_compliant_color_transform(dup(&cm), dup(&t2), 0, &mut out) },
		OAKNODE_OK
	);
	assert!(!out.ctx.is_null());
	oaknode::bridge::common::colortransform_free(&mut out);

	// Failure: null out, empty manager.
	assert_eq!(
		unsafe { oaknode_colormanager_get_compliant_color_transform(dup(&cm), dup(&t), 0, std::ptr::null_mut()) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_colormanager_get_compliant_color_transform(CHandle::null(), dup(&t), 0, &mut out) },
		OAKNODE_E_INVALID
	);

	oaknode::bridge::common::colortransform_free(&mut t);
	oaknode::bridge::common::colortransform_free(&mut t2);
	free(&mut cm);
	unsafe { oaknode_project_free(&mut p) };
}
