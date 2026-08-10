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

//! Direct coverage of the in-crate C ABI mocks (`src/bridge/teststubs.rs`)
//! and the bridge externs: error paths (null / wrong-kind handles),
//! no-op callbacks and the shared helpers. These exercise the same `extern
//! "C"` symbols the crate's commands call, so they pin the bridge contract
//! and cover the mock's defensive branches.

use std::ffi::{c_char, CString};

use oaktimeline::bridge::common::{
	oakcommon_xml_reader_attribute_count, oakcommon_xml_reader_attribute_name,
	oakcommon_xml_reader_attribute_value, oakcommon_xml_reader_has_error, oakcommon_xml_reader_name,
	oakcommon_xml_reader_read_element_text, oakcommon_xml_writer_write_characters,
	oakcommon_xml_writer_write_end_document,
};
use oaktimeline::bridge::node::{
	oaknode_block_are_linked, oaknode_block_as_node, oaknode_block_clip_create,
	oaknode_block_from_node, oaknode_block_get_enabled, oaknode_block_get_in, oaknode_block_get_kind,
	oaknode_block_get_length, oaknode_block_get_next, oaknode_block_get_out,
	oaknode_block_get_previous, oaknode_block_get_track, oaknode_block_gap_create,
	oaknode_block_link, oaknode_block_set_enabled, oaknode_block_set_length_and_media_in,
	oaknode_block_set_length_and_media_out, oaknode_block_unlink,
	oaknode_clip_add_cache_passthrough_from, oaknode_clip_get_media_in, oaknode_clip_set_media_in,
	oaknode_command_create_remove_node, oaknode_node_connect, oaknode_node_copy_in_graph,
	oaknode_node_disconnect, oaknode_node_get_markers, oaknode_node_get_project,
	oaknode_node_get_work_area, oaknode_node_output_connection_count, oaknode_project_add_node,
	oaknode_project_remove_node, oaknode_sequence_as_node, oaknode_sequence_from_node,
	oaknode_sequence_get_all_track_at, oaknode_sequence_get_all_track_count,
	oaknode_sequence_get_track_list, oaknode_track_create, oaknode_track_get_block_at,
	oaknode_track_get_block_containing_time, oaknode_track_get_block_count, oaknode_track_get_length,
	oaknode_track_get_locked, oaknode_track_get_nearest_block_after_or_at,
	oaknode_track_get_nearest_block_before_or_at, oaknode_track_get_sequence,
	oaknode_track_insert_block_after, oaknode_track_prepend_block, oaknode_track_replace_block,
	oaknode_track_ripple_remove_block, oaknode_track_set_locked, oaknode_tracklist_array_append,
	oaknode_tracklist_array_remove_last, oaknode_tracklist_get_track_at,
	oaknode_tracklist_get_track_count, oaknode_tracklist_get_type,
};
use oaktimeline::bridge::teststubs::{MockKind, MockNode, MockUndoStack, xml_reader_handle};
use oaktimeline::bridge::undo::{
	oakundo_command_init, oakundo_command_init_multi, oakundo_command_multi_add_child,
	oakundo_command_redo_now, oakundo_command_undo_now, oakundo_stack_push,
	OakUndoCommandVtable,
};
use oaktimeline::handle::{CHandle, get, get_mut, make_owned};

fn make(kind: MockKind) -> CHandle {
	make_owned(MockNode {
		kind,
		..Default::default()
	})
}

fn addr(h: &CHandle) -> *mut MockNode {
	unsafe { get_mut::<MockNode>(h).unwrap() as *mut MockNode }
}

// ---- undo mocks --------------------------------------------------------

/// `oakundo_command_init` with a null vtable returns an empty handle.
#[test]
fn undo_init_null_vtable() {
	let h = unsafe { oakundo_command_init(std::ptr::null(), std::ptr::null_mut()) };
	assert!(h.is_null());
}

/// `oakundo_command_init_multi` + `multi_add_child` box a no-op command.
#[test]
fn undo_multi_helpers() {
	let multi = unsafe { oakundo_command_init_multi() };
	assert!(!multi.is_null());
	let child = unsafe { oakundo_command_init_multi() };
	assert_eq!(unsafe { oakundo_command_multi_add_child(multi.clone(), child) }, 0);
	unsafe { oakundo_command_redo_now(multi.clone()) };
	unsafe { oakundo_command_undo_now(multi) };
}

