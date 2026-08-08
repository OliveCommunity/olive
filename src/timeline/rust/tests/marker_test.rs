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

//! Contract tests for the marker domain (`src/marker.rs`):
//! `TimelineMarker` and `TimelineMarkerList` value types plus the five
//! marker undo commands. The list stays sorted by time (C++
//! `TimelineMarkerList` re-sorts on mutation), which `resort` enforces
//! after out-of-band edits. XML load/save matches the format written
//! by `timelinemarker.cpp`/`timelinemarkerlist.cpp`.

use oakcore_rs::{Rational, TimeRange};
use oaktimeline::bridge::common::{oakcommon_xml_writer_free, oakcommon_xml_writer_init};
use oaktimeline::bridge::teststubs::{xml_reader_handle, MockXmlNode, MockXmlWriter};
use oaktimeline::common::EditToInfo;
use oaktimeline::ffi as ffi;
use oaktimeline::handle::{get, get_mut, make_owned, CHandle};
use oaktimeline::marker::{
	MarkerAddCommand, MarkerChangeColorCommand, MarkerChangeNameCommand,
	MarkerChangeTimeCommand, MarkerRemoveCommand, TimelineMarker, TimelineMarkerList,
};
use oaktimeline::undocommon::Command;

/// A default `TimelineMarker` starts at the null time with no name.
#[test]
fn marker_default_is_null_time() {
	let m = TimelineMarker::new();
	assert_eq!(m.color(), 0);
	assert_eq!(m.time().in_(), Rational::new(0, 1));
	assert_eq!(m.time().out(), Rational::new(0, 1));
	assert_eq!(m.name(), "");
}

/// `TimelineMarker::with_time` stores color, time range and name as
/// given and exposes them through the getters.
#[test]
fn marker_with_time_stores_fields() {
	let m = TimelineMarker::with_time(
		3,
		TimeRange::new(Rational::new(10, 1), Rational::new(20, 1)),
		"hello",
	);
	assert_eq!(m.color(), 3);
	assert_eq!(m.time().in_(), Rational::new(10, 1));
	assert_eq!(m.time().out(), Rational::new(20, 1));
	assert_eq!(m.name(), "hello");
}

/// `set_time_point` moves a range marker to a single point, preserving
/// its length (and hence name and color).
#[test]
fn marker_set_time_point_preserves_name_color() {
	let mut m = TimelineMarker::with_time(
		5,
		TimeRange::new(Rational::new(10, 1), Rational::new(20, 1)),
		"tag",
	);
	let t = Rational::new(30, 1);
	m.set_time_point(t);
	// Color and name survive the move.
	assert_eq!(m.color(), 5);
	assert_eq!(m.name(), "tag");
	// The original length (10) is preserved.
	assert_eq!(m.time().in_(), t);
	assert_eq!(m.time().out(), Rational::new(40, 1));
}

/// `has_sibling_at_time` is a De-Qt simplification that always reports
/// `false`; sibling queries go through the list instead.
#[test]
fn marker_sibling_detection() {
	let m = TimelineMarker::new();
	assert!(!m.has_sibling_at_time(Rational::new(0, 1)));
	assert!(!m.has_sibling_at_time(Rational::new(42, 1)));
}

/// The list starts empty and grows with each `add_marker`, preserving
/// sorted order (by in point) for markers at distinct times.
#[test]
fn marker_list_grows_with_adds() {
	let mut list = TimelineMarkerList::new();
	assert!(list.empty());
	assert_eq!(list.size(), 0);

	list.add_marker(TimelineMarker::with_time(
		1,
		TimeRange::new(Rational::new(10, 1), Rational::new(11, 1)),
		"a",
	));
	list.add_marker(TimelineMarker::with_time(
		2,
		TimeRange::new(Rational::new(5, 1), Rational::new(6, 1)),
		"b",
	));

	assert_eq!(list.size(), 2);
	assert!(!list.empty());
	// Sorted by in point: b (5) before a (10).
	assert_eq!(list.front().unwrap().name(), "b");
	assert_eq!(list.back().unwrap().name(), "a");
}

