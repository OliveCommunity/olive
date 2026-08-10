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

//! Integration tests for the **timeline** family
//! (`engine/include/oakengine/timeline.h`, wrapped by
//! `crates/oakengine/src/timeline.rs` — sequences, tracks, clips, blocks,
//! markers, the workarea, the track-height constants and the multicam
//! helpers).
//!
//! All 139 exported functions are exercised against the REAL module
//! crates (oaknode/oaktimeline/oakcommon/oakundo; no mocks of any API).
//!
//! ## Serialization
//!
//! The facade owns a process-wide undo stack and per-sequence
//! marker-list / workarea caches, so every test that mutates state is
//! serialized inside the single `timeline_zu_lifecycle` test (the same
//! convention as `tests/timeline.rs` and the undo family). The
//! `timeline_zu_failure_paths` test only exercises non-mutating NULL /
//! empty-handle / garbage-argument calls and runs in parallel.
//!
//! ## Documented stubs (asserted on their documented behavior)
//!
//! - `oakengine_sequence_ripple_tracks_command` → NULL (no module C
//!   creator for `TrackListRippleToolCommand`).
//! - `oakengine_sequence_move_clip` / `oakengine_sequence_move_track` →
//!   E_STATE (-2) (the module's gap+place composition faults).
//! - `oakengine_sequence_add_default_transition` → E_STATE (-2) for a
//!   non-empty clip set, 0 for an empty one.
//! - `oakengine_sequence_get_video_auto_cache` → 0 /
//!   `oakengine_sequence_set_video_auto_cache` → 0 (no module accessor).
//! - `oakengine_marker_create` → NULL (no standalone marker handle);
//!   `oakengine_clip_find_multicam` → NULL;
//!   `oakengine_multicam_switch_source` → 0 with a live node, -1 with
//!   NULL (the capi's `Q_UNUSED` body).
//! - `oakengine_clip_request_invalidate` /
//!   `oakengine_clip_request_invalidate_connected` /
//!   `oakengine_clip_discard_cache` → NULL-safe no-ops (headless capi
//!   behavior); `oakengine_clip_add_cache_passthrough` → module no-op.
//! - `oakengine_sequence_add_footage_clip` /
//!   `oakengine_sequence_add_sequence_clip` → NULL with a non-empty last
//!   error (module clips declare no `buffer_in` input and every facade
//!   sequence lives in its own scratch project, so the cross-project
//!   check / footage connection fails cleanly — there is no legal path).
//! - `oakengine_sequence_set_preview_divider` → 0 for a valid divider
//!   (the module's `VideoParams` model drops the divider; the getter
//!   reports 1), -1 for divider < 1.
//!
//! ## Notes / deviations observed while writing these tests
//!
//! - The facade has no `oakengine_sequence_free` / `oakengine_track_free`
//!   / `oakengine_clip_free`, and `oakengine_sequence_new` keeps each
//!   sequence in its own scratch project (documented deviation), so the
//!   oaknode debug alive counter can only return to baseline for the
//!   project shell: `oakengine_project_create` bumps it by exactly 1 and
//!   `oakengine_project_free` brings it back. Sequence/track/clip nodes
//!   remain counted for the process (see the alive assertions in
//!   `timeline_zu_lifecycle`).
//!
//! ## Real bugs found (all reproduced with assertions in this file; see
//! each site for the precise repro)
//!
//! 1. **`oakengine_clip_toggle_enabled(NULL, 0)` aborts the process** —
//!    `slice::from_raw_parts(NULL, 0)` (src/timeline.rs:2637) is a
//!    non-unwinding UB panic that the `catch_unwind` guard cannot catch;
//!    repro in the ignored `timeline_zu_crash_repros` test (run with
//!    `--ignored` to see the SIGABRT). The same NULL+0 slice exists in
//!    `oakengine_sequence_delete_clips` (src/timeline.rs:2453) for
//!    `clips == NULL && clip_count == 0 && ripple == 1 &&
//!    ripple_range_count == 0`.
//! 2. **Module `BlockSplitCommand` misplaces both split halves**
//!    (crates/oaktimeline/src/undosplit.rs): the left half is anchored at
//!    the OLD out-point (it calls the out-anchored
//!    `set_length_and_media_out` instead of an in-anchored setter) and the
//!    right half starts at 0 (its in is never moved to the point). Splitting
//!    [0, 30) at frame 20 yields [10, 30) + [0, 10) instead of
//!    [0, 20) + [20, 30).
//! 3. **`oakengine_sequence_split_clips` (batch split) is a silent no-op**:
//!    the module's `BlockSplitPreservingLinksCommand` never runs `prepare()`
//!    (only `new().to_command()` is built), so `redo()` iterates an empty
//!    child list; the facade reports 0 and nothing is split.
//! 4. **`oakengine_sequence_trim_clips_to` never applies a trim**: it builds
//!    its trim command with the TRACK handle where the BLOCK belongs
//!    (`trim_cmd(track, ...)`, src/timeline.rs:3034), so the redo calls
//!    `oaknode_block_set_length_and_media_out` on a track node and the
//!    module rejects it — the call reports the would-be count and changes
//!    nothing.
//! 5. **`oakengine_sequence_delete_empty_tracks` removes nothing**: unlike
//!    `oakengine_sequence_remove_track` it skips the live
//!    `oaknode_tracklist_remove_track` compensation, and the module's
//!    `TimelineRemoveTrackCommand::redo` is a documented no-op — the call
//!    reports the number of empty tracks found and leaves them in place.
//! 6. **`oakengine_sequence_ripple_delete_clip` /
//!    `oakengine_sequence_ripple_delete_range` are silent no-ops**: the
//!    module's `TrackRippleRemoveAreaCommand::prepare` needs
//!    `oaknode_track_get_nearest_block_before_or_at`, which the oaknode
//!    bridge does not expose, so it finds no block and removes nothing; the
//!    facade reports success.
//!
//! ## Naming
//!
//! The suite lives in `tests/it_timeline.rs` (target `it_timeline`),
//! matching the other `it_<family>` integration tests in this directory.

#[path = "common/mod.rs"]
mod common;

use std::ffi::{c_char, c_int, c_void};

use oakengine::handle::{
	box_handle, free_box, CHandle, OakEngineBlock, OakEngineClip, OakEngineFootage,
	OakEngineMarker, OakEngineMarkerList, OakEngineNode, OakEngineProject, OakEngineSequence,
	OakEngineTrack, OakEngineTrackList, OakEngineWorkarea,
};
use oakengine::node::{
	oakengine_footage_borrow, oakengine_project_create, oakengine_project_free,
	oakengine_project_new,
};
use oakengine::timeline::*;
use oakengine::undo::oakengine_undo_command_free;

/// Read a NUL-terminated facade string into a Rust String.
unsafe fn read_buf(buf: &mut [c_char]) -> String {
	std::ffi::CStr::from_ptr(buf.as_ptr())
		.to_string_lossy()
		.into_owned()
}

/// Box a NULL module handle as a live (empty) engine box. `unbox` then
/// fails with E_INVALID — the "empty handle" (ctx == NULL) case that a
/// plugin can hand to every function.
macro_rules! empty_box {
	($t:ty) => {
		box_handle::<$t>(CHandle::null())
	};
}

/// Force the runtime-dlsym'd symbols into the link: the oaknode module
/// resolves `oakcommon_videoparams_*` and `oakundo_command_init` at
/// runtime with `dlsym(RTLD_DEFAULT)`, and nothing references those
/// codegen units at link time unless named here.
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

/// The module live count of owned node/project handles
/// (`oaknode_debug_alive_count`), the only debug counter backing the
/// timeline family's node objects.
fn alive() -> c_int {
	unsafe { oaknode::ffi::node::oaknode_debug_alive_count() }
}

/// Place a raw module clip on `track` (returns the module handle).
unsafe fn module_clip_on(track: CHandle, in_num: c_int, in_den: c_int) -> CHandle {
	let clip = oaknode::ffi::block::oaknode_block_clip_create();
	oaknode::ffi::block::oaknode_block_set_in(clip, in_num, in_den);
	oaknode::ffi::block::oaknode_block_set_length_and_media_in(clip, 1, 1);
	oaknode::ffi::block::oaknode_clip_set_media_in(clip, 0, 1);
	oaknode::ffi::track::oaknode_track_append_block(track, clip);
	clip
}

/// Assert `last_error` is non-empty (a failure path recorded a reason).
unsafe fn assert_last_error() {
	let mut err = [0 as c_char; 256];
	let elen = unsafe { oakengine_sequence_last_error(err.as_mut_ptr(), 256) };
	assert!(elen > 0, "last_error must be non-empty");
}

/// The clip at (track_type, track_index, clip_index), panicking if none.
unsafe fn clip_at_ok(
	seq: *mut OakEngineSequence,
	tt: c_int,
	ti: c_int,
	ci: c_int,
) -> *mut OakEngineClip {
	let c = unsafe { oakengine_sequence_clip_at(seq, tt, ti, ci) };
	assert!(!c.is_null(), "expected clip at ({tt},{ti},{ci})");
	c
}

// ---------------------------------------------------------------------------
// Serialized lifecycle test (all mutating operations, in ONE test because
// the facade's undo stack and per-sequence caches are process-wide)
// ---------------------------------------------------------------------------

