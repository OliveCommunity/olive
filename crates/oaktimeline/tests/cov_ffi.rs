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

//! Coverage for the remaining reachable paths of `src/ffi.rs`: the marker
//! command factories on valid/invalid lists, the work-area command
//! factories, the load/save XML paths, the edit-family command factories
//! and the string/`write_cstr` helpers. Run with `--features test-stubs`.
#![cfg(feature = "test-stubs")]

use std::ffi::{c_char, CString};

use oaktimeline::bridge::common::{
	oakcommon_xml_writer_init, oakcommon_xml_writer_write_attribute,
	oakcommon_xml_writer_write_end_element, oakcommon_xml_writer_write_start_element,
	oakcommon_xml_writer_write_text_element,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode, MockXmlNode, xml_reader_handle};
use oaktimeline::error::{
	OAKTIMELINE_E_FAILED, OAKTIMELINE_E_INVALID, OAKTIMELINE_E_NOT_FOUND, OAKTIMELINE_OK,
};
use oaktimeline::ffi as ffi;
use oaktimeline::handle::{CHandle, get, make_owned};

// ---- marker exports ----------------------------------------------------

/// `oaktimeline_marker_add` rejects a null list handle.
#[test]
fn marker_add_null_list() {
	let name = CString::new("m").unwrap();
	let r = unsafe { ffi::marker::oaktimeline_marker_add(CHandle::null(), 0, 1, 1, 1, name.as_ptr(), 0) };
	assert_eq!(r, OAKTIMELINE_E_INVALID);
}

/// A null name is treated as the empty string.
#[test]
fn marker_add_null_name() {
	let h = unsafe { ffi::marker::oaktimeline_marker_list_create() };
	let r = unsafe { ffi::marker::oaktimeline_marker_add(h.clone(), 0, 1, 1, 1, std::ptr::null(), 0) };
	assert_eq!(r, OAKTIMELINE_OK);
	let mut count = 0;
	unsafe { ffi::marker::oaktimeline_marker_count(h.clone(), &mut count) };
	assert_eq!(count, 1);
}

/// `oaktimeline_marker_count` / `marker_at` reject null list handles.
#[test]
fn marker_count_and_at_null_list() {
	let mut count = 0;
	assert_eq!(
		unsafe { ffi::marker::oaktimeline_marker_count(CHandle::null(), &mut count) },
		OAKTIMELINE_E_INVALID
	);

	let mut in_num = 0;
	let mut buf = [0 as c_char; 8];
	let r = unsafe {
		ffi::marker::oaktimeline_marker_at(
			CHandle::null(),
			0,
			&mut in_num,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			buf.as_mut_ptr(),
			8,
		)
	};
	assert_eq!(r, OAKTIMELINE_E_INVALID);
}

/// A null name buffer or zero size is a no-op on the two-stage name write.
#[test]
fn marker_at_null_name_buffer() {
	let h = unsafe { ffi::marker::oaktimeline_marker_list_create() };
	let name = CString::new("mark").unwrap();
	unsafe { ffi::marker::oaktimeline_marker_add(h.clone(), 0, 1, 1, 1, name.as_ptr(), 1) };

	// Null buffer: still reports the needed size.
	let needed = unsafe {
		ffi::marker::oaktimeline_marker_at(
			h.clone(),
			0,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			8,
		)
	};
	assert_eq!(needed as usize, "mark".len() + 1);

	// Zero-size buffer: no write happens (would otherwise overflow).
	let mut buf = [9 as c_char; 4];
	let needed = unsafe {
		ffi::marker::oaktimeline_marker_at(
			h.clone(),
			0,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			buf.as_mut_ptr(),
			0,
		)
	};
	assert_eq!(needed as usize, "mark".len() + 1);
	assert_eq!(buf, [9 as c_char; 4]);
}

