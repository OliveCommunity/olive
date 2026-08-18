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

//! Integration tests for the **timeline editing family** (M12 P4): the
//! cross-track move export `oakengine_sequence_move_clip_to_track`, the
//! sequence marker surface (add / remove / list, all undoable), and the
//! sequence work-area surface (set / get / enable / clear, live and
//! undoable). Coverage rules (see the family test charter):
//!
//! 1. no mocks — every call goes through the real facade into the real
//!    module crates (the `oakcore_audioparams_*` accessors the facade
//!    reads through are its own in-dylib implementations, re-exported by
//!    `tests/common`; no media is decoded, so no FFmpeg);
//! 2. every export under test is exercised on a legal path with the
//!    result asserted;
//! 3. illegal inputs (NULL seq, bad track types, out-of-range indices,
//!    negative times) always yield a negative error code — never a crash;
//! 4. the undoable exports round-trip through `oakengine_project_undo` /
//!    `oakengine_project_redo`.
//!
//! ## Serialization
//!
//! The tests assemble projects and push undo commands on the facade's
//! process-wide undo stack, so every test takes the shared stack lock (the
//! same pattern as `it_export`/`it_undo`).

use super::common;

use std::ffi::{c_char, c_int};
use std::sync::Mutex;

use crate::handle::{OakEngineFootage, OakEngineProject, OakEngineSequence, free_box};
use crate::node::{
	oakengine_footage_free, oakengine_project_create, oakengine_project_free,
	oakengine_project_import_footage, oakengine_project_new, oakengine_project_redo,
	oakengine_project_undo,
};
use crate::timeline::{
	oakengine_clip_get_range, oakengine_sequence_add_footage_clip_ex,
	oakengine_sequence_add_track, oakengine_sequence_clip_at, oakengine_sequence_clip_count,
	oakengine_sequence_get_workarea, oakengine_sequence_marker_add, oakengine_sequence_marker_at,
	oakengine_sequence_marker_count, oakengine_sequence_marker_remove,
	oakengine_sequence_move_clip_to_track, oakengine_sequence_new, oakengine_sequence_name,
	oakengine_sequence_set_video_params, oakengine_sequence_set_workarea,
	oakengine_sequence_set_workarea_undoable, oakengine_sequence_workarea_is_enabled,
};
use crate::undo::oakengine_undo_clear;

/// `OAKENGINE_TRACK_TYPE_*` (timeline.h).
const TRACK_VIDEO: c_int = 0;
const TRACK_AUDIO: c_int = 1;

/// Serializes every test here: the facade's global undo stack is shared
/// with the it_undo / it_export / it_storage tests.
static SERIAL: Mutex<()> = Mutex::new(());

/// Both lock guards held by [`serial`].
struct SerialGuard {
	/// The [`SERIAL`] lock.
	_task: std::sync::MutexGuard<'static, ()>,
	/// The facade-wide undo-stack lock.
	_stack: parking_lot::ReentrantMutexGuard<'static, ()>,
}

/// Take the [`SERIAL`] lock AND the global undo-stack lock, recovering
/// from any poisoning.
fn serial() -> SerialGuard {
	let _task = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
	let _stack = super::it_undo::GLOBAL_STACK_LOCK
		.lock();
	SerialGuard { _task, _stack }
}

/// Reads a NUL-terminated string from a facade two-stage buffer.
unsafe fn read_str(buf: *const c_char) -> String {
	if buf.is_null() {
		return String::new();
	}
	let len = (0..4096).find(|&i| unsafe { *buf.add(i) } == 0).unwrap_or(0);
	String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(buf as *const u8, len) })
		.into_owned()
}

/// A temp file path (per-process so parallel test binaries never collide).
fn temp_path(kind: &str) -> std::path::PathBuf {
	std::env::temp_dir().join(format!("oakengine-it-timeline-{kind}-{}.bin", std::process::id()))
}

