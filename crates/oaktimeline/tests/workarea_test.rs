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

//! Contract tests for the work area domain (`src/workarea.rs`):
//! `TimelineWorkArea` value type, its two undo commands, and the
//! `reset_in`/`reset_out` sentinels. XML load/save matches the format
//! written by `timelineworkarea.cpp` (`<version=1>` attribute plus
//! `enabled`/`in`/`out` text elements).
#![cfg(feature = "test-stubs")]

use oakcore_rs::{Rational, TimeRange};
use oaktimeline::bridge::common::{oakcommon_xml_writer_free, oakcommon_xml_writer_init};
use oaktimeline::bridge::teststubs::{xml_reader_handle, MockXmlNode, MockXmlWriter};
use oaktimeline::ffi as ffi;
use oaktimeline::handle::{get, get_mut, make_owned};
use oaktimeline::undocommon::Command;
use oaktimeline::workarea::{
	reset_in, reset_out, TimelineWorkArea, WorkareaSetEnabledCommand, WorkareaSetRangeCommand,
};

/// `reset_in` is the null rational (0/1) marking an unset work area
/// start.
#[test]
fn reset_in_is_zero() {
	assert_eq!(reset_in(), Rational::new(0, 1));
}

/// `reset_out` is the RATIONAL_MAX sentinel (2147483647/1) marking an
/// unset work area end.
#[test]
fn reset_out_is_zero() {
	assert_eq!(reset_out(), Rational::new(2147483647, 1));
}

/// A default work area is disabled with the reset range
/// (0/1 .. 2147483647/1).
#[test]
fn workarea_default_is_disabled_null_range() {
	let wa = TimelineWorkArea::new();
	assert!(!wa.enabled());
	assert_eq!(wa.in_(), reset_in());
	assert_eq!(wa.out(), reset_out());
}

/// `set_enabled`/`enabled` toggle the enabled flag round-trip.
#[test]
fn workarea_enabled_toggle() {
	let mut wa = TimelineWorkArea::new();
	assert!(!wa.enabled());
	wa.set_enabled(true);
	assert!(wa.enabled());
	wa.set_enabled(false);
	assert!(!wa.enabled());
}

/// `set_range` stores a range and `range`/`in_`/`out`/`length` expose
/// it consistently.
#[test]
fn workarea_set_range_exposes_parts() {
	let mut wa = TimelineWorkArea::new();
	let r = TimeRange::new(Rational::new(10, 1), Rational::new(20, 1));
	wa.set_range(r);
	assert_eq!(*wa.range(), r);
	assert_eq!(wa.in_(), Rational::new(10, 1));
	assert_eq!(wa.out(), Rational::new(20, 1));
	assert_eq!(wa.length(), Rational::new(10, 1));
}

/// `set_range` stores the supplied range verbatim. `TimeRange::new`
/// normalizes (swaps) when `out < in` (C++ parity), so a "zero out"
/// range `(5, 0)` is stored normalized as `(0, 5)`; the C++ zero-out
/// guard lives in the facade layer, not in `set_range`.
#[test]
fn workarea_zero_out_resets_range() {
	let mut wa = TimelineWorkArea::new();
	let r = TimeRange::new(Rational::new(5, 1), Rational::new(0, 1));
	wa.set_range(r);
	// `TimeRange::new` swapped 0 < 5 before the value was stored.
	assert_eq!(wa.in_(), Rational::new(0, 1));
	assert_eq!(wa.out(), Rational::new(5, 1));
	assert_eq!(*wa.range(), r);
}

/// `WorkareaSetEnabledCommand` redo enables / undo restores the prior
/// flag.
#[test]
fn workarea_set_enabled_command_redo_undo() {
	let wa_h = make_owned(TimelineWorkArea::new());
	let mut cmd = WorkareaSetEnabledCommand::new(wa_h.clone(), true);
	cmd.redo();
	assert!(unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().enabled());
	cmd.undo();
	assert!(!unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().enabled());
}

/// `WorkareaSetRangeCommand` redo stores the new range and undo
/// restores the previous one.
#[test]
fn workarea_set_range_command_redo_undo() {
	let wa_h = make_owned(TimelineWorkArea::new());
	let new_range = TimeRange::new(Rational::new(10, 1), Rational::new(20, 1));
	let mut cmd = WorkareaSetRangeCommand::new(wa_h.clone(), new_range);
	cmd.redo();
	assert_eq!(*unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().range(), new_range);
	cmd.undo();
	// Undo restores the range captured at construction (the reset range).
	assert_eq!(unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().in_(), reset_in());
	assert_eq!(unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().out(), reset_out());
}

