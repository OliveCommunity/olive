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

//! Coverage for `src/ffi.rs`: the C ABI export layer (`marker`, `workarea`
//! and `edit` submodules). These tests drive every reachable branch of the
//! export bodies — both the success paths (constructing command handles,
//! enumerating markers/work areas) and the error paths (null / out-of-range /
//! wrong inputs mapping to `Error::Invalid` / `Error::NotFound`).
//!
//! A few branches are unreachable through the public API with the test-stub
//! mocks and are deliberately skipped (see the module summary):
//!   * `marker_list_of` / `workarea_of` error branch (the mock
//!     `oaknode_node_get_markers` / `oaknode_node_get_work_area` only fail on
//!     a null `ctx`, which is already rejected earlier in the export);
//!   * `marker_set_props_command` / `split_command` "multi init failed" branch
//!     (`oakundo_command_init_multi` never returns null in the mock);
//!   * `write_cstr` NULL/size<=0 guard (only ever called with a non-NULL
//!     buffer and `size >= name.len()+1`);
//!   * `xml_element_text` empty-result branch (only reached inside
//!     `workarea_load`, whose loop stops before calling it with an invalid
//!     reader).
#![cfg(feature = "test-stubs")]

use std::ffi::c_char;
use std::ffi::CString;

use oaktimeline::bridge::node as onode;
use oaktimeline::bridge::teststubs::{xml_reader_handle, MockKind, MockNode, MockXmlNode};
use oaktimeline::error::{OAKTIMELINE_E_INVALID, OAKTIMELINE_OK};
use oaktimeline::ffi::{edit, marker, workarea};
use oaktimeline::handle::{make_owned, CHandle};

// ---- helpers ---------------------------------------------------------

/// A NUL-terminated C string that lives for the call.
fn cstr(s: &str) -> CString {
	CString::new(s).unwrap()
}

/// A new detached video track (type 0).
fn mk_track() -> CHandle {
	unsafe { onode::oaknode_track_create(0) }
}

/// A new clip block.
fn mk_clip() -> CHandle {
	unsafe { onode::oaknode_block_clip_create() }
}

/// A new, empty owning marker list.
fn mk_marker_list() -> CHandle {
	unsafe { marker::oaktimeline_marker_list_create() }
}

/// A new, empty owning work area.
fn mk_workarea() -> CHandle {
	unsafe { workarea::oaktimeline_workarea_create() }
}

/// A new, empty owning track list of video type.
fn mk_track_list() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::TrackList,
		track_type: 0,
		..Default::default()
	})
}

/// A new, empty owning sequence.
fn mk_sequence() -> CHandle {
	make_owned(MockNode {
		kind: MockKind::Sequence,
		..Default::default()
	})
}

/// Add one marker to `list` (direct, no command) at in=1/1 out=5/1, color 3.
fn add_single_marker(list: &CHandle) {
	let name = cstr("m1");
	let r = unsafe { marker::oaktimeline_marker_add(list.clone(), 1, 1, 5, 1, name.as_ptr(), 3) };
	assert_eq!(r, OAKTIMELINE_OK);
}

// ---- marker module ---------------------------------------------------

/// `oaktimeline_marker_list_create`/`marker_add`/`marker_count`/`marker_at`
/// success paths.
#[test]
fn marker_create_add_count_at() {
	let list = mk_marker_list();
	add_single_marker(&list);

	let mut count = 0;
	let r = unsafe { marker::oaktimeline_marker_count(list.clone(), &mut count) };
	assert_eq!(r, OAKTIMELINE_OK);
	assert_eq!(count, 1);

	let mut in_num = 0;
	let mut in_den = 0;
	let mut out_num = 0;
	let mut out_den = 0;
	let mut color = 0;
	let mut name_buf = [0 as c_char; 64];
	let needed = unsafe {
		marker::oaktimeline_marker_at(
			list.clone(),
			0,
			&mut in_num,
			&mut in_den,
			&mut out_num,
			&mut out_den,
			&mut color,
			name_buf.as_mut_ptr(),
			name_buf.len() as i32,
		)
	};
	assert!(needed > 0, "two-stage name length must be positive");
	assert_eq!(in_num, 1);
	assert_eq!(out_num, 5);
	assert_eq!(color, 3);
}

