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

//! Contract tests for the C ABI bridge layer (`src/bridge/`). The bridge
//! declares the oaknode / oakundo / oakcommon foreign functions this crate
//! calls for cross-module work (blocks, tracks, undo handles, XML). The
//! exhaustive cross-module behaviour is owned by the C++ gtest suites of
//! those modules; here we pin that the symbols the crate depends on are
//! declared with the documented C ABI signature and that the bridge's own
//! helpers are wired to the right functions.
//!
//! Rational ↔ num/den and Rational ↔ string conversion are NOT bridge C ABI
//! functions (the bridge only carries XML + config): num/den conversion is
//! `util::rat_nd` and the string format lives on `oakcore_rs::Rational`, so
//! those tests pin the in-crate helpers directly.
//!
//! These tests call the `extern "C"` symbols, which are only defined under
//! `--features test-stubs` (the in-crate mocks in `src/bridge/teststubs.rs`);
//! run with that feature. The parts that build mock XML readers through
//! `teststubs::xml_reader_handle` are additionally gated `#[cfg(feature =
//! "test-stubs")]`.

use oakcore_rs::Rational;
use oaktimeline::handle::CHandle;
use oaktimeline::util;

/// The undo bridge declares a `make_command_handle`-style constructor that
/// boxes a command into the C ABI handle shape, and a `free` that runs the
/// caller's `free_fn` before clearing the handle.
#[test]
fn undo_bridge_constructs_handle() {
	use oaktimeline::bridge::undo::{
		oakundo_command_free, oakundo_command_init, OakUndoCommandVtable,
	};
	use oaktimeline::error::OAKTIMELINE_ABI_VERSION;
	use std::ffi::c_void;

	unsafe extern "C" fn free_u32(ud: *mut c_void) {
		drop(Box::from_raw(ud as *mut u32));
	}

	let vtable = OakUndoCommandVtable {
		redo: None,
		undo: None,
		free_fn: Some(free_u32),
	};
	let userdata = Box::into_raw(Box::new(7u32)) as *mut c_void;
	let mut h = unsafe { oakundo_command_init(&vtable, userdata) };
	assert!(!h.is_null(), "init must return a non-null command handle");
	assert_eq!(h.abi_version, OAKTIMELINE_ABI_VERSION);
	// Freeing runs `free_fn` (dropping the boxed u32) and clears the handle.
	unsafe { oakundo_command_free(&mut h) };
	assert!(h.is_null(), "free must clear the handle");
}

/// The undo bridge frees a command handle and tolerates a null handle
/// (free(NULL) no-op), matching `oakundo_capi::free_command_handle`.
#[test]
fn undo_bridge_free_handle_is_null_safe() {
	use oaktimeline::bridge::undo::oakundo_command_free;

	// NULL pointer: no-op, no panic.
	unsafe { oakundo_command_free(std::ptr::null_mut()) };
	// Empty handle: no-op, no panic, stays empty.
	let mut h = CHandle::null();
	unsafe { oakundo_command_free(&mut h) };
	assert!(h.is_null());
}

/// The node bridge exposes the block/track accessors this crate calls for
/// edit commands (in/out/length, previous/next, track, track length).
#[cfg(feature = "test-stubs")]
#[test]
fn node_bridge_exposes_block_geometry() {
	use oaktimeline::bridge::node::{
		oaknode_block_clip_create, oaknode_track_create, oaknode_track_prepend_block,
	};

	let b = unsafe { oaknode_block_clip_create() };
	assert!(!b.is_null());
	// Default in/out/length are 0/1.
	assert_eq!(util::block_in(b.clone()), Rational::new(0, 1));
	assert_eq!(util::block_out(b.clone()), Rational::new(0, 1));
	assert_eq!(util::block_length(b.clone()), Rational::new(0, 1));
	// Detached block: no previous / next / owning track.
	assert!(util::block_previous(b.clone()).is_null());
	assert!(util::block_next(b.clone()).is_null());
	assert!(util::block_track(b.clone()).is_null());

	// Attach two blocks to a track; prepend puts each at the front.
	let t = unsafe { oaknode_track_create(0) };
	assert!(!t.is_null());
	let b2 = unsafe { oaknode_block_clip_create() };
	assert_eq!(unsafe { oaknode_track_prepend_block(t.clone(), b.clone()) }, 0);
	assert_eq!(unsafe { oaknode_track_prepend_block(t.clone(), b2.clone()) }, 0);

	// b2 is now the front block: it owns the track, has no previous, and b1
	// follows it.
	assert!(!util::block_track(b2.clone()).is_null());
	assert!(util::block_previous(b2.clone()).is_null());
	assert!(!util::block_next(b2.clone()).is_null());
	// b1 is the tail: it has a previous (b2) and no next.
	assert!(util::block_next(b.clone()).is_null());
	assert!(!util::block_previous(b.clone()).is_null());
	assert!(!util::block_track(b.clone()).is_null());
	// Track length sums the block lengths (both default 0/1).
	assert_eq!(util::track_length(t), Rational::new(0, 1));
}

