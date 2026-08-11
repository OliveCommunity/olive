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

//! OTIO load/save task tests: synthetic documents are built in-test with the
//! `oakotio` model, loaded through `oaktask_create_project_load_otio` /
//! `oaktask_create_project_save_otio` (driving the oaknode C ABI stubs), and
//! exports are re-parsed with `oakotio` for a round-trip check.
//!
//! The tasks dispatch on the filename extension (see
//! `oaktask::project::format`): `.otio` parses/serializes OpenTimelineIO
//! JSON, `.fcpxml` the FCPXML interchange. The FCPXML tests build documents
//! with `oakotio`'s fcpxml writer and re-parse exports with its reader.

mod common;

use std::ffi::{c_char, c_int, c_void};
use std::sync::atomic::Ordering;

use oakotio::{
	Clip, Composable, ExternalReference, Gap, MediaReference, RationalTime, Serializable,
	SerializableCollection, TimeRange, Timeline, Track, Transition,
};

use common::*;
use oaktask::ffi::project::{
	oaktask_create_project_load_otio, oaktask_create_project_save_otio,
	oaktask_load_otio_set_confirm_cb, oaktask_load_otio_take_project,
};
use oaktask::ffi::task::{
	oaktask_debug_alive_count, oaktask_task_error, oaktask_task_free, oaktask_task_start_sync,
};

unsafe extern "C" fn reject_all(
	_seq: *const *const c_char,
	_count: c_int,
	_ud: *mut c_void,
) -> c_int {
	0
}

fn free(t: &mut oaktask::handle::CHandle) {
	unsafe {
		oaktask_task_free(t);
	}
}

fn cstr_read(buf: &[i8]) -> String {
	let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
	let bytes = unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, len) };
	String::from_utf8_lossy(bytes).into_owned()
}

/// Build a single-video-track timeline: one clip (ExternalReference) and one
/// gap, mirroring the golden `golden_timeline.json` block layout.
fn synthetic_timeline(name: &str, url: &str) -> Timeline {
	let mut timeline = Timeline::new(name);

	let mut video = Track::new("Video");
	let mut clip = Clip::new("My Sequence Clip");
	clip.set_source_range(TimeRange::new(
		RationalTime::new(576.0, 24.0),
		RationalTime::new(1152.0, 24.0),
	));
	clip.set_media_reference(MediaReference::ExternalReference(ExternalReference::new(
		url, None,
	)));
	video.append_child(Composable::Clip(clip));
	video.append_child(Composable::Gap(Gap::new(
		TimeRange::new(RationalTime::new(0.0, 24.0), RationalTime::new(576.0, 24.0)),
		"My Sequence Gap",
	)));

	timeline.tracks_mut().append_child(Composable::Track(video));
	timeline
}