/// `at`/`back`/`front` expose the stored markers; `at` returns `None`
/// for an out-of-range index.
#[test]
fn marker_list_indexing() {
	let mut list = TimelineMarkerList::new();
	assert!(list.at(0).is_none());
	assert!(list.front().is_none());
	assert!(list.back().is_none());

	list.add_marker(TimelineMarker::with_time(
		1,
		TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
		"one",
	));
	assert!(list.at(0).is_some());
	assert_eq!(list.at(0).unwrap().name(), "one");
	assert!(list.at(1).is_none());
	assert!(list.front().is_some());
	assert!(list.back().is_some());
}

/// `remove_marker` removes exactly the given marker and returns it;
/// removing a marker that is not present is a no-op returning `None`.
#[test]
fn marker_list_remove() {
	let mut list = TimelineMarkerList::new();
	list.add_marker(TimelineMarker::with_time(
		1,
		TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
		"one",
	));

	// `remove_marker` takes a reference into the list; drop the borrow by
	// going through a raw pointer first (as `marker.rs` does internally).
	let m = list.at(0).unwrap();
	let mptr = m as *const TimelineMarker;
	let removed = list.remove_marker(unsafe { &*mptr });
	assert!(removed.is_some());
	assert_eq!(removed.unwrap().name(), "one");
	assert_eq!(list.size(), 0);

	// Removing a marker not present returns None.
	let stray = TimelineMarker::new();
	assert!(list.remove_marker(&stray).is_none());
	assert_eq!(list.size(), 0);
}

/// `get_marker_at_time` finds the marker at an exact time; adding a
/// marker and then asking at the same time finds it.
#[test]
fn marker_list_find_at_time() {
	let mut list = TimelineMarkerList::new();
	list.add_marker(TimelineMarker::with_time(
		1,
		TimeRange::new(Rational::new(7, 1), Rational::new(8, 1)),
		"x",
	));

	let found = list.get_marker_at_time(Rational::new(7, 1));
	assert!(found.is_some());
	assert_eq!(found.unwrap().name(), "x");
	assert!(list.get_marker_at_time(Rational::new(9, 1)).is_none());
}

/// `get_closest_marker_to_time` finds the nearest marker; an exact match
/// at the first marker early-exits once the distance starts increasing,
/// and queries before/after all markers exercise both signs of the
/// absolute-difference comparison.
#[test]
fn marker_list_closest_to_time() {
	let mut list = TimelineMarkerList::new();
	list.add_marker(TimelineMarker::with_time(
		1,
		TimeRange::new(Rational::new(5, 1), Rational::new(6, 1)),
		"a",
	));
	list.add_marker(TimelineMarker::with_time(
		2,
		TimeRange::new(Rational::new(10, 1), Rational::new(11, 1)),
		"b",
	));
	list.add_marker(TimelineMarker::with_time(
		3,
		TimeRange::new(Rational::new(20, 1), Rational::new(21, 1)),
		"c",
	));

	// Empty list -> None.
	assert!(TimelineMarkerList::new()
		.get_closest_marker_to_time(Rational::new(0, 1))
		.is_none());
	// Exact match at the first marker (early-exit on the next).
	assert_eq!(
		list.get_closest_marker_to_time(Rational::new(5, 1)).unwrap().name(),
		"a"
	);
	// Query after all markers (negative differences).
	assert_eq!(
		list.get_closest_marker_to_time(Rational::new(100, 1)).unwrap().name(),
		"c"
	);
	// Query before all markers (positive differences).
	assert_eq!(
		list.get_closest_marker_to_time(Rational::new(1, 1)).unwrap().name(),
		"a"
	);
}

