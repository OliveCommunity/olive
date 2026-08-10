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

//! Smoke tests for the timeline family (`engine/include/oakengine/timeline.h`).
//!
//! The facade owns a process-wide undo stack, so every test that pushes
//! undoable commands (sequence creation, tracks, markers, workarea, clip
//! editing) is serialized inside the single `timeline_lifecycle` test; the
//! failure-path test only exercises non-mutating NULL-handle calls and runs
//! in parallel.
//!
//! Clips are placed through `oaknode::ffi` directly (the module clip has no
//! `buffer_in` input, so `oakengine_sequence_add_footage_clip` cannot
//! connect a footage node; that test is `#[ignore]`d with the reason).

#[path = "common/mod.rs"]
mod common;

use std::ffi::{c_char, c_int};

use oakfacade::handle::{box_handle, OakEngineNode};
use oakfacade::node::{
	oakengine_footage_borrow, oakengine_project_create, oakengine_project_free,
	oakengine_project_new,
};
use oakfacade::timeline::{
	oakengine_block_get_range, oakengine_block_get_track, oakengine_block_is_enabled,
	oakengine_block_is_gap, oakengine_block_link_count, oakengine_block_next,
	oakengine_block_prev, oakengine_block_set_enabled, oakengine_block_set_length_and_media_out,
	oakengine_clip_are_linked, oakengine_clip_get_range, oakengine_clip_get_sequence,
	oakengine_clip_is_enabled, oakengine_clip_toggle_enabled, oakengine_clip_trim,
	oakengine_marker_commit_time, oakengine_marker_get_color, oakengine_marker_get_name,
	oakengine_marker_get_time, oakengine_marker_has_sibling_at_time, oakengine_marker_list_add,
	oakengine_marker_list_at, oakengine_marker_list_count, oakengine_marker_list_marker_at_time,
	oakengine_marker_remove, oakengine_marker_set_time_command, oakengine_marker_set_time_live,
	oakengine_multicam_switch_source, oakengine_node_is_block, oakengine_node_is_transition,
	oakengine_sequence_add_default_nodes, oakengine_sequence_add_footage_clip,
	oakengine_sequence_add_track, oakengine_sequence_clip_at, oakengine_sequence_clip_count,
	oakengine_sequence_delete_empty_tracks, oakengine_sequence_get_frame_rate,
	oakengine_sequence_get_length, oakengine_sequence_get_length_rational,
	oakengine_sequence_get_playhead, oakengine_sequence_get_playhead_seconds,
	oakengine_sequence_get_audio_params, oakengine_sequence_get_preview_divider,
	oakengine_sequence_get_video_auto_cache,
	oakengine_sequence_get_workarea, oakengine_sequence_last_error,
	oakengine_sequence_marker_add, oakengine_sequence_marker_add_ex, oakengine_sequence_marker_at,
	oakengine_sequence_marker_count, oakengine_sequence_marker_remove,
	oakengine_sequence_marker_remove_many, oakengine_sequence_marker_rename,
	oakengine_sequence_move_clip, oakengine_sequence_name, oakengine_sequence_new,
	oakengine_sequence_remove_track, oakengine_sequence_ripple_delete_clip,
	oakengine_sequence_ripple_delete_range,
	oakengine_sequence_set_audio_params, oakengine_sequence_set_playhead,
	oakengine_sequence_set_workarea,
	oakengine_sequence_split_clip, oakengine_sequence_track_at, oakengine_sequence_track_count,
	oakengine_sequence_track_list, oakengine_sequence_trim_clips_to,
	oakengine_sequence_workarea_is_enabled, oakengine_track_block_at, oakengine_track_block_count,
	oakengine_track_get_height, oakengine_track_get_length, oakengine_track_is_locked,
	oakengine_track_is_muted, oakengine_track_is_range_free, oakengine_track_set_height,
	oakengine_track_set_locked, oakengine_track_set_muted, oakengine_track_type,
	oakengine_track_visible_block_at_time, oakengine_workarea_create, oakengine_workarea_free,
	oakengine_workarea_get, oakengine_workarea_reset_in_out, oakengine_workarea_set_enabled,
	oakengine_workarea_set_range, oakengine_workarea_set_range_undoable,
	oakengine_workarea_set_enabled_undoable,
};