/// Null-list error paths for `marker_add`/`marker_count`/`marker_at`.
#[test]
fn marker_null_list_errors() {
	let name = cstr("x");
	let r =
		unsafe { marker::oaktimeline_marker_add(CHandle::null(), 1, 1, 5, 1, name.as_ptr(), 0) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);

	let mut count = 0;
	let r = unsafe { marker::oaktimeline_marker_count(CHandle::null(), &mut count) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);

	let mut out = 0;
	let r = unsafe {
		marker::oaktimeline_marker_at(
			CHandle::null(),
			0,
			&mut out,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			0,
		)
	};
	assert_eq!(r, OAKTIMELINE_E_INVALID);
}

/// `oaktimeline_marker_remove_at_command` success path (in-bounds index).
#[test]
fn marker_remove_at_command_success() {
	let list = mk_marker_list();
	add_single_marker(&list);
	let h = unsafe { marker::oaktimeline_marker_remove_at_command(list.clone(), 0) };
	assert!(!h.is_null());
}

/// `oaktimeline_marker_set_time_command` success and out-of-range paths.
#[test]
fn marker_set_time_command() {
	let list = mk_marker_list();
	add_single_marker(&list);

	// Success: in-bounds index returns an owning command handle.
	let h = unsafe { marker::oaktimeline_marker_set_time_command(list.clone(), 0, 2, 1, 6, 1) };
	assert!(!h.is_null());

	// Out-of-range index -> NotFound.
	let h2 = unsafe { marker::oaktimeline_marker_set_time_command(list.clone(), 9, 2, 1, 6, 1) };
	assert!(h2.is_null());
}

/// `oaktimeline_marker_set_props_command` covers every branch: combined
/// color+name (multi command), color-only, name-only, the invalid
/// color<0&name==NULL case, and the out-of-range NotFound case.
#[test]
fn marker_set_props_command_branches() {
	let list = mk_marker_list();
	add_single_marker(&list);

	// Combined color + name -> multi command.
	let name = cstr("renamed");
	let h =
		unsafe { marker::oaktimeline_marker_set_props_command(list.clone(), 0, 7, name.as_ptr()) };
	assert!(!h.is_null());

	// Color-only (name == NULL) -> color child; also exercises the
	// `cstr_to_string(NULL)` empty-string path.
	let h = unsafe {
		marker::oaktimeline_marker_set_props_command(list.clone(), 0, 9, std::ptr::null())
	};
	assert!(!h.is_null());

	// Name-only (color < 0) -> name child.
	let name = cstr("onlyname");
	let h =
		unsafe { marker::oaktimeline_marker_set_props_command(list.clone(), 0, -1, name.as_ptr()) };
	assert!(!h.is_null());

	// color < 0 AND name == NULL -> Invalid.
	let h = unsafe {
		marker::oaktimeline_marker_set_props_command(list.clone(), 0, -1, std::ptr::null())
	};
	assert!(h.is_null());

	// Out-of-range index -> NotFound.
	let name = cstr("x");
	let h =
		unsafe { marker::oaktimeline_marker_set_props_command(list.clone(), 50, 1, name.as_ptr()) };
	assert!(h.is_null());
}

/// `oaktimeline_marker_list_of` with a null owner and with a viewer node
/// whose marker list is populated.
#[test]
fn marker_list_of() {
	// Null owner -> null handle.
	let h = unsafe { marker::oaktimeline_marker_list_of(CHandle::null()) };
	assert!(h.is_null());

	// A node carrying a marker list -> borrowed non-null handle.
	let inner = mk_marker_list();
	let owner = make_owned(MockNode {
		kind: MockKind::Node,
		markers: inner.clone(),
		..Default::default()
	});
	let h = unsafe { marker::oaktimeline_marker_list_of(owner.clone()) };
	assert!(!h.is_null());
}

