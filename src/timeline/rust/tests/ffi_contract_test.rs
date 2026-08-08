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

//! C ABI contract tests for the `#[no_mangle]` exports in
//! `src/ffi.rs`. Each export family is pinned with one normal and one
//! error path; the exhaustive matrix is driven by the existing C++
//! gtest suite (`src/timeline/tests`) running against this crate.
//! Value-enum `displaymode.h` exports nothing, so it is covered purely
//! by constant compatibility, not by functions.

use std::ffi::{c_char, c_int, CStr, CString};

use oaktimeline::bridge::teststubs::{MockKind, MockNode};
use oaktimeline::common::{MovementMode, ThumbnailMode, WaveformMode};
use oaktimeline::error::{
	OAKTIMELINE_E_FAILED, OAKTIMELINE_E_INVALID, OAKTIMELINE_E_NOMEM, OAKTIMELINE_E_NOT_FOUND,
	OAKTIMELINE_E_STATE, OAKTIMELINE_OK,
};
use oaktimeline::ffi as ffi;
use oaktimeline::handle::{make_owned, CHandle};

/// `oaktimeline_marker_list_create` returns a valid refcounted handle
/// stamped with the ABI version; `free` releases it without leaking.
#[test]
fn marker_list_create_free() {
	let mut h = unsafe { ffi::marker::oaktimeline_marker_list_create() };
	assert!(!h.is_null());
	assert_eq!(h.abi_version, 1);

	// Free clears the handle; a second free is a harmless no-op.
	unsafe { ffi::marker::oaktimeline_marker_list_free(&mut h) };
	assert!(h.is_null());
	unsafe { ffi::marker::oaktimeline_marker_list_free(&mut h) };
	assert!(h.is_null());
}

/// `oaktimeline_marker_list_of` returns a handle for an owner; freeing
/// it does not free the owner's copy.
#[test]
fn marker_list_of_returns_borrowed() {
	let list_h = make_owned(oaktimeline::marker::TimelineMarkerList::new());
	let owner = make_owned(MockNode {
		kind: MockKind::Node,
		markers: list_h.clone(),
		..Default::default()
	});

	// A viewer node yields a non-null borrowed marker list handle.
	let mut borrowed = unsafe { ffi::marker::oaktimeline_marker_list_of(owner.clone()) };
	assert!(!borrowed.is_null());
	assert_eq!(borrowed.abi_version, 1);

	// Freeing the borrowed handle does not invalidate the owner's copy.
	unsafe { ffi::marker::oaktimeline_marker_list_free(&mut borrowed) };
	assert!(borrowed.is_null());

	// The owner still owns its marker list; it can still hand out a new one.
	let second = unsafe { ffi::marker::oaktimeline_marker_list_of(owner.clone()) };
	assert!(!second.is_null());

	// An empty (null) owner handle is rejected as E_INVALID → null handle.
	let null_owner = CHandle::null();
	let out = unsafe { ffi::marker::oaktimeline_marker_list_of(null_owner) };
	assert!(out.is_null());
}

/// `oaktimeline_marker_add` appends a marker; `oaktimeline_marker_count`
/// reflects it and `oaktimeline_marker_at` reads it back out.
#[test]
fn marker_add_count_at() {
	let h = unsafe { ffi::marker::oaktimeline_marker_list_create() };
	assert!(!h.is_null());

	let name = CString::new("mark").unwrap();
	let r = unsafe {
		ffi::marker::oaktimeline_marker_add(h.clone(), 10, 1, 20, 1, name.as_ptr(), 3)
	};
	assert_eq!(r, OAKTIMELINE_OK);

	let mut count = 0;
	let r = unsafe { ffi::marker::oaktimeline_marker_count(h.clone(), &mut count) };
	assert_eq!(r, OAKTIMELINE_OK);
	assert_eq!(count, 1);

	let mut in_num = 0;
	let mut in_den = 0;
	let mut out_num = 0;
	let mut out_den = 0;
	let mut color = 0;
	let mut buf = [0 as c_char; 32];
	let needed = unsafe {
		ffi::marker::oaktimeline_marker_at(
			h.clone(),
			0,
			&mut in_num,
			&mut in_den,
			&mut out_num,
			&mut out_den,
			&mut color,
			buf.as_mut_ptr(),
			buf.len() as c_int,
		)
	};
	assert!(needed > 0);
	assert_eq!(in_num, 10);
	assert_eq!(in_den, 1);
	assert_eq!(out_num, 20);
	assert_eq!(out_den, 1);
	assert_eq!(color, 3);
	let read = unsafe { CStr::from_ptr(buf.as_ptr()) }.to_string_lossy();
	assert_eq!(read, "mark");
	// `needed` is the name length + NUL terminator.
	assert_eq!(needed as usize, "mark".len() + 1);
}