/// The remove/set-time/set-props command factories on a non-empty list.
#[test]
fn marker_command_factories_valid_list() {
	let h = unsafe { ffi::marker::oaktimeline_marker_list_create() };
	let name = CString::new("m").unwrap();
	unsafe { ffi::marker::oaktimeline_marker_add(h.clone(), 0, 1, 1, 1, name.as_ptr(), 3) };

	// In-bounds index: non-null command handles.
	assert!(!unsafe { ffi::marker::oaktimeline_marker_remove_at_command(h.clone(), 0) }.is_null());
	assert!(!unsafe {
		ffi::marker::oaktimeline_marker_set_time_command(h.clone(), 0, 2, 1, 3, 1)
	}
	.is_null());

	// set_props with color only, name only, and both.
	assert!(!unsafe {
		ffi::marker::oaktimeline_marker_set_props_command(h.clone(), 0, 4, std::ptr::null())
	}
	.is_null());
	assert!(!unsafe {
		ffi::marker::oaktimeline_marker_set_props_command(h.clone(), 0, -1, name.as_ptr())
	}
	.is_null());
	assert!(!unsafe {
		ffi::marker::oaktimeline_marker_set_props_command(h.clone(), 0, 4, name.as_ptr())
	}
	.is_null());

	// Out-of-range index on the one-marker list: E_NOT_FOUND.
	assert!(unsafe { ffi::marker::oaktimeline_marker_remove_at_command(h.clone(), 7) }.is_null());
	assert!(unsafe {
		ffi::marker::oaktimeline_marker_set_time_command(h.clone(), 7, 2, 1, 3, 1)
	}
	.is_null());
	assert!(unsafe {
		ffi::marker::oaktimeline_marker_set_props_command(h.clone(), 7, 1, std::ptr::null())
	}
	.is_null());

	// color < 0 with a null name is invalid.
	assert!(unsafe {
		ffi::marker::oaktimeline_marker_set_props_command(h.clone(), 0, -1, std::ptr::null())
	}
	.is_null());
}

/// `oaktimeline_marker_list_load` parses `marker` elements with attributes;
/// a null list or reader is rejected.
#[test]
fn marker_list_load_xml() {
	// Null list / reader → E_INVALID.
	assert_eq!(
		unsafe { ffi::marker::oaktimeline_marker_list_load(CHandle::null(), CHandle::null()) },
		OAKTIMELINE_E_INVALID
	);

	let h = unsafe { ffi::marker::oaktimeline_marker_list_create() };
	let reader = xml_reader_handle(vec![MockXmlNode {
		name: "marker".to_string(),
		text: String::new(),
		attrs: vec![
			("name".to_string(), "one".to_string()),
			("in".to_string(), "30000/1001".to_string()),
			("out".to_string(), "60000/1001".to_string()),
			("color".to_string(), "2".to_string()),
		],
	}]);

	let r = unsafe { ffi::marker::oaktimeline_marker_list_load(h.clone(), reader) };
	assert_eq!(r, OAKTIMELINE_OK);
	let mut count = 0;
	unsafe { ffi::marker::oaktimeline_marker_count(h.clone(), &mut count) };
	assert_eq!(count, 1);
}

/// `oaktimeline_marker_list_save` writes the marker XML; a null list or
/// writer is rejected.
#[test]
fn marker_list_save_xml() {
	assert_eq!(
		unsafe { ffi::marker::oaktimeline_marker_list_save(CHandle::null(), CHandle::null()) },
		OAKTIMELINE_E_INVALID
	);

	let h = unsafe { ffi::marker::oaktimeline_marker_list_create() };
	let name = CString::new("m").unwrap();
	unsafe { ffi::marker::oaktimeline_marker_add(h.clone(), 0, 1, 1, 1, name.as_ptr(), 3) };

	let w = unsafe { oakcommon_xml_writer_init() };
	let r = unsafe { ffi::marker::oaktimeline_marker_list_save(h.clone(), w.clone()) };
	assert_eq!(r, OAKTIMELINE_OK);
	let buf = unsafe { get::<oaktimeline::bridge::teststubs::MockXmlWriter>(&w) }
		.unwrap()
		.buf
		.clone();
	assert!(buf.contains("<marker>"));
	assert!(buf.contains("name=\"m\""));
}