/// `oaktimeline_marker_list_load` / `oaktimeline_marker_list_save` with
/// null arguments.
#[test]
fn marker_list_load_save_null() {
	let list = mk_marker_list();
	// Null list (reader also null) -> Invalid.
	let r = unsafe { marker::oaktimeline_marker_list_load(CHandle::null(), CHandle::null()) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);
	// Null list for save -> Invalid.
	let r = unsafe { marker::oaktimeline_marker_list_save(CHandle::null(), CHandle::null()) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);
	// Null reader / writer (valid list) -> Invalid.
	let r = unsafe { marker::oaktimeline_marker_list_load(list.clone(), CHandle::null()) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);
	let r = unsafe { marker::oaktimeline_marker_list_save(list.clone(), CHandle::null()) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);
}

/// `oaktimeline_marker_list_load` over a reader with a marker element and a
/// non-marker element (exercises the skip/`other` path).
#[test]
fn marker_list_load_reader() {
	let list = mk_marker_list();
	let reader = xml_reader_handle(vec![
		MockXmlNode {
			name: "other".into(),
			text: String::new(),
			attrs: vec![],
		},
		MockXmlNode {
			name: "marker".into(),
			text: String::new(),
			attrs: vec![("name".into(), "m2".into()), ("color".into(), "5".into())],
		},
	]);
	let r = unsafe { marker::oaktimeline_marker_list_load(list.clone(), reader.clone()) };
	assert_eq!(r, OAKTIMELINE_OK);

	let mut count = 0;
	unsafe { marker::oaktimeline_marker_count(list.clone(), &mut count) };
	assert_eq!(count, 1);
}

/// `oaktimeline_marker_list_save` over a writer with a marker present.
#[test]
fn marker_list_save_writer() {
	let list = mk_marker_list();
	add_single_marker(&list);
	let writer = unsafe { oaktimeline::bridge::common::oakcommon_xml_writer_init() };
	let r = unsafe { marker::oaktimeline_marker_list_save(list.clone(), writer.clone()) };
	assert_eq!(r, OAKTIMELINE_OK);
}

// ---- workarea module -------------------------------------------------

/// `oaktimeline_workarea_of` with a null owner and with a viewer node whose
/// work area is populated.
#[test]
fn workarea_of() {
	// Null owner -> null handle.
	let h = unsafe { workarea::oaktimeline_workarea_of(CHandle::null()) };
	assert!(h.is_null());

	// A node carrying a work area -> borrowed non-null handle.
	let inner = mk_workarea();
	let owner = make_owned(MockNode {
		kind: MockKind::Node,
		work_area: inner.clone(),
		..Default::default()
	});
	let h = unsafe { workarea::oaktimeline_workarea_of(owner.clone()) };
	assert!(!h.is_null());
}