/// Every mutating path of the timeline family: sequence creation and
/// inspection, video/audio params, playhead, markers, workarea (+ ripple
/// in-to-out), track structure, clip editing (split / trim / delete /
/// ripple / link / toggle), the marker-handle and workarea-handle
/// families, the free contracts, and the oaknode alive-count deltas.
#[test]
fn timeline_zu_lifecycle() {
	common::force_link();
	let _ = force_runtime_syms();

	let base = alive();

	// ---- project + sequence creation --------------------------------------
	let project = unsafe { oakengine_project_create() };
	assert!(!project.is_null());
	assert_eq!(alive(), base + 1, "project_create must own one node");
	assert_eq!(unsafe { oakengine_project_new(project) }, 0);

	// NULL project -> NULL sequence.
	assert!(unsafe { oakengine_sequence_new(std::ptr::null_mut(), c"x".as_ptr()) }.is_null());

	let seq = unsafe { oakengine_sequence_new(project, c"Test Sequence".as_ptr()) };
	assert!(!seq.is_null());
	assert_eq!(alive(), base + 2, "sequence_new must own one node");

	// A second sequence for the self-nest / cross-project checks.
	let seq2 = unsafe { oakengine_sequence_new(project, c"Nested".as_ptr()) };
	assert!(!seq2.is_null());

	// ---- name (buf/size convention) ----------------------------------------
	let mut buf = [0 as c_char; 256];
	let len = unsafe { oakengine_sequence_name(seq, buf.as_mut_ptr(), 256) };
	assert_eq!(len, 13);
	assert_eq!(unsafe { read_buf(&mut buf) }, "Test Sequence");
	// Two-stage: NULL buffer / zero size only reports the length.
	assert_eq!(unsafe { oakengine_sequence_name(seq, std::ptr::null_mut(), 0) }, 13);
	// A too-small buffer is truncated by the module (2 chars + NUL).
	let mut small = [0 as c_char; 3];
	let len = unsafe { oakengine_sequence_name(seq, small.as_mut_ptr(), 3) };
	assert_eq!(len, 13);
	assert_eq!(unsafe { read_buf(&mut small) }, "Te");

	// ---- length / frame rate / video params ---------------------------------
	let mut seconds = -1.0;
	assert_eq!(unsafe { oakengine_sequence_get_length(seq, &mut seconds) }, 0);
	assert_eq!(seconds, 0.0);
	let (mut n, mut d) = (-1, -1);
	assert_eq!(unsafe { oakengine_sequence_get_length_rational(seq, &mut n, &mut d) }, 0);
	assert_eq!((n, d), (0, 1));
	// NULL out params are fine.
	assert_eq!(
		unsafe { oakengine_sequence_get_length_rational(seq, std::ptr::null_mut(), std::ptr::null_mut()) },
		0
	);
	let (mut fn_, mut fd) = (0, 0);
	assert_eq!(unsafe { oakengine_sequence_get_frame_rate(seq, &mut fn_, &mut fd) }, 0);
	assert_eq!((fn_, fd), (30, 1));

	let (mut w, mut h, mut pn, mut pd) = (0, 0, 0, 0);
	assert_eq!(
		unsafe { oakengine_sequence_get_video_params(seq, &mut w, &mut h, &mut pn, &mut pd) },
		0
	);
	assert_eq!((w, h), (1920, 1080));
	assert_eq!((pn, pd), (1, 1));

	let (mut exw, mut exh, mut exfn, mut exfd, mut expn, mut expd, mut exi, mut exf, mut exdv) =
		(0, 0, 0, 0, 0, 0, -1, -1, 0);
	assert_eq!(
		unsafe {
			oakengine_sequence_get_video_params_ex(
				seq,
				&mut exw,
				&mut exh,
				&mut exfn,
				&mut exfd,
				&mut expn,
				&mut expd,
				&mut exi,
				&mut exf,
				&mut exdv,
			)
		},
		0
	);
	assert_eq!((exw, exh), (1920, 1080));
	assert_eq!((exfn, exfd), (30, 1));
	assert_eq!((expn, expd), (1, 1));
	assert_eq!(exi, 0); // progressive
	assert_eq!(exf, 4); // f32
	assert_eq!(exdv, 1);

	// ---- set_video_params (legal matrix + validation) -----------------------
	// Change, read back, restore to 30 fps BEFORE any frame conversions.
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 1280, 720, 24, 1, 1, 1, 0, 4, 0) },
		0
	);
	assert_eq!(
		unsafe { oakengine_sequence_get_video_params(seq, &mut w, &mut h, &mut pn, &mut pd) },
		0
	);
	assert_eq!((w, h), (1280, 720));
	assert_eq!(
		unsafe { oakengine_sequence_get_frame_rate(seq, &mut fn_, &mut fd) },
		0
	);
	assert_eq!((fn_, fd), (24, 1));
	// Restore the 30 fps timebase.
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 1920, 1080, 30, 1, 1, 1, 0, 4, 0) },
		0
	);
	assert_eq!(
		unsafe { oakengine_sequence_get_frame_rate(seq, &mut fn_, &mut fd) },
		0
	);
	assert_eq!((fn_, fd), (30, 1));
	// -1 leaves a field unchanged (only the width changes).
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 640, -1, -1, -1, -1, -1, -1, -1, 0) },
		0
	);
	assert_eq!(
		unsafe { oakengine_sequence_get_video_params(seq, &mut w, &mut h, &mut pn, &mut pd) },
		0
	);
	assert_eq!((w, h), (640, 1080));
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 1920, -1, -1, -1, -1, -1, -1, -1, 0) },
		0
	);
	// Validation failures (each sets a last error).
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 0, 1080, 30, 1, 1, 1, 0, 4, 0) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 1920, 1080, 0, 1, 1, 1, 0, 4, 0) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 1920, 1080, 30, 1, 1, 1, 5, 4, 0) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 1920, 1080, 30, 1, 1, 1, 0, 99, 0) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(seq, 1920, 1080, -1, 1, 1, 1, 0, 4, 0) },
		-1
	);
	unsafe { assert_last_error() };

	// ---- audio params (round-trip through the oakcore stub store) ----------
	let (mut arate, mut alayout) = (0 as c_int, 0u64);
	assert_eq!(
		unsafe { oakengine_sequence_get_audio_params(seq, &mut arate, &mut alayout) },
		0
	);
	assert!(arate > 0);
	assert_eq!(unsafe { oakengine_sequence_set_audio_params(seq, 48000, 0x3, 1) }, 0);
	assert_eq!(
		unsafe { oakengine_sequence_get_audio_params(seq, &mut arate, &mut alayout) },
		0
	);
	assert_eq!((arate, alayout), (48000, 0x3));
	// A no-op change (same values) succeeds without a new command.
	assert_eq!(unsafe { oakengine_sequence_set_audio_params(seq, 48000, 0x3, 1) }, 0);

	// ---- preview divider / video auto-cache (module-stubbed) ----------------
	assert_eq!(unsafe { oakengine_sequence_get_preview_divider(seq) }, 1);
	assert_eq!(unsafe { oakengine_sequence_set_preview_divider(seq, 2, 0) }, 0);
	assert_eq!(unsafe { oakengine_sequence_get_preview_divider(seq) }, 1);
	assert_eq!(unsafe { oakengine_sequence_set_preview_divider(seq, 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_set_preview_divider(seq, -2, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_get_video_auto_cache(seq) }, 0);
	assert_eq!(unsafe { oakengine_sequence_set_video_auto_cache(seq, 1, 1) }, 0);
	assert_eq!(unsafe { oakengine_sequence_get_video_auto_cache(seq) }, 0);

	// ---- track counts (fresh sequence: none) ---------------------------------
	let (mut v, mut a, mut s) = (-1, -1, -1);
	assert_eq!(unsafe { oakengine_sequence_track_count(seq, &mut v, &mut a, &mut s) }, 0);
	assert_eq!((v, a, s), (0, 0, 0));
	assert_eq!(
		unsafe {
			oakengine_sequence_track_count(
				seq,
				std::ptr::null_mut(),
				std::ptr::null_mut(),
				std::ptr::null_mut(),
			)
		},
		0
	);

	// ---- playhead -------------------------------------------------------------
	let mut ts = -1i64;
	assert_eq!(unsafe { oakengine_sequence_get_playhead(seq, &mut ts) }, 0);
	assert_eq!(ts, 0);
	assert_eq!(unsafe { oakengine_sequence_set_playhead(seq, 90) }, 0);
	assert_eq!(unsafe { oakengine_sequence_get_playhead(seq, &mut ts) }, 0);
	assert_eq!(ts, 90);
	let mut phs = 0.0;
	assert_eq!(unsafe { oakengine_sequence_get_playhead_seconds(seq, &mut phs) }, 0);
	assert!((phs - 3.0).abs() < 1e-6);
	assert_eq!(unsafe { oakengine_sequence_set_playhead(seq, 0) }, 0);

	// ---- workarea (sequence) + ripple in-to-out --------------------------------
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(seq) }, 0);
	assert_eq!(unsafe { oakengine_sequence_set_workarea(seq, 1, 0, 300) }, 0);
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(seq) }, 1);
	let (mut wi, mut wo) = (-1i64, -1i64);
	assert_eq!(unsafe { oakengine_sequence_get_workarea(seq, &mut wi, &mut wo) }, 0);
	assert_eq!((wi, wo), (0, 300));
	assert_eq!(unsafe { oakengine_sequence_set_workarea(seq, 0, 0, 300) }, 0);
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(seq) }, 0);

	// ripple_delete_in_to_out requires the workarea enabled.
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_in_to_out(seq, 0, 0, 300) }, -2);
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_in_to_out(seq, 1, -1, 300) }, -1);
	unsafe { assert_last_error() };
	assert_eq!(unsafe { oakengine_sequence_set_workarea(seq, 1, 0, 300) }, 0);
	// Gap-fill variant (ripple = 0) on the (still empty) tracks.
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_in_to_out(seq, 0, 0, 300) }, 0);
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(seq) }, 0);
	// Ripple variant (ripple = 1) after re-enabling.
	assert_eq!(unsafe { oakengine_sequence_set_workarea(seq, 1, 0, 300) }, 0);
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_in_to_out(seq, 1, 0, 300) }, 0);
	// Disabled again -> E_STATE.
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_in_to_out(seq, 0, 0, 300) }, -2);

	// ---- sequence markers -----------------------------------------------------
	assert_eq!(unsafe { oakengine_sequence_marker_count(seq) }, 0);
	assert_eq!(unsafe { oakengine_sequence_marker_add(seq, 30, c"One".as_ptr()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_marker_add_ex(seq, 60, c"Two".as_ptr(), 2) }, 0);
	assert_eq!(unsafe { oakengine_sequence_marker_count(seq) }, 2);

	let (mut mtime, mut mcolor) = (-1i64, -1);
	let mut mname = [0 as c_char; 64];
	assert_eq!(
		unsafe { oakengine_sequence_marker_at(seq, 0, &mut mtime, mname.as_mut_ptr(), 64, &mut mcolor) },
		0
	);
	assert_eq!(mtime, 30);
	assert_eq!(mcolor, 0);
	assert_eq!(unsafe { read_buf(&mut mname) }, "One");
	// Out-of-range index -> the module's NOT_FOUND (-40004) passes through.
	assert_eq!(
		unsafe { oakengine_sequence_marker_at(seq, 5, &mut mtime, mname.as_mut_ptr(), 64, &mut mcolor) },
		-40004
	);
	// Duplicate time -> E_STATE.
	assert_eq!(unsafe { oakengine_sequence_marker_add(seq, 30, c"dup".as_ptr()) }, -2);
	// Rename, then remove many at once.
	assert_eq!(unsafe { oakengine_sequence_marker_rename(seq, 30, c"Renamed".as_ptr()) }, 0);
	assert_eq!(
		unsafe { oakengine_sequence_marker_remove_many(seq, [30i64, 60].as_ptr(), 2) },
		2
	);
	assert_eq!(unsafe { oakengine_sequence_marker_count(seq) }, 0);
	// Removing a nonexistent time -> E_NOT_FOUND (-4).
	assert_eq!(unsafe { oakengine_sequence_marker_remove(seq, 999) }, -4);
	assert_eq!(unsafe { oakengine_sequence_marker_remove_many(seq, [999i64].as_ptr(), 1) }, -4);
	unsafe { assert_last_error() };
	// NULL name -> empty name.
	assert_eq!(unsafe { oakengine_sequence_marker_add(seq, 90, std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_marker_count(seq) }, 1);
	assert_eq!(unsafe { oakengine_sequence_marker_remove(seq, 90) }, 0);

	// ---- tracks ----------------------------------------------------------------
	let idx = unsafe { oakengine_sequence_add_track(seq, 0) };
	assert_eq!(idx, 0);
	assert_eq!(unsafe { oakengine_sequence_add_track(seq, 1) }, 0); // audio
	assert_eq!(unsafe { oakengine_sequence_add_track(seq, 2) }, 0); // subtitle
	// Garbage track types -> E_INVALID.
	assert_eq!(unsafe { oakengine_sequence_add_track(seq, 3) }, -1);
	assert_eq!(unsafe { oakengine_sequence_add_track(seq, -1) }, -1);
	unsafe { assert_last_error() };

	assert_eq!(unsafe { oakengine_sequence_track_count(seq, &mut v, &mut a, &mut s) }, 0);
	assert_eq!((v, a, s), (1, 1, 1));

	let track = unsafe { oakengine_sequence_track_at(seq, 0, 0) };
	assert!(!track.is_null());
	assert_eq!(unsafe { oakengine_track_type(track) }, 0); // video
	assert!(unsafe { oakengine_sequence_track_at(seq, 0, 5) }.is_null());
	assert!(unsafe { oakengine_sequence_track_at(seq, 3, 0) }.is_null());
	assert!(!unsafe { oakengine_sequence_track_list(seq, 0) }.is_null());
	assert!(unsafe { oakengine_sequence_track_list(seq, 99) }.is_null());

	// Track height / mute / lock (straight setters, NOT undoable).
	let mut h = 0.0;
	assert_eq!(unsafe { oakengine_track_get_height(seq, 0, 0, &mut h) }, 0);
	assert!((h - 3.0).abs() < 1e-9);
	assert_eq!(unsafe { oakengine_track_set_height(seq, 0, 0, 5.0) }, 0);
	assert_eq!(unsafe { oakengine_track_get_height(seq, 0, 0, &mut h) }, 0);
	assert!((h - 5.0).abs() < 1e-9);
	assert_eq!(unsafe { oakengine_track_set_height(seq, 0, 0, -1.0) }, -1);
	unsafe { assert_last_error() };
	assert_eq!(unsafe { oakengine_track_is_muted(seq, 0, 0) }, 0);
	assert_eq!(unsafe { oakengine_track_set_muted(seq, 0, 0, 1) }, 0);
	assert_eq!(unsafe { oakengine_track_is_muted(seq, 0, 0) }, 1);
	assert_eq!(unsafe { oakengine_track_set_muted(seq, 0, 0, 0) }, 0);
	assert_eq!(unsafe { oakengine_track_is_locked(seq, 0, 0) }, 0);
	assert_eq!(unsafe { oakengine_track_set_locked(seq, 0, 0, 1) }, 0);
	assert_eq!(unsafe { oakengine_track_is_locked(seq, 0, 0) }, 1);
	assert_eq!(unsafe { oakengine_track_set_locked(seq, 0, 0, 0) }, 0);

	// Track length (empty -> 0) and free-range query.
	let mut tlen = -1i64;
	assert_eq!(unsafe { oakengine_track_get_length(seq, 0, 0, &mut tlen) }, 0);
	assert_eq!(tlen, 0);
	assert_eq!(unsafe { oakengine_track_is_range_free(seq, 0, 0, 0, 30) }, 1);
	// Bad track index -> E_NOT_FOUND (-4).
	assert_eq!(unsafe { oakengine_track_get_length(seq, 0, 99, &mut tlen) }, -4);
	assert_eq!(unsafe { oakengine_track_is_range_free(seq, 0, 99, 0, 30) }, -4);
	// Invalid ranges.
	assert_eq!(unsafe { oakengine_track_is_range_free(seq, 0, 0, -1, 30) }, -1);
	assert_eq!(unsafe { oakengine_track_is_range_free(seq, 0, 0, 30, 30) }, -1);
	unsafe { assert_last_error() };

	// ---- add_track_command (unpushed command + live out-track) ---------------
	let mut out_track: *mut OakEngineTrack = std::ptr::null_mut();
	let tcmd = unsafe { oakengine_sequence_add_track_command(seq, 0, 1, &mut out_track) };
	assert!(!tcmd.is_null());
	assert!(!out_track.is_null());
	assert_eq!(unsafe { oakengine_track_type(out_track) }, 0);
	// The command owns an internal track node (TimelineAddTrackCommand),
	// and the out-track box wraps the facade-created track. Freeing the
	// command releases ITS node; the out-track was adopted by the list, so
	// its box release un-counts nothing.
	let alive_before = alive();
	unsafe { oakengine_undo_command_free(tcmd) };
	assert_eq!(alive(), alive_before - 1, "freeing the command releases its internal track node");
	unsafe { free_box::<OakEngineTrack>(out_track) };
	assert_eq!(alive(), alive_before - 1, "the adopted out-track box un-counts nothing on release");
	// NULL / garbage paths.
	assert!(unsafe { oakengine_sequence_add_track_command(std::ptr::null_mut(), 0, 0, std::ptr::null_mut()) }
		.is_null());
	assert!(unsafe { oakengine_sequence_add_track_command(seq, 99, 0, std::ptr::null_mut()) }.is_null());
	assert!(unsafe { oakengine_sequence_add_track_command(seq, -2, 0, std::ptr::null_mut()) }.is_null());

	// The live-compensation track makes video = 2 now.
	assert_eq!(unsafe { oakengine_sequence_track_count(seq, &mut v, &mut a, &mut s) }, 0);
	assert_eq!((v, a, s), (2, 1, 1));

	// ---- move_track (stub for a real move, validated indices) ----------------
	assert_eq!(unsafe { oakengine_sequence_move_track(seq, 0, 0, 0) }, 0); // no-op move
	assert_eq!(unsafe { oakengine_sequence_move_track(seq, 0, 0, 1) }, -2); // stub
	assert_eq!(unsafe { oakengine_sequence_move_track(seq, 0, 0, 9) }, -4); // out of range
	unsafe { assert_last_error() };

	// ---- ripple_tracks_command (stub -> NULL) ---------------------------------
	// The facade has no clip-placement path for a module clip without a
	// footage node; the raw clips are appended through oaknode::ffi directly.
	let _ = unsafe { module_clip_on((*track).handle, 0, 1) };
	let clip_a = unsafe { clip_at_ok(seq, 0, 0, 0) };
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 1);
	let (mut cin, mut cout, mut cmi) = (-1i64, -1i64, -1i64);
	assert_eq!(unsafe { oakengine_clip_get_range(clip_a, &mut cin, &mut cout, &mut cmi) }, 0);
	assert_eq!((cin, cout, cmi), (0, 30, 0));
	assert!(!unsafe { oakengine_clip_get_sequence(clip_a) }.is_null());

	// Media range as rationals: media_in (0,1), out = in + length (1s = 1/1).
	let (mut mn, mut md, mut xon, mut xod) = (-1i64, -1i64, -1i64, -1i64);
	assert_eq!(
		unsafe { oakengine_clip_get_media_range_rational(clip_a, &mut mn, &mut md, &mut xon, &mut xod) },
		0
	);
	assert_eq!((mn, md, xon, xod), (0, 1, 1, 1));
	assert_eq!(
		unsafe { oakengine_clip_get_media_in_rational(clip_a, &mut mn, &mut md) },
		0
	);
	assert_eq!((mn, md), (0, 1));

	// Media-in writes: live (non-undoable) then undoable.
	assert_eq!(unsafe { oakengine_clip_set_media_in(clip_a, 10, 0) }, 0);
	assert_eq!(
		unsafe { oakengine_clip_get_media_in_rational(clip_a, &mut mn, &mut md) },
		0
	);
	assert_eq!((mn, md), (1, 3)); // 10 frames at 30 fps = 1/3 s
	assert_eq!(unsafe { oakengine_clip_set_media_in(clip_a, 0, 1) }, 0);
	assert_eq!(unsafe { oakengine_clip_set_media_in_rational(clip_a, 2, 3, 0) }, 0);
	assert_eq!(
		unsafe { oakengine_clip_get_media_in_rational(clip_a, &mut mn, &mut md) },
		0
	);
	assert_eq!((mn, md), (2, 3));
	assert_eq!(unsafe { oakengine_clip_set_media_in_rational(clip_a, 0, 1, 0) }, 0);
	// Zero denominator -> E_INVALID.
	assert_eq!(unsafe { oakengine_clip_set_media_in_rational(clip_a, 0, 0, 0) }, -1);
	unsafe { assert_last_error() };

	// Enabled toggle (undoable).
	assert_eq!(unsafe { oakengine_clip_is_enabled(clip_a) }, 1);
	let mut clip_a_ptr = clip_a;
	assert_eq!(unsafe { oakengine_clip_toggle_enabled(&mut clip_a_ptr, 1) }, 1);
	assert_eq!(unsafe { oakengine_clip_is_enabled(clip_a) }, 0);
	assert_eq!(unsafe { oakengine_clip_toggle_enabled(&mut clip_a_ptr, 1) }, 1);
	assert_eq!(unsafe { oakengine_clip_is_enabled(clip_a) }, 1);

	// Second clip at [40, 70).
	unsafe { module_clip_on((*track).handle, 4, 3) };
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 2);
	let mut bin = -1i64;
	let mut bout = -1i64;
	let mut bmi = -1i64;
	let clip_b = unsafe { clip_at_ok(seq, 0, 0, 1) };
	assert_eq!(unsafe { oakengine_clip_get_range(clip_b, &mut bin, &mut bout, &mut bmi) }, 0);
	assert_eq!((bin, bout, bmi), (40, 70, 0));

	// Links: unlinked, link, unlink (one undoable command each).
	assert_eq!(unsafe { oakengine_clip_are_linked(clip_a, clip_b) }, 0);
	let mut clips2 = [clip_a, clip_b];
	assert_eq!(unsafe { oakengine_clip_set_linked(clips2.as_mut_ptr(), 2, 1) }, 0);
	assert_eq!(unsafe { oakengine_clip_are_linked(clip_a, clip_b) }, 1);
	assert_eq!(unsafe { oakengine_clip_set_linked(clips2.as_mut_ptr(), 2, 0) }, 0);
	assert_eq!(unsafe { oakengine_clip_are_linked(clip_a, clip_b) }, 0);
	// Zero count succeeds; NULL with count > 0 fails.
	assert_eq!(unsafe { oakengine_clip_set_linked(std::ptr::null_mut(), 0, 1) }, 0);
	assert_eq!(unsafe { oakengine_clip_set_linked(std::ptr::null_mut(), 1, 1) }, -1);
	unsafe { assert_last_error() };

	// ---- block traversal -------------------------------------------------------
	assert_eq!(unsafe { oakengine_track_block_count(track) }, 2);
	let blk_a = unsafe { oakengine_track_block_at(track, 0) };
	assert!(!blk_a.is_null());
	assert!(unsafe { oakengine_track_block_at(track, 5) }.is_null());
	assert_eq!(unsafe { oakengine_block_is_gap(blk_a) }, 0);
	assert!(!unsafe { oakengine_block_get_track(blk_a) }.is_null());
	let blk_b = unsafe { oakengine_block_next(blk_a) };
	assert!(!blk_b.is_null());
	assert!(unsafe { oakengine_block_prev(blk_a) }.is_null());
	assert!(unsafe { oakengine_block_next(blk_b) }.is_null());
	assert!(!unsafe { oakengine_block_prev(blk_b) }.is_null());
	let (mut bin2, mut bout2) = (-1i64, -1i64);
	assert_eq!(unsafe { oakengine_block_get_range(blk_a, &mut bin2, &mut bout2) }, 0);
	assert_eq!((bin2, bout2), (0, 30));
	assert_eq!(unsafe { oakengine_block_link_count(blk_a) }, 0);
	assert!(unsafe { oakengine_block_link_at(blk_a, 0) }.is_null());
	assert!(unsafe { oakengine_block_link_at(blk_a, -1) }.is_null());

	// Block at time / visible / nearest.
	assert!(!unsafe { oakengine_track_block_at_time(track, 5) }.is_null());
	assert!(unsafe { oakengine_track_block_at_time(track, 100) }.is_null());
	assert!(!unsafe { oakengine_track_visible_block_at_time(track, 5) }.is_null());
	assert!(!unsafe { oakengine_track_nearest_block_before(track, 35) }.is_null());
	assert!(unsafe { oakengine_track_nearest_block_before(track, 5) }.is_null());
	assert!(!unsafe { oakengine_track_nearest_block_after(track, 30) }.is_null());
	assert!(unsafe { oakengine_track_nearest_block_after(track, 70) }.is_null());
	assert!(!unsafe { oakengine_track_nearest_block_before_or_at(track, 35) }.is_null());
	assert!(!unsafe { oakengine_track_nearest_block_after_or_at(track, 40) }.is_null());

	// Block enable + resize (undoable; in-point kept).
	assert_eq!(unsafe { oakengine_block_set_enabled(blk_a, 0) }, 0);
	assert_eq!(unsafe { oakengine_block_is_enabled(blk_a) }, 0);
	assert_eq!(unsafe { oakengine_block_set_enabled(blk_a, 1) }, 0);
	assert_eq!(unsafe { oakengine_block_is_enabled(blk_a) }, 1);
	// Resize B from 30 frames to 60 (in kept: [40, 100)), then back.
	assert_eq!(unsafe { oakengine_block_set_length_and_media_out(blk_b, 60) }, 0);
	assert_eq!(unsafe { oakengine_block_get_range(blk_b, &mut bin2, &mut bout2) }, 0);
	assert_eq!((bin2, bout2), (40, 100));
	assert_eq!(unsafe { oakengine_block_set_length_and_media_out(blk_b, 30) }, 0);
	assert_eq!(unsafe { oakengine_block_get_range(blk_b, &mut bin2, &mut bout2) }, 0);
	assert_eq!((bin2, bout2), (40, 70));

	// ---- clip editing: split / trim / delete / ripple -------------------------
	// NOTE (real module bug, see the report): the module's BlockSplitCommand
	// misplaces both halves — the left half is anchored at the OLD out-point
	// (length = point - in applied with `set_length_and_media_out`) and the
	// right half starts at 0 (its in is never moved to the point). Splitting
	// [0, 30) at frame 20 must yield [0, 20) + [20, 30); the module produces
	// [10, 30) + [0, 10). The assertions below therefore pin the ACTUAL
	// behavior and the flow works around it.
	assert_eq!(unsafe { oakengine_sequence_split_clip(seq, 0, 0, 0, 20) }, 0);
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 3);
	// Split outside the clip -> E_INVALID + last error.
	assert_eq!(unsafe { oakengine_sequence_split_clip(seq, 0, 0, 0, 100) }, -1);
	unsafe { assert_last_error() };
	// Split a nonexistent clip -> E_NOT_FOUND.
	assert_eq!(unsafe { oakengine_sequence_split_clip(seq, 0, 9, 0, 10) }, -4);
	unsafe { assert_last_error() };

	// Actual geometry after the split: clip0 = [10, 30) (wrong; expected
	// [20, 30)), clip1 = [0, 10) (wrong; expected [0, 20)), B = [40, 70).
	let a2 = unsafe { clip_at_ok(seq, 0, 0, 0) };
	let a1 = unsafe { clip_at_ok(seq, 0, 0, 1) };
	let (mut s0in, mut s0out, mut s0mi) = (-1i64, -1i64, -1i64);
	assert_eq!(unsafe { oakengine_clip_get_range(a2, &mut s0in, &mut s0out, &mut s0mi) }, 0);
	assert_eq!((s0in, s0out), (10, 30)); // BUG: module split misplaced the halves
	let (mut s1in, mut s1out, mut s1mi) = (-1i64, -1i64, -1i64);
	assert_eq!(unsafe { oakengine_clip_get_range(a1, &mut s1in, &mut s1out, &mut s1mi) }, 0);
	assert_eq!((s1in, s1out), (0, 10)); // BUG: module split misplaced the halves

	// Trim A2 to [25, 35) (trim works on any clip geometry).
	assert_eq!(unsafe { oakengine_clip_trim(a2, 25, 35) }, 0);
	assert_eq!(unsafe { oakengine_clip_get_range(a2, &mut cin, &mut cout, &mut cmi) }, 0);
	assert_eq!((cin, cout), (25, 35));
	// Invalid trim -> E_INVALID.
	assert_eq!(unsafe { oakengine_clip_trim(a2, 30, 30) }, -1);
	assert_eq!(unsafe { oakengine_clip_trim(a2, -1, 10) }, -1);
	unsafe { assert_last_error() };

	// Keep a2's box for the later stub checks (the box stays valid even
	// after the clip is removed from the track — the node stays in the
	// sequence's scratch graph).
	let mut remaining = a2;

	// Batch split: REAL BUG (see the report) — the facade reports success
	// but the module's `BlockSplitPreservingLinksCommand` never runs its
	// `prepare()` (which is what builds the child `BlockSplitCommand`s), so
	// `redo()` iterates an EMPTY child list and NOTHING is split. The count
	// stays 3 and every clip keeps its range.
	let mut a2_ptr = a2;
	assert_eq!(unsafe { oakengine_sequence_split_clips(seq, &mut a2_ptr, 1, 28) }, 0);
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 3); // BUG: no-op split
	let (mut a2in, mut a2out, mut a2mi) = (-1i64, -1i64, -1i64);
	assert_eq!(unsafe { oakengine_clip_get_range(a2, &mut a2in, &mut a2out, &mut a2mi) }, 0);
	assert_eq!((a2in, a2out), (25, 35)); // BUG: unchanged, nothing was split
	// No clip spans the time -> E_NOT_FOUND.
	assert_eq!(unsafe { oakengine_sequence_split_clips(seq, &mut a2_ptr, 1, 5) }, -4);
	unsafe { assert_last_error() };
	// NULL / zero-count args -> E_INVALID.
	assert_eq!(unsafe { oakengine_sequence_split_clips(seq, std::ptr::null_mut(), 0, 15) }, -1);
	unsafe { assert_last_error() };

	// trim_clips_to: REAL BUG (see the report) — `oakengine_sequence_trim_clips_to`
	// builds its trim command with the TRACK handle where the BLOCK handle
	// belongs (`trim_cmd(track, ...)` in src/timeline.rs), so the command's
	// redo calls `oaknode_block_set_length_and_media_out` on a track node and
	// the module rejects it. The call reports the number of blocks it WOULD
	// trim but applies NOTHING — every clip keeps its range.
	assert_eq!(unsafe { oakengine_sequence_trim_clips_to(seq, 0, 30) }, 1); // would trim 1
	let (mut t1in, mut t1out, mut t1mi) = (-1i64, -1i64, -1i64);
	assert_eq!(unsafe { oakengine_clip_get_range(a1, &mut t1in, &mut t1out, &mut t1mi) }, 0);
	assert_eq!((t1in, t1out), (0, 10)); // BUG: the trim never applied
	assert_eq!(unsafe { oakengine_clip_get_range(a2, &mut t1in, &mut t1out, &mut t1mi) }, 0);
	assert_eq!((t1in, t1out), (25, 35)); // BUG: the trim never applied
	assert_eq!(unsafe { oakengine_sequence_trim_clips_to(seq, 2, 30) }, -1); // bad edge
	unsafe { assert_last_error() };

	// move_clip is a documented stub -> E_STATE (a2 still exists).
	assert_eq!(unsafe { oakengine_sequence_move_clip(seq, 0, 0, 0, 50) }, -2);
	unsafe { assert_last_error() };
	// move_clip on a nonexistent clip -> E_NOT_FOUND.
	assert_eq!(unsafe { oakengine_sequence_move_clip(seq, 0, 9, 0, 50) }, -4);
	unsafe { assert_last_error() };

	// Batch delete: remove the a1 piece leaving a gap (no ripple).
	let a1b = unsafe { clip_at_ok(seq, 0, 0, 1) };
	let mut rippled = -1;
	let mut a1b_ptr = a1b;
	assert_eq!(
		unsafe { oakengine_sequence_delete_clips(seq, &mut a1b_ptr, 1, 0, std::ptr::null(), 0, &mut rippled) },
		0
	);
	assert_eq!(rippled, 0);
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 2);
	// Batch delete with ripple=1 ripples the deleted clip's range closed.
	let b3 = unsafe { clip_at_ok(seq, 0, 0, 1) };
	let mut b3_ptr = b3;
	assert_eq!(
		unsafe { oakengine_sequence_delete_clips(seq, &mut b3_ptr, 1, 1, std::ptr::null(), 0, &mut rippled) },
		0
	);
	assert_eq!(rippled, 1);
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 1);
	// Empty batch (count 0, no ripple) is a clean no-op.
	assert_eq!(
		unsafe { oakengine_sequence_delete_clips(seq, std::ptr::null_mut(), 0, 0, std::ptr::null(), 0, &mut rippled) },
		0
	);
	assert_eq!(rippled, 0);
	// Bad ripple-range track type -> E_INVALID.
	let bad_range = [3i64, 0, 0, 10];
	assert_eq!(
		unsafe { oakengine_sequence_delete_clips(seq, &mut a1b_ptr, 0, 1, bad_range.as_ptr(), 1, &mut rippled) },
		-1
	);
	unsafe { assert_last_error() };

	// Ripple delete the addressed clip: REAL BUG (see the report) — the
	// facade reports success but the module's `TrackRippleRemoveAreaCommand`
	// no-ops (its `prepare()` needs `oaknode_track_get_nearest_block_before_or_at`,
	// which the oaknode bridge does not expose, so it finds no block and
	// removes nothing). The clip stays on the track.
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_clip(seq, 0, 0, 0) }, 0);
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 1); // BUG: no-op
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_clip(seq, 0, 9, 0) }, -4);
	unsafe { assert_last_error() };

	// add_default_transition: empty set is a no-op, non-empty is a stub.
	assert_eq!(unsafe { oakengine_sequence_add_default_transition(seq, std::ptr::null_mut(), 0) }, 0);
	assert_eq!(unsafe { oakengine_sequence_add_default_transition(seq, &mut remaining, 1) }, -2);
	unsafe { assert_last_error() };

	// Ripple delete a range: same no-op bug (same underlying command).
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_range(seq, 0, 10) }, 0);
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_range(seq, 10, 10) }, -1); // empty range
	unsafe { assert_last_error() };
	assert_eq!(unsafe { oakengine_sequence_clip_count(seq, 0, 0) }, 1); // BUG: no-op

	// ---- add_default_nodes + remove_track + delete_empty_tracks --------------
	// Runs after the clip phase so video track 0 keeps its content.
	assert_eq!(unsafe { oakengine_sequence_add_default_nodes(seq) }, 0);
	assert_eq!(unsafe { oakengine_sequence_track_count(seq, &mut v, &mut a, &mut s) }, 0);
	assert_eq!((v, a, s), (3, 2, 1));

	assert_eq!(unsafe { oakengine_sequence_remove_track(seq, 1, 0) }, 0);
	assert_eq!(unsafe { oakengine_sequence_track_count(seq, &mut v, &mut a, &mut s) }, 0);
	assert_eq!((v, a, s), (3, 1, 1));
	assert_eq!(unsafe { oakengine_sequence_remove_track(seq, 1, 5) }, -4);
	unsafe { assert_last_error() };

	// delete_empty_tracks: REAL BUG (see the report) — it reports the number
	// of empty tracks found but removes NOTHING: unlike
	// `oakengine_sequence_remove_track` it skips the live
	// `oaknode_tracklist_remove_track` compensation, and the module's
	// `TimelineRemoveTrackCommand::redo` is itself a documented no-op, so the
	// pushed commands change nothing. The counts below stay as they were.
	assert_eq!(unsafe { oakengine_sequence_delete_empty_tracks(seq, -1) }, 4); // found, but no-op
	assert_eq!(unsafe { oakengine_sequence_delete_empty_tracks(seq, 0) }, 2); // found, but no-op
	assert_eq!(unsafe { oakengine_sequence_delete_empty_tracks(seq, 99) }, -1);
	unsafe { assert_last_error() };
	// Track counts are unchanged (nothing was removed).
	assert_eq!(unsafe { oakengine_sequence_track_count(seq, &mut v, &mut a, &mut s) }, 0);
	assert_eq!((v, a, s), (3, 1, 1));

	// ---- detached clip created by the facade ---------------------------------
	// The block-family accessors take `OakEngineBlock*`; the clip box is the
	// layout-identical `OakEngineClip` wrapper, so it is cast (both are
	// `#[repr(C)]` wrappers around one CHandle).
	let detached = unsafe { oakengine_clip_create_empty(c"Detached".as_ptr()) };
	assert!(!detached.is_null());
	let dblk = detached.cast::<OakEngineBlock>();
	assert_eq!(unsafe { oakengine_clip_is_enabled(detached) }, 1);
	assert_eq!(unsafe { oakengine_block_is_enabled(dblk) }, 1);
	assert_eq!(unsafe { oakengine_block_set_enabled(dblk, 0) }, 0);
	assert_eq!(unsafe { oakengine_block_is_enabled(dblk) }, 0);
	assert_eq!(unsafe { oakengine_block_set_enabled(dblk, 1) }, 0);
	assert_eq!(unsafe { oakengine_block_is_gap(dblk) }, 0);
	assert_eq!(unsafe { oakengine_block_link_count(dblk) }, 0);
	assert!(unsafe { oakengine_block_link_at(dblk, 0) }.is_null());
	assert!(unsafe { oakengine_block_get_track(dblk) }.is_null());
	assert!(unsafe { oakengine_block_next(dblk) }.is_null());
	assert!(unsafe { oakengine_block_prev(dblk) }.is_null());
	assert!(unsafe { oakengine_clip_get_sequence(detached) }.is_null());
	assert!(unsafe { oakengine_clip_in_transition(dblk) }.is_null());
	assert!(unsafe { oakengine_clip_out_transition(dblk) }.is_null());
	assert!(unsafe { oakengine_transition_connected_in_block(dblk) }.is_null());
	assert!(unsafe { oakengine_transition_connected_out_block(dblk) }.is_null());
	assert!(unsafe { oakengine_clip_get_connected_viewer(dblk) }.is_null());
	// Detached clips are trackless: media queries work, edits need a track.
	// NOTE: the frame-timestamp media-in setter needs the track for its
	// timebase, but the rational variant applies directly (no track needed).
	assert_eq!(
		unsafe { oakengine_clip_get_media_in_rational(detached, &mut mn, &mut md) },
		0
	);
	assert_eq!((mn, md), (0, 1));
	assert_eq!(unsafe { oakengine_clip_set_media_in(detached, 5, 0) }, -2); // needs a track timebase
	assert_eq!(unsafe { oakengine_clip_set_media_in_rational(detached, 1, 1, 0) }, 0);
	assert_eq!(
		unsafe { oakengine_clip_get_media_in_rational(detached, &mut mn, &mut md) },
		0
	);
	assert_eq!((mn, md), (1, 1));
	assert_eq!(unsafe { oakengine_clip_set_media_in_rational(detached, 0, 1, 0) }, 0);
	assert_eq!(unsafe { oakengine_clip_set_media_in_rational(detached, 1, 0, 0) }, -1); // den 0
	unsafe { assert_last_error() };
	assert_eq!(unsafe { oakengine_clip_trim(detached, 0, 10) }, -2);
	assert_eq!(unsafe { oakengine_block_set_length_and_media_out(dblk, 40) }, -2);
	assert_eq!(
		unsafe { oakengine_clip_get_range(detached, &mut cin, &mut cout, &mut cmi) },
		-2
	);
	unsafe { assert_last_error() };
	// Cache no-op stubs on a live handle.
	unsafe { oakengine_clip_request_invalidate(detached, 0, 10, 1) };
	unsafe { oakengine_clip_request_invalidate_connected(detached, 0, 0, 1, 1, 1) };
	unsafe { oakengine_clip_discard_cache(detached) };
	unsafe { oakengine_clip_add_cache_passthrough(detached, remaining) };
	// NULL-label variant also creates a clip.
	let detached2 = unsafe { oakengine_clip_create_empty(std::ptr::null()) };
	assert!(!detached2.is_null());
	unsafe { free_box::<OakEngineClip>(detached2) };

	// ---- node helpers over a boxed module clip node ---------------------------
	let node_box = unsafe { box_handle::<OakEngineNode>((*remaining).handle) };
	assert_eq!(unsafe { oakengine_node_is_block(node_box) }, 1);
	assert_eq!(unsafe { oakengine_node_is_transition(node_box) }, 0);
	assert_eq!(
		unsafe { oakengine_multicam_switch_source(node_box, std::ptr::null_mut(), 0, 0, 0.0, std::ptr::null_mut()) },
		0
	);
	assert!(unsafe { oakengine_clip_find_multicam(node_box) }.is_null());
	unsafe { free_box::<OakEngineNode>(node_box) };

	// ---- standalone workarea handle family -------------------------------------
	let wa = unsafe { oakengine_workarea_create() };
	assert!(!wa.is_null());
	let (mut wn0, mut wd0, mut wn1, mut wd1, mut wen) = (-1i64, -1i64, -1i64, -1i64, -1);
	assert_eq!(
		unsafe { oakengine_workarea_get(wa, &mut wn0, &mut wd0, &mut wn1, &mut wd1, &mut wen) },
		0
	);
	assert_eq!((wn0, wd0, wn1, wd1, wen), (0, 1, 2147483647, 1, 0));
	assert_eq!(unsafe { oakengine_workarea_set_range(wa, 10, 1, 20, 1) }, 0);
	assert_eq!(unsafe { oakengine_workarea_set_enabled(wa, 1) }, 0);
	assert_eq!(
		unsafe { oakengine_workarea_get(wa, &mut wn0, &mut wd0, &mut wn1, &mut wd1, &mut wen) },
		0
	);
	assert_eq!((wn0, wd0, wn1, wd1, wen), (10, 1, 20, 1, 1));
	// Undoable variants (pushed, not added to a parent).
	assert_eq!(
		unsafe { oakengine_workarea_set_range_undoable(wa, 30, 1, 40, 1, 10, 1, 20, 1, std::ptr::null_mut()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_workarea_set_enabled_undoable(wa, 0, std::ptr::null_mut()) },
		0
	);
	assert_eq!(
		unsafe { oakengine_workarea_get(wa, &mut wn0, &mut wd0, &mut wn1, &mut wd1, &mut wen) },
		0
	);
	assert_eq!((wn0, wd0, wn1, wd1, wen), (30, 1, 40, 1, 0));
	// Reset sentinels: in = 0/1, out = RATIONAL_MAX/1.
	let (mut rn, mut rd, mut ron, mut rod) = (-1i64, -1i64, -1i64, -1i64);
	unsafe { oakengine_workarea_reset_in_out(&mut rn, &mut rd, &mut ron, &mut rod) };
	assert_eq!((rn, rd), (0, 1));
	assert_eq!((ron, rod), (2147483647, 1));
	// Free contracts: NULL, empty box, then the live handle.
	unsafe { oakengine_workarea_free(std::ptr::null_mut()) };
	let empty_wa = unsafe { empty_box!(OakEngineWorkarea) };
	unsafe { oakengine_workarea_free(empty_wa) };
	unsafe { oakengine_workarea_free(wa) };

	// ---- standalone marker-list handle family -----------------------------------
	let list_h = unsafe { oakengine::bridge::timeline::oaktimeline_marker_list_create() };
	assert!(!list_h.is_null());
	let list = unsafe { box_handle::<OakEngineMarkerList>(list_h) };
	assert_eq!(unsafe { oakengine_marker_list_count(list) }, 0);
	assert_eq!(
		unsafe { oakengine_marker_list_add(list, 1, 1, 1, 1, c"lm".as_ptr(), 5) },
		0
	);
	assert_eq!(
		unsafe { oakengine_marker_list_add(list, 5, 1, 5, 1, std::ptr::null(), 1) },
		0
	);
	assert_eq!(unsafe { oakengine_marker_list_count(list) }, 2);

	let m1 = unsafe { oakengine_marker_list_at(list, 0) };
	assert!(!m1.is_null());
	assert!(unsafe { oakengine_marker_list_at(list, 5) }.is_null());
	let m2 = unsafe { oakengine_marker_list_marker_at_time(list, 1, 1) };
	assert!(!m2.is_null());
	assert!(unsafe { oakengine_marker_list_marker_at_time(list, 9, 1) }.is_null());

	// Marker getters (rational time, buf/size name, color).
	let (mut g0, mut g1, mut g2, mut g3) = (-1i64, -1i64, -1i64, -1i64);
	assert_eq!(unsafe { oakengine_marker_get_time(m1, &mut g0, &mut g1, &mut g2, &mut g3) }, 0);
	assert_eq!((g0, g1, g2, g3), (1, 1, 1, 1));
	let mut mname2 = [0 as c_char; 64];
	assert_eq!(unsafe { oakengine_marker_get_name(m1, mname2.as_mut_ptr(), 64) }, 2);
	assert_eq!(unsafe { read_buf(&mut mname2) }, "lm");
	assert_eq!(unsafe { oakengine_marker_get_name(m1, std::ptr::null_mut(), 0) }, 2);
	assert_eq!(unsafe { oakengine_marker_get_color(m1) }, 5);
	assert_eq!(unsafe { oakengine_marker_has_sibling_at_time(m1, 1, 1) }, 0);

	// Time edits (live + command + commit).
	assert_eq!(unsafe { oakengine_marker_set_time_live(m1, 2, 1, 2, 1) }, 0);
	assert_eq!(unsafe { oakengine_marker_get_time(m1, &mut g0, &mut g1, &mut g2, &mut g3) }, 0);
	assert_eq!((g0, g1, g2, g3), (2, 1, 2, 1));
	let time_cmd = unsafe { oakengine_marker_set_time_command(m1, 3, 1) };
	assert!(!time_cmd.is_null());
	unsafe { oakengine_undo_command_free(time_cmd) };
	assert!(unsafe { oakengine_marker_set_time_command(m1, 3, 0) }.is_null()); // den 0
	assert_eq!(
		unsafe { oakengine_marker_commit_time(m1, 2, 1, 2, 1, 4, 1, 4, 1, std::ptr::null_mut()) },
		0
	);
	assert_eq!(unsafe { oakengine_marker_get_time(m1, &mut g0, &mut g1, &mut g2, &mut g3) }, 0);
	assert_eq!((g0, g1, g2, g3), (4, 1, 4, 1));

	// Re-add the marker (its data is read back through the list).
	assert_eq!(unsafe { oakengine_marker_list_add_existing(list, m1) }, 0);
	assert_eq!(unsafe { oakengine_marker_list_count(list) }, 3);

	// Batch property set (color + name, one undoable command).
	assert_eq!(
		unsafe { oakengine_marker_set_properties([m1].as_mut_ptr(), 1, 7, c"renamed".as_ptr(), 0, 0, 1, 0, 1, std::ptr::null_mut()) },
		0
	);
	assert_eq!(unsafe { oakengine_marker_get_color(m1) }, 7);
	assert_eq!(unsafe { oakengine_marker_get_name(m1, mname2.as_mut_ptr(), 64) }, 7);
	assert_eq!(unsafe { read_buf(&mut mname2) }, "renamed");
	// No-op property set (nothing to change) succeeds with zero commands.
	assert_eq!(
		unsafe { oakengine_marker_set_properties([m1].as_mut_ptr(), 1, -1, std::ptr::null(), 0, 0, 1, 0, 1, std::ptr::null_mut()) },
		0
	);
	// NULL markers / zero count -> E_INVALID.
	assert_eq!(
		unsafe { oakengine_marker_set_properties(std::ptr::null_mut(), 0, 0, std::ptr::null(), 0, 0, 1, 0, 1, std::ptr::null_mut()) },
		-1
	);

	// Remove the marker (undoable).
	assert_eq!(unsafe { oakengine_marker_remove(m1) }, 0);
	assert_eq!(unsafe { oakengine_marker_list_count(list) }, 2);
	// Detached-marker creation is a stub -> NULL.
	assert!(unsafe { oakengine_marker_create(0, 0, 1, 0, 1, std::ptr::null()) }.is_null());
	// Free contracts: NULL, then the (borrowed) marker box.
	unsafe { oakengine_marker_free(std::ptr::null_mut()) };
	let empty_marker = unsafe { empty_box!(OakEngineMarker) };
	unsafe { oakengine_marker_free(empty_marker) };
	unsafe { oakengine_marker_free(m1) };
	unsafe { free_box::<OakEngineMarker>(m2) };
	// The surviving duplicate is the copy made by add_existing BEFORE the
	// property set (m1 — the re-colored original — was index 0 and was the
	// one removed), so it carries the original color 5 / name "lm".
	let dup = unsafe { oakengine_marker_list_marker_at_time(list, 4, 1) };
	assert!(!dup.is_null());
	assert_eq!(unsafe { oakengine_marker_get_color(dup) }, 5);
	assert_eq!(unsafe { oakengine_marker_get_name(dup, mname2.as_mut_ptr(), 64) }, 2);
	assert_eq!(unsafe { read_buf(&mut mname2) }, "lm");
	unsafe { oakengine_marker_free(dup) };
	unsafe { free_box::<OakEngineMarkerList>(list) };

	// ---- add_sequence_clip / add_footage_clip (documented clean failures) ------
	// Self-nesting is rejected.
	assert!(unsafe { oakengine_sequence_add_sequence_clip(seq, seq, 0, 0, 0, 30, 0) }.is_null());
	unsafe { assert_last_error() };
	// A second sequence lives in its own scratch project -> cross-project.
	assert!(unsafe { oakengine_sequence_add_sequence_clip(seq, seq2, 0, 0, 0, 30, 0) }.is_null());
	unsafe { assert_last_error() };
	// Subtitle sequence clips are unsupported.
	assert!(unsafe { oakengine_sequence_add_sequence_clip(seq, seq2, 2, 0, 0, 30, 0) }.is_null());
	unsafe { assert_last_error() };
	// Invalid range.
	assert!(unsafe { oakengine_sequence_add_sequence_clip(seq, seq2, 0, 0, 30, 30, 0) }.is_null());
	unsafe { assert_last_error() };
	// Footage clips: the footage lives in the real project, the sequence in
	// its scratch project -> different projects -> clean NULL + error.
	let footage_node = unsafe {
		oaknode::ffi::footage::oaknode_footage_create((*project).handle, c"/no/such/media.mp4".as_ptr())
	};
	assert!(!footage_node.is_null());
	let footage_node_box = unsafe { box_handle::<OakEngineNode>(footage_node) };
	let footage = unsafe { oakengine_footage_borrow(footage_node_box) };
	assert!(!footage.is_null());
	assert!(unsafe { oakengine_sequence_add_footage_clip(seq, footage, 0, 0, 0, 30, 0) }.is_null());
	unsafe { assert_last_error() };
	// Invalid clip range.
	assert!(unsafe { oakengine_sequence_add_footage_clip(seq, footage, 0, 0, 30, 30, 0) }.is_null());
	unsafe { assert_last_error() };
	// Bad track type (subtitle clips unsupported).
	assert!(unsafe { oakengine_sequence_add_footage_clip(seq, footage, 2, 0, 0, 30, 0) }.is_null());
	unsafe { assert_last_error() };
	// Out-of-range track index.
	assert!(unsafe { oakengine_sequence_add_footage_clip(seq, footage, 0, 9, 0, 30, 0) }.is_null());
	unsafe { assert_last_error() };
	unsafe { free_box::<OakEngineFootage>(footage) };
	unsafe { free_box::<OakEngineNode>(footage_node_box) };

	// ---- input ID getters (static strings) --------------------------------------
	let ids = [
		(oakengine_clip_buffer_input_id() as *const c_char, "buffer_in"),
		(oakengine_clip_speed_input_id() as *const c_char, "speed_in"),
		(oakengine_clip_reverse_input_id() as *const c_char, "reverse_in"),
		(oakengine_clip_maintain_audio_pitch_input_id() as *const c_char, "maintain_audio_pitch_in"),
		(oakengine_clip_loop_mode_input_id() as *const c_char, "loop_in"),
		(oakengine_clip_auto_cache_input_id() as *const c_char, "autocache_in"),
	];
	for (p, expect) in ids {
		assert!(!p.is_null());
		assert_eq!(unsafe { std::ffi::CStr::from_ptr(p) }.to_str().unwrap(), expect);
	}

	// ---- track height constants -------------------------------------------------
	assert_eq!(unsafe { oakengine_track_height_default() }, 3.0);
	assert_eq!(unsafe { oakengine_track_default_height_in_pixels() }, 39); // 3.0 * 13px font
	assert_eq!(unsafe { oakengine_track_height_internal_to_pixels(3.0) }, 39);
	assert_eq!(unsafe { oakengine_track_height_pixels_to_internal(13) }, 1.0);
	assert_eq!(unsafe { oakengine_track_height_interval() }, 0.5);
	assert_eq!(unsafe { oakengine_track_height_minimum() }, 1.5);

	// ---- last_error is readable any time ----------------------------------------
	let mut err = [0 as c_char; 256];
	let elen = unsafe { oakengine_sequence_last_error(err.as_mut_ptr(), 256) };
	assert!(elen >= 0);

	// ---- cleanup: free the project shell. The alive count drops by exactly
	// the project node; sequence/track/clip nodes stay live for the process
	// (no `oakengine_sequence_free`; sequences keep their own scratch
	// projects — see the module docs).
	let alive_before_free = alive();
	unsafe { oakengine_project_free(project) };
	assert_eq!(alive(), alive_before_free - 1, "project_free releases the project shell");

	// Free the remaining borrowed clip/track boxes (they release borrowed
	// module handles; the node objects stay owned by the scratch projects).
	unsafe { free_box::<OakEngineClip>(detached) };
	unsafe { free_box::<OakEngineClip>(remaining) };
	unsafe { free_box::<OakEngineTrack>(track) };
}