/// `oakundo_stack_push` records the push in the stack's mock.
#[test]
fn undo_stack_push_records() {
	let stack = make_owned(MockUndoStack { pushes: 0 });
	let cmd = unsafe { oakundo_command_init_multi() };
	let text = CString::new("trim").unwrap();
	assert_eq!(unsafe { oakundo_stack_push(stack.clone(), cmd, text.as_ptr()) }, 0);
	assert_eq!(unsafe { get::<MockUndoStack>(&stack) }.unwrap().pushes, 1);
}

// ---- XML mocks ---------------------------------------------------------

/// Reader attribute/name accessors on missing state return error codes.
#[test]
fn xml_reader_accessors_missing() {
	let mut buf = [0 as c_char; 16];
	// No current element: every accessor returns 0.
	let mut r = xml_reader_handle(Vec::new());
	let mut found = 0;
	unsafe { oaktimeline::bridge::common::oakcommon_xml_reader_read_next_start_element(
		r.clone(), &mut found,
	) };
	assert_eq!(unsafe { oakcommon_xml_reader_name(r.clone(), buf.as_mut_ptr(), 16) }, 0);
	assert_eq!(unsafe { oakcommon_xml_reader_read_element_text(r.clone(), buf.as_mut_ptr(), 16) }, 0);

	// Attribute accessors with a null reader and out-of-range index.
	let mut count = 99;
	unsafe { oakcommon_xml_reader_attribute_count(CHandle::null(), &mut count) };
	assert_eq!(count, 0);
	assert_eq!(unsafe { oakcommon_xml_reader_attribute_name(CHandle::null(), 0, buf.as_mut_ptr(), 16) }, 0);
	assert_eq!(unsafe { oakcommon_xml_reader_attribute_value(CHandle::null(), 0, buf.as_mut_ptr(), 16) }, 0);

	// With an element but no attributes: count 0, name/value fail.
	let mut r2 = xml_reader_handle(vec![oaktimeline::bridge::teststubs::MockXmlNode {
		name: "m".to_string(),
		text: String::new(),
		attrs: Vec::new(),
	}]);
	let mut found = 0;
	unsafe { oaktimeline::bridge::common::oakcommon_xml_reader_read_next_start_element(
		r2.clone(), &mut found,
	) };
	unsafe { oakcommon_xml_reader_attribute_count(r2.clone(), &mut count) };
	assert_eq!(count, 0);
	assert_eq!(unsafe { oakcommon_xml_reader_attribute_name(r2.clone(), 0, buf.as_mut_ptr(), 16) }, 0);
	assert_eq!(unsafe { oakcommon_xml_reader_attribute_value(r2.clone(), 0, buf.as_mut_ptr(), 16) }, 0);

	// `has_error` writes the error flag; null reader → 0.
	let mut err = 99;
	unsafe { oakcommon_xml_reader_has_error(CHandle::null(), &mut err) };
	assert_eq!(err, 0);
	unsafe { oakcommon_xml_reader_has_error(r2.clone(), &mut err) };
	assert_eq!(err, 0);
}

/// Writer characters / end-document accumulate or no-op.
#[test]
fn xml_writer_characters_and_end() {
	use oaktimeline::bridge::common::oakcommon_xml_writer_init;

	let w = unsafe { oakcommon_xml_writer_init() };
	let text = CString::new("abc").unwrap();
	assert_eq!(unsafe { oakcommon_xml_writer_write_characters(w.clone(), text.as_ptr()) }, 0);
	assert_eq!(unsafe { oakcommon_xml_writer_write_end_document(w.clone()) }, 0);
	let buf = unsafe { get::<oaktimeline::bridge::teststubs::MockXmlWriter>(&w) }
		.unwrap()
		.buf
		.clone();
	assert_eq!(buf, "abc");
}

// ---- node mocks: failure paths -----------------------------------------