/// Build a project + sequence with `video_tracks` video tracks, each
/// carrying one clip spanning `0..100` frames (media-in 0), at 25 fps.
/// The source file is a plain byte blob — the module does not probe media.
///
/// Returns `(project, sequence, footage)` — the caller releases the
/// footage with `oakengine_footage_free`, the sequence box with `free_box`,
/// and the project with `oakengine_project_free`.
///
/// # Safety
/// The returned handles must be released by the caller exactly once.
unsafe fn assemble_timeline_sequence(
	video_tracks: c_int,
) -> (*mut OakEngineProject, *mut OakEngineSequence, *mut OakEngineFootage) {
	unsafe {
		// The caller holds the storage-config lock + storage_off_guard for
		// the whole body (assembly AND the subsequent command pushes), so no
		// lock is taken here (re-locking the same std mutex would deadlock).
		let media = temp_path("media");
		std::fs::write(&media, b"oak timeline test footage").expect("write the source blob");
		let media_c = std::ffi::CString::new(media.to_string_lossy().into_owned()).unwrap();

		let project = oakengine_project_create();
		assert!(!project.is_null());
		assert_eq!(oakengine_project_new(project), 0);

		let footage = oakengine_project_import_footage(project, media_c.as_ptr());
		assert!(!footage.is_null(), "import the source blob");

		let seq = oakengine_sequence_new(project, c"Timeline Test".as_ptr());
		assert!(!seq.is_null());
		assert_eq!(
			oakengine_sequence_set_video_params(seq, 640, 480, 25, 1, 1, 1, 0, 4, 0),
			0,
			"set the sequence frame rate"
		);

		for _ in 0..video_tracks {
			let track = oakengine_sequence_add_track(seq, TRACK_VIDEO);
			assert!(track >= 0, "add the video track");
			// Only the first track carries a clip: the move tests need an
			// empty destination track (and `oakengine_sequence_add_track`
			// returns its index, which is 0 for the first call).
			if track == 0 {
				let clip = oakengine_sequence_add_footage_clip_ex(seq, footage, TRACK_VIDEO, track, 0, 100, 0);
				assert!(!clip.is_null(), "place the clip");
				free_box(clip);
			}
		}

		(project, seq, footage)
	}
}

/// Release the assembly returned by [`assemble_timeline_sequence`].
///
/// # Safety
/// The handles must be the ones returned by [`assemble_timeline_sequence`].
unsafe fn drop_timeline_sequence(
	project: *mut OakEngineProject,
	seq: *mut OakEngineSequence,
	footage: *mut OakEngineFootage,
) {
	unsafe {
		if !footage.is_null() {
			oakengine_footage_free(footage);
		}
		free_box::<OakEngineSequence>(seq);
		oakengine_project_free(project);
	}
}

/// The (in, out) frame range of the clip at `(track, index)`, read back
/// through the facade. `None` when the track has no such clip.
unsafe fn clip_range_of(seq: *mut OakEngineSequence, track: c_int, index: c_int) -> Option<(i64, i64)> {
	unsafe {
		let clip = oakengine_sequence_clip_at(seq, TRACK_VIDEO, track, index);
		if clip.is_null() {
			return None;
		}
		let mut in_ts: i64 = 0;
		let mut out_ts: i64 = 0;
		let mut media_in: i64 = 0;
		assert_eq!(oakengine_clip_get_range(clip, &mut in_ts, &mut out_ts, &mut media_in), 0);
		free_box(clip);
		Some((in_ts, out_ts))
	}
}

// ---------------------------------------------------------------------------
// Cross-track move (oakengine_sequence_move_clip_to_track)
// ---------------------------------------------------------------------------

/// A cross-track move lands the clip on the destination track at the new
/// in point, and the source spot becomes a gap (the source track's clip
/// count drops to zero). One undoable entry: undo restores the clip to its
/// original track/position, redo re-applies the move.
#[test]
fn move_clip_to_track_cross_track_roundtrip() {
	let _g = serial();
	common::force_link();
	unsafe {
		// Storage off for the whole body: the assembly and the move both
		// push commands (the assembly helper's guard is dropped on return).
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(2);
		assert_eq!(oakengine_undo_clear(), 0);

		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 0), 1);
		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 1), 0);
		assert_eq!(clip_range_of(seq, 0, 0), Some((0, 100)));

		// Track 0 clip 0 → track 1 at frame 30.
		let rc = oakengine_sequence_move_clip_to_track(seq, TRACK_VIDEO, 0, 0, 1, 30);
		assert_eq!(rc, 0, "cross-track move succeeds");

		assert_eq!(
			oakengine_sequence_clip_count(seq, TRACK_VIDEO, 0),
			0,
			"source spot becomes a gap"
		);
		assert_eq!(
			oakengine_sequence_clip_count(seq, TRACK_VIDEO, 1),
			1,
			"the clip lands on the destination track"
		);
		assert_eq!(clip_range_of(seq, 1, 0), Some((30, 130)));

		// One undo restores the original layout.
		assert_eq!(oakengine_project_undo(project), 0);
		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 0), 1);
		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 1), 0);
		assert_eq!(clip_range_of(seq, 0, 0), Some((0, 100)));

		// Redo re-applies the move.
		assert_eq!(oakengine_project_redo(project), 0);
		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 0), 0);
		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 1), 1);
		assert_eq!(clip_range_of(seq, 1, 0), Some((30, 130)));

		drop_timeline_sequence(project, seq, footage);
	}
}