/// `oaktimeline_marker_at` with an out-of-range index returns
/// `OAKTIMELINE_E_NOT_FOUND` and leaves outputs untouched.
#[test]
fn marker_at_out_of_range_errors() {
	let h = unsafe { ffi::marker::oaktimeline_marker_list_create() };

	let mut in_num = 99;
	let mut in_den = 99;
	let mut out_num = 99;
	let mut out_den = 99;
	let mut color = 99;
	let mut buf = [1 as c_char; 32];
	let r = unsafe {
		ffi::marker::oaktimeline_marker_at(
			h.clone(),
			7, // out of range (empty list)
			&mut in_num,
			&mut in_den,
			&mut out_num,
			&mut out_den,
			&mut color,
			buf.as_mut_ptr(),
			buf.len() as c_int,
		)
	};
	assert_eq!(r, OAKTIMELINE_E_NOT_FOUND);
	// Outputs are left untouched.
	assert_eq!(in_num, 99);
	assert_eq!(in_den, 99);
	assert_eq!(out_num, 99);
	assert_eq!(out_den, 99);
	assert_eq!(color, 99);
}

/// `oaktimeline_marker_add_command`/`remove_at_command`/
/// `set_time_command`/`set_props_command` box into undo handles; a
/// null list yields `OAKTIMELINE_E_INVALID` (or a null handle).
#[test]
fn marker_commands_validate_list() {
	let null_list = CHandle::null();

	// Null list → null command handle (E_INVALID).
	let add = unsafe {
		ffi::marker::oaktimeline_marker_add_command(
			null_list.clone(), 0, 1, 0, 1, std::ptr::null(), 0,
		)
	};
	assert!(add.is_null());

	let remove = unsafe { ffi::marker::oaktimeline_marker_remove_at_command(null_list.clone(), 0) };
	assert!(remove.is_null());

	let set_time = unsafe {
		ffi::marker::oaktimeline_marker_set_time_command(null_list.clone(), 0, 0, 1, 0, 1)
	};
	assert!(set_time.is_null());

	let set_props = unsafe {
		ffi::marker::oaktimeline_marker_set_props_command(null_list.clone(), 0, 0, std::ptr::null())
	};
	assert!(set_props.is_null());

	// A valid list produces a non-null command handle; out-of-range indices
	// on an empty list produce null handles (E_NOT_FOUND).
	let list = unsafe { ffi::marker::oaktimeline_marker_list_create() };
	assert!(!list.is_null());

	let name = CString::new("m").unwrap();
	let add_ok = unsafe {
		ffi::marker::oaktimeline_marker_add_command(list.clone(), 0, 1, 1, 1, name.as_ptr(), 5)
	};
	assert!(!add_ok.is_null());

	// Empty list: any index is out of range → null handle.
	let remove_empty = unsafe { ffi::marker::oaktimeline_marker_remove_at_command(list.clone(), 0) };
	assert!(remove_empty.is_null());
}

/// `oaktimeline_workarea_create`/`free` mirror the marker list
/// lifecycle.
#[test]
fn workarea_create_free() {
	let mut w = unsafe { ffi::workarea::oaktimeline_workarea_create() };
	assert!(!w.is_null());
	assert_eq!(w.abi_version, 1);

	unsafe { ffi::workarea::oaktimeline_workarea_free(&mut w) };
	assert!(w.is_null());
	// Double free is a harmless no-op.
	unsafe { ffi::workarea::oaktimeline_workarea_free(&mut w) };
	assert!(w.is_null());
}