/// Write a timeline to a temp file, load it via the OTIO task, and return the
/// task handle.
fn load_timeline(timeline: &Timeline) -> (oaktask::handle::CHandle, std::path::PathBuf) {
	let path = std::env::temp_dir().join(format!("oak-otio-load-{}.otio", std::process::id()));
	timeline.to_json_file(&path).unwrap();
	let path_str = path.to_string_lossy().into_owned();
	let task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	(task, path)
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

/// A synthetic OTIO document imports into a new project: the clip's media
/// in/length are converted through `Rational::from_double`, footage is
/// created and linked, and the project is produced on `take_project`.
#[test]
fn otio_load_builds_project_from_synthetic_document() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let (mut task, path) = load_timeline(&synthetic_timeline(
		"Seq A",
		"file:///tmp/oak-otio-test.mp4",
	));
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);

	// Clip + gap appended to the track.
	assert_eq!(APPENDED_BLOCKS.load(Ordering::SeqCst), 2);
	// One footage created for the clip's ExternalReference.
	assert_eq!(FOOTAGE_CREATE_COUNT.load(Ordering::SeqCst), 1);
	// Clip media in: 576/24 = 24s -> Rational(24, 1).
	assert_eq!(MEDIA_IN_NUM.load(Ordering::SeqCst), 24);
	assert_eq!(MEDIA_IN_DEN.load(Ordering::SeqCst), 1);
	// The last length write is the gap's: 576/24 = 24s.
	assert_eq!(SET_LENGTH_NUM.load(Ordering::SeqCst), 24);
	assert_eq!(SET_LENGTH_DEN.load(Ordering::SeqCst), 1);
	// Video track wiring: transform connected between footage and block.
	assert!(CONNECT_INPUT_IDS
		.lock()
		.unwrap()
		.iter()
		.any(|id| id == "tex_in"));
	assert!(CONNECT_INPUT_IDS
		.lock()
		.unwrap()
		.iter()
		.any(|id| id == "buffer_in"));

	let project = unsafe { oaktask_load_otio_take_project(task) };
	assert!(!project.ctx.is_null());
	// Second take is empty (ownership transferred).
	assert!(unsafe { oaktask_load_otio_take_project(task) }
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A `SerializableCollection` root imports every timeline; clips sharing one
/// URL resolve to a single footage node.
#[test]
fn otio_load_collection_dedups_footage() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let url = "file:///tmp/oak-shared.mp4";
	let collection = SerializableCollection::new(
		"Sequences",
		vec![
			Serializable::Timeline(synthetic_timeline("A", url)),
			Serializable::Timeline(synthetic_timeline("B", url)),
		],
	);
	let path = std::env::temp_dir().join("oak-otio-collection.otio");
	Serializable::SerializableCollection(collection)
		.to_json_file(&path)
		.unwrap();
	let path_str = path.to_string_lossy().into_owned();

	let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(
		APPENDED_BLOCKS.load(Ordering::SeqCst),
		4,
		"one clip+gap per timeline"
	);
	assert_eq!(
		FOOTAGE_CREATE_COUNT.load(Ordering::SeqCst),
		1,
		"shared URL deduplicated"
	);
	assert!(!(unsafe { oaktask_load_otio_take_project(task) })
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// The import-confirm callback can reject the whole import: the task still
/// finishes (C++ parity: cancel + return true) but no project is produced.
#[test]
fn otio_load_rejected_confirm_cancels_import() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	unsafe { oaktask_load_otio_set_confirm_cb(Some(reject_all), std::ptr::null_mut()) };

	let (mut task, path) =
		load_timeline(&synthetic_timeline("Seq A", "file:///tmp/oak-reject.mp4"));
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert!(
		unsafe { oaktask_load_otio_take_project(task) }
			.ctx
			.is_null(),
		"rejected import has no project"
	);

	unsafe { oaktask_load_otio_set_confirm_cb(None, std::ptr::null_mut()) };
	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// The confirm callback receives the sequence labels in order and can accept
/// the import.
#[test]
fn otio_load_confirm_receives_sequence_names() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	*NODE_LABEL.lock().unwrap() = "Seq A".to_string();
	let seen: std::sync::Mutex<Vec<String>> = std::sync::Mutex::new(Vec::new());
	unsafe extern "C" fn accept_and_record(
		seq: *const *const c_char,
		count: c_int,
		userdata: *mut c_void,
	) -> c_int {
		let seen = &*(userdata as *const std::sync::Mutex<Vec<String>>);
		for i in 0..count {
			if !(*seq.add(i as usize)).is_null() {
				seen.lock().unwrap().push(
					std::ffi::CStr::from_ptr(*seq.add(i as usize))
						.to_string_lossy()
						.into_owned(),
				);
			}
		}
		1
	}

	let seen_ptr: *const std::sync::Mutex<Vec<String>> = &seen;
	unsafe { oaktask_load_otio_set_confirm_cb(Some(accept_and_record), seen_ptr as *mut c_void) };

	let (mut task, path) = load_timeline(&synthetic_timeline("Seq A", "file:///tmp/oak-names.mp4"));
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(seen.lock().unwrap().clone(), vec!["Seq A".to_string()]);
	assert!(!(unsafe { oaktask_load_otio_take_project(task) })
		.ctx
		.is_null());

	unsafe { oaktask_load_otio_set_confirm_cb(None, std::ptr::null_mut()) };
	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// An unrecognized root schema fails with the C++ error message.
#[test]
fn otio_load_unknown_root_schema_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let path = std::env::temp_dir().join("oak-otio-unknown.otio");
	std::fs::write(&path, r#"{"OTIO_SCHEMA": "WeirdThing.9", "a": 1}"#).unwrap();
	let path_str = path.to_string_lossy().into_owned();

	let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(
		cstr_read(&buf).contains("Unknown OpenTimelineIO root element"),
		"error was: {}",
		cstr_read(&buf)
	);
	assert!(unsafe { oaktask_load_otio_take_project(task) }
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Corrupt JSON fails with a clear parse error.
#[test]
fn otio_load_corrupt_json_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let path = std::env::temp_dir().join("oak-otio-corrupt.otio");
	std::fs::write(&path, "this is not json {").unwrap();
	let path_str = path.to_string_lossy().into_owned();

	let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(
		cstr_read(&buf).contains("Failed to load OpenTimelineIO"),
		"error was: {}",
		cstr_read(&buf)
	);

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A clip whose media reference is `MissingReference` imports without
/// creating footage.
#[test]
fn otio_load_missing_reference_creates_no_footage() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let mut timeline = Timeline::new("Seq");
	let mut video = Track::new("Video");
	let mut clip = Clip::new("Lost Clip");
	clip.set_source_range(TimeRange::new(
		RationalTime::new(0.0, 24.0),
		RationalTime::new(24.0, 24.0),
	));
	clip.set_media_reference(MediaReference::MissingReference(
		oakotio::MissingReference::new(),
	));
	video.append_child(Composable::Clip(clip));
	timeline.tracks_mut().append_child(Composable::Track(video));

	let (mut task, path) = load_timeline(&timeline);
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(APPENDED_BLOCKS.load(Ordering::SeqCst), 1);
	assert_eq!(FOOTAGE_CREATE_COUNT.load(Ordering::SeqCst), 0);
	assert!(!(unsafe { oaktask_load_otio_take_project(task) })
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A track whose kind is neither Video nor Audio is skipped (C++ parity).
#[test]
fn otio_load_unknown_track_kind_skipped() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let mut timeline = Timeline::new("Seq");
	let mut subtitle = Track::new("Subtitles");
	subtitle.append_child(Composable::Clip(Clip::new("Sub Clip")));
	timeline
		.tracks_mut()
		.append_child(Composable::Track(subtitle));

	let (mut task, path) = load_timeline(&timeline);
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(APPENDED_BLOCKS.load(Ordering::SeqCst), 0);
	assert!(!(unsafe { oaktask_load_otio_take_project(task) })
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A transition between two clips wires the block connections and converts
/// its offsets through `Rational::fromRationalTime`.
#[test]
fn otio_load_transition_connects_blocks() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let mut timeline = Timeline::new("Seq");
	let mut video = Track::new("Video");
	let mut clip = Clip::new("In");
	clip.set_source_range(TimeRange::new(
		RationalTime::new(0.0, 24.0),
		RationalTime::new(48.0, 24.0),
	));
	video.append_child(Composable::Clip(clip));
	let mut transition = Transition::new("Cross");
	transition.set_in_offset(RationalTime::new(12.0, 24.0));
	transition.set_out_offset(RationalTime::new(12.0, 24.0));
	video.append_child(Composable::Transition(transition));
	let mut clip = Clip::new("Out");
	clip.set_source_range(TimeRange::new(
		RationalTime::new(48.0, 24.0),
		RationalTime::new(48.0, 24.0),
	));
	video.append_child(Composable::Clip(clip));
	timeline.tracks_mut().append_child(Composable::Track(video));

	let (mut task, path) = load_timeline(&timeline);
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);
	assert_eq!(APPENDED_BLOCKS.load(Ordering::SeqCst), 3);
	// 12/24 = 0.5s -> Rational(1, 2).
	assert_eq!(TRANSITION_IN_NUM.load(Ordering::SeqCst), 1);
	assert_eq!(TRANSITION_IN_DEN.load(Ordering::SeqCst), 2);
	assert_eq!(TRANSITION_OUT_NUM.load(Ordering::SeqCst), 1);
	assert_eq!(TRANSITION_OUT_DEN.load(Ordering::SeqCst), 2);
	let connects = CONNECT_INPUT_IDS.lock().unwrap();
	assert!(
		connects.iter().any(|id| id == "out_block_in"),
		"previous clip -> transition: {connects:?}"
	);
	assert!(
		connects.iter().any(|id| id == "in_block_in"),
		"transition -> next clip: {connects:?}"
	);

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

/// Point the save-path stubs at one sequence with a video clip and a media
/// reference.
fn configure_one_sequence_save() {
	FOLDER_CHILD_COUNT.store(1, Ordering::SeqCst);
	*NODE_ID.lock().unwrap() = "org.olivevideoeditor.Olive.sequence".to_string();
	*NODE_LABEL.lock().unwrap() = "My Sequence".to_string();
	VIDEO_FRAME_RATE_NUM.store(25, Ordering::SeqCst);
	VIDEO_FRAME_RATE_DEN.store(1, Ordering::SeqCst);
	VIDEO_DURATION.store(100, Ordering::SeqCst);
	TRACK_LIST_TRACK_COUNT.store(1, Ordering::SeqCst);
	TRACK_TYPE.store(0, Ordering::SeqCst); // video
	TRACK_BLOCK_COUNT.store(1, Ordering::SeqCst);
	BLOCK_KIND.store(1, Ordering::SeqCst); // clip
	BLOCK_IN_NUM.store(576, Ordering::SeqCst);
	BLOCK_IN_DEN.store(24, Ordering::SeqCst);
	BLOCK_LENGTH_NUM.store(25, Ordering::SeqCst);
	BLOCK_LENGTH_DEN.store(1, Ordering::SeqCst);
	*FOOTAGE_FILENAME.lock().unwrap() = "file:///tmp/video.mp4".to_string();
}

/// Run the OTIO save task to a temp file and parse the result back with
/// `oakotio`.
fn run_save_and_parse() -> (oaktask::handle::CHandle, std::path::PathBuf, Serializable) {
	let out = std::env::temp_dir().join(format!("oak-otio-save-{}.otio", std::process::id()));
	let out_str = out.to_string_lossy().into_owned();
	let _ = std::fs::remove_file(&out);

	let mut task =
		unsafe { oaktask_create_project_save_otio(fake_handle(), common::cstr_of(&out_str)) };
	let started = unsafe { oaktask_task_start_sync(task) };
	let text = std::fs::read_to_string(&out).unwrap_or_default();
	let parsed = oakotio::from_json_string(&text).ok();
	free(&mut task);
	assert_eq!(started, 1, "save task should succeed; file was: {text}");
	let parsed = parsed.expect("saved file parses as OTIO");
	(task, out, parsed)
}

/// A single sequence exports as a `Timeline` root whose track/clip/media
/// values round-trip through the C++-parity model.
#[test]
fn otio_save_single_timeline_round_trip() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();

	let (_task, out, parsed) = run_save_and_parse();

	let timeline = match &parsed {
		Serializable::Timeline(t) => t,
		other => panic!("expected Timeline root, got {}", other.schema_name()),
	};
	assert_eq!(timeline.name(), "My Sequence");
	let tracks = timeline.tracks().children();
	assert_eq!(tracks.len(), 1);
	let track = tracks[0].as_track().expect("track");
	assert_eq!(track.kind(), "Video");
	assert_eq!(
		track.children().len(),
		1,
		"no trailing gap: track length 0 < clip duration"
	);

	let clip = track.children()[0].as_clip().expect("clip");
	assert_eq!(clip.name(), "My Sequence");
	let range = clip.source_range().expect("clip source_range");
	// 576/24 = 24s at 25fps -> {600, 25}; 25/1 = 25s at 25fps -> {625, 25}.
	assert_eq!(
		(range.start_time().value(), range.start_time().rate()),
		(600.0, 25.0)
	);
	assert_eq!(
		(range.duration().value(), range.duration().rate()),
		(625.0, 25.0)
	);

	let external = clip
		.media_reference()
		.expect("media reference")
		.as_external_reference()
		.expect("ExternalReference");
	assert_eq!(external.target_url(), "file:///tmp/video.mp4");
	let available = external.available_range().expect("available_range");
	assert_eq!(
		(
			available.start_time().value(),
			available.start_time().rate()
		),
		(0.0, 25.0)
	);
	assert_eq!(
		(available.duration().value(), available.duration().rate()),
		(100.0, 25.0)
	);

	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A track shorter than the list maximum is padded with a trailing `Gap`
/// whose duration is the leftover seconds at rate 1.0 (C++ parity).
#[test]
fn otio_save_pads_shorter_track_with_gap() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();
	TRACK_LENGTH_NUM.store(50, Ordering::SeqCst); // 50s track -> clip 25s leaves 25s
	TRACK_LENGTH_DEN.store(1, Ordering::SeqCst);

	let (_task, out, parsed) = run_save_and_parse();

	let timeline = parsed.as_timeline().expect("Timeline root");
	let track = &timeline.tracks().children()[0];
	let track = track.as_track().expect("track");
	assert_eq!(track.children().len(), 2);
	let gap = track.children()[1].as_gap().expect("trailing gap");
	let range = gap.source_range().expect("gap source_range");
	assert_eq!(
		(range.start_time().value(), range.start_time().rate()),
		(625.0, 25.0),
		"starts at the track duration"
	);
	assert_eq!(
		(range.duration().value(), range.duration().rate()),
		(25.0, 1.0),
		"leftover seconds at rate 1.0"
	);

	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Audio clips write the fixed 48 kHz available range and an "Audio" track
/// kind.
#[test]
fn otio_save_audio_track_uses_48k_available_range() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();
	AUDIO_TRACK_LIST_NULL.store(0, Ordering::SeqCst);
	TRACK_TYPE.store(1, Ordering::SeqCst); // audio
	*FOOTAGE_FILENAME.lock().unwrap() = "file:///tmp/audio.wav".to_string();

	let (_task, out, parsed) = run_save_and_parse();

	let timeline = parsed.as_timeline().expect("Timeline root");
	let track = &timeline.tracks().children()[0];
	let track = track.as_track().expect("track");
	assert_eq!(track.kind(), "Audio");
	let clip = track.children()[0].as_clip().expect("clip");
	let external = clip
		.media_reference()
		.expect("media reference")
		.as_external_reference()
		.expect("ExternalReference");
	assert_eq!(external.target_url(), "file:///tmp/audio.wav");
	let available = external.available_range().expect("available_range");
	assert_eq!(
		(
			available.start_time().value(),
			available.start_time().rate()
		),
		(0.0, 48000.0)
	);
	assert_eq!(
		(available.duration().value(), available.duration().rate()),
		(0.0, 48000.0)
	);

	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A clip with no connected footage exports without a media reference.
#[test]
fn otio_save_clip_without_media_has_no_reference() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();
	FOOTAGE_FOUND.store(0, Ordering::SeqCst);

	let (_task, out, parsed) = run_save_and_parse();

	let timeline = parsed.as_timeline().expect("Timeline root");
	let track = &timeline.tracks().children()[0];
	let track = track.as_track().expect("track");
	let clip = track.children()[0].as_clip().expect("clip");
	assert!(
		clip.media_reference().is_none(),
		"no footage -> no media reference"
	);

	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A transition block exports its offsets (Rational -> RationalTime at 24).
#[test]
fn otio_save_transition_serializes_offsets() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();
	BLOCK_KIND.store(3, Ordering::SeqCst); // transition
	TRANSITION_IN_NUM.store(12, Ordering::SeqCst);
	TRANSITION_IN_DEN.store(24, Ordering::SeqCst);
	TRANSITION_OUT_NUM.store(12, Ordering::SeqCst);
	TRANSITION_OUT_DEN.store(24, Ordering::SeqCst);

	let (_task, out, parsed) = run_save_and_parse();

	let timeline = parsed.as_timeline().expect("Timeline root");
	let track = &timeline.tracks().children()[0];
	let track = track.as_track().expect("track");
	let transition = track.children()[0].as_transition().expect("transition");
	assert_eq!(
		(
			transition.in_offset().value(),
			transition.in_offset().rate()
		),
		(12.0, 24.0)
	);
	assert_eq!(
		(
			transition.out_offset().value(),
			transition.out_offset().rate()
		),
		(12.0, 24.0)
	);

	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// Multiple sequences export as a `SerializableCollection` named "Sequences".
#[test]
fn otio_save_writes_collection_for_multiple_sequences() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();
	FOLDER_CHILD_COUNT.store(2, Ordering::SeqCst);

	let (_task, out, parsed) = run_save_and_parse();

	let collection = match &parsed {
		Serializable::SerializableCollection(c) => c,
		other => panic!(
			"expected SerializableCollection root, got {}",
			other.schema_name()
		),
	};
	assert_eq!(collection.name(), "Sequences");
	assert_eq!(collection.children().len(), 2);

	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A project with no sequences fails with the C++ error message.
#[test]
fn otio_save_no_sequences_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	FOLDER_CHILD_COUNT.store(0, Ordering::SeqCst);

	let out = std::env::temp_dir().join("oak-otio-save-none.otio");
	let out_str = out.to_string_lossy().into_owned();
	let mut task =
		unsafe { oaktask_create_project_save_otio(fake_handle(), common::cstr_of(&out_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(
		cstr_read(&buf).contains("Project contains no sequences to export."),
		"error was: {}",
		cstr_read(&buf)
	);

	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A sequence with a non-positive frame rate fails to serialize.
#[test]
fn otio_save_bad_frame_rate_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();
	VIDEO_FRAME_RATE_NUM.store(0, Ordering::SeqCst);
	VIDEO_FRAME_RATE_DEN.store(1, Ordering::SeqCst);

	let out = std::env::temp_dir().join("oak-otio-save-badrate.otio");
	let out_str = out.to_string_lossy().into_owned();
	let mut task =
		unsafe { oaktask_create_project_save_otio(fake_handle(), common::cstr_of(&out_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(
		cstr_read(&buf).contains("Failed to serialize sequence"),
		"error was: {}",
		cstr_read(&buf)
	);

	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A track with an unknown native type fails the whole export (C++ parity:
/// serialize_track falls through to fail).
#[test]
fn otio_save_unknown_track_type_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();
	TRACK_TYPE.store(-1, Ordering::SeqCst); // OAKNODE_TRACK_TYPE_NONE

	let out = std::env::temp_dir().join("oak-otio-save-badtrack.otio");
	let out_str = out.to_string_lossy().into_owned();
	let mut task =
		unsafe { oaktask_create_project_save_otio(fake_handle(), common::cstr_of(&out_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(
		cstr_read(&buf).contains("Failed to serialize sequence"),
		"error was: {}",
		cstr_read(&buf)
	);

	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

// ---------------------------------------------------------------------------
// FCPXML load/save
// ---------------------------------------------------------------------------

/// Write a timeline to a temp `.fcpxml` file (via the `oakotio` fcpxml
/// writer) and load it through the OTIO task, returning the task handle.
fn load_fcpxml(timeline: &Timeline) -> (oaktask::handle::CHandle, std::path::PathBuf) {
	let path = std::env::temp_dir().join(format!("oak-fcpxml-load-{}.fcpxml", std::process::id()));
	oakotio::to_fcpxml_file(&[timeline.clone()], &path).unwrap();
	let path_str = path.to_string_lossy().into_owned();
	let task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	(task, path)
}

/// A synthetic FCPXML document (built with the `oakotio` fcpxml writer)
/// imports into a new project exactly like the OTIO document: the same
/// track/clip/footage code produces the same numbers, so the two formats
/// share one code path after the parse.
#[test]
fn fcpxml_load_builds_project_from_synthetic_document() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let (mut task, path) = load_fcpxml(&synthetic_timeline(
		"Seq A",
		"file:///tmp/oak-otio-test.mp4",
	));
	assert!(!task.ctx.is_null());
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 1);

	// Clip + gap appended to the track.
	assert_eq!(APPENDED_BLOCKS.load(Ordering::SeqCst), 2);
	// One footage created for the clip's ExternalReference.
	assert_eq!(FOOTAGE_CREATE_COUNT.load(Ordering::SeqCst), 1);
	// Clip media in: 576/24 = 24s -> Rational(24, 1).
	assert_eq!(MEDIA_IN_NUM.load(Ordering::SeqCst), 24);
	assert_eq!(MEDIA_IN_DEN.load(Ordering::SeqCst), 1);
	// The last length write is the gap's: 576/24 = 24s.
	assert_eq!(SET_LENGTH_NUM.load(Ordering::SeqCst), 24);
	assert_eq!(SET_LENGTH_DEN.load(Ordering::SeqCst), 1);
	// Video track wiring: transform connected between footage and block.
	assert!(CONNECT_INPUT_IDS
		.lock()
		.unwrap()
		.iter()
		.any(|id| id == "tex_in"));
	assert!(CONNECT_INPUT_IDS
		.lock()
		.unwrap()
		.iter()
		.any(|id| id == "buffer_in"));

	let project = unsafe { oaktask_load_otio_take_project(task) };
	assert!(!project.ctx.is_null());
	// Second take is empty (ownership transferred).
	assert!(unsafe { oaktask_load_otio_take_project(task) }
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// An FCPXML export of one sequence re-parses with the `oakotio` fcpxml
/// reader: the timeline/track/clip/media values match the OTIO export
/// (same serialization, different codec).
#[test]
fn fcpxml_save_round_trips_through_parse() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();

	let out = std::env::temp_dir().join(format!("oak-fcpxml-save-{}.fcpxml", std::process::id()));
	let out_str = out.to_string_lossy().into_owned();
	let _ = std::fs::remove_file(&out);

	let mut task =
		unsafe { oaktask_create_project_save_otio(fake_handle(), common::cstr_of(&out_str)) };
	let started = unsafe { oaktask_task_start_sync(task) };
	let text = std::fs::read_to_string(&out).unwrap_or_default();
	free(&mut task);
	assert_eq!(started, 1, "save task should succeed; file was: {text}");

	let timelines = oakotio::from_fcpxml_string(&text).expect("saved file parses as FCPXML");
	assert_eq!(timelines.len(), 1);
	let timeline = &timelines[0];
	assert_eq!(timeline.name(), "My Sequence");
	let tracks = timeline.tracks().children();
	assert_eq!(tracks.len(), 1);
	let track = tracks[0].as_track().expect("track");
	assert_eq!(track.kind(), "Video");
	assert_eq!(
		track.children().len(),
		1,
		"no trailing gap: track length 0 < clip duration"
	);

	let clip = track.children()[0].as_clip().expect("clip");
	assert_eq!(clip.name(), "My Sequence");
	let range = clip.source_range().expect("clip source_range");
	// 576/24 = 24s at 25fps -> {600, 25}; 25/1 = 25s at 25fps -> {625, 25}.
	assert_eq!(
		(range.start_time().value(), range.start_time().rate()),
		(600.0, 25.0)
	);
	assert_eq!(
		(range.duration().value(), range.duration().rate()),
		(625.0, 25.0)
	);

	let external = clip
		.media_reference()
		.expect("media reference")
		.as_external_reference()
		.expect("ExternalReference");
	assert_eq!(external.target_url(), "file:///tmp/video.mp4");
	let available = external.available_range().expect("available_range");
	// Asset duration 100/25 = 4s at 25fps -> {0, 25} .. {100, 25}.
	assert_eq!(
		(
			available.start_time().value(),
			available.start_time().rate()
		),
		(0.0, 25.0)
	);
	assert_eq!(
		(available.duration().value(), available.duration().rate()),
		(100.0, 25.0)
	);

	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// A full save -> load cycle through the tasks: the FCPXML file written by
/// the save task is imported by the load task with the same track/clip/
/// footage structure.
#[test]
fn fcpxml_save_reimports_through_load_task() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();

	let out = std::env::temp_dir().join(format!("oak-fcpxml-cycle-{}.fcpxml", std::process::id()));
	let out_str = out.to_string_lossy().into_owned();
	let _ = std::fs::remove_file(&out);

	let mut save =
		unsafe { oaktask_create_project_save_otio(fake_handle(), common::cstr_of(&out_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(save) }, 1);
	free(&mut save);

	reset_stubs();
	let mut load = unsafe { oaktask_create_project_load_otio(common::cstr_of(&out_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(load) }, 1);
	assert_eq!(
		APPENDED_BLOCKS.load(Ordering::SeqCst),
		1,
		"one clip re-imported"
	);
	assert_eq!(FOOTAGE_CREATE_COUNT.load(Ordering::SeqCst), 1);
	assert!(!(unsafe { oaktask_load_otio_take_project(load) })
		.ctx
		.is_null());

	free(&mut load);
	let _ = std::fs::remove_file(&out);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

// ---------------------------------------------------------------------------
// Extension dispatch matrix
// ---------------------------------------------------------------------------

/// The load task dispatches on the filename extension, case-insensitively:
/// `.otio`/`.OTIO` parse OpenTimelineIO JSON, `.fcpxml`/`.FCPXML` parse
/// FCPXML, and any other extension fails with "Unknown project file
/// format".
#[test]
fn otio_load_extension_dispatch_matrix() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	for (ext, is_otio) in [
		(".otio", true),
		(".OTIO", true),
		(".fcpxml", false),
		(".FCPXML", false),
	] {
		let timeline = synthetic_timeline("Seq A", "file:///tmp/oak-dispatch.mp4");
		let path = std::env::temp_dir().join(format!("oak-dispatch-{}{ext}", std::process::id()));
		if is_otio {
			timeline.to_json_file(&path).unwrap();
		} else {
			oakotio::to_fcpxml_file(&[timeline], &path).unwrap();
		}
		let path_str = path.to_string_lossy().into_owned();

		let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
		assert_eq!(
			unsafe { oaktask_task_start_sync(task) },
			1,
			"extension {ext} should import"
		);
		assert_eq!(
			APPENDED_BLOCKS.load(Ordering::SeqCst),
			2,
			"extension {ext}: clip + gap"
		);
		assert!(
			!(unsafe { oaktask_load_otio_take_project(task) })
				.ctx
				.is_null(),
			"extension {ext}"
		);

		free(&mut task);
		let _ = std::fs::remove_file(&path);
		reset_stubs();
	}
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// An unknown extension fails the load with a clear format error.
#[test]
fn otio_load_unknown_extension_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let path =
		std::env::temp_dir().join(format!("oak-otio-unknown-ext-{}.txt", std::process::id()));
	std::fs::write(&path, "whatever").unwrap();
	let path_str = path.to_string_lossy().into_owned();

	let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(
		cstr_read(&buf).contains("Unknown project file format"),
		"error was: {}",
		cstr_read(&buf)
	);
	assert!(unsafe { oaktask_load_otio_take_project(task) }
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// An unknown extension fails the save with a clear format error, before any
/// serialization work happens.
#[test]
fn otio_save_unknown_extension_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();
	configure_one_sequence_save();

	let out =
		std::env::temp_dir().join(format!("oak-otio-save-unknown-{}.xyz", std::process::id()));
	let out_str = out.to_string_lossy().into_owned();
	let mut task =
		unsafe { oaktask_create_project_save_otio(fake_handle(), common::cstr_of(&out_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	assert!(
		cstr_read(&buf).contains("Unknown project file format"),
		"error was: {}",
		cstr_read(&buf)
	);

	free(&mut task);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

// ---------------------------------------------------------------------------
// FCPXML error paths
// ---------------------------------------------------------------------------

/// A corrupt `.fcpxml` document fails with a clear FCPXML parse error.
#[test]
fn fcpxml_load_corrupt_xml_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let path =
		std::env::temp_dir().join(format!("oak-fcpxml-corrupt-{}.fcpxml", std::process::id()));
	std::fs::write(&path, "<fcpxml version=\"1.10\"><resources>").unwrap();
	let path_str = path.to_string_lossy().into_owned();

	let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	let msg = cstr_read(&buf);
	assert!(msg.contains("Failed to load FCPXML"), "error was: {msg}");
	assert!(unsafe { oaktask_load_otio_take_project(task) }
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}

/// An FCPXML version this crate cannot read fails the load with the
/// underlying version error surfaced in the task message.
#[test]
fn fcpxml_load_unknown_version_fails() {
	let _guard = MANAGER_LOCK.lock().unwrap();
	reset_stubs();

	let path =
		std::env::temp_dir().join(format!("oak-fcpxml-version-{}.fcpxml", std::process::id()));
	std::fs::write(
		&path,
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE fcpxml>\n<fcpxml version=\"2.0\"><library/></fcpxml>",
	)
	.unwrap();
	let path_str = path.to_string_lossy().into_owned();

	let mut task = unsafe { oaktask_create_project_load_otio(common::cstr_of(&path_str)) };
	assert_eq!(unsafe { oaktask_task_start_sync(task) }, 0);
	let needed = unsafe { oaktask_task_error(task, std::ptr::null_mut(), 0) };
	let mut buf = vec![0i8; needed as usize];
	unsafe { oaktask_task_error(task, buf.as_mut_ptr(), needed) };
	let msg = cstr_read(&buf);
	assert!(msg.contains("Failed to load FCPXML"), "error was: {msg}");
	assert!(msg.contains("version"), "error was: {msg}");
	assert!(unsafe { oaktask_load_otio_take_project(task) }
		.ctx
		.is_null());

	free(&mut task);
	let _ = std::fs::remove_file(&path);
	assert_eq!(unsafe { oaktask_debug_alive_count() }, 0);
}