/// Block geometry accessors reject null handles.
#[test]
fn block_geometry_null_handles() {
	let mut n = 0;
	let mut d = 0;
	assert_eq!(unsafe { oaknode_block_get_in(CHandle::null(), &mut n, &mut d) }, -1);
	assert_eq!(unsafe { oaknode_block_get_out(CHandle::null(), &mut n, &mut d) }, -1);
	assert_eq!(unsafe { oaknode_block_get_length(CHandle::null(), &mut n, &mut d) }, -1);
	assert_eq!(unsafe { oaknode_block_set_length_and_media_out(CHandle::null(), 1, 1) }, -1);
	assert_eq!(unsafe { oaknode_block_set_length_and_media_in(CHandle::null(), 1, 1) }, -1);
	assert_eq!(unsafe { oaknode_block_get_enabled(CHandle::null(), &mut n) }, -1);
	assert_eq!(unsafe { oaknode_block_set_enabled(CHandle::null(), 1) }, -1);

	let mut out = CHandle::null();
	assert_eq!(unsafe { oaknode_block_get_previous(CHandle::null(), &mut out) }, -1);
	assert_eq!(unsafe { oaknode_block_get_next(CHandle::null(), &mut out) }, -1);
	assert_eq!(unsafe { oaknode_block_get_track(CHandle::null(), &mut out) }, -1);
}

/// `oaknode_block_link`/`unlink` reject invalid handles and link/unlink
/// valid ones.
#[test]
fn block_link_unlink() {
	let a = make(MockKind::Clip);
	let b = make(MockKind::Clip);

	assert_eq!(unsafe { oaknode_block_link(CHandle::null(), b.clone()) }, -1);
	assert_eq!(unsafe { oaknode_block_unlink(a.clone(), CHandle::null()) }, -1);

	assert_eq!(unsafe { oaknode_block_link(a.clone(), b.clone()) }, 0);
	let mut linked = 0;
	unsafe { oaknode_block_are_linked(a.clone(), b.clone(), &mut linked) };
	assert_eq!(linked, 1);

	assert_eq!(unsafe { oaknode_block_unlink(a.clone(), b.clone()) }, 0);
	unsafe { oaknode_block_are_linked(a.clone(), b.clone(), &mut linked) };
	assert_eq!(linked, 0);

	// are_linked with invalid handles → -1.
	assert_eq!(unsafe { oaknode_block_are_linked(CHandle::null(), b.clone(), &mut linked) }, -1);
}

/// Track accessors reject null / wrong-kind handles.
#[test]
fn track_accessors_invalid() {
	let clip = make(MockKind::Clip);
	let mut n = 0;
	let mut d = 0;
	let mut out = CHandle::null();

	assert_eq!(unsafe { oaknode_track_get_length(CHandle::null(), &mut n, &mut d) }, -1);
	assert_eq!(unsafe { oaknode_track_get_sequence(CHandle::null(), &mut out) }, -1);
	assert_eq!(unsafe { oaknode_track_prepend_block(CHandle::null(), clip.clone()) }, -1);
	assert_eq!(unsafe { oaknode_track_ripple_remove_block(CHandle::null(), clip.clone()) }, -1);

	let mut locked = 0;
	assert_eq!(unsafe { oaknode_track_get_locked(CHandle::null(), &mut locked) }, -1);
	assert_eq!(unsafe { oaknode_track_get_locked(clip.clone(), &mut locked) }, -1);
	assert_eq!(unsafe { oaknode_track_set_locked(CHandle::null(), 1) }, -1);
	assert_eq!(unsafe { oaknode_track_set_locked(clip.clone(), 1) }, -1);

	let mut count = 0;
	assert_eq!(unsafe { oaknode_track_get_block_count(CHandle::null(), &mut count) }, -1);
	assert_eq!(unsafe { oaknode_track_get_block_count(clip.clone(), &mut count) }, -1);
	assert_eq!(unsafe { oaknode_track_get_block_at(CHandle::null(), 0, &mut out) }, -1);
	assert_eq!(unsafe { oaknode_track_get_block_at(clip.clone(), 0, &mut out) }, -1);
	assert_eq!(unsafe { oaknode_track_get_block_at(make(MockKind::Track), 3, &mut out) }, -1);

	// containing-time queries.
	assert_eq!(unsafe { oaknode_track_get_block_containing_time(CHandle::null(), 1, 1, &mut out) }, -1);
	assert_eq!(
		unsafe { oaknode_track_get_block_containing_time(clip.clone(), 1, 1, &mut out) },
		-1
	);
	assert_eq!(unsafe { oaknode_track_get_nearest_block_before_or_at(CHandle::null(), 1, 1, &mut out) }, -1);
	assert_eq!(
		unsafe { oaknode_track_get_nearest_block_before_or_at(clip.clone(), 1, 1, &mut out) },
		-1
	);
	assert_eq!(unsafe { oaknode_track_get_nearest_block_after_or_at(CHandle::null(), 1, 1, &mut out) }, -1);
	assert_eq!(
		unsafe { oaknode_track_get_nearest_block_after_or_at(clip.clone(), 1, 1, &mut out) },
		-1
	);
}