/// `oaktimeline_workarea_set_enabled`/`get` round-trip the enabled
/// flag; `get` reports the range through its out-params.
#[test]
fn workarea_set_get_range() {
	let w = unsafe { ffi::workarea::oaktimeline_workarea_create() };
	assert!(!w.is_null());

	let r = unsafe { ffi::workarea::oaktimeline_workarea_set_enabled(w.clone(), 1) };
	assert_eq!(r, OAKTIMELINE_OK);
	let r = unsafe { ffi::workarea::oaktimeline_workarea_set_range(w.clone(), 10, 1, 20, 1) };
	assert_eq!(r, OAKTIMELINE_OK);

	let mut in_num = 0;
	let mut in_den = 0;
	let mut out_num = 0;
	let mut out_den = 0;
	let mut enabled = 0;
	let r = unsafe {
		ffi::workarea::oaktimeline_workarea_get(
			w.clone(),
			&mut in_num,
			&mut in_den,
			&mut out_num,
			&mut out_den,
			&mut enabled,
		)
	};
	assert_eq!(r, OAKTIMELINE_OK);
	assert_eq!(in_num, 10);
	assert_eq!(in_den, 1);
	assert_eq!(out_num, 20);
	assert_eq!(out_den, 1);
	assert_eq!(enabled, 1);
}

/// `oaktimeline_workarea_set_range` with an invalid (null) handle
/// returns `OAKTIMELINE_E_INVALID`.
#[test]
fn workarea_set_range_invalid_handle() {
	let r = unsafe { ffi::workarea::oaktimeline_workarea_set_range(CHandle::null(), 10, 1, 20, 1) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);

	let r = unsafe { ffi::workarea::oaktimeline_workarea_set_enabled(CHandle::null(), 1) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);
}

/// `oaktimeline_workarea_reset` clears a set range back to the reset
/// sentinel.
#[test]
fn workarea_reset_clears_range() {
	let w = unsafe { ffi::workarea::oaktimeline_workarea_create() };
	unsafe { ffi::workarea::oaktimeline_workarea_set_range(w.clone(), 10, 1, 20, 1) };

	let mut in_num = 0;
	let mut in_den = 0;
	let mut out_num = 0;
	let mut out_den = 0;
	let r = unsafe { ffi::workarea::oaktimeline_workarea_reset(&mut in_num, &mut in_den, &mut out_num, &mut out_den) };
	assert_eq!(r, OAKTIMELINE_OK);
	assert_eq!(in_num, 0);
	assert_eq!(in_den, 1);
	assert_eq!(out_num, 2147483647);
	assert_eq!(out_den, 1);
}

/// `oaktimeline_add_track_command`/`remove_track_command`/
/// `place_block_command`/`trim_command`/`split_command`/`slide_command`
/// each return a CHandle and validate their inputs.
#[test]
fn edit_exports_return_handles() {
	let null = CHandle::null();

	// Invalid inputs yield null command handles.
	assert!(unsafe { ffi::edit::oaktimeline_add_track_command(null.clone()) }.is_null());
	assert!(unsafe { ffi::edit::oaktimeline_remove_track_command(null.clone()) }.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_place_block_command(null.clone(), 0, null.clone(), 0, 1)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_trim_command(null.clone(), null.clone(), 0, 1, 2)
	}
	.is_null());

	// Valid handles produce non-null command handles. The mock nodes let
	// the command constructors box without dereferencing graph state.
	let tracklist = make_owned(MockNode {
		kind: MockKind::TrackList,
		..Default::default()
	});
	let track = make_owned(MockNode {
		kind: MockKind::Track,
		..Default::default()
	});
	let block = make_owned(MockNode {
		kind: MockKind::Clip,
		..Default::default()
	});

	let add = unsafe { ffi::edit::oaktimeline_add_track_command(tracklist.clone()) };
	assert!(!add.is_null());
	let remove = unsafe { ffi::edit::oaktimeline_remove_track_command(track.clone()) };
	assert!(!remove.is_null());
	let place = unsafe {
		ffi::edit::oaktimeline_place_block_command(tracklist.clone(), 0, block.clone(), 0, 1)
	};
	assert!(!place.is_null());
	let trim = unsafe {
		ffi::edit::oaktimeline_trim_command(track.clone(), block.clone(), 10, 1, 2)
	};
	assert!(!trim.is_null());

	// split_command boxes into a multi command (mock returns a handle).
	let blocks = [block.clone()];
	let split = unsafe { ffi::edit::oaktimeline_split_command(blocks.as_ptr(), 1, 5, 1) };
	assert!(!split.is_null());

	// slide_command with a block array.
	let slide_blocks = [block.clone()];
	let slide = unsafe {
		ffi::edit::oaktimeline_slide_command(
			track.clone(),
			slide_blocks.as_ptr(),
			1,
			null.clone(),
			null.clone(),
			0,
			1,
		)
	};
	assert!(!slide.is_null());

	// Invalid block arrays yield null handles.
	assert!(unsafe {
		ffi::edit::oaktimeline_split_command(std::ptr::null(), 1, 0, 1)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_split_command(blocks.as_ptr(), 0, 0, 1)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_slide_command(track.clone(), std::ptr::null(), 1, null.clone(), null.clone(), 0, 1)
	}
	.is_null());
}