/// Read a NUL-terminated facade string into a Rust String.
unsafe fn read_buf(buf: &mut [c_char]) -> String {
	std::ffi::CStr::from_ptr(buf.as_ptr())
		.to_string_lossy()
		.into_owned()
}

/// Force the runtime-dlsym'd symbols into the link: the oaknode module
/// resolves `oakcommon_videoparams_*` and `oakundo_command_init` at runtime
/// with `dlsym(RTLD_DEFAULT)`, and nothing references those codegen units at
/// link time unless named here (the node.rs family force-links
/// `oakundo_command_init` for the same reason).
fn force_runtime_syms() -> usize {
	let fns: [usize; 8] = [
		oakcommon::ffi::videoparams::oakcommon_videoparams_init_basic as *const () as usize,
		oakcommon::ffi::videoparams::oakcommon_videoparams_set_frame_rate as *const () as usize,
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_width as *const () as usize,
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_height as *const () as usize,
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_format as *const () as usize,
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_channel_count as *const () as usize,
		oakcommon::ffi::videoparams::oakcommon_videoparams_get_frame_rate as *const () as usize,
		oakundo::ffi::command::oakundo_command_init as *const () as usize,
	];
	fns.iter().sum()
}

/// Convert a facade `CHandle` to the layout-identical oaknode `CHandle`
/// (distinct Rust types over the same C ABI struct).
fn to_node_handle(h: oakfacade::handle::CHandle) -> oaknode::handle::CHandle {
	oaknode::handle::CHandle {
		ctx: h.ctx,
		addref: h.addref,
		release: h.release,
		abi_version: h.abi_version,
	}
}

/// Convert an oaknode `CHandle` back to the facade `CHandle`.
fn to_facade_handle(h: oaknode::handle::CHandle) -> oakfacade::handle::CHandle {
	oakfacade::handle::CHandle {
		ctx: h.ctx,
		addref: h.addref,
		release: h.release,
		abi_version: h.abi_version,
	}
}

// ---------------------------------------------------------------------------
// Serialized stack-mutating test
// ---------------------------------------------------------------------------