/// `to_command` boxes a work area command into a CHandle for the undo
/// stack.
#[test]
fn workarea_commands_box_to_chandle() {
	let wa_h = make_owned(TimelineWorkArea::new());
	let enabled_cmd = WorkareaSetEnabledCommand::new(wa_h.clone(), true).to_command();
	assert!(!enabled_cmd.is_null());
	assert_eq!(enabled_cmd.abi_version, 1);

	let range_cmd = WorkareaSetRangeCommand::new(
		wa_h.clone(),
		TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
	)
	.to_command();
	assert!(!range_cmd.is_null());
	assert_eq!(range_cmd.abi_version, 1);
}

/// `Command` trait dispatch routes through the same redo/undo bodies as
/// the inherent methods (used by the undo stack vtable).
#[test]
fn workarea_commands_trait_dispatch() {
	let wa_h = make_owned(TimelineWorkArea::new());

	let mut e = WorkareaSetEnabledCommand::new(wa_h.clone(), true);
	Command::redo(&mut e);
	assert!(unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().enabled());
	Command::undo(&mut e);
	assert!(!unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().enabled());

	let mut r = WorkareaSetRangeCommand::new(
		wa_h.clone(),
		TimeRange::new(Rational::new(3, 1), Rational::new(4, 1)),
	);
	Command::redo(&mut r);
	assert_eq!(unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().in_(), Rational::new(3, 1));
	Command::undo(&mut r);
	assert_eq!(unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().in_(), reset_in());
}

/// Save writes `<version=1>` plus `enabled`/`in`/`out` text elements;
/// load reads them back (XML golden round-trip).
#[test]
fn workarea_xml_round_trip() {
	// Set up an enabled work area with a known range.
	let wa_h = make_owned(TimelineWorkArea::new());
	{
		let wa = unsafe { get_mut::<TimelineWorkArea>(&wa_h) }.unwrap();
		wa.set_enabled(true);
		wa.set_range(TimeRange::new(Rational::new(10, 1), Rational::new(20, 1)));
	}

	// Save into a mock writer.
	let mut writer = unsafe { oakcommon_xml_writer_init() };
	let r = unsafe { ffi::workarea::oaktimeline_workarea_save(wa_h.clone(), writer.clone()) };
	assert_eq!(r, 0);
	let buf = unsafe { get::<MockXmlWriter>(&writer) }.unwrap().buf.clone();
	assert!(buf.contains("version=\"1\""), "buf: {buf}");
	assert!(buf.contains("<enabled>1</enabled>"), "buf: {buf}");
	assert!(buf.contains("<in>10/1</in>"), "buf: {buf}");
	assert!(buf.contains("<out>20/1</out>"), "buf: {buf}");
	unsafe { oakcommon_xml_writer_free(&mut writer) };

	// Load from a mock reader back into a fresh work area.
	let reader = xml_reader_handle(vec![
		MockXmlNode {
			name: "enabled".to_string(),
			text: "1".to_string(),
			attrs: Vec::new(),
		},
		MockXmlNode {
			name: "in".to_string(),
			text: "10/1".to_string(),
			attrs: Vec::new(),
		},
		MockXmlNode {
			name: "out".to_string(),
			text: "20/1".to_string(),
			attrs: Vec::new(),
		},
	]);
	let wa2_h = make_owned(TimelineWorkArea::new());
	let r2 = unsafe { ffi::workarea::oaktimeline_workarea_load(wa2_h.clone(), reader.clone()) };
	assert_eq!(r2, 0);
	let wa2 = unsafe { get::<TimelineWorkArea>(&wa2_h) }.unwrap();
	assert!(wa2.enabled());
	assert_eq!(wa2.in_(), Rational::new(10, 1));
	assert_eq!(wa2.out(), Rational::new(20, 1));
}

/// An unset work area serializes to `enabled=0` and round-trips as a
/// disabled, null-range work area.
#[test]
fn workarea_unset_round_trips_disabled() {
	let wa_h = make_owned(TimelineWorkArea::new());

	let mut writer = unsafe { oakcommon_xml_writer_init() };
	let r = unsafe { ffi::workarea::oaktimeline_workarea_save(wa_h.clone(), writer.clone()) };
	assert_eq!(r, 0);
	let buf = unsafe { get::<MockXmlWriter>(&writer) }.unwrap().buf.clone();
	assert!(buf.contains("<enabled>0</enabled>"), "buf: {buf}");
	unsafe { oakcommon_xml_writer_free(&mut writer) };

	let reader = xml_reader_handle(vec![MockXmlNode {
		name: "enabled".to_string(),
		text: "0".to_string(),
		attrs: Vec::new(),
	}]);
	let wa2_h = make_owned(TimelineWorkArea::new());
	let r2 = unsafe { ffi::workarea::oaktimeline_workarea_load(wa2_h.clone(), reader.clone()) };
	assert_eq!(r2, 0);
	let wa2 = unsafe { get::<TimelineWorkArea>(&wa2_h) }.unwrap();
	assert!(!wa2.enabled());
	assert_eq!(wa2.in_(), reset_in());
	assert_eq!(wa2.out(), reset_out());
}