/// Moving to the same track (destination index == source index) is a
/// time-only move, equivalent to `oakengine_sequence_move_clip`.
#[test]
fn move_clip_to_track_same_track_is_time_only() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(2);
		assert_eq!(oakengine_undo_clear(), 0);

		let rc = oakengine_sequence_move_clip_to_track(seq, TRACK_VIDEO, 0, 0, 0, 40);
		assert_eq!(rc, 0, "same-track move succeeds");
		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 0), 1);
		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 1), 0);
		assert_eq!(clip_range_of(seq, 0, 0), Some((40, 140)));

		assert_eq!(oakengine_project_undo(project), 0);
		assert_eq!(clip_range_of(seq, 0, 0), Some((0, 100)));

		drop_timeline_sequence(project, seq, footage);
	}
}

/// Illegal inputs never crash: NULL sequence, unknown track types,
/// negative in points, missing clips and out-of-range destination tracks
/// all yield a negative error code.
#[test]
fn move_clip_to_track_rejects_illegal_inputs() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(1);
		assert_eq!(oakengine_undo_clear(), 0);

		// NULL sequence.
		assert!(oakengine_sequence_move_clip_to_track(
			std::ptr::null_mut(), TRACK_VIDEO, 0, 0, 1, 10
		) < 0);
		// Unknown track types.
		assert!(oakengine_sequence_move_clip_to_track(seq, 3, 0, 0, 1, 10) < 0);
		assert!(oakengine_sequence_move_clip_to_track(seq, -1, 0, 0, 1, 10) < 0);
		// Negative destination in point.
		assert!(oakengine_sequence_move_clip_to_track(seq, TRACK_VIDEO, 0, 0, 1, -5) < 0);
		// Missing clip (index 5 on a one-clip track).
		assert!(oakengine_sequence_move_clip_to_track(seq, TRACK_VIDEO, 0, 5, 1, 10) < 0);
		assert!(oakengine_sequence_move_clip_to_track(seq, TRACK_VIDEO, 2, 0, 1, 10) < 0);
		// Destination track out of range.
		assert!(oakengine_sequence_move_clip_to_track(seq, TRACK_VIDEO, 0, 0, 9, 10) < 0);
		// The rejections left the sequence untouched.
		assert_eq!(oakengine_sequence_clip_count(seq, TRACK_VIDEO, 0), 1);
		assert_eq!(clip_range_of(seq, 0, 0), Some((0, 100)));

		drop_timeline_sequence(project, seq, footage);
	}
}

// ---------------------------------------------------------------------------
// Sequence markers (oakengine_sequence_marker_*)
// ---------------------------------------------------------------------------

/// `oakengine_sequence_marker_add` inserts an undoable marker; the list
/// exposes it through count/at (time in the sequence's frame timebase,
/// name and color round-trip). Undo removes it, redo re-adds it.
#[test]
fn markers_add_list_and_undo_roundtrip() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(0);
		assert_eq!(oakengine_undo_clear(), 0);

		assert_eq!(oakengine_sequence_marker_count(seq), 0);

		let rc = oakengine_sequence_marker_add(seq, 60, c"scene 1".as_ptr());
		assert_eq!(rc, 0, "add a marker at frame 60");
		assert_eq!(oakengine_sequence_marker_count(seq), 1);

		let mut time: i64 = -1;
		let mut color: c_int = -1;
		let mut name_buf = [0 as c_char; 64];
		assert_eq!(
			oakengine_sequence_marker_at(
				seq, 0, &mut time, name_buf.as_mut_ptr(), 64, &mut color
			),
			0
		);
		assert_eq!(time, 60);
		assert_eq!(color, 0);
		assert_eq!(read_str(name_buf.as_ptr()), "scene 1");

		// Undo removes the marker, redo re-adds it.
		assert_eq!(oakengine_project_undo(project), 0);
		assert_eq!(oakengine_sequence_marker_count(seq), 0);
		assert_eq!(oakengine_project_redo(project), 0);
		assert_eq!(oakengine_sequence_marker_count(seq), 1);
		time = -1;
		assert_eq!(oakengine_sequence_marker_at(seq, 0, &mut time, std::ptr::null_mut(), 0, std::ptr::null_mut()), 0);
		assert_eq!(time, 60);

		drop_timeline_sequence(project, seq, footage);
	}
}