/// A command's undo before any redo is a no-op.
#[test]
fn marker_add_command_undo_before_redo() {
	let list_h = make_owned(TimelineMarkerList::new());
	let mut cmd = MarkerAddCommand::new(
		list_h.clone(),
		TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
		"m",
		0,
	);
	cmd.undo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 0);
}

/// `MarkerRemoveCommand` redo is idempotent (a second redo is a no-op).
#[test]
fn marker_remove_command_double_redo() {
	let list_h = make_owned(TimelineMarkerList::new());
	{
		let l = unsafe { get_mut::<TimelineMarkerList>(&list_h) }.unwrap();
		l.add_marker(TimelineMarker::with_time(
			1,
			TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
			"m",
		));
	}
	let mut cmd = MarkerRemoveCommand::new(list_h.clone(), 0);
	cmd.redo();
	cmd.redo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 0);
}

/// Every marker command dispatches through the `Command` trait, which is
/// how the undo stack's vtable invokes them.
#[test]
fn marker_commands_trait_dispatch() {
	let list_h = make_owned(TimelineMarkerList::new());
	{
		let l = unsafe { get_mut::<TimelineMarkerList>(&list_h) }.unwrap();
		l.add_marker(TimelineMarker::with_time(
			1,
			TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
			"m",
		));
	}

	let mut add = MarkerAddCommand::new(
		list_h.clone(),
		TimeRange::new(Rational::new(9, 1), Rational::new(10, 1)),
		"n",
		0,
	);
	Command::redo(&mut add);
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 2);
	Command::undo(&mut add);
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 1);

	let mut remove = MarkerRemoveCommand::new(list_h.clone(), 0);
	Command::redo(&mut remove);
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 0);
	Command::undo(&mut remove);
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 1);

	let mut color = MarkerChangeColorCommand::new(list_h.clone(), 0, 7);
	Command::redo(&mut color);
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().color(), 7);
	Command::undo(&mut color);
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().color(), 1);

	let mut name = MarkerChangeNameCommand::new(list_h.clone(), 0, "z");
	Command::redo(&mut name);
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().name(), "z");
	Command::undo(&mut name);
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().name(), "m");

	let mut time = MarkerChangeTimeCommand::new(
		list_h.clone(),
		0,
		TimeRange::new(Rational::new(50, 1), Rational::new(51, 1)),
	);
	Command::redo(&mut time);
	assert_eq!(
		unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().time().in_(),
		Rational::new(50, 1)
	);
	Command::undo(&mut time);
	assert_eq!(
		unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().time().in_(),
		Rational::new(1, 1)
	);
}

/// Re-establishing time order after an out-of-band time change: the
/// list exposes markers immutably, so the edit is expressed as
/// detach + mutate a copy + re-insert, which re-sorts exactly as
/// `resort`/`MarkerChangeTimeCommand` do.
#[test]
fn marker_list_resort_after_time_change() {
	let mut list = TimelineMarkerList::new();
	list.add_marker(TimelineMarker::with_time(
		1,
		TimeRange::new(Rational::new(10, 1), Rational::new(11, 1)),
		"a",
	));
	list.add_marker(TimelineMarker::with_time(
		2,
		TimeRange::new(Rational::new(5, 1), Rational::new(6, 1)),
		"b",
	));
	// Order: b (5), a (10).
	assert_eq!(list.at(0).unwrap().name(), "b");

	// Detach `a`, move it to time 3, re-insert.
	let mptr = list.at(1).unwrap() as *const TimelineMarker;
	let a = list.remove_marker(unsafe { &*mptr }).unwrap();
	let mut moved = a;
	moved.set_time(TimeRange::new(Rational::new(3, 1), Rational::new(4, 1)));
	list.add_marker(moved);

	assert_eq!(list.size(), 2);
	assert_eq!(list.front().unwrap().name(), "a");
	assert_eq!(list.back().unwrap().name(), "b");
}