/// `oaktimeline_workarea_get` null-handle error path.
#[test]
fn workarea_get_null() {
	let r = unsafe {
		workarea::oaktimeline_workarea_get(
			CHandle::null(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
		)
	};
	assert_eq!(r, OAKTIMELINE_E_INVALID);
}

/// `oaktimeline_workarea_set_range_command` null and valid paths.
#[test]
fn workarea_set_range_command() {
	// Null work area -> null handle.
	let h = unsafe {
		workarea::oaktimeline_workarea_set_range_command(CHandle::null(), 1, 1, 2, 1, 0, 1, 1, 1)
	};
	assert!(h.is_null());

	// Valid work area -> owning command handle.
	let w = mk_workarea();
	let h = unsafe {
		workarea::oaktimeline_workarea_set_range_command(w.clone(), 1, 1, 2, 1, 0, 1, 1, 1)
	};
	assert!(!h.is_null());
}

/// `oaktimeline_workarea_set_enabled_command` null and valid paths.
#[test]
fn workarea_set_enabled_command() {
	let h = unsafe { workarea::oaktimeline_workarea_set_enabled_command(CHandle::null(), 1) };
	assert!(h.is_null());

	let w = mk_workarea();
	let h = unsafe { workarea::oaktimeline_workarea_set_enabled_command(w.clone(), 1) };
	assert!(!h.is_null());
}

/// `oaktimeline_workarea_reset` null-pointer and success paths.
#[test]
fn workarea_reset() {
	// A null out pointer -> Invalid.
	let r = unsafe {
		workarea::oaktimeline_workarea_reset(
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
		)
	};
	assert_eq!(r, OAKTIMELINE_E_INVALID);

	// Success: all four out params are filled.
	let mut in_num = 0;
	let mut in_den = 0;
	let mut out_num = 0;
	let mut out_den = 0;
	let r = unsafe {
		workarea::oaktimeline_workarea_reset(&mut in_num, &mut in_den, &mut out_num, &mut out_den)
	};
	assert_eq!(r, OAKTIMELINE_OK);
	// reset sentinel: in = 0/1, out = RATIONAL_MAX (2147483647/1).
	assert_eq!(in_num, 0);
	assert_eq!(in_den, 1);
	assert_eq!(out_num, 2147483647);
	assert_eq!(out_den, 1);
}

/// `oaktimeline_workarea_load` null-path and the unknown-element skip branch.
#[test]
fn workarea_load() {
	// Null work area (and reader) -> Invalid.
	let r = unsafe { workarea::oaktimeline_workarea_load(CHandle::null(), CHandle::null()) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);

	// Null reader with a valid work area -> Invalid.
	let w = mk_workarea();
	let r = unsafe { workarea::oaktimeline_workarea_load(w.clone(), CHandle::null()) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);

	// A reader with an unknown element hits the else/skip branch.
	let reader = xml_reader_handle(vec![MockXmlNode {
		name: "bogus".into(),
		text: String::new(),
		attrs: vec![],
	}]);
	let r = unsafe { workarea::oaktimeline_workarea_load(w.clone(), reader.clone()) };
	assert_eq!(r, OAKTIMELINE_OK);
}

/// `oaktimeline_workarea_save` null-path and writer success path.
#[test]
fn workarea_save() {
	let r = unsafe { workarea::oaktimeline_workarea_save(CHandle::null(), CHandle::null()) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);

	let w = mk_workarea();
	let r = unsafe { workarea::oaktimeline_workarea_save(w.clone(), CHandle::null()) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);

	let writer = unsafe { oaktimeline::bridge::common::oakcommon_xml_writer_init() };
	let r = unsafe { workarea::oaktimeline_workarea_save(w.clone(), writer.clone()) };
	assert_eq!(r, OAKTIMELINE_OK);
}

// ---- edit module -----------------------------------------------------

/// `oaktimeline_replace_block_with_gap_command` null and valid paths.
#[test]
fn replace_block_with_gap_command() {
	// Null track -> null handle.
	let b = mk_clip();
	let h = unsafe { edit::oaktimeline_replace_block_with_gap_command(CHandle::null(), b.clone()) };
	assert!(h.is_null());
	// Null block -> null handle.
	let t = mk_track();
	let h = unsafe { edit::oaktimeline_replace_block_with_gap_command(t.clone(), CHandle::null()) };
	assert!(h.is_null());

	// Valid track + block -> owning command handle.
	let h = unsafe { edit::oaktimeline_replace_block_with_gap_command(t.clone(), b.clone()) };
	assert!(!h.is_null());
}

/// `oaktimeline_move_block_command` null and valid paths.
#[test]
fn move_block_command() {
	// Null list / block -> null handle.
	let list = mk_track_list();
	let b = mk_clip();
	let h = unsafe { edit::oaktimeline_move_block_command(CHandle::null(), 0, b.clone(), 10, 1) };
	assert!(h.is_null());
	let h =
		unsafe { edit::oaktimeline_move_block_command(list.clone(), 0, CHandle::null(), 10, 1) };
	assert!(h.is_null());

	// Valid list + block + in point -> owning command handle.
	let h = unsafe { edit::oaktimeline_move_block_command(list.clone(), 0, b.clone(), 10, 1) };
	assert!(!h.is_null());
}

/// `oaktimeline_split_preserving_links_command` null-arg error and success.
#[test]
fn split_preserving_links_command() {
	// Error: null blocks / zero counts.
	let h = unsafe {
		edit::oaktimeline_split_preserving_links_command(
			std::ptr::null(),
			1,
			std::ptr::null(),
			std::ptr::null(),
			1,
		)
	};
	assert!(h.is_null());

	// Error: blocks non-null but count <= 0.
	let blocks = [mk_clip()];
	let nums = [10i64];
	let dens = [1i64];
	let h = unsafe {
		edit::oaktimeline_split_preserving_links_command(
			blocks.as_ptr(),
			0,
			nums.as_ptr(),
			dens.as_ptr(),
			1,
		)
	};
	assert!(h.is_null());

	// Success.
	let h = unsafe {
		edit::oaktimeline_split_preserving_links_command(
			blocks.as_ptr(),
			1,
			nums.as_ptr(),
			dens.as_ptr(),
			1,
		)
	};
	assert!(!h.is_null());
}

/// `oaktimeline_ripple_delete_gaps_command` null-arg error and success.
#[test]
fn ripple_delete_gaps_command() {
	let seq = mk_sequence();
	let t = mk_track();
	let in_nums = [0i64];
	let in_dens = [1i64];
	let out_nums = [10i64];
	let out_dens = [1i64];
	let tracks = [t.clone()];

	// Error: null sequence.
	let h = unsafe {
		edit::oaktimeline_ripple_delete_gaps_command(
			CHandle::null(),
			in_nums.as_ptr(),
			in_dens.as_ptr(),
			out_nums.as_ptr(),
			out_dens.as_ptr(),
			tracks.as_ptr(),
			1,
		)
	};
	assert!(h.is_null());

	// Error: null arrays / range_count <= 0.
	let h = unsafe {
		edit::oaktimeline_ripple_delete_gaps_command(
			seq.clone(),
			std::ptr::null(),
			std::ptr::null(),
			std::ptr::null(),
			std::ptr::null(),
			tracks.as_ptr(),
			1,
		)
	};
	assert!(h.is_null());
	let h = unsafe {
		edit::oaktimeline_ripple_delete_gaps_command(
			seq.clone(),
			in_nums.as_ptr(),
			in_dens.as_ptr(),
			out_nums.as_ptr(),
			out_dens.as_ptr(),
			tracks.as_ptr(),
			0,
		)
	};
	assert!(h.is_null());

	// Success.
	let h = unsafe {
		edit::oaktimeline_ripple_delete_gaps_command(
			seq.clone(),
			in_nums.as_ptr(),
			in_dens.as_ptr(),
			out_nums.as_ptr(),
			out_dens.as_ptr(),
			tracks.as_ptr(),
			1,
		)
	};
	assert!(!h.is_null());
}

/// `oaktimeline_ripple_remove_area_command` null-track error and success.
#[test]
fn ripple_remove_area_command() {
	let h = unsafe { edit::oaktimeline_ripple_remove_area_command(CHandle::null(), 0, 1, 10, 1) };
	assert!(h.is_null());

	let t = mk_track();
	let h = unsafe { edit::oaktimeline_ripple_remove_area_command(t.clone(), 0, 1, 10, 1) };
	assert!(!h.is_null());
}

/// `oaktimeline_insert_gaps_command` null-list error and success.
#[test]
fn insert_gaps_command() {
	let h = unsafe { edit::oaktimeline_insert_gaps_command(CHandle::null(), 5, 1, 3, 1) };
	assert!(h.is_null());

	let list = mk_track_list();
	let h = unsafe { edit::oaktimeline_insert_gaps_command(list.clone(), 5, 1, 3, 1) };
	assert!(!h.is_null());
}