/// Track geometry success paths: insert-after on an empty track, replace,
/// containing-time and nearest queries, block replacement.
#[test]
fn track_geometry_success() {
	let t = make(MockKind::Track);
	let a = make(MockKind::Clip);
	let b = make(MockKind::Clip);

	// insert-after on an empty track appends (before not found).
	assert_eq!(unsafe { oaknode_track_insert_block_after(t.clone(), a.clone(), b.clone()) }, 0);

	// Prepend b, then a.
	unsafe { oaknode_track_prepend_block(t.clone(), b.clone()) };
	unsafe { oaknode_track_prepend_block(t.clone(), a.clone()) };

	// Block containing time 5: none (no geometry set) → -1 with null out.
	let mut out = CHandle::null();
	assert_eq!(unsafe { oaknode_track_get_block_containing_time(t.clone(), 5, 1, &mut out) }, -1);

	// Nearest before/after return the front block by in point.
	assert_eq!(unsafe { oaknode_track_get_nearest_block_before_or_at(t.clone(), 5, 1, &mut out) }, 0);
	assert!(!out.is_null());
	assert_eq!(unsafe { oaknode_track_get_nearest_block_after_or_at(t.clone(), 5, 1, &mut out) }, 0);

	// Replace a with b.
	let c = make(MockKind::Clip);
	assert_eq!(unsafe { oaknode_track_replace_block(t.clone(), a.clone(), c.clone()) }, 0);
	assert_eq!(unsafe { oaknode_track_replace_block(t.clone(), make(MockKind::Clip), c.clone()) }, -1);
	assert_eq!(unsafe { oaknode_track_replace_block(CHandle::null(), a.clone(), c.clone()) }, -1);

	// Ripple-remove a block not on the track → -1.
	let orphan = make(MockKind::Clip);
	assert_eq!(unsafe { oaknode_track_ripple_remove_block(t.clone(), orphan) }, -1);
}

/// Track-list accessors reject invalid handles / kinds and return the
/// per-type lists.
#[test]
fn tracklist_and_sequence_accessors() {
	let list = make(MockKind::TrackList);
	let clip = make(MockKind::Clip);
	let mut kind = 99;
	let mut count = 99;
	let mut out = CHandle::null();

	assert_eq!(unsafe { oaknode_tracklist_get_type(CHandle::null(), &mut kind) }, -1);
	assert_eq!(unsafe { oaknode_tracklist_get_type(clip.clone(), &mut kind) }, -1);
	assert_eq!(unsafe { oaknode_tracklist_get_track_count(CHandle::null(), &mut count) }, -1);
	assert_eq!(unsafe { oaknode_tracklist_get_track_count(clip.clone(), &mut count) }, -1);
	assert_eq!(unsafe { oaknode_tracklist_get_track_at(CHandle::null(), 0, &mut out) }, -1);
	assert_eq!(unsafe { oaknode_tracklist_get_track_at(clip.clone(), 0, &mut out) }, -1);
	assert_eq!(unsafe { oaknode_tracklist_get_track_at(list.clone(), 0, &mut out) }, -1);

	// Append/remove-last are no-ops returning 0.
	assert_eq!(unsafe { oaknode_tracklist_array_append(list.clone()) }, 0);
	assert_eq!(unsafe { oaknode_tracklist_array_remove_last(list.clone()) }, 0);

	let seq = make(MockKind::Sequence);
	assert_eq!(unsafe { oaknode_sequence_get_track_list(CHandle::null(), 0, &mut out) }, -1);
	assert_eq!(unsafe { oaknode_sequence_get_track_list(clip.clone(), 0, &mut out) }, -1);
	assert_eq!(unsafe { oaknode_sequence_get_all_track_count(CHandle::null(), &mut count) }, -1);
	assert_eq!(unsafe { oaknode_sequence_get_all_track_count(clip.clone(), &mut count) }, -1);
	assert_eq!(unsafe { oaknode_sequence_get_all_track_at(CHandle::null(), 0, &mut out) }, -1);
	assert_eq!(unsafe { oaknode_sequence_get_all_track_at(seq.clone(), 0, &mut out) }, -1);

	// sequence_get_track_list finds the matching per-type list.
	let video_list = make(MockKind::TrackList);
	unsafe {
		get_mut::<MockNode>(&video_list).unwrap().track_type = 0;
	}
	unsafe {
		get_mut::<MockNode>(&seq).unwrap().blocks.push(addr(&video_list));
	}
	assert_eq!(unsafe { oaknode_sequence_get_track_list(seq.clone(), 0, &mut out) }, 0);
	assert!(!out.is_null());
}