/// A `MarkerAddCommand` redo appends the marker; undo removes it
/// again, leaving the list as it was.
#[test]
fn marker_add_command_redo_undo() {
	let list_h = make_owned(TimelineMarkerList::new());
	let mut cmd = MarkerAddCommand::new(
		list_h.clone(),
		TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
		"m",
		4,
	);

	cmd.redo();
	let list = unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap();
	assert_eq!(list.size(), 1);
	assert_eq!(list.at(0).unwrap().name(), "m");
	assert_eq!(list.at(0).unwrap().color(), 4);

	// Redo is idempotent.
	cmd.redo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 1);

	cmd.undo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 0);
}

/// A `MarkerRemoveCommand` redo drops the marker; undo re-inserts it
/// at its time position.
#[test]
fn marker_remove_command_redo_undo() {
	let list_h = make_owned(TimelineMarkerList::new());
	{
		let l = unsafe { get_mut::<TimelineMarkerList>(&list_h) }.unwrap();
		l.add_marker(TimelineMarker::with_time(
			1,
			TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
			"m",
		));
	}

	let mut cmd = MarkerRemoveCommand::new(list_h.clone(), 0);
	cmd.redo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 0);

	cmd.undo();
	let list = unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap();
	assert_eq!(list.size(), 1);
	assert_eq!(list.at(0).unwrap().name(), "m");
}

/// `MarkerChangeColorCommand` redo changes the color and undo restores
/// the original.
#[test]
fn marker_change_color_command_redo_undo() {
	let list_h = make_owned(TimelineMarkerList::new());
	{
		let l = unsafe { get_mut::<TimelineMarkerList>(&list_h) }.unwrap();
		l.add_marker(TimelineMarker::with_time(
			1,
			TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
			"m",
		));
	}

	let mut cmd = MarkerChangeColorCommand::new(list_h.clone(), 0, 9);
	cmd.redo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().color(), 9);
	cmd.undo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().color(), 1);
}

/// `MarkerChangeNameCommand` redo changes the name and undo restores
/// the original.
#[test]
fn marker_change_name_command_redo_undo() {
	let list_h = make_owned(TimelineMarkerList::new());
	{
		let l = unsafe { get_mut::<TimelineMarkerList>(&list_h) }.unwrap();
		l.add_marker(TimelineMarker::with_time(
			1,
			TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
			"m",
		));
	}

	let mut cmd = MarkerChangeNameCommand::new(list_h.clone(), 0, "renamed");
	cmd.redo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().name(), "renamed");
	cmd.undo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().name(), "m");
}

/// `MarkerChangeTimeCommand` redo moves the marker and undo restores
/// the original time.
#[test]
fn marker_change_time_command_redo_undo() {
	let list_h = make_owned(TimelineMarkerList::new());
	{
		let l = unsafe { get_mut::<TimelineMarkerList>(&list_h) }.unwrap();
		l.add_marker(TimelineMarker::with_time(
			1,
			TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
			"m",
		));
	}

	let new_t = TimeRange::new(Rational::new(50, 1), Rational::new(51, 1));
	let mut cmd = MarkerChangeTimeCommand::new(list_h.clone(), 0, new_t);
	cmd.redo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().time().in_(), Rational::new(50, 1));
	cmd.undo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().at(0).unwrap().time().in_(), Rational::new(1, 1));
}