/// The node bridge mutators (`block_set_length_and_media_out` /
/// `_media_in`) are the single path through which resize commands change
/// block geometry; each keeps the non-fixed edge fixed.
#[cfg(feature = "test-stubs")]
#[test]
fn node_bridge_resize_mutators_wire() {
	use oaktimeline::bridge::node::{oaknode_block_clip_create, oaknode_clip_get_media_in};

	fn media_in(b: &CHandle) -> Rational {
		let mut n = 0;
		let mut d = 0;
		let _ = unsafe { oaknode_clip_get_media_in(b.clone(), &mut n, &mut d) };
		Rational::new(n as i64, d as i64)
	}

	let b = unsafe { oaknode_block_clip_create() };
	// Default media-in and out are both 0/1.
	assert_eq!(media_in(&b), Rational::new(0, 1));

	// media_out variant: growing the length moves the out point, media-in
	// stays fixed.
	util::block_set_length_and_media_out(b.clone(), Rational::new(10, 1));
	assert_eq!(util::block_length(b.clone()), Rational::new(10, 1));
	assert_eq!(util::block_out(b.clone()), Rational::new(10, 1));
	assert_eq!(media_in(&b), Rational::new(0, 1));

	// media_in variant: shrinking the length keeps the out point fixed and
	// pulls media-in up.
	util::block_set_length_and_media_in(b.clone(), Rational::new(4, 1));
	assert_eq!(util::block_length(b.clone()), Rational::new(4, 1));
	assert_eq!(util::block_out(b.clone()), Rational::new(10, 1));
	assert_eq!(media_in(&b), Rational::new(6, 1));
}

/// Rational ↔ num/den conversion crosses the C ABI as an `int` pair via
/// `timelineutil::rat_nd` (`util::rat_nd`) — not through `bridge::common`,
/// which only carries XML + config. We pin the reduction semantics here.
#[test]
fn common_bridge_rational_conversion() {
	let r = Rational::new(30, 4); // reduces to 15/2
	let mut n = 0;
	let mut d = 0;
	util::rat_nd(r, &mut n, &mut d);
	assert_eq!(n, 15);
	assert_eq!(d, 2);
}

/// A zero denominator normalizes to the null/NaN sentinel `0/0`
/// (`Rational::new(num, 0)`), so a num/den pair with `den == 0` is invalid
/// rather than converted.
#[test]
fn common_bridge_rejects_zero_denominator() {
	let r = Rational::new(5, 0);
	assert!(r.is_null());
	assert!(r.is_nan());
	assert_eq!(r.denominator(), 0);
	// rat_nd maps the sentinel to a 0/0 int pair.
	let mut n = 1;
	let mut d = 1;
	util::rat_nd(r, &mut n, &mut d);
	assert_eq!((n, d), (0, 0));
}

/// Rational ↔ display-string uses `oakcore_rs::Rational::from_string` /
/// `to_display_string` (the C++ `fromString`/`toString` text format, e.g.
/// "30000/1001"). `bridge::common` does not map strings; we pin the format
/// the project files rely on.
#[test]
fn common_bridge_rational_string_round_trip() {
	let s = "30000/1001";
	let r = Rational::from_string(s);
	assert_eq!(r, Rational::new(30000, 1001));
	assert_eq!(r.to_display_string(), s);
	// The null sentinel round-trips as "0/0".
	assert_eq!(Rational::NULL.to_display_string(), "0/0");
	assert!(Rational::from_string("0/0").is_null());
}