// ---- workarea exports --------------------------------------------------

/// `oaktimeline_workarea_of` returns a borrowed handle for a viewer node
/// and rejects null owners.
#[test]
fn workarea_of_borrows() {
	let wa_h = make_owned(oaktimeline::workarea::TimelineWorkArea::new());
	let owner = make_owned(MockNode {
		kind: MockKind::Node,
		work_area: wa_h.clone(),
		..Default::default()
	});

	let borrowed = unsafe { ffi::workarea::oaktimeline_workarea_of(owner.clone()) };
	assert!(!borrowed.is_null());
	assert!(unsafe { ffi::workarea::oaktimeline_workarea_of(CHandle::null()) }.is_null());
}

/// `oaktimeline_workarea_get` rejects a null handle.
#[test]
fn workarea_get_null() {
	let r = unsafe {
		ffi::workarea::oaktimeline_workarea_get(
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

/// `oaktimeline_workarea_get` accepts NULL out params.
#[test]
fn workarea_get_null_out_params() {
	let w = unsafe { ffi::workarea::oaktimeline_workarea_create() };
	unsafe { ffi::workarea::oaktimeline_workarea_set_range(w.clone(), 10, 1, 20, 1) };
	let r = unsafe {
		ffi::workarea::oaktimeline_workarea_get(
			w.clone(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
		)
	};
	assert_eq!(r, OAKTIMELINE_OK);
}

/// The work-area command factories (set-range / set-enabled) validate and
/// build command handles.
#[test]
fn workarea_command_factories() {
	let w = unsafe { ffi::workarea::oaktimeline_workarea_create() };
	unsafe { ffi::workarea::oaktimeline_workarea_set_range(w.clone(), 10, 1, 20, 1) };

	assert!(!unsafe {
		ffi::workarea::oaktimeline_workarea_set_range_command(w.clone(), 1, 1, 5, 1, 10, 1, 20, 1)
	}
	.is_null());
	assert!(!unsafe { ffi::workarea::oaktimeline_workarea_set_enabled_command(w.clone(), 1) }.is_null());

	// Null work area → null command handles.
	assert!(unsafe {
		ffi::workarea::oaktimeline_workarea_set_range_command(CHandle::null(), 1, 1, 5, 1, 10, 1, 20, 1)
	}
	.is_null());
	assert!(unsafe { ffi::workarea::oaktimeline_workarea_set_enabled_command(CHandle::null(), 1) }.is_null());
}

/// `oaktimeline_workarea_reset` rejects null out params.
#[test]
fn workarea_reset_null_out_params() {
	let r = unsafe {
		ffi::workarea::oaktimeline_workarea_reset(
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
		)
	};
	assert_eq!(r, OAKTIMELINE_E_INVALID);
}

/// `oaktimeline_workarea_load` reads enabled/in/out elements (and skips
/// unknown ones); null handles are rejected.
#[test]
fn workarea_load_xml() {
	assert_eq!(
		unsafe { ffi::workarea::oaktimeline_workarea_load(CHandle::null(), CHandle::null()) },
		OAKTIMELINE_E_INVALID
	);

	let w = unsafe { ffi::workarea::oaktimeline_workarea_create() };
	let reader = xml_reader_handle(vec![
		MockXmlNode {
			name: "enabled".to_string(),
			text: "1".to_string(),
			attrs: Vec::new(),
		},
		MockXmlNode {
			name: "in".to_string(),
			text: "30000/1001".to_string(),
			attrs: Vec::new(),
		},
		MockXmlNode {
			name: "out".to_string(),
			text: "60000/1001".to_string(),
			attrs: Vec::new(),
		},
		MockXmlNode {
			name: "unknown".to_string(),
			text: "x".to_string(),
			attrs: Vec::new(),
		},
	]);
	let r = unsafe { ffi::workarea::oaktimeline_workarea_load(w.clone(), reader) };
	assert_eq!(r, OAKTIMELINE_OK);
}

/// `oaktimeline_workarea_save` writes the work-area XML; null handles are
/// rejected.
#[test]
fn workarea_save_xml() {
	assert_eq!(
		unsafe { ffi::workarea::oaktimeline_workarea_save(CHandle::null(), CHandle::null()) },
		OAKTIMELINE_E_INVALID
	);

	let w = unsafe { ffi::workarea::oaktimeline_workarea_create() };
	unsafe { ffi::workarea::oaktimeline_workarea_set_enabled(w.clone(), 1) };
	let writer = unsafe { oakcommon_xml_writer_init() };
	let r = unsafe { ffi::workarea::oaktimeline_workarea_save(w.clone(), writer.clone()) };
	assert_eq!(r, OAKTIMELINE_OK);
	let buf = unsafe { get::<oaktimeline::bridge::teststubs::MockXmlWriter>(&writer) }
		.unwrap()
		.buf
		.clone();
	assert!(buf.contains("enabled"));
}

// ---- edit exports ------------------------------------------------------

/// `oaktimeline_replace_block_with_gap_command` validates and builds.
#[test]
fn replace_block_with_gap_command() {
	let track = make_owned(MockNode {
		kind: MockKind::Track,
		..Default::default()
	});
	let block = make_owned(MockNode {
		kind: MockKind::Clip,
		..Default::default()
	});

	assert!(!unsafe {
		ffi::edit::oaktimeline_replace_block_with_gap_command(track.clone(), block.clone())
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_replace_block_with_gap_command(CHandle::null(), block.clone())
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_replace_block_with_gap_command(track.clone(), CHandle::null())
	}
	.is_null());
}

/// `oaktimeline_split_preserving_links_command` validates and builds.
#[test]
fn split_preserving_links_command() {
	let block = make_owned(MockNode {
		kind: MockKind::Clip,
		..Default::default()
	});
	let blocks = [block.clone()];
	let nums = [5i64];
	let dens = [1i64];

	assert!(!unsafe {
		ffi::edit::oaktimeline_split_preserving_links_command(blocks.as_ptr(), 1, nums.as_ptr(), dens.as_ptr(), 1)
	}
	.is_null());

	// Invalid inputs → null handle.
	assert!(unsafe {
		ffi::edit::oaktimeline_split_preserving_links_command(std::ptr::null(), 1, nums.as_ptr(), dens.as_ptr(), 1)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_split_preserving_links_command(blocks.as_ptr(), 0, nums.as_ptr(), dens.as_ptr(), 1)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_split_preserving_links_command(blocks.as_ptr(), 1, std::ptr::null(), dens.as_ptr(), 1)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_split_preserving_links_command(blocks.as_ptr(), 1, nums.as_ptr(), std::ptr::null(), 1)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_split_preserving_links_command(blocks.as_ptr(), 1, nums.as_ptr(), dens.as_ptr(), 0)
	}
	.is_null());
}

/// `oaktimeline_ripple_delete_gaps_command` validates and builds.
#[test]
fn ripple_delete_gaps_command() {
	let seq = make_owned(MockNode {
		kind: MockKind::Sequence,
		..Default::default()
	});
	let track = make_owned(MockNode {
		kind: MockKind::Track,
		..Default::default()
	});
	let in_nums = [0i64];
	let in_dens = [1i64];
	let out_nums = [5i64];
	let out_dens = [1i64];
	let tracks = [track.clone()];

	assert!(!unsafe {
		ffi::edit::oaktimeline_ripple_delete_gaps_command(
			seq.clone(),
			in_nums.as_ptr(),
			in_dens.as_ptr(),
			out_nums.as_ptr(),
			out_dens.as_ptr(),
			tracks.as_ptr(),
			1,
		)
	}
	.is_null());

	// Invalid inputs → null handle.
	assert!(unsafe {
		ffi::edit::oaktimeline_ripple_delete_gaps_command(
			CHandle::null(),
			in_nums.as_ptr(),
			in_dens.as_ptr(),
			out_nums.as_ptr(),
			out_dens.as_ptr(),
			tracks.as_ptr(),
			1,
		)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_ripple_delete_gaps_command(
			seq.clone(),
			std::ptr::null(),
			in_dens.as_ptr(),
			out_nums.as_ptr(),
			out_dens.as_ptr(),
			tracks.as_ptr(),
			1,
		)
	}
	.is_null());
	assert!(unsafe {
		ffi::edit::oaktimeline_ripple_delete_gaps_command(
			seq.clone(),
			in_nums.as_ptr(),
			in_dens.as_ptr(),
			out_nums.as_ptr(),
			out_dens.as_ptr(),
			tracks.as_ptr(),
			0,
		)
	}
	.is_null());
}

/// `oaktimeline_ripple_remove_area_command` and
/// `oaktimeline_insert_gaps_command` validate and build.
#[test]
fn ripple_area_and_insert_gaps_commands() {
	let track = make_owned(MockNode {
		kind: MockKind::Track,
		..Default::default()
	});
	let list = make_owned(MockNode {
		kind: MockKind::TrackList,
		..Default::default()
	});

	assert!(!unsafe { ffi::edit::oaktimeline_ripple_remove_area_command(track.clone(), 0, 1, 5, 1) }.is_null());
	assert!(unsafe { ffi::edit::oaktimeline_ripple_remove_area_command(CHandle::null(), 0, 1, 5, 1) }.is_null());

	assert!(!unsafe { ffi::edit::oaktimeline_insert_gaps_command(list.clone(), 0, 1, 5, 1) }.is_null());
	assert!(unsafe { ffi::edit::oaktimeline_insert_gaps_command(CHandle::null(), 0, 1, 5, 1) }.is_null());
}

/// The XML writer externs used by the save paths accumulate into the mock
/// writer's buffer (direct bridge coverage).
#[test]
fn xml_writer_externs_accumulate() {
	let w = unsafe { oakcommon_xml_writer_init() };
	assert!(!w.is_null());

	let start = CString::new("marker").unwrap();
	let key = CString::new("name").unwrap();
	let val = CString::new("m").unwrap();
	let text_name = CString::new("enabled").unwrap();
	let text_val = CString::new("1").unwrap();

	unsafe { oakcommon_xml_writer_write_start_element(w.clone(), start.as_ptr()) };
	unsafe { oakcommon_xml_writer_write_attribute(w.clone(), key.as_ptr(), val.as_ptr()) };
	unsafe { oakcommon_xml_writer_write_text_element(w.clone(), text_name.as_ptr(), text_val.as_ptr()) };
	unsafe { oakcommon_xml_writer_write_end_element(w.clone()) };

	let buf = unsafe { get::<oaktimeline::bridge::teststubs::MockXmlWriter>(&w) }
		.unwrap()
		.buf
		.clone();
	assert!(buf.contains("<marker>"));
	assert!(buf.contains(" name=\"m\""));
	assert!(buf.contains("<enabled>1</enabled>"));
	assert!(buf.contains("</>"));
}

/// Sanity: the FFI entry points never panic on garbage-ish but valid-shaped
/// inputs (guard behaviour).
#[test]
fn ffi_guards_return_codes() {
	// A null list to marker_count maps to E_INVALID, not a panic.
	let mut count = 0;
	assert_eq!(
		unsafe { ffi::marker::oaktimeline_marker_count(CHandle::null(), &mut count) },
		OAKTIMELINE_E_INVALID
	);
	// The failure constant is negative and distinct from OK.
	assert_ne!(OAKTIMELINE_E_FAILED, OAKTIMELINE_OK);
	assert_ne!(OAKTIMELINE_E_INVALID, OAKTIMELINE_E_NOT_FOUND);
}

/// Silence unused warnings for helpers imported for the save tests.
#[allow(dead_code)]
fn _uses_writer_mut() -> *mut oaktimeline::bridge::teststubs::MockXmlWriter {
	std::ptr::null_mut()
}