/// Sequence creation, tracks, playhead, markers, workarea and clip editing —
/// all in ONE test because the facade's undo stack is process-wide (the
/// same serialization the undo/node families use).
#[test]
fn timeline_lifecycle() {
	common::force_link();
	let _ = force_runtime_syms();

	// ---- project + sequence creation ----------------------------------
	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);

	// NULL project -> NULL.
	assert!(unsafe { oakengine_sequence_new(std::ptr::null_mut(), c"x".as_ptr()) }.is_null());

	let seq = unsafe { oakengine_sequence_new(project, c"Test Sequence".as_ptr()) };
	assert!(!seq.is_null());

	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_sequence_name(seq, buf.as_mut_ptr(), 256) };
	assert_eq!(len, 13);
	assert_eq!(unsafe { read_buf(&mut buf) }, "Test Sequence");

	// Fresh sequence: zero length, default frame rate.
	let mut seconds = -1.0;
	assert_eq!(unsafe { oakengine_sequence_get_length(seq, &mut seconds) }, 0);
	assert_eq!(seconds, 0.0);
	let mut num = -1;
	let mut den = -1;
	assert_eq!(
		unsafe { oakengine_sequence_get_length_rational(seq, &mut num, &mut den) },
		0
	);
	assert_eq!((num, den), (0, 1));
	let mut fps_num = 0;
	let mut fps_den = 0;
	assert_eq!(
		unsafe { oakengine_sequence_get_frame_rate(seq, &mut fps_num, &mut fps_den) },
		0
	);
	assert_eq!((fps_num, fps_den), (30, 1));

	// ---- tracks ---------------------------------------------------------
	let idx = unsafe { oakengine_sequence_add_track(seq, 0) };
	assert_eq!(idx, 0);
	let mut video = -1;
	let mut audio = -1;
	let mut subtitle = -1;
	assert_eq!(
		unsafe { oakengine_sequence_track_count(seq, &mut video, &mut audio, &mut subtitle) },
		0
	);
	assert_eq!(video, 1);
	assert_eq!(audio, 0);
	assert_eq!(subtitle, 0);

	let track = unsafe { oakengine_sequence_track_at(seq, 0, 0) };
	assert!(!track.is_null());
	assert_eq!(unsafe { oakengine_track_type(track) }, 0); // video
	assert!(unsafe { oakengine_sequence_track_at(seq, 0, 5) }.is_null());
	assert!(!unsafe { oakengine_sequence_track_list(seq, 0) }.is_null());
	assert!(unsafe { oakengine_sequence_track_list(seq, 99) }.is_null());

	// Track height / mute / lock (NOT undoable, straight setters).
	let mut h = 0.0;
	assert_eq!(unsafe { oakengine_track_get_height(seq, 0, 0, &mut h) }, 0);
	assert!((h - 3.0).abs() < 1e-9);
	assert_eq!(unsafe { oakengine_track_set_height(seq, 0, 0, 5.0) }, 0);
	assert_eq!(unsafe { oakengine_track_get_height(seq, 0, 0, &mut h) }, 0);
	assert!((h - 5.0).abs() < 1e-9);
	assert_eq!(unsafe { oakengine_track_is_muted(seq, 0, 0) }, 0);
	assert_eq!(unsafe { oakengine_track_set_muted(seq, 0, 0, 1) }, 0);
	assert_eq!(unsafe { oakengine_track_is_muted(seq, 0, 0) }, 1);
	assert_eq!(unsafe { oakengine_track_is_locked(seq, 0, 0) }, 0);
	assert_eq!(unsafe { oakengine_track_set_locked(seq, 0, 0, 1) }, 0);
	assert_eq!(unsafe { oakengine_track_is_locked(seq, 0, 0) }, 1);
	assert_eq!(unsafe { oakengine_track_set_locked(seq, 0, 0, 0) }, 0);

	// Track length (empty -> 0) and free-range query.
	let mut tlen = -1;
	assert_eq!(unsafe { oakengine_track_get_length(seq, 0, 0, &mut tlen) }, 0);
	assert_eq!(tlen, 0);
	assert_eq!(unsafe { oakengine_track_is_range_free(seq, 0, 0, 0, 30) }, 1);
	// Bad track index -> E_NOT_FOUND (-4).
	assert_eq!(unsafe { oakengine_track_get_length(seq, 0, 99, &mut tlen) }, -4);
	assert_eq!(unsafe { oakengine_track_is_range_free(seq, 0, 99, 0, 30) }, -4);

	// ---- playhead ---------------------------------------------------------
	assert_eq!(unsafe { oakengine_sequence_set_playhead(seq, 90) }, 0);
	let mut ph = -1;
	assert_eq!(unsafe { oakengine_sequence_get_playhead(seq, &mut ph) }, 0);
	assert_eq!(ph, 90);
	let mut phs = 0.0;
	assert_eq!(unsafe { oakengine_sequence_get_playhead_seconds(seq, &mut phs) }, 0);
	assert!((phs - 3.0).abs() < 1e-6);

	// ---- audio params (round-trip through oakcore) -------------------------
	let mut arate: c_int = 0;
	let mut alayout: u64 = 0;
	assert_eq!(unsafe { oakengine_sequence_get_audio_params(seq, &mut arate, &mut alayout) }, 0);
	assert!(arate > 0);
	assert_eq!(unsafe { oakengine_sequence_set_audio_params(seq, 48000, 0x3, 1) }, 0);
	assert_eq!(unsafe { oakengine_sequence_get_audio_params(seq, &mut arate, &mut alayout) }, 0);
	assert_eq!(arate, 48000);
	assert_eq!(alayout, 0x3);
	// A no-op change (same values) is a success without a new command.
	assert_eq!(unsafe { oakengine_sequence_set_audio_params(seq, 48000, 0x3, 1) }, 0);
	// NULL handle -> E_INVALID.
	assert_eq!(
		unsafe { oakengine_sequence_set_audio_params(std::ptr::null_mut(), 48000, 0x3, 1) },
		-1
	);

	// ---- markers -----------------------------------------------------------
	assert_eq!(unsafe { oakengine_sequence_marker_count(seq) }, 0);
	assert_eq!(unsafe { oakengine_sequence_marker_add(seq, 30, c"One".as_ptr()) }, 0);
	assert_eq!(
		unsafe { oakengine_sequence_marker_add_ex(seq, 60, c"Two".as_ptr(), 2) },
		0
	);
	assert_eq!(unsafe { oakengine_sequence_marker_count(seq) }, 2);

	let mut mtime = -1;
	let mut mcolor = -1;
	let mut mname = [0 as c_char; 64];
	assert_eq!(
		unsafe { oakengine_sequence_marker_at(seq, 0, &mut mtime, mname.as_mut_ptr(), 64, &mut mcolor) },
		0
	);
	assert_eq!(mtime, 30);
	assert_eq!(mcolor, 0);
	assert_eq!(unsafe { read_buf(&mut mname) }, "One");

	// A duplicate time is rejected with E_STATE.
	assert_eq!(unsafe { oakengine_sequence_marker_add(seq, 30, c"dup".as_ptr()) }, -2);

	// Rename, then remove many at once.
	assert_eq!(unsafe { oakengine_sequence_marker_rename(seq, 30, c"Renamed".as_ptr()) }, 0);
	assert_eq!(
		unsafe {
			oakengine_sequence_marker_remove_many(seq, [30i64, 60].as_ptr(), 2)
		},
		2
	);
	assert_eq!(unsafe { oakengine_sequence_marker_count(seq) }, 0);
	// Removing a nonexistent time -> E_NOT_FOUND (-4).
	assert_eq!(unsafe { oakengine_sequence_marker_remove(seq, 999) }, -4);

	// ---- workarea ------------------------------------------------------------
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(seq) }, 0);
	assert_eq!(unsafe { oakengine_sequence_set_workarea(seq, 1, 0, 300) }, 0);
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(seq) }, 1);
	let mut wa_in = -1;
	let mut wa_out = -1;
	assert_eq!(
		unsafe { oakengine_sequence_get_workarea(seq, &mut wa_in, &mut wa_out) },
		0
	);
	assert_eq!((wa_in, wa_out), (0, 300));

	// Reset sentinels: in = 0/1, out = RATIONAL_MAX/1.
	let mut rn = -1;
	let mut rd = -1;
	let mut ron = -1;
	let mut rod = -1;
	unsafe { oakengine_workarea_reset_in_out(&mut rn, &mut rd, &mut ron, &mut rod) };
	assert_eq!((rn, rd), (0, 1));
	assert_eq!(ron, i32::MAX as i64);
	assert_eq!(rod, 1);

	// Standalone workarea round-trip.
	let wa = oakengine_workarea_create();
	assert!(!wa.is_null());
	assert_eq!(unsafe { oakengine_workarea_set_range(wa, 10, 1, 20, 1) }, 0);
	assert_eq!(unsafe { oakengine_workarea_set_enabled(wa, 1) }, 0);
	let mut wn0 = -1;
	let mut wd0 = -1;
	let mut wn1 = -1;
	let mut wd1 = -1;
	let mut wen = -1;
	assert_eq!(
		unsafe { oakengine_workarea_get(wa, &mut wn0, &mut wd0, &mut wn1, &mut wd1, &mut wen) },
		0
	);
	assert_eq!((wn0, wd0, wn1, wd1, wen), (10, 1, 20, 1, 1));
	// Undoable variant on the standalone handle (pushed, not added to a parent).
	assert_eq!(
		unsafe { oakengine_workarea_set_range_undoable(wa, 30, 1, 40, 1, 10, 1, 20, 1, std::ptr::null_mut()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_workarea_set_enabled_undoable(wa, 0, std::ptr::null_mut()) },
		0
	);
	unsafe { oakengine_workarea_free(wa) };

	// ---- clip editing (blocks placed through oaknode::ffi directly) --------
	// The module clip has no `buffer_in` input, so footage clips cannot be
	// placed; a raw clip node is appended to the track instead.
	let track_module = to_node_handle(unsafe { (*track).handle });
	let clip = unsafe { oaknode::ffi::block::oaknode_block_clip_create() };
	assert!(!clip.ctx.is_null());
	unsafe { oaknode::ffi::block::oaknode_block_set_in(clip.clone(), 0, 1) };
	unsafe { oaknode::ffi::block::oaknode_block_set_length_and_media_in(clip.clone(), 1, 1) };
	unsafe { oaknode::ffi::block::oaknode_clip_set_media_in(clip.clone(), 0, 1) };
	assert_eq!(
		unsafe { oaknode::ffi::track::oaknode_track_append_block(track_module.clone(), clip.clone()) },
		0
	);

	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 1);
	let mut clip_at = unsafe { oakengine_sequence_clip_at(seq, 0, 0, 0) };
	assert!(!clip_at.is_null());
	let mut cin = -1;
	let mut cout = -1;
	let mut cmi = -1;
	assert_eq!(
		unsafe { oakengine_clip_get_range(clip_at, &mut cin, &mut cout, &mut cmi) },
		0
	);
	assert_eq!((cin, cout, cmi), (0, 30, 0));

	// Generic block traversal.
	assert_eq!(unsafe { oakengine_track_block_count(track) }, 1);
	let blk = unsafe { oakengine_track_block_at(track, 0) };
	assert!(!blk.is_null());
	assert_eq!(unsafe { oakengine_block_is_gap(blk) }, 0);
	let mut bin = -1;
	let mut bout = -1;
	assert_eq!(unsafe { oakengine_block_get_range(blk, &mut bin, &mut bout) }, 0);
	assert_eq!((bin, bout), (0, 30));
	assert!(!unsafe { oakengine_block_get_track(blk) }.is_null());
	assert!(unsafe { oakengine_block_next(blk) }.is_null());
	assert!(unsafe { oakengine_block_prev(blk) }.is_null());
	assert_eq!(unsafe { oakengine_block_link_count(blk) }, 0);

	// Clip enable toggling (undoable, one command).
	assert_eq!(unsafe { oakengine_clip_toggle_enabled(&mut clip_at, 1) }, 1);
	assert_eq!(unsafe { oakengine_clip_is_enabled(clip_at) }, 0);
	assert_eq!(unsafe { oakengine_block_set_enabled(blk, 1) }, 0);
	assert_eq!(unsafe { oakengine_block_is_enabled(blk) }, 1);

	// Block resize (undoable): length 30 -> 40, in stays.
	assert_eq!(unsafe { oakengine_block_set_length_and_media_out(blk, 40) }, 0);
	assert_eq!(unsafe { oakengine_block_get_range(blk, &mut bin, &mut bout) }, 0);
	assert_eq!((bin, bout), (0, 40));

	// Split at 20 -> two clips (the module's split lists the right half
	// first: clip0 = [20, 40)).
	assert_eq!(unsafe { oakengine_sequence_split_clip(seq, 0, 0, 0, 20) }, 0);
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 2);
	// Split outside the clip -> E_INVALID and a last-error.
	assert_eq!(unsafe { oakengine_sequence_split_clip(seq, 0, 0, 0, 100) }, -1);
	let mut err = [0 as c_char; 256];
	let elen = unsafe { oakengine_sequence_last_error(err.as_mut_ptr(), 256) };
	assert!(elen > 0, "last_error must be non-empty after a failed split");

	// Trim the first clip [20, 40) to [25, 35).
	let clip0 = unsafe { oakengine_sequence_clip_at(seq, 0, 0, 0) };
	assert!(!clip0.is_null());
	assert_eq!(unsafe { oakengine_clip_trim(clip0, 25, 35) }, 0);
	let mut cin2 = -1;
	let mut cout2 = -1;
	let mut cmi2 = -1;
	assert_eq!(
		unsafe { oakengine_clip_get_range(clip0, &mut cin2, &mut cout2, &mut cmi2) },
		0
	);
	assert_eq!((cin2, cout2), (25, 35));

	// Trim clips to a point on every unlocked track (edge 0 = in).
	assert!(unsafe { oakengine_sequence_trim_clips_to(seq, 0, 25) } >= 0);

	// Move the clip is a documented stub (the module's gap+place composition
	// faults) -> E_STATE.
	assert_eq!(unsafe { oakengine_sequence_move_clip(seq, 0, 0, 0, 50) }, -2);

	// Ripple delete the addressed clip.
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_clip(seq, 0, 0, 0) }, 0);

	// Ripple delete a range on every track.
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_range(seq, 0, 10) }, 0);

	// Linked clips: two fresh clips linked then unlinked.
	let clip_b = unsafe { oaknode::ffi::block::oaknode_block_clip_create() };
	unsafe { oaknode::ffi::block::oaknode_block_set_in(clip_b.clone(), 0, 1) };
	unsafe { oaknode::ffi::block::oaknode_block_set_length_and_media_in(clip_b.clone(), 1, 3) };
	unsafe { oaknode::ffi::block::oaknode_clip_set_media_in(clip_b.clone(), 0, 1) };
	assert_eq!(
		unsafe { oaknode::ffi::track::oaknode_track_append_block(track_module.clone(), clip_b.clone()) },
		0
	);
	let clip_b_engine = unsafe { oakengine_sequence_clip_at(seq, 0, 0, 0) };
	assert!(!clip_b_engine.is_null());
	let mut clips = [clip_b_engine, clip_at];
	assert_eq!(unsafe { oakengine_clip_toggle_enabled(clips.as_mut_ptr(), 2) }, 2);
	assert_eq!(unsafe { oakengine_clip_are_linked(clips[0], clips[1]) }, 0);

	// ---- default nodes + track removal ----------------------------------
	// Add one video + one audio track as one command.
	assert_eq!(unsafe { oakengine_sequence_add_default_nodes(seq) }, 0);
	assert_eq!(
		unsafe { oakengine_sequence_track_count(seq, &mut video, &mut audio, &mut subtitle) },
		0
	);
	assert_eq!(video, 2);
	assert_eq!(audio, 1);

	// Remove the audio track.
	assert_eq!(unsafe { oakengine_sequence_remove_track(seq, 1, 0) }, 0);
	assert_eq!(
		unsafe { oakengine_sequence_track_count(seq, &mut video, &mut audio, &mut subtitle) },
		0
	);
	assert_eq!(audio, 0);

	// Delete empty tracks (the extra video track is empty).
	assert_eq!(unsafe { oakengine_sequence_delete_empty_tracks(seq, -1) }, 1);

	// ---- cleanup ----------------------------------------------------------
	unsafe { oakengine_project_free(project) };
}