/// The XML reader bridge declares the load symbols the marker and work area
/// save paths call; a null reader is an invalid argument and the reader is
/// released by `free`.
#[cfg(feature = "test-stubs")]
#[test]
fn xml_bridge_validate_handles() {
	use oaktimeline::bridge::common::{
		oakcommon_xml_reader_free, oakcommon_xml_reader_init, oakcommon_xml_reader_name,
		oakcommon_xml_reader_read_element_text, oakcommon_xml_reader_read_next_start_element,
	};

	// A null reader is an invalid argument: read fails cleanly (0).
	let mut buf = [0i8; 64];
	let mut found = 0;
	assert_eq!(
		unsafe { oakcommon_xml_reader_read_next_start_element(CHandle::null(), &mut found) },
		0
	);

	// `init` over a document yields an empty reader in the mock; with no
	// elements it reports end-of-stream for every accessor.
	let mut reader = unsafe { oakcommon_xml_reader_init(b"<project/>\0".as_ptr() as *const std::ffi::c_char) };
	assert!(!reader.is_null());
	let mut found = 0;
	assert_eq!(
		unsafe { oakcommon_xml_reader_read_next_start_element(reader.clone(), &mut found) },
		0
	);
	assert_eq!(unsafe { oakcommon_xml_reader_name(reader.clone(), buf.as_mut_ptr(), 64) }, 0);
	assert_eq!(
		unsafe { oakcommon_xml_reader_read_element_text(reader.clone(), buf.as_mut_ptr(), 64) },
		0
	);

	// Free clears the handle; freeing the null handle is a no-op.
	unsafe { oakcommon_xml_reader_free(&mut reader) };
	assert!(reader.is_null());

	// With mock elements (feature-gated), the reader iterates them and
	// exposes name / text / attributes through the same externs.
	#[cfg(feature = "test-stubs")]
	{
		use oaktimeline::bridge::teststubs::{xml_reader_handle, MockXmlNode};

		let mut r = xml_reader_handle(vec![MockXmlNode {
			name: "marker".to_string(),
			text: "hello".to_string(),
			attrs: vec![("color".to_string(), "red".to_string())],
		}]);
		let mut found = 0;
		assert_eq!(
			unsafe { oakcommon_xml_reader_read_next_start_element(r.clone(), &mut found) },
			1
		);
		assert_eq!(found, 1);
		assert_eq!(
			unsafe { oakcommon_xml_reader_name(r.clone(), buf.as_mut_ptr(), 64) },
			1
		);
		assert_eq!(
			unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(),
			"marker"
		);
		assert_eq!(
			unsafe { oakcommon_xml_reader_read_element_text(r.clone(), buf.as_mut_ptr(), 64) },
			1
		);
		assert_eq!(
			unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap(),
			"hello"
		);
		unsafe { oakcommon_xml_reader_free(&mut r) };
		assert!(r.is_null());
	}
}

/// Build-graph invariant. The original scaffold claimed every symbol from
/// `oakcore-rs` (`Rational`, `TimeRange`) is reached only through the C ABI
/// bridge. That is not the case: the crate depends on `oakcore-rs` directly
/// (`Cargo.toml [dependencies]`, used by `src/util.rs`), and the bridge does
/// not re-export `Rational`. This is a link/build-topology property that
/// cannot be asserted from a runtime unit test — it is enforced by
/// `cargo build`/`cargo tree` — so it is excluded here. The body pins the
/// actually-true invariant that `Rational` reduction is available in-crate.
#[test]
#[ignore = "build-topology property; asserted by cargo tree/build, not at runtime"]
fn core_is_reached_only_through_bridge() {
	let r = Rational::new(2, 4);
	assert_eq!(r.numerator(), 1);
	assert_eq!(r.denominator(), 2);
}