/// Node-level accessors: project/markers/work-area/connect/copy.
#[test]
fn node_accessors() {
	let clip = make(MockKind::Clip);
	let mut out = CHandle::null();
	let mut count = 99;

	assert_eq!(unsafe { oaknode_node_get_project(CHandle::null(), &mut out) }, -1);
	assert_eq!(unsafe { oaknode_node_output_connection_count(CHandle::null(), &mut count) }, -1);
	assert_eq!(unsafe { oaknode_node_get_markers(CHandle::null(), &mut out) }, -1);
	assert_eq!(unsafe { oaknode_node_get_work_area(CHandle::null(), &mut out) }, -1);

	// Null markers/work area → null borrowed handle.
	unsafe { oaknode_node_get_markers(clip.clone(), &mut out) };
	assert!(out.is_null());
	unsafe { oaknode_node_get_work_area(clip.clone(), &mut out) };
	assert!(out.is_null());

	// Project add/remove reject null handles.
	assert_eq!(unsafe { oaknode_project_add_node(CHandle::null(), clip.clone()) }, -1);
	assert_eq!(unsafe { oaknode_project_remove_node(CHandle::null(), clip.clone()) }, -1);

	// Connect bumps the output connection count; disconnect no-ops.
	let a = make(MockKind::Clip);
	let b = make(MockKind::Clip);
	let input_id = CString::new("tex_in").unwrap();
	assert_eq!(unsafe { oaknode_node_connect(CHandle::null(), b.clone(), input_id.as_ptr()) }, -1);
	assert_eq!(unsafe { oaknode_node_connect(a.clone(), b.clone(), input_id.as_ptr()) }, 0);
	assert_eq!(unsafe { oaknode_node_output_connection_count(a.clone(), &mut count) }, 0);
	assert_eq!(count, 1);
	assert_eq!(unsafe { oaknode_node_disconnect(b.clone(), input_id.as_ptr()) }, 0);

	// copy_in_graph clones the node; a null node yields a null clone.
	assert_eq!(unsafe { oaknode_node_copy_in_graph(CHandle::null(), &mut out) }.is_null(), true);
	let copy = unsafe { oaknode_node_copy_in_graph(a.clone(), &mut out) };
	assert!(!copy.is_null());
	assert!(!out.is_null());
}