// ---------------------------------------------------------------------------
// Non-mutating failure paths (NULL / empty-box / garbage arguments; no undo
// stack or per-sequence caches touched, so it runs in parallel)
// ---------------------------------------------------------------------------

/// Every timeline export rejects NULL and empty-handle arguments with a
/// clean documented value — never a crash/abort/panic.
#[test]
fn timeline_zu_failure_paths() {
	common::force_link();
	let _ = force_runtime_syms();

	let mut buf = [0 as c_char; 256];
	let mut n = 0i64;
	let mut d = 0i64;
	let mut i32v = 0;
	let mut i32w = 0;
	let mut f64v = 0.0;
	let mut u64v = 0u64;

	// ---- sequence family: NULL handles ---------------------------------------
	assert!(unsafe { oakengine_sequence_new(std::ptr::null_mut(), c"x".as_ptr()) }.is_null());
	assert_eq!(unsafe { oakengine_sequence_name(std::ptr::null(), buf.as_mut_ptr(), 64) }, -1);
	assert_eq!(unsafe { oakengine_sequence_get_length(std::ptr::null(), &mut f64v) }, -1);
	assert_eq!(
		unsafe { oakengine_sequence_get_length_rational(std::ptr::null(), &mut i32v, &mut i32w) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_get_frame_rate(std::ptr::null(), &mut i32v, &mut i32w) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_get_video_params(std::ptr::null(), &mut i32v, &mut i32w, &mut i32v, &mut i32w) },
		-1
	);
	assert_eq!(
		unsafe {
			oakengine_sequence_get_video_params_ex(
				std::ptr::null(),
				&mut i32v,
				&mut i32w,
				&mut i32v,
				&mut i32w,
				&mut i32v,
				&mut i32w,
				&mut i32v,
				&mut i32w,
				&mut i32v,
			)
		},
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_set_video_params(std::ptr::null_mut(), 1920, 1080, 30, 1, 1, 1, 0, 4, 0) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_get_audio_params(std::ptr::null(), &mut i32v, &mut u64v) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_sequence_set_audio_params(std::ptr::null_mut(), 48000, 0x3, 1) },
		-1
	);
	assert_eq!(unsafe { oakengine_sequence_get_preview_divider(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_set_preview_divider(std::ptr::null_mut(), 1, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_get_video_auto_cache(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_set_video_auto_cache(std::ptr::null_mut(), 1, 1) }, -1);
	assert_eq!(
		unsafe { oakengine_sequence_track_count(std::ptr::null(), &mut i32v, &mut i32v, &mut i32v) },
		-1
	);
	assert_eq!(unsafe { oakengine_sequence_get_playhead(std::ptr::null(), &mut n) }, -1);
	assert_eq!(unsafe { oakengine_sequence_set_playhead(std::ptr::null_mut(), 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_get_playhead_seconds(std::ptr::null(), &mut f64v) }, -1);
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_sequence_get_workarea(std::ptr::null(), &mut n, &mut d) }, -1);
	assert_eq!(unsafe { oakengine_sequence_set_workarea(std::ptr::null_mut(), 1, 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_sequence_marker_count(std::ptr::null()) }, 0);
	assert_eq!(
		unsafe { oakengine_sequence_marker_at(std::ptr::null(), 0, &mut n, buf.as_mut_ptr(), 64, &mut i32v) },
		-1
	);
	assert_eq!(unsafe { oakengine_sequence_marker_add(std::ptr::null_mut(), 0, c"x".as_ptr()) }, -1);
	assert_eq!(
		unsafe { oakengine_sequence_marker_add_ex(std::ptr::null_mut(), 0, c"x".as_ptr(), 0) },
		-1
	);
	assert_eq!(unsafe { oakengine_sequence_marker_remove(std::ptr::null_mut(), 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_marker_rename(std::ptr::null_mut(), 0, c"x".as_ptr()) }, -1);
	assert_eq!(unsafe { oakengine_sequence_marker_remove_many(std::ptr::null_mut(), &n, 1) }, -1);
	assert_eq!(unsafe { oakengine_sequence_marker_remove_many(std::ptr::null_mut(), std::ptr::null(), 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_add_track(std::ptr::null_mut(), 0) }, -1);
	assert!(unsafe { oakengine_sequence_add_track_command(std::ptr::null_mut(), 0, 0, std::ptr::null_mut()) }
		.is_null());
	assert!(unsafe { oakengine_sequence_ripple_tracks_command(std::ptr::null_mut(), 0, std::ptr::null(), 0, 0, 0, 99) }
		.is_null());
	assert!(unsafe { oakengine_sequence_add_footage_clip(std::ptr::null_mut(), std::ptr::null_mut(), 0, 0, 0, 1, 0) }
		.is_null());
	assert!(unsafe { oakengine_sequence_add_sequence_clip(std::ptr::null_mut(), std::ptr::null_mut(), 0, 0, 0, 1, 0) }
		.is_null());
	assert_eq!(unsafe { oakengine_sequence_clip_count(std::ptr::null_mut(), 0, 0) }, -1);
	assert!(unsafe { oakengine_sequence_clip_at(std::ptr::null_mut(), 0, 0, 0) }.is_null());
	assert_eq!(unsafe { oakengine_sequence_split_clip(std::ptr::null_mut(), 0, 0, 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_clip(std::ptr::null_mut(), 0, 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_move_clip(std::ptr::null_mut(), 0, 0, 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_sequence_split_clips(std::ptr::null_mut(), std::ptr::null_mut(), 0, 10) }, -1);
	let mut rippled = -1;
	assert_eq!(
		unsafe { oakengine_sequence_delete_clips(std::ptr::null_mut(), std::ptr::null_mut(), 0, 0, std::ptr::null(), 0, &mut rippled) },
		-1
	);
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_range(std::ptr::null_mut(), 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_sequence_ripple_delete_in_to_out(std::ptr::null_mut(), 0, 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_sequence_trim_clips_to(std::ptr::null_mut(), 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_sequence_delete_empty_tracks(std::ptr::null_mut(), -1) }, -1);
	assert_eq!(unsafe { oakengine_sequence_remove_track(std::ptr::null_mut(), 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_move_track(std::ptr::null_mut(), 0, 0, 1) }, -1);
	assert_eq!(unsafe { oakengine_sequence_add_default_nodes(std::ptr::null_mut()) }, -1);
	// add_default_transition IGNORES the sequence handle entirely: NULL seq
	// with an empty clip set is a clean no-op, NULL seq with clips -> E_INVALID.
	assert_eq!(unsafe { oakengine_sequence_add_default_transition(std::ptr::null_mut(), std::ptr::null_mut(), 0) }, 0);
	assert_eq!(unsafe { oakengine_sequence_add_default_transition(std::ptr::null_mut(), std::ptr::null_mut(), 1) }, -1);
	assert!(unsafe { oakengine_sequence_track_at(std::ptr::null(), 0, 0) }.is_null());
	assert!(unsafe { oakengine_sequence_track_list(std::ptr::null_mut(), 0) }.is_null());

	// ---- clip / block / track families: NULL handles --------------------------
	assert_eq!(
		unsafe { oakengine_clip_get_range(std::ptr::null(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert!(unsafe { oakengine_clip_get_sequence(std::ptr::null()) }.is_null());
	assert_eq!(
		unsafe { oakengine_clip_get_media_range_rational(std::ptr::null(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_clip_get_media_in_rational(std::ptr::null(), std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert_eq!(unsafe { oakengine_clip_set_media_in(std::ptr::null_mut(), 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_clip_set_media_in_rational(std::ptr::null_mut(), 0, 1, 0) }, -1);
	assert_eq!(unsafe { oakengine_clip_set_media_in_rational(std::ptr::null_mut(), 1, 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_clip_is_enabled(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_clip_are_linked(std::ptr::null(), std::ptr::null()) }, 0);
	// CRASH BUG (repro in the ignored `timeline_zu_crash_repros` test):
	// `oakengine_clip_toggle_enabled(NULL, 0)` reaches
	// `slice::from_raw_parts(NULL, 0)` (src/timeline.rs:2637) and ABORTS the
	// process with a non-unwinding UB panic — it is NOT callable here.
	assert_eq!(unsafe { oakengine_clip_toggle_enabled(std::ptr::null_mut(), 1) }, -1);
	assert_eq!(unsafe { oakengine_clip_set_linked(std::ptr::null_mut(), 0, 1) }, 0);
	assert_eq!(unsafe { oakengine_clip_set_linked(std::ptr::null_mut(), 1, 1) }, -1);
	// NOTE: `oakengine_clip_create_empty` is exercised in the serialized
	// lifecycle test (it creates an oaknode node, which would race the
	// alive-count assertions there if called from this parallel test).
	unsafe { oakengine_clip_request_invalidate(std::ptr::null_mut(), 0, 10, 1) };
	unsafe { oakengine_clip_request_invalidate_connected(std::ptr::null_mut(), 0, 0, 1, 1, 1) };
	unsafe { oakengine_clip_discard_cache(std::ptr::null_mut()) };
	unsafe { oakengine_clip_add_cache_passthrough(std::ptr::null_mut(), std::ptr::null_mut()) };
	assert!(unsafe { oakengine_clip_in_transition(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_clip_out_transition(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_clip_get_connected_viewer(std::ptr::null()) }.is_null());

	assert_eq!(unsafe { oakengine_block_is_enabled(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_block_set_enabled(std::ptr::null_mut(), 1) }, -1);
	assert_eq!(unsafe { oakengine_block_set_length_and_media_out(std::ptr::null_mut(), 10) }, -1);
	assert_eq!(unsafe { oakengine_block_set_length_and_media_out(std::ptr::null_mut(), 0) }, -1);
	assert_eq!(unsafe { oakengine_block_is_gap(std::ptr::null()) }, 0);
	assert!(unsafe { oakengine_block_get_track(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_block_next(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_block_prev(std::ptr::null()) }.is_null());
	assert_eq!(
		unsafe { oakengine_block_get_range(std::ptr::null(), std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert_eq!(unsafe { oakengine_block_link_count(std::ptr::null()) }, 0);
	assert!(unsafe { oakengine_block_link_at(std::ptr::null(), 0) }.is_null());

	assert_eq!(unsafe { oakengine_track_block_count(std::ptr::null()) }, -1);
	assert!(unsafe { oakengine_track_block_at(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_track_block_at_time(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_track_visible_block_at_time(std::ptr::null_mut(), 0) }.is_null());
	assert!(unsafe { oakengine_track_nearest_block_before(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_track_nearest_block_after(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_track_nearest_block_before_or_at(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_track_nearest_block_after_or_at(std::ptr::null(), 0) }.is_null());
	assert_eq!(unsafe { oakengine_track_type(std::ptr::null()) }, -1);
	assert_eq!(unsafe { oakengine_track_get_height(std::ptr::null(), 0, 0, &mut f64v) }, -1);
	assert_eq!(unsafe { oakengine_track_set_height(std::ptr::null_mut(), 0, 0, 1.0) }, -1);
	assert_eq!(unsafe { oakengine_track_is_muted(std::ptr::null(), 0, 0) }, 0);
	assert_eq!(unsafe { oakengine_track_set_muted(std::ptr::null_mut(), 0, 0, 1) }, -1);
	assert_eq!(unsafe { oakengine_track_is_locked(std::ptr::null(), 0, 0) }, 0);
	assert_eq!(unsafe { oakengine_track_set_locked(std::ptr::null_mut(), 0, 0, 1) }, -1);
	assert_eq!(unsafe { oakengine_track_get_length(std::ptr::null(), 0, 0, &mut n) }, -1);
	assert_eq!(unsafe { oakengine_track_is_range_free(std::ptr::null(), 0, 0, 0, 10) }, -1);
	assert_eq!(unsafe { oakengine_track_is_range_free(std::ptr::null(), 0, 0, -1, 10) }, -1);

	// ---- marker handle family: NULL / empty boxes ------------------------------
	assert_eq!(unsafe { oakengine_marker_list_count(std::ptr::null()) }, 0);
	assert_eq!(
		unsafe { oakengine_marker_list_add(std::ptr::null_mut(), 0, 1, 0, 1, c"x".as_ptr(), 0) },
		-1
	);
	assert_eq!(
		unsafe { oakengine_marker_list_add_existing(std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert!(unsafe { oakengine_marker_list_at(std::ptr::null(), 0) }.is_null());
	assert!(unsafe { oakengine_marker_list_at(std::ptr::null(), -1) }.is_null());
	assert!(unsafe { oakengine_marker_list_marker_at_time(std::ptr::null(), 0, 1) }.is_null());
	assert!(unsafe { oakengine_marker_create(0, 0, 1, 0, 1, std::ptr::null()) }.is_null());
	unsafe { oakengine_marker_free(std::ptr::null_mut()) };
	assert_eq!(
		unsafe { oakengine_marker_get_time(std::ptr::null(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) },
		-1
	);
	assert_eq!(unsafe { oakengine_marker_get_name(std::ptr::null(), std::ptr::null_mut(), 0) }, -1);
	assert_eq!(unsafe { oakengine_marker_get_color(std::ptr::null()) }, -1);
	assert_eq!(unsafe { oakengine_marker_has_sibling_at_time(std::ptr::null(), 0, 1) }, 0);
	assert_eq!(unsafe { oakengine_marker_set_time_live(std::ptr::null_mut(), 0, 1, 0, 1) }, -1);
	assert_eq!(
		unsafe { oakengine_marker_commit_time(std::ptr::null_mut(), 0, 1, 0, 1, 0, 1, 0, 1, std::ptr::null_mut()) },
		-1
	);
	assert!(unsafe { oakengine_marker_set_time_command(std::ptr::null_mut(), 1, 0) }.is_null());
	assert!(unsafe { oakengine_marker_set_time_command(std::ptr::null_mut(), 1, 1) }.is_null());
	assert_eq!(unsafe { oakengine_marker_remove(std::ptr::null_mut()) }, -1);
	assert_eq!(
		unsafe { oakengine_marker_set_properties(std::ptr::null_mut(), 0, 0, std::ptr::null(), 0, 0, 1, 0, 1, std::ptr::null_mut()) },
		-1
	);

	// ---- workarea family: NULL / empty boxes ------------------------------------
	let wa = unsafe { oakengine_workarea_create() };
	assert!(!wa.is_null());
	unsafe { oakengine_workarea_free(wa) };
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
	// reset_in_out is a pure out-param filler; NULL pointers are safe no-ops.
	unsafe { oakengine_workarea_reset_in_out(std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) };

	// ---- node helpers -------------------------------------------------------------
	assert_eq!(unsafe { oakengine_node_is_block(std::ptr::null()) }, 0);
	assert_eq!(unsafe { oakengine_node_is_transition(std::ptr::null()) }, 0);
	assert!(unsafe { oakengine_clip_find_multicam(std::ptr::null_mut()) }.is_null());
	assert_eq!(
		unsafe { oakengine_multicam_switch_source(std::ptr::null_mut(), std::ptr::null_mut(), 0, 0, 0.0, std::ptr::null_mut()) },
		-1
	);
	assert!(unsafe { oakengine_transition_connected_in_block(std::ptr::null()) }.is_null());
	assert!(unsafe { oakengine_transition_connected_out_block(std::ptr::null()) }.is_null());

	// ---- empty boxes: non-NULL pointers wrapping a NULL CHandle ------------------
	// `unbox` rejects them with E_INVALID even where a NULL pointer is a
	// documented 0-return (the box is non-NULL).
	let eseq = unsafe { empty_box!(OakEngineSequence) };
	let etrack = unsafe { empty_box!(OakEngineTrack) };
	let eclip = unsafe { empty_box!(OakEngineClip) };
	let eblk = unsafe { empty_box!(OakEngineBlock) };
	let emarker = unsafe { empty_box!(OakEngineMarker) };
	let elist = unsafe { empty_box!(OakEngineMarkerList) };
	let ewa = unsafe { empty_box!(OakEngineWorkarea) };
	let enode = unsafe { empty_box!(OakEngineNode) };

	assert_eq!(unsafe { oakengine_sequence_name(eseq, buf.as_mut_ptr(), 64) }, -1);
	assert_eq!(unsafe { oakengine_sequence_marker_count(eseq) }, -1);
	assert_eq!(unsafe { oakengine_sequence_get_preview_divider(eseq) }, -1);
	assert_eq!(unsafe { oakengine_sequence_get_video_auto_cache(eseq) }, -1);
	assert_eq!(unsafe { oakengine_sequence_workarea_is_enabled(eseq) }, -1);
	assert_eq!(unsafe { oakengine_sequence_add_track(eseq, 0) }, -1);
	assert_eq!(unsafe { oakengine_sequence_add_track(eseq, 3) }, -1); // type check first
	assert!(unsafe { oakengine_sequence_track_at(eseq, 0, 0) }.is_null());
	assert!(unsafe { oakengine_sequence_track_at(eseq, 3, 0) }.is_null());
	assert!(unsafe { oakengine_sequence_track_list(eseq, 99) }.is_null());
	assert_eq!(unsafe { oakengine_sequence_clip_count(eseq, 0, 0) }, -1);
	assert!(unsafe { oakengine_sequence_clip_at(eseq, 0, 0, 0) }.is_null());
	assert_eq!(unsafe { oakengine_sequence_marker_at(eseq, -1, &mut n, buf.as_mut_ptr(), 64, &mut i32v) }, -1);
	assert_eq!(unsafe { oakengine_track_type(etrack) }, -1);
	assert_eq!(unsafe { oakengine_track_block_count(etrack) }, -1);
	assert!(unsafe { oakengine_track_block_at(etrack, 0) }.is_null());
	assert_eq!(unsafe { oakengine_track_is_muted(eseq, 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_track_is_locked(eseq, 0, 0) }, -1);
	assert_eq!(unsafe { oakengine_clip_get_range(eclip, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) }, -1);
	assert_eq!(unsafe { oakengine_clip_is_enabled(eclip) }, -1);
	assert_eq!(unsafe { oakengine_clip_are_linked(eclip, eclip) }, -1);
	assert!(unsafe { oakengine_clip_get_sequence(eclip) }.is_null());
	let eblk2 = eclip.cast::<OakEngineBlock>();
	assert!(unsafe { oakengine_clip_in_transition(eblk2) }.is_null());
	assert!(unsafe { oakengine_clip_out_transition(eblk2) }.is_null());
	assert!(unsafe { oakengine_clip_get_connected_viewer(eblk2) }.is_null());
	assert_eq!(unsafe { oakengine_block_is_enabled(eblk) }, -1);
	assert_eq!(unsafe { oakengine_block_set_enabled(eblk, 1) }, -1);
	assert_eq!(unsafe { oakengine_block_is_gap(eblk) }, -1);
	assert_eq!(unsafe { oakengine_block_link_count(eblk) }, -1);
	assert!(unsafe { oakengine_block_link_at(eblk, 0) }.is_null());
	assert!(unsafe { oakengine_block_get_track(eblk) }.is_null());
	assert!(unsafe { oakengine_block_next(eblk) }.is_null());
	assert!(unsafe { oakengine_block_prev(eblk) }.is_null());
	assert_eq!(unsafe { oakengine_marker_get_color(emarker) }, -1);
	assert_eq!(unsafe { oakengine_marker_has_sibling_at_time(emarker, 0, 1) }, -1);
	assert_eq!(unsafe { oakengine_marker_get_time(emarker, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) }, -1);
	assert_eq!(unsafe { oakengine_marker_get_name(emarker, std::ptr::null_mut(), 0) }, -1);
	assert_eq!(unsafe { oakengine_marker_list_count(elist) }, -1);
	assert!(unsafe { oakengine_marker_list_at(elist, 0) }.is_null());
	assert!(unsafe { oakengine_marker_list_marker_at_time(elist, 0, 1) }.is_null());
	assert_eq!(unsafe { oakengine_workarea_get(ewa, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) }, -1);
	assert_eq!(unsafe { oakengine_node_is_block(enode) }, -1);
	assert_eq!(unsafe { oakengine_node_is_transition(enode) }, -1);
	assert!(unsafe { oakengine_clip_find_multicam(enode) }.is_null());

	// Free the empty boxes (all are NULL-CHandle wrappers; release is a no-op).
	unsafe { free_box::<OakEngineSequence>(eseq) };
	unsafe { free_box::<OakEngineTrack>(etrack) };
	unsafe { free_box::<OakEngineClip>(eclip) };
	unsafe { free_box::<OakEngineBlock>(eblk) };
	unsafe { free_box::<OakEngineMarker>(emarker) };
	unsafe { free_box::<OakEngineMarkerList>(elist) };
	unsafe { free_box::<OakEngineWorkarea>(ewa) };
	unsafe { free_box::<OakEngineNode>(enode) };

	// ---- last_error is always readable -------------------------------------------
	let elen = unsafe { oakengine_sequence_last_error(buf.as_mut_ptr(), 256) };
	assert!(elen >= 0);
}

// ---------------------------------------------------------------------------
// Crash-bug reproductions (ignored: running them aborts the process by
// design, which is the point — see the report)
// ---------------------------------------------------------------------------

/// `oakengine_clip_toggle_enabled(NULL, 0)` crashes the process with a
/// non-unwinding UB panic inside `slice::from_raw_parts(NULL, 0)`
/// (src/timeline.rs:2637). Run with `--ignored` to reproduce the abort.
///
/// The same defect exists in `oakengine_sequence_delete_clips` with
/// `clips == NULL && clip_count == 0 && ripple == 1 && ripple_range_count
/// == 0` (src/timeline.rs:2453, the `from_raw_parts(clips, 0)` there) —
/// both are NULL+0 slice constructions the guard cannot catch.
#[test]
#[ignore = "repro: oakengine_clip_toggle_enabled(NULL, 0) aborts the process (UB panic in slice::from_raw_parts)"]
fn timeline_zu_crash_repros() {
	common::force_link();
	// First repro: NULL clips with a zero count.
	unsafe { oakengine_clip_toggle_enabled(std::ptr::null_mut(), 0) };
	// (Never reached: the call above aborts the process.)
}