// ---------------------------------------------------------------------------
// Non-mutating failure paths (no undo-stack access; run in parallel)
// ---------------------------------------------------------------------------

/// NULL handles yield the header's documented values/codes.
#[test]
fn timeline_failure_paths() {
	common::force_link();

	// NULL sequence.
	let mut buf = [0 as c_char; 64];
	assert_eq!(unsafe { oakengine_sequence_name(std::ptr::null(), buf.as_mut_ptr(), 64) }, -1);
	assert_eq!(unsafe { oakengine_sequence_add_track(std::ptr::null_mut(), 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_marker_count(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_marker_add(std::ptr::null_mut(), 0, c"x".as_ptr()) }, -1);
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_get_preview_divider(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_get_video_auto_cache(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_set_playhead(std::ptr::null_mut(), 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_set_workarea(std::ptr::null_mut(), 1, 0, 10) }, -1);
	assert!(unsafe { oakengine_sequence_track_at(std::ptr::null(), 0, 0) }.is_null());
	assert!(unsafe { oakengine_sequence_track_list(std::ptr::null_mut(), 0) }.is_null());
	assert!(unsafe { oakengine_sequence_clip_at(std::ptr::null_mut(), 0, 0, 0) }.is_null());
	assert_eq!(unsafe { oakengine_sequence_clip_count(std::ptr::null_mut(), 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_clip(std::ptr::null_mut(), 0, 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_range(std::ptr::null_mut(), 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_sequence_remove_track(std::ptr::null_mut(), 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_add_default_nodes(std::ptr::null_mut()) }, -1);

	// NULL clip / block handles.
	assert_eq!(
		unsafe { oakengine_clip_get_range(std::ptr::null(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert_eq!(unsafe { oakengine_clip_trim(std::ptr::null_mut(), 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_clip_is_enabled(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_clip_are_linked(std::ptr::null(), std::ptr::null()) }, 0);
	assert!(unsafe { oakengine_clip_get_sequence(std::ptr::null()) }.is_null());
	assert_eq!(unsafe { oakengine_track_block_count(std::ptr::null()) }, -1);
	assert!(unsafe { oakengine_track_block_at(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_track_visible_block_at_time(std::ptr::null_mut(), 0) }.is_null());
	assert!(unsafe { oakengine_block_get_track(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_block_next(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_block_prev(std::ptr::null()) }.is_null());
	assert_eq!(unsafe { oakengine_block_is_gap(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_block_is_enabled(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_block_set_enabled(std::ptr::null_mut(), 1) }, -1);
	assert_eq!(unsafe { oakengine_block_link_count(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_block_set_length_and_media_out(std::ptr::null_mut(), 10) }, -1);
	assert_eq!(unsafe { oakengine_track_type(std::ptr::null()) }, -1);
	assert_eq!(unsafe { oakengine_node_is_block(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_node_is_transition(std::ptr::null()) }, 0);

	// Marker handle family NULL paths.
	assert_eq!(unsafe { oakengine_marker_list_count(std::ptr::null()) }, 0);
	assert!(unsafe { oakengine_marker_list_at(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_marker_list_marker_at_time(std::ptr::null(), 1, 1) }.is_null());
	assert_eq!(
		unsafe { oakengine_marker_list_add(std::ptr::null_mut(), 0, 1, 0, 1, c"x".as_ptr(), 0) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_marker_get_time(std::ptr::null(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert_eq!(unsafe { oakengine_marker_get_name(std::ptr::null(), std::ptr::null_mut(), 0) }, -1);
	assert_eq!(unsafe { oakengine_marker_get_color(std::ptr::null()) }, -1);
	assert_eq!(unsafe { oakengine_marker_has_sibling_at_time(std::ptr::null(), 1, 1) }, 0);
	assert_eq!(unsafe { oakengine_marker_remove(std::ptr::null_mut()) }, -1);
	assert!(unsafe { oakengine_marker_set_time_command(std::ptr::null_mut(), 1, 1) }.is_null());
	assert_eq!(unsafe { oakengine_marker_set_time_live(std::ptr::null_mut(), 0, 1, 0, 1) }, -1);
	assert_eq!(
		unsafe { oakengine_marker_commit_time(std::ptr::null_mut(), 0, 1, 0, 1, 1, 1, 1, 1, std::ptr::null_mut()) },
		-1
	);

	// Workarea handle NULL paths.
	assert_eq!(
		unsafe { oakengine_workarea_get(std::ptr::null(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert_eq!(unsafe { oakengine_workarea_set_range(std::ptr::null_mut(), 0, 1, 1, 1) }, -1);
	assert_eq!(unsafe { oakengine_workarea_set_enabled(std::ptr::null_mut(), 1) }, -1);
	assert_eq!(
		unsafe { oakengine_workarea_set_range_undoable(std::ptr::null_mut(), 0, 1, 1, 1, 0, 1, 0, 1, std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_workarea_set_enabled_undoable(std::ptr::null_mut(), 1, std::ptr::null_mut()) },
		-1
	);

	// Multicam NULL path.
	assert_eq!(
		unsafe { oakengine_multicam_switch_source(std::ptr::null_mut(), std::ptr::null_mut(), 0, 0, 0.0, std::ptr::null_mut()) },
		-1
	);
}

/// Footage clips need the footage node connected to the clip's `buffer_in`
/// input, which module clips do not declare; the placement is expected to
/// fail with a non-empty last error.
#[test]
#[ignore = "module clips have no buffer input; footage clips cannot be placed"]
fn footage_clip_placement() {
	common::force_link();

	let project = oakengine_project_create();
	assert!(!project.is_null());
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);
	let seq = unsafe { oakengine_sequence_new(project, c"FC".as_ptr()) };
	assert!(!seq.is_null());
	assert_eq!(unsafe { oakengine_sequence_add_track(seq, 0) }, 0);

	// A footage node created through oaknode::ffi directly (no media needed).
	let footage = unsafe {
		oaknode::ffi::footage::oaknode_footage_create(
			to_node_handle((*project).handle),
			c"/no/such/media.mp4".as_ptr(),
		)
	};
	assert!(!footage.ctx.is_null());
	let node_box = unsafe { box_handle::<OakEngineNode>(to_facade_handle(footage)) };
	let footage_handle = unsafe { oakengine_footage_borrow(node_box) };
	assert!(!footage_handle.is_null());

	let clip = unsafe { oakengine_sequence_add_footage_clip(seq, footage_handle, 0, 0, 0, 30, 0) };
	assert!(clip.is_null());
	let mut err = [0 as c_char; 256];
	let elen = unsafe { oakengine_sequence_last_error(err.as_mut_ptr(), 256) };
	assert!(elen > 0, "last_error must explain the failed footage placement");

	unsafe { oakengine_project_free(project) };
}