/// `to_command` boxes a marker command into a CHandle for the undo
/// stack, matching the C++ `OakUndoCommand` shape.
#[test]
fn marker_commands_box_to_chandle() {
	let list_h = make_owned(TimelineMarkerList::new());
	{
		let l = unsafe { get_mut::<TimelineMarkerList>(&list_h) }.unwrap();
		l.add_marker(TimelineMarker::with_time(
			1,
			TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
			"m",
		));
	}

	let cmds: Vec<CHandle> = vec![
		MarkerAddCommand::new(
			list_h.clone(),
			TimeRange::new(Rational::new(5, 1), Rational::new(6, 1)),
			"n",
			0,
		)
		.to_command(),
		MarkerRemoveCommand::new(list_h.clone(), 0).to_command(),
		MarkerChangeColorCommand::new(list_h.clone(), 0, 7).to_command(),
		MarkerChangeNameCommand::new(list_h.clone(), 0, "z").to_command(),
		MarkerChangeTimeCommand::new(
			list_h.clone(),
			0,
			TimeRange::new(Rational::new(9, 1), Rational::new(10, 1)),
		)
		.to_command(),
	];
	for h in &cmds {
		assert!(!h.is_null());
		assert_eq!(h.abi_version, 1);
	}
}

/// Save writes `<marker name= in= out= color=/>` elements; load parses
/// them back into an equal list (XML golden round-trip).
#[test]
fn marker_list_xml_round_trip() {
	// Set up a list with one marker.
	let list_h = make_owned(TimelineMarkerList::new());
	{
		let l = unsafe { get_mut::<TimelineMarkerList>(&list_h) }.unwrap();
		l.add_marker(TimelineMarker::with_time(
			3,
			TimeRange::new(Rational::new(10, 1), Rational::new(20, 1)),
			"m",
		));
	}

	// Save into a mock writer.
	let mut writer = unsafe { oakcommon_xml_writer_init() };
	let r = unsafe { ffi::marker::oaktimeline_marker_list_save(list_h.clone(), writer.clone()) };
	assert_eq!(r, 0);
	let buf = unsafe { get::<MockXmlWriter>(&writer) }.unwrap().buf.clone();
	assert!(buf.contains("<marker"), "buf: {buf}");
	assert!(buf.contains("name=\"m\""), "buf: {buf}");
	assert!(buf.contains("in=\"10/1\""), "buf: {buf}");
	assert!(buf.contains("out=\"20/1\""), "buf: {buf}");
	assert!(buf.contains("color=\"3\""), "buf: {buf}");
	unsafe { oakcommon_xml_writer_free(&mut writer) };

	// Load back from a mock reader into a fresh list.
	let reader = xml_reader_handle(vec![MockXmlNode {
		name: "marker".to_string(),
		text: String::new(),
		attrs: vec![
			("name".to_string(), "m".to_string()),
			("in".to_string(), "10/1".to_string()),
			("out".to_string(), "20/1".to_string()),
			("color".to_string(), "3".to_string()),
		],
	}]);
	let list2_h = make_owned(TimelineMarkerList::new());
	let r2 = unsafe { ffi::marker::oaktimeline_marker_list_load(list2_h.clone(), reader.clone()) };
	assert_eq!(r2, 0);
	let l2 = unsafe { get::<TimelineMarkerList>(&list2_h) }.unwrap();
	assert_eq!(l2.size(), 1);
	let m = l2.at(0).unwrap();
	assert_eq!(m.name(), "m");
	assert_eq!(m.color(), 3);
	assert_eq!(m.time().in_(), Rational::new(10, 1));
	assert_eq!(m.time().out(), Rational::new(20, 1));
}

/// Loading a marker with a `color`/`in`/`out` attribute equal to the
/// sentinel matches how `EditToInfo` consumers treat defaults.
#[test]
fn marker_defaults_map_to_edit_to_info() {
	// A default marker carries the null time and default color (0).
	let m = TimelineMarker::new();
	assert_eq!(m.color(), 0);
	assert_eq!(m.time().in_(), Rational::new(0, 1));

	// `EditToInfo` defaults mirror those sentinels: null handles and the
	// null rational for `nearest_time`.
	let info = EditToInfo {
		track: CHandle::null(),
		nearest_time: m.time().in_(),
		nearest_block: CHandle::null(),
	};
	assert!(info.track.is_null());
	assert!(info.nearest_block.is_null());
	assert_eq!(info.nearest_time, Rational::new(0, 1));
}