/// Block-kind/from-node/clip helpers.
#[test]
fn block_kind_and_clip_helpers() {
	let clip = make(MockKind::Clip);
	let gap = make(MockKind::Gap);
	let track = make(MockKind::Track);
	let mut kind = 99;

	assert_eq!(unsafe { oaknode_block_get_kind(CHandle::null(), &mut kind) }, -1);
	assert_eq!(unsafe { oaknode_block_get_kind(clip.clone(), &mut kind) }, 0);
	assert_eq!(kind, 1);
	assert_eq!(unsafe { oaknode_block_get_kind(gap.clone(), &mut kind) }, 0);
	assert_eq!(kind, 2);

	// block_from_node: clip/gap views, null otherwise.
	assert!(!unsafe { oaknode_block_from_node(clip.clone()) }.is_null());
	assert!(unsafe { oaknode_block_from_node(track.clone()) }.is_null());
	assert!(unsafe { oaknode_block_from_node(CHandle::null()) }.is_null());

	// clip media-in accessors reject non-clips and null handles.
	let mut n = 0;
	let mut d = 0;
	assert_eq!(unsafe { oaknode_clip_get_media_in(CHandle::null(), &mut n, &mut d) }, -1);
	assert_eq!(unsafe { oaknode_clip_get_media_in(gap.clone(), &mut n, &mut d) }, -1);
	assert_eq!(unsafe { oaknode_clip_set_media_in(CHandle::null(), 1, 1) }, -1);
	assert_eq!(unsafe { oaknode_clip_set_media_in(gap.clone(), 1, 1) }, -1);
	assert_eq!(unsafe { oaknode_clip_set_media_in(clip.clone(), 5, 1) }, 0);
	assert_eq!(unsafe { oaknode_clip_get_media_in(clip.clone(), &mut n, &mut d) }, 0);
	assert_eq!((n, d), (5, 1));

	// Passthrough copy is a no-op returning 0.
	assert_eq!(unsafe { oaknode_clip_add_cache_passthrough_from(clip.clone(), gap.clone()) }, 0);
}

/// `oaknode_block_as_node` / `oaknode_sequence_as_node` /
/// `oaknode_sequence_from_node` borrow the node view.
#[test]
fn node_view_helpers() {
	let clip = make(MockKind::Clip);
	let seq = make(MockKind::Sequence);

	assert!(!unsafe { oaknode_block_as_node(clip.clone()) }.is_null());
	assert!(!unsafe { oaknode_sequence_as_node(seq.clone()) }.is_null());
	assert!(!unsafe { oaknode_sequence_from_node(seq.clone()) }.is_null());

	// Remove-command factory returns a command handle.
	let cmd = unsafe { oaknode_command_create_remove_node(clip.clone()) };
	assert!(!cmd.is_null());
}

/// Shared mock helpers: add/sub with differing denominators (exercised via
/// `block_set_length_and_media_*`), and null-pointer writes.
#[test]
fn shared_pair_arithmetic() {
	// media_in (0,3) + length (1,2): different denominators.
	let b = make(MockKind::Clip);
	unsafe { get_mut::<MockNode>(&b).unwrap().media_in = (0, 3) };
	assert_eq!(unsafe { oaknode_block_set_length_and_media_out(b.clone(), 1, 2) }, 0);
	let out = unsafe { get::<MockNode>(&b) }.unwrap().out;
	assert_eq!(out, (3, 6)); // (0*2 + 1*3, 3*2)

	// media_in (1,3) with out (0,3): sub with equal denominators.
	let c = make(MockKind::Clip);
	unsafe { get_mut::<MockNode>(&c).unwrap().out = (0, 3) };
	assert_eq!(unsafe { oaknode_block_set_length_and_media_in(c.clone(), 1, 3) }, 0);
	assert_eq!(unsafe { get::<MockNode>(&c) }.unwrap().media_in, (-1, 3));

	// write_cstr with a null buffer is a no-op; getters tolerate null out.
	let mut n = 0;
	assert_eq!(unsafe { oaknode_block_get_length(make(MockKind::Clip), &mut n, std::ptr::null_mut()) }, 0);

	// Null out pointer to block_get_previous is a no-op returning 0.
	assert_eq!(unsafe { oaknode_block_get_previous(make(MockKind::Clip), std::ptr::null_mut()) }, 0);
}

/// `oaknode_track_get_length` accepts any node handle (the mock sums the
/// blocks it owns; a clip owns none).
#[test]
fn track_length_wrong_kind() {
	let clip = make(MockKind::Clip);
	let mut n = 0;
	let mut d = 0;
	assert_eq!(unsafe { oaknode_track_get_length(clip, &mut n, &mut d) }, 0);
	assert_eq!((n, d), (0, 1));
}