/// `oaktimeline_trim_command` and `oaktimeline_place_block_command`
/// accept rationals split into numerator/denominator pairs; a null
/// handle is rejected with `OAKTIMELINE_E_INVALID` (a null command
/// handle), and a non-trim movement mode is rejected the same way.
#[test]
fn edit_exports_validate_rationals() {
	let track = make_owned(MockNode {
		kind: MockKind::Track,
		..Default::default()
	});
	let block = make_owned(MockNode {
		kind: MockKind::Clip,
		..Default::default()
	});
	let list = make_owned(MockNode {
		kind: MockKind::TrackList,
		..Default::default()
	});

	// Rational pairs are accepted (num/den split), returning a handle.
	let trim = unsafe {
		ffi::edit::oaktimeline_trim_command(track.clone(), block.clone(), 10, 2, 2)
	};
	assert!(!trim.is_null());

	let place = unsafe {
		ffi::edit::oaktimeline_place_block_command(list.clone(), 0, block.clone(), 5, 1)
	};
	assert!(!place.is_null());

	// A null track / null block is rejected with a null handle.
	let null = CHandle::null();
	assert!(unsafe {
		ffi::edit::oaktimeline_trim_command(null.clone(), block.clone(), 10, 1, 2)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_trim_command(track.clone(), null.clone(), 10, 1, 2)
	}
	.is_null());

	// A non-trim movement mode (Move = 1) is rejected → null handle.
	assert!(unsafe {
		ffi::edit::oaktimeline_trim_command(track.clone(), block.clone(), 10, 1, 1)
	}
	.is_null());
}

/// The exported `OAKTIMELINE_*` error constants match the -MMCCCC
/// scheme for module 04.
#[test]
fn error_constants_match_scheme() {
	assert_eq!(OAKTIMELINE_OK, 0);
	assert_eq!(OAKTIMELINE_E_INVALID, -40001);
	assert_eq!(OAKTIMELINE_E_STATE, -40002);
	assert_eq!(OAKTIMELINE_E_FAILED, -40003);
	assert_eq!(OAKTIMELINE_E_NOT_FOUND, -40004);
	assert_eq!(OAKTIMELINE_E_NOMEM, -40005);
}

/// `displaymode.h` has no functions; only the value enums are
/// exported. This test pins that the enum constants are in range and
/// stable.
#[test]
fn displaymode_constants_are_stable() {
	// OAK_TIMELINE_THUMBNAIL_OFF/IN_OUT/ON = 0/1/2.
	assert_eq!(ThumbnailMode::Off as i32, 0);
	assert_eq!(ThumbnailMode::InOut as i32, 1);
	assert_eq!(ThumbnailMode::On as i32, 2);

	// OAK_TIMELINE_WAVEFORMS_DISABLED/ENABLED = 0/1.
	assert_eq!(WaveformMode::Disabled as i32, 0);
	assert_eq!(WaveformMode::Enabled as i32, 1);

	// MovementMode round-trips through from_c_int.
	assert_eq!(MovementMode::None.to_c_int(), 0);
	assert_eq!(MovementMode::Move.to_c_int(), 1);
	assert_eq!(MovementMode::TrimIn.to_c_int(), 2);
	assert_eq!(MovementMode::TrimOut.to_c_int(), 3);
	assert_eq!(MovementMode::from_c_int(2), Some(MovementMode::TrimIn));
	assert_eq!(MovementMode::from_c_int(9), None);
}