/// `oakengine_sequence_marker_remove` removes the marker at a time
/// (undoable); removing from an empty spot is an error that leaves the list
/// intact.
#[test]
fn markers_remove_and_reject_duplicates() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(0);
		assert_eq!(oakengine_undo_clear(), 0);

		assert_eq!(oakengine_sequence_marker_add(seq, 30, std::ptr::null()), 0);
		assert_eq!(oakengine_sequence_marker_add(seq, 90, c"end".as_ptr()), 0);
		assert_eq!(oakengine_sequence_marker_count(seq), 2);

		// A second marker at the same time is rejected (module asserts on
		// duplicate in points).
		assert!(oakengine_sequence_marker_add(seq, 30, c"dup".as_ptr()) < 0);
		assert_eq!(oakengine_sequence_marker_count(seq), 2);

		// Removing the marker at frame 30 (undoable).
		assert_eq!(oakengine_sequence_marker_remove(seq, 30), 0);
		assert_eq!(oakengine_sequence_marker_count(seq), 1);
		let mut time: i64 = -1;
		assert_eq!(oakengine_sequence_marker_at(seq, 0, &mut time, std::ptr::null_mut(), 0, std::ptr::null_mut()), 0);
		assert_eq!(time, 90, "the surviving marker is the one at 90");

		// Removing from an empty time is an error.
		assert!(oakengine_sequence_marker_remove(seq, 30) < 0);
		assert_eq!(oakengine_sequence_marker_count(seq), 1);

		// Undo restores the removed marker.
		assert_eq!(oakengine_project_undo(project), 0);
		assert_eq!(oakengine_sequence_marker_count(seq), 2);

		drop_timeline_sequence(project, seq, footage);
	}
}

/// Marker inputs: NULL sequence and out-of-range indices are errors, never
/// crashes.
#[test]
fn markers_reject_illegal_inputs() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(0);
		assert_eq!(oakengine_undo_clear(), 0);

		assert!(oakengine_sequence_marker_add(std::ptr::null_mut(), 10, std::ptr::null()) < 0);
		assert!(oakengine_sequence_marker_remove(std::ptr::null_mut(), 10) < 0);
		assert_eq!(oakengine_sequence_marker_count(std::ptr::null_mut()), 0);
		assert!(oakengine_sequence_marker_remove(seq, 10) < 0, "no marker at frame 10");

		let mut time: i64 = 0;
		assert!(
			oakengine_sequence_marker_at(seq, 3, &mut time, std::ptr::null_mut(), 0, std::ptr::null_mut()) < 0,
			"index out of range"
		);

		drop_timeline_sequence(project, seq, footage);
	}
}

// ---------------------------------------------------------------------------
// Sequence work area (oakengine_sequence_workarea_*)
// ---------------------------------------------------------------------------

/// `oakengine_sequence_set_workarea` (live) round-trips through
/// `oakengine_sequence_get_workarea` / `oakengine_sequence_workarea_is_enabled`;
/// disabling clears the enabled flag.
#[test]
fn workarea_set_get_clear_roundtrip() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(0);

		assert_eq!(oakengine_sequence_workarea_is_enabled(seq), 0);
		let mut in_ts: i64 = -1;
		let mut out_ts: i64 = -1;
		assert_eq!(oakengine_sequence_get_workarea(seq, &mut in_ts, &mut out_ts), 0);
		// Default range: the reset sentinel, 0..RATIONAL_MAX in the frame
		// timebase (a huge positive out point).
		assert_eq!(in_ts, 0);
		assert!(out_ts > 0, "default out is the reset sentinel, got {out_ts}");

		// Set an enabled range and read it back.
		assert_eq!(oakengine_sequence_set_workarea(seq, 1, 100, 200), 0);
		assert_eq!(oakengine_sequence_workarea_is_enabled(seq), 1);
		assert_eq!(oakengine_sequence_get_workarea(seq, &mut in_ts, &mut out_ts), 0);
		assert_eq!((in_ts, out_ts), (100, 200));

		// Disable ("clear") keeps the range but flips the flag.
		assert_eq!(oakengine_sequence_set_workarea(seq, 0, 100, 200), 0);
		assert_eq!(oakengine_sequence_workarea_is_enabled(seq), 0);
		assert_eq!(oakengine_sequence_get_workarea(seq, &mut in_ts, &mut out_ts), 0);
		assert_eq!((in_ts, out_ts), (100, 200));

		drop_timeline_sequence(project, seq, footage);
	}
}

/// `oakengine_sequence_set_workarea_undoable` changes enabled + range as
/// ONE undoable entry: undo restores the pre-change range (the caller
/// supplied old in/out), redo re-applies the new one.
#[test]
fn workarea_undoable_set_roundtrips() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(0);
		assert_eq!(oakengine_undo_clear(), 0);

		// Start from a live-set range so the undo has something to restore.
		assert_eq!(oakengine_sequence_set_workarea(seq, 1, 100, 200), 0);

		let rc = oakengine_sequence_set_workarea_undoable(seq, 1, 300, 400, 100, 200);
		assert_eq!(rc, 0, "undoable workarea set succeeds");
		let mut in_ts: i64 = 0;
		let mut out_ts: i64 = 0;
		assert_eq!(oakengine_sequence_get_workarea(seq, &mut in_ts, &mut out_ts), 0);
		assert_eq!((in_ts, out_ts), (300, 400));
		assert_eq!(oakengine_sequence_workarea_is_enabled(seq), 1);

		// One undo entry: enabled + range restored together.
		assert_eq!(oakengine_project_undo(project), 0);
		assert_eq!(oakengine_sequence_workarea_is_enabled(seq), 1);
		assert_eq!(oakengine_sequence_get_workarea(seq, &mut in_ts, &mut out_ts), 0);
		assert_eq!((in_ts, out_ts), (100, 200));

		// Redo re-applies.
		assert_eq!(oakengine_project_redo(project), 0);
		assert_eq!(oakengine_sequence_get_workarea(seq, &mut in_ts, &mut out_ts), 0);
		assert_eq!((in_ts, out_ts), (300, 400));

		drop_timeline_sequence(project, seq, footage);
	}
}

/// Work-area inputs: NULL sequence and negative ranges are errors.
#[test]
fn workarea_rejects_illegal_inputs() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(0);

		assert_eq!(oakengine_sequence_workarea_is_enabled(std::ptr::null_mut()), 0);
		assert!(oakengine_sequence_get_workarea(std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) < 0);
		assert!(oakengine_sequence_set_workarea(std::ptr::null_mut(), 1, 0, 10) < 0);
		assert!(oakengine_sequence_set_workarea_undoable(std::ptr::null_mut(), 1, 0, 10, 0, 0) < 0);
		// Negative range values are rejected by the undoable export (never a
		// crash). The live setter is a plain setter (C++ parity — it stores
		// what it is given), so only the command path validates.
		assert!(oakengine_sequence_set_workarea_undoable(seq, 1, 0, 10, -1, 0) < 0);
		assert!(oakengine_sequence_set_workarea_undoable(seq, 1, -1, 10, 0, 0) < 0);

		drop_timeline_sequence(project, seq, footage);
	}
}

/// The sequence name getter used by the app's `refresh_sequence_info`
/// (two-stage buf/size) is exercised here as the assembly smoke check.
#[test]
fn assembled_sequence_has_expected_name() {
	let _g = serial();
	common::force_link();
	unsafe {
		let _storage = common::storage_off_guard();
		let (project, seq, footage) = assemble_timeline_sequence(1);
		let mut buf = [0 as c_char; 64];
		assert!(oakengine_sequence_name(seq, buf.as_mut_ptr(), 64) > 0);
		assert_eq!(read_str(buf.as_ptr()), "Timeline Test");
		drop_timeline_sequence(project, seq, footage);
	}
}
