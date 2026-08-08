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

//! In-crate C ABI mocks, compiled only with `--features test-stubs`.
//!
//! Each `#[no_mangle]` function here supplies a definition for the `extern
//! "C"` symbol declared in [`super::undo`] / [`super::common`] /
//! [`super::node`], so unit and integration tests link without the real
//! oaknode / oakundo / oakcommon DLLs.
//!
//! The mocks are intentionally simple and single-threaded. Handles box a
//! [`MockNode`] (for every node-graph kind), a [`MockMarkerList`], a
//! [`MockWorkArea`] or a [`MockUndoCommand`]. References between boxes use
//! raw pointers; the caller is assumed to hold a live handle for the
//! duration an object is referenced by the graph (matching how the real
//! crate's tests drive the facade).
//!
//! The mock node graph is a simplification of the C++ one:
//! - a track owns an ordered list of block pointers ([`MockNode::blocks`]);
//! - `in`/`out`/`length`/`media_in` are stored as `(i32, i32)` pairs;
//! - `block_set_length_and_media_*` keep the non-fixed edge fixed by
//!   recomputing length from the fixed edge (see the doc on each function);
//! - `ripple_remove_block` / `replace_block` mutate the owning track's list
//!   and clear/rewrite the block's `track` pointer.

use std::ffi::{c_char, c_int, c_void};

use crate::handle::{CHandle, RefBox, get, get_mut, make_owned};

// ---------------------------------------------------------------------------
// Undo command mock
// ---------------------------------------------------------------------------

/// The state boxed behind an undo-command handle created by the mock
/// `oakundo_command_init`.
pub struct MockUndoCommand {
    /// The caller-supplied callback table.
    pub vtable: super::undo::OakUndoCommandVtable,
    /// The caller-supplied userdata (a `*mut` boxed timeline command).
    pub userdata: *mut c_void,
}

// The mock is single-threaded; the raw userdata pointer is fine to move
// across the crate's handle boundaries.
unsafe impl Send for MockUndoCommand {}

/// `oakundo_command_init`: box the vtable + userdata and hand back an owning
/// handle.
#[no_mangle]
pub extern "C" fn oakundo_command_init(
    vtable: *const super::undo::OakUndoCommandVtable,
    userdata: *mut c_void,
) -> CHandle {
    if vtable.is_null() {
        return CHandle::null();
    }
    // SAFETY: caller guarantees a valid vtable pointer for the duration of
    // the call (mirrors the C ABI contract). We copy it out; it need not
    // outlive the call.
    let vt = unsafe { std::ptr::read(vtable) };
    make_owned(MockUndoCommand {
        vtable: vt,
        userdata,
    })
}

/// `oakundo_command_init_multi`: an empty multi command (no-op here).
#[no_mangle]
pub extern "C" fn oakundo_command_init_multi() -> CHandle {
    make_owned(MockUndoCommand {
        vtable: super::undo::OakUndoCommandVtable {
            redo: None,
            undo: None,
            free_fn: None,
        },
        userdata: std::ptr::null_mut(),
    })
}

/// `oakundo_command_multi_add_child`: no-op in the mock (returns 0).
#[no_mangle]
pub extern "C" fn oakundo_command_multi_add_child(_multi: CHandle, _child: CHandle) -> c_int {
    0
}

/// `oakundo_command_redo_now`: invoke the redo callback.
#[no_mangle]
pub extern "C" fn oakundo_command_redo_now(command: CHandle) -> c_int {
    // SAFETY: the handle boxes a MockUndoCommand (created by init above).
    if let Some(m) = unsafe { get_mut::<MockUndoCommand>(&command) } {
        if let Some(f) = m.vtable.redo {
            // SAFETY: the callback is the one the crate registered for this
            // command's userdata.
            unsafe { f(m.userdata) };
        }
    }
    0
}

/// `oakundo_command_undo_now`: invoke the undo callback.
#[no_mangle]
pub extern "C" fn oakundo_command_undo_now(command: CHandle) -> c_int {
    // SAFETY: the handle boxes a MockUndoCommand.
    if let Some(m) = unsafe { get_mut::<MockUndoCommand>(&command) } {
        if let Some(f) = m.vtable.undo {
            // SAFETY: callback registered by the crate for this userdata.
            unsafe { f(m.userdata) };
        }
    }
    0
}

/// `oakundo_command_free`: run `free_fn`, drop the box and clear the handle.
/// NULL / empty-handle is a no-op.
#[no_mangle]
pub extern "C" fn oakundo_command_free(command: *mut CHandle) {
    if command.is_null() {
        return;
    }
    // SAFETY: caller passes a valid pointer.
    let h = unsafe { &mut *command };
    if h.ctx.is_null() {
        return;
    }
    // SAFETY: handle boxes a MockUndoCommand.
    if let Some(m) = unsafe { get_mut::<MockUndoCommand>(h) } {
        if let Some(f) = m.vtable.free_fn {
            // SAFETY: callback registered by the crate for this userdata.
            unsafe { f(m.userdata) };
        }
    }
    // Take ownership of the box (count is 1) and drop it, then clear.
    // SAFETY: h.ctx is a RefBox<MockUndoCommand> with a single reference
    // (the handle we are freeing).
    unsafe { drop(Box::from_raw(h.ctx as *mut RefBox<MockUndoCommand>)) };
    h.ctx = std::ptr::null_mut();
    h.addref = None;
    h.release = None;
    h.abi_version = 0;
}

/// `oakundo_stack_push`: record the push into the stack's mock (returns 0).
#[no_mangle]
pub extern "C" fn oakundo_stack_push(
    stack: CHandle,
    _command: CHandle,
    _text: *const c_char,
) -> c_int {
    // SAFETY: the stack handle boxes a MockUndoStack.
    if let Some(s) = unsafe { get_mut::<MockUndoStack>(&stack) } {
        s.pushes += 1;
    }
    0
}

/// The boxed state of an undo stack handle used by `oakundo_stack_push`.
pub struct MockUndoStack {
    /// Number of successful pushes.
    pub pushes: i32,
}

// ---------------------------------------------------------------------------
// oakcommon mocks (XML reader/writer + config)
// ---------------------------------------------------------------------------

/// A single flat XML "start element" the reader yields.
pub struct MockXmlNode {
    /// Element name.
    pub name: String,
    /// Inner text (`read_element_text`).
    pub text: String,
    /// Attribute name/value pairs.
    pub attrs: Vec<(String, String)>,
}

/// The boxed state of an `oakcommon_xml_reader_*` handle.
pub struct MockXmlReader {
    /// Elements to iterate, in order.
    pub nodes: Vec<MockXmlNode>,
    /// Cursor: index of the element the next `read_next_start_element`
    /// yields; "current element" is `nodes[cur-1]`.
    pub cur: usize,
    /// Whether the reader is in an error state.
    pub error: bool,
}

impl MockXmlReader {
    /// Build a reader over the given elements.
    pub fn new(nodes: Vec<MockXmlNode>) -> Self {
        MockXmlReader {
            nodes,
            cur: 0,
            error: false,
        }
    }
}

/// Helper to build a reader boxed into a handle (used by tests).
pub fn xml_reader_handle(nodes: Vec<MockXmlNode>) -> CHandle {
    make_owned(MockXmlReader::new(nodes))
}

/// `oakcommon_xml_reader_init`: build an empty reader over a NUL-terminated
/// document. The mock only supports the `reader_handle` builder, so a
/// document string yields an empty reader.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_init(_data: *const c_char) -> CHandle {
    make_owned(MockXmlReader::new(Vec::new()))
}

/// `oakcommon_xml_reader_free`: release and clear; NULL / empty no-op.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_free(reader: *mut CHandle) {
    free_box::<MockXmlReader>(reader);
}

/// `oakcommon_xml_reader_skip_current_element`: no-op (flat model).
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_skip_current_element(_reader: CHandle) -> c_int {
    0
}

/// `oakcommon_xml_reader_read_next_start_element`: advance and write the
/// element name. Returns 1 while elements remain, else 0.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_read_next_start_element(
    reader: CHandle,
    name: *mut c_char,
    buf_size: c_int,
) -> c_int {
    // SAFETY: handle boxes a MockXmlReader.
    let Some(r) = (unsafe { get_mut::<MockXmlReader>(&reader) }) else {
        return 0;
    };
    if r.cur >= r.nodes.len() {
        return 0;
    }
    let n = r.nodes[r.cur].name.clone();
    r.cur += 1;
    write_cstr(name, buf_size, &n);
    1
}

/// `oakcommon_xml_reader_name`: the current element's name.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_name(reader: CHandle, name: *mut c_char, buf_size: c_int) -> c_int {
    // SAFETY: handle boxes a MockXmlReader.
    let Some(r) = (unsafe { get::<MockXmlReader>(&reader) }) else {
        return 0;
    };
    let Some(n) = r.nodes.get(r.cur.wrapping_sub(1)) else {
        return 0;
    };
    write_cstr(name, buf_size, &n.name);
    1
}

/// `oakcommon_xml_reader_read_element_text`: the current element's text.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_read_element_text(
    reader: CHandle,
    text: *mut c_char,
    buf_size: c_int,
) -> c_int {
    // SAFETY: handle boxes a MockXmlReader.
    let Some(r) = (unsafe { get::<MockXmlReader>(&reader) }) else {
        return 0;
    };
    let Some(n) = r.nodes.get(r.cur.wrapping_sub(1)) else {
        return 0;
    };
    write_cstr(text, buf_size, &n.text);
    1
}

/// `oakcommon_xml_reader_attribute_count`: attribute count on the current
/// element.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_attribute_count(reader: CHandle, count: *mut c_int) -> c_int {
    // SAFETY: caller passes a valid count pointer.
    let c = match unsafe { get::<MockXmlReader>(&reader) } {
        Some(r) => r
            .nodes
            .get(r.cur.wrapping_sub(1))
            .map(|n| n.attrs.len() as c_int)
            .unwrap_or(0),
        None => 0,
    };
    // SAFETY: caller guarantees `count` is a valid pointer.
    unsafe { *count = c };
    1
}

/// `oakcommon_xml_reader_attribute_name`: name of attribute `i`.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_attribute_name(
    reader: CHandle,
    index: c_int,
    name: *mut c_char,
    buf_size: c_int,
) -> c_int {
    // SAFETY: handle boxes a MockXmlReader.
    let Some(r) = (unsafe { get::<MockXmlReader>(&reader) }) else {
        return 0;
    };
    let Some(n) = r.nodes.get(r.cur.wrapping_sub(1)) else {
        return 0;
    };
    let Some((k, _)) = n.attrs.get(index as usize) else {
        return 0;
    };
    write_cstr(name, buf_size, k);
    1
}

/// `oakcommon_xml_reader_attribute_value`: value of attribute `i`.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_attribute_value(
    reader: CHandle,
    index: c_int,
    value: *mut c_char,
    buf_size: c_int,
) -> c_int {
    // SAFETY: handle boxes a MockXmlReader.
    let Some(r) = (unsafe { get::<MockXmlReader>(&reader) }) else {
        return 0;
    };
    let Some(n) = r.nodes.get(r.cur.wrapping_sub(1)) else {
        return 0;
    };
    let Some((_, v)) = n.attrs.get(index as usize) else {
        return 0;
    };
    write_cstr(value, buf_size, v);
    1
}

/// `oakcommon_xml_reader_has_error`: whether the reader errored.
#[no_mangle]
pub extern "C" fn oakcommon_xml_reader_has_error(reader: CHandle, has_error: *mut c_int) -> c_int {
    let e = match unsafe { get::<MockXmlReader>(&reader) } {
        Some(r) => r.error as c_int,
        None => 0,
    };
    // SAFETY: caller guarantees `has_error` is a valid pointer.
    unsafe { *has_error = e };
    1
}

/// The boxed state of an `oakcommon_xml_writer_*` handle.
pub struct MockXmlWriter {
    /// Accumulated output.
    pub buf: String,
}

/// `oakcommon_xml_writer_init`: a new writer.
#[no_mangle]
pub extern "C" fn oakcommon_xml_writer_init() -> CHandle {
    make_owned(MockXmlWriter { buf: String::new() })
}

/// `oakcommon_xml_writer_free`: release and clear; NULL / empty no-op.
#[no_mangle]
pub extern "C" fn oakcommon_xml_writer_free(writer: *mut CHandle) {
    free_box::<MockXmlWriter>(writer);
}

/// `oakcommon_xml_writer_write_start_element`: append `<name>`.
#[no_mangle]
pub extern "C" fn oakcommon_xml_writer_write_start_element(writer: CHandle, name: *const c_char) -> c_int {
    // SAFETY: caller passes a NUL-terminated string.
    let n = unsafe { cstr(name) };
    // SAFETY: handle boxes a MockXmlWriter.
    if let Some(w) = unsafe { get_mut::<MockXmlWriter>(&writer) } {
        w.buf.push_str(&format!("<{}>", n));
    }
    0
}

/// `oakcommon_xml_writer_write_end_element`: append `</>`.
#[no_mangle]
pub extern "C" fn oakcommon_xml_writer_write_end_element(writer: CHandle) -> c_int {
    // SAFETY: handle boxes a MockXmlWriter.
    if let Some(w) = unsafe { get_mut::<MockXmlWriter>(&writer) } {
        w.buf.push_str("</>");
    }
    0
}

/// `oakcommon_xml_writer_write_end_document`: no-op.
#[no_mangle]
pub extern "C" fn oakcommon_xml_writer_write_end_document(_writer: CHandle) -> c_int {
    0
}

/// `oakcommon_xml_writer_write_attribute`: append ` key="value"`.
#[no_mangle]
pub extern "C" fn oakcommon_xml_writer_write_attribute(
    writer: CHandle,
    key: *const c_char,
    value: *const c_char,
) -> c_int {
    // SAFETY: caller passes NUL-terminated strings.
    let (k, v) = unsafe { (cstr(key), cstr(value)) };
    // SAFETY: handle boxes a MockXmlWriter.
    if let Some(w) = unsafe { get_mut::<MockXmlWriter>(&writer) } {
        w.buf.push_str(&format!(" {}=\"{}\"", k, v));
    }
    0
}

/// `oakcommon_xml_writer_write_characters`: append raw text.
#[no_mangle]
pub extern "C" fn oakcommon_xml_writer_write_characters(writer: CHandle, text: *const c_char) -> c_int {
    // SAFETY: caller passes a NUL-terminated string.
    let t = unsafe { cstr(text) };
    // SAFETY: handle boxes a MockXmlWriter.
    if let Some(w) = unsafe { get_mut::<MockXmlWriter>(&writer) } {
        w.buf.push_str(&t);
    }
    0
}

/// `oakcommon_xml_writer_write_text_element`: append `<name>text</name>`.
#[no_mangle]
pub extern "C" fn oakcommon_xml_writer_write_text_element(
    writer: CHandle,
    name: *const c_char,
    text: *const c_char,
) -> c_int {
    // SAFETY: caller passes NUL-terminated strings.
    let (n, t) = unsafe { (cstr(name), cstr(text)) };
    // SAFETY: handle boxes a MockXmlWriter.
    if let Some(w) = unsafe { get_mut::<MockXmlWriter>(&writer) } {
        w.buf.push_str(&format!("<{}>{}</{}>", n, t, n));
    }
    0
}

/// `oakcommon_config_get_int`: return the supplied default (the mock keeps no
/// config store; the marker default colour path uses the default).
#[no_mangle]
pub extern "C" fn oakcommon_config_get_int(
    _group: *const c_char,
    _key: *const c_char,
    default: c_int,
) -> c_int {
    default
}

// ---------------------------------------------------------------------------
// oaknode mocks
// ---------------------------------------------------------------------------

/// Kind of a graph node in the mock.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum MockKind {
    /// A clip block.
    Clip,
    /// A gap block.
    Gap,
    /// A track.
    Track,
    /// A track list (owns a set of same-typed tracks).
    TrackList,
    /// A sequence/timeline.
    Sequence,
    /// A generic node.
    Node,
    /// A project.
    Project,
}

/// The state boxed behind every node-graph handle.
#[derive(Clone)]
pub struct MockNode {
    /// What kind of graph node this is.
    pub kind: MockKind,
    /// Locked flag (tracks only).
    pub locked: bool,
    /// Track type (`OAKNODE_TRACK_TYPE_*`; tracks and track lists only).
    pub track_type: i32,
    /// In point as `(num, den)`.
    pub in_: (i32, i32),
    /// Out point as `(num, den)`.
    pub out: (i32, i32),
    /// Length as `(num, den)`.
    pub length: (i32, i32),
    /// Media-in as `(num, den)`.
    pub media_in: (i32, i32),
    /// Enabled flag.
    pub enabled: bool,
    /// Previous block on the track (raw pointer; null when none).
    pub prev: *mut MockNode,
    /// Next block on the track (raw pointer; null when none).
    pub next: *mut MockNode,
    /// Owning track (raw pointer; null when detached).
    pub track: *mut MockNode,
    /// Linked blocks (raw pointers).
    pub links: Vec<*mut MockNode>,
    /// Owning sequence (raw pointer; null when detached).
    pub sequence: *mut MockNode,
    /// Owning project (raw pointer; null when detached).
    pub project: *mut MockNode,
    /// Output connection count.
    pub output_conns: i32,
    /// Ordered owned blocks (tracks only).
    pub blocks: Vec<*mut MockNode>,
    /// Borrowed marker list handle (viewer nodes only).
    pub markers: CHandle,
    /// Borrowed work area handle (viewer nodes only).
    pub work_area: CHandle,
}

// The mock is single-threaded; raw pointer members are fine.
unsafe impl Send for MockNode {}

impl Default for MockNode {
    fn default() -> Self {
        MockNode {
            kind: MockKind::Node,
            locked: false,
            track_type: -1, // OAKNODE_TRACK_TYPE_NONE
            in_: (0, 1),
            out: (0, 1),
            length: (0, 1),
            media_in: (0, 1),
            enabled: true,
            prev: std::ptr::null_mut(),
            next: std::ptr::null_mut(),
            track: std::ptr::null_mut(),
            links: Vec::new(),
            sequence: std::ptr::null_mut(),
            project: std::ptr::null_mut(),
            output_conns: 0,
            blocks: Vec::new(),
            markers: CHandle::null(),
            work_area: CHandle::null(),
        }
    }
}

/// `oaknode_block_clip_create`: a new clip block.
#[no_mangle]
pub extern "C" fn oaknode_block_clip_create() -> CHandle {
    make_owned(MockNode {
        kind: MockKind::Clip,
        ..Default::default()
    })
}

/// `oaknode_block_gap_create`: a new gap block.
#[no_mangle]
pub extern "C" fn oaknode_block_gap_create() -> CHandle {
    make_owned(MockNode {
        kind: MockKind::Gap,
        ..Default::default()
    })
}

/// `oaknode_block_as_node`: a borrowed generic-node view of a block.
#[no_mangle]
pub extern "C" fn oaknode_block_as_node(block: CHandle) -> CHandle {
    ref_clone(&block)
}

/// `oaknode_block_get_in`: read the in point as an int pair.
#[no_mangle]
pub extern "C" fn oaknode_block_get_in(
    block: CHandle,
    numerator: *mut c_int,
    denominator: *mut c_int,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get::<MockNode>(&block) }) else {
        return -1;
    };
    write_pair(numerator, denominator, b.in_);
    0
}

/// `oaknode_block_get_out`: read the out point as an int pair.
#[no_mangle]
pub extern "C" fn oaknode_block_get_out(
    block: CHandle,
    numerator: *mut c_int,
    denominator: *mut c_int,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get::<MockNode>(&block) }) else {
        return -1;
    };
    write_pair(numerator, denominator, b.out);
    0
}

/// `oaknode_block_get_length`: read the length as an int pair.
#[no_mangle]
pub extern "C" fn oaknode_block_get_length(
    block: CHandle,
    numerator: *mut c_int,
    denominator: *mut c_int,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get::<MockNode>(&block) }) else {
        return -1;
    };
    write_pair(numerator, denominator, b.length);
    0
}

/// `oaknode_block_set_length_and_media_out`: set length, keeping the media-in
/// fixed (the out point follows).
#[no_mangle]
pub extern "C" fn oaknode_block_set_length_and_media_out(
    block: CHandle,
    numerator: c_int,
    denominator: c_int,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get_mut::<MockNode>(&block) }) else {
        return -1;
    };
    b.length = (numerator, denominator);
    b.out = add_pair(b.media_in, b.length);
    0
}

/// `oaknode_block_set_length_and_media_in`: set length, keeping the out point
/// fixed (the media-in follows).
#[no_mangle]
pub extern "C" fn oaknode_block_set_length_and_media_in(
    block: CHandle,
    numerator: c_int,
    denominator: c_int,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get_mut::<MockNode>(&block) }) else {
        return -1;
    };
    b.length = (numerator, denominator);
    b.media_in = sub_pair(b.out, b.length);
    0
}

/// `oaknode_block_get_enabled`: read the enabled flag.
#[no_mangle]
pub extern "C" fn oaknode_block_get_enabled(block: CHandle, enabled: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get::<MockNode>(&block) }) else {
        return -1;
    };
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *enabled = b.enabled as c_int };
    0
}

/// `oaknode_block_set_enabled`: set the enabled flag.
#[no_mangle]
pub extern "C" fn oaknode_block_set_enabled(block: CHandle, enabled: c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get_mut::<MockNode>(&block) }) else {
        return -1;
    };
    b.enabled = enabled != 0;
    0
}

/// `oaknode_block_get_previous`: write a borrowed handle to the previous block.
#[no_mangle]
pub extern "C" fn oaknode_block_get_previous(block: CHandle, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get::<MockNode>(&block) }) else {
        return -1;
    };
    write_ptr_handle(out, b.prev);
    0
}

/// `oaknode_block_get_next`: write a borrowed handle to the next block.
#[no_mangle]
pub extern "C" fn oaknode_block_get_next(block: CHandle, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get::<MockNode>(&block) }) else {
        return -1;
    };
    write_ptr_handle(out, b.next);
    0
}

/// `oaknode_block_get_track`: write a borrowed handle to the owning track.
#[no_mangle]
pub extern "C" fn oaknode_block_get_track(block: CHandle, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get::<MockNode>(&block) }) else {
        return -1;
    };
    write_ptr_handle(out, b.track);
    0
}

/// `oaknode_block_link`: link two blocks so they move together.
#[no_mangle]
pub extern "C" fn oaknode_block_link(a: CHandle, b: CHandle) -> c_int {
    // SAFETY: both handles box MockNodes.
    let (pa, pb) = unsafe {
        match (get_mut::<MockNode>(&a), get_mut::<MockNode>(&b)) {
            (Some(ma), Some(mb)) => (ma as *mut MockNode, mb as *mut MockNode),
            _ => return -1,
        }
    };
    // SAFETY: pa/pb are the heap addresses of the two boxes.
    let (pa, pb) = (pa, pb);
    // SAFETY: boxes remain alive (handles held).
    let (pa, pb) = unsafe { (pa, pb) };
    // SAFETY: both boxes remain alive (handles held).
    unsafe { (*pa).links.push(pb) };
    // SAFETY: both boxes remain alive (handles held).
    unsafe { (*pb).links.push(pa) };
    0
}

/// `oaknode_block_unlink`: unlink two blocks.
#[no_mangle]
pub extern "C" fn oaknode_block_unlink(a: CHandle, b: CHandle) -> c_int {
    // SAFETY: both handles box MockNodes.
    let (pa, pb) = unsafe {
        match (get_mut::<MockNode>(&a), get_mut::<MockNode>(&b)) {
            (Some(ma), Some(mb)) => (ma as *mut MockNode, mb as *mut MockNode),
            _ => return -1,
        }
    };
    // SAFETY: both boxes remain alive (handles held).
    let (pa, pb) = unsafe { (pa, pb) };
    // SAFETY: both boxes remain alive (handles held).
    unsafe { (*pa).links.retain(|&p| p != pb) };
    // SAFETY: both boxes remain alive (handles held).
    unsafe { (*pb).links.retain(|&p| p != pa) };
    0
}

/// `oaknode_track_get_length`: read the track length as an int pair.
#[no_mangle]
pub extern "C" fn oaknode_track_get_length(track: CHandle, numerator: *mut c_int, denominator: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get::<MockNode>(&track) }) else {
        return -1;
    };
    let mut acc = (0, 1);
    for &p in &t.blocks {
        // SAFETY: block pointers reference alive boxes.
        let b = unsafe { &*p };
        acc = add_pair(acc, b.length);
    }
    write_pair(numerator, denominator, acc);
    0
}

/// `oaknode_track_get_sequence`: write a borrowed handle to the owning sequence.
#[no_mangle]
pub extern "C" fn oaknode_track_get_sequence(track: CHandle, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get::<MockNode>(&track) }) else {
        return -1;
    };
    write_ptr_handle(out, t.sequence);
    0
}

/// `oaknode_track_prepend_block`: insert a block at the front of the track.
#[no_mangle]
pub extern "C" fn oaknode_track_prepend_block(track: CHandle, block: CHandle) -> c_int {
    // SAFETY: both handles box MockNodes.
    let (pt, pb) = unsafe {
        match (get_mut::<MockNode>(&track), get_mut::<MockNode>(&block)) {
            (Some(mt), Some(mb)) => (mt as *mut MockNode, mb as *mut MockNode),
            _ => return -1,
        }
    };
    // SAFETY: boxes remain alive (handles held).
    let (pt, pb) = unsafe { (pt, pb) };
    // SAFETY: both boxes remain alive (handles held).
    let t = unsafe { &mut *pt };
    let b = unsafe { &mut *pb };
    t.blocks.insert(0, pb);
    b.track = pt;
    let next = t.blocks.get(1).copied().unwrap_or(std::ptr::null_mut());
    b.next = next;
    if !next.is_null() {
        // SAFETY: `next` is a block box that remains alive (in the track list).
        unsafe { (*next).prev = pb };
    }
    b.prev = std::ptr::null_mut();
    0
}

/// `oaknode_track_insert_block_after`: insert `block` after `before` on `track`.
#[no_mangle]
pub extern "C" fn oaknode_track_insert_block_after(
    track: CHandle,
    block: CHandle,
    before: CHandle,
) -> c_int {
    // SAFETY: handles box MockNodes.
    let (pt, pb, pbefore) = unsafe {
        match (get_mut::<MockNode>(&track), get_mut::<MockNode>(&block)) {
            (Some(mt), Some(mb)) => (mt as *mut MockNode, mb as *mut MockNode, if before.is_null() {
                std::ptr::null_mut()
            } else {
                match get::<MockNode>(&before) {
                    Some(mbf) => mbf as *const MockNode as *mut MockNode,
                    None => return -1,
                }
            }),
            _ => return -1,
        }
    };
    // SAFETY: boxes remain alive (handles held).
    let (pt, pb, pbefore) = unsafe { (pt, pb, pbefore) };
    let t = unsafe { &mut *pt };
    let b = unsafe { &mut *pb };
    // A null predecessor inserts at the front (C++ prepend semantics); a
    // non-null predecessor inserts immediately after it, or at the end when
    // it is not on this track.
    let pos = if pbefore.is_null() {
        0
    } else {
        t.blocks
            .iter()
            .position(|&p| p == pbefore)
            .map(|i| i + 1)
            .unwrap_or(t.blocks.len())
    };
    t.blocks.insert(pos, pb);
    b.track = pt;
    // Relink neighbours around the inserted block.
    if pos > 0 {
        b.prev = t.blocks[pos - 1];
    } else {
        b.prev = std::ptr::null_mut();
    }
    b.next = t.blocks.get(pos + 1).copied().unwrap_or(std::ptr::null_mut());
    if !b.prev.is_null() {
        // SAFETY: `b.prev` is a block box that remains alive.
        unsafe { (*(b.prev)).next = pb };
    }
    if !b.next.is_null() {
        // SAFETY: `b.next` is a block box that remains alive.
        unsafe { (*(b.next)).prev = pb };
    }
    0
}

/// `oaknode_track_ripple_remove_block`: remove a block, shifting later ones
/// earlier; ownership returns to the caller (the caller keeps its handle).
#[no_mangle]
pub extern "C" fn oaknode_track_ripple_remove_block(track: CHandle, block: CHandle) -> c_int {
    // SAFETY: handles box MockNodes.
    let (pt, pb) = unsafe {
        match (get_mut::<MockNode>(&track), get_mut::<MockNode>(&block)) {
            (Some(mt), Some(mb)) => (mt as *mut MockNode, mb as *mut MockNode),
            _ => return -1,
        }
    };
    // SAFETY: boxes remain alive.
    let (pt, pb) = unsafe { (pt, pb) };
    let t = unsafe { &mut *pt };
    let b = unsafe { &mut *pb };
    let Some(pos) = t.blocks.iter().position(|&p| p == pb) else {
        return -1;
    };
    let prev = b.prev;
    let next = b.next;
    t.blocks.remove(pos);
    b.track = std::ptr::null_mut();
    b.prev = std::ptr::null_mut();
    b.next = std::ptr::null_mut();
    if !prev.is_null() {
        // SAFETY: `prev` is a block box that remains alive.
        unsafe { (*prev).next = next };
    }
    if !next.is_null() {
        // SAFETY: `next` is a block box that remains alive.
        unsafe { (*next).prev = prev };
    }
    0
}

/// `oaknode_track_replace_block`: replace `old_block` with `new_block`.
#[no_mangle]
pub extern "C" fn oaknode_track_replace_block(
    track: CHandle,
    old_block: CHandle,
    new_block: CHandle,
) -> c_int {
    // SAFETY: handles box MockNodes.
    let (pt, po, pn) = unsafe {
        match (
            get_mut::<MockNode>(&track),
            get_mut::<MockNode>(&old_block),
            get_mut::<MockNode>(&new_block),
        ) {
            (Some(mt), Some(mo), Some(mn)) => (mt as *mut MockNode, mo as *mut MockNode, mn as *mut MockNode),
            _ => return -1,
        }
    };
    // SAFETY: boxes remain alive (handles held).
    let (pt, po, pn) = unsafe { (pt, po, pn) };
    let t = unsafe { &mut *pt };
    let o = unsafe { &mut *po };
    let n = unsafe { &mut *pn };
    let Some(pos) = t.blocks.iter().position(|&p| p == po) else {
        return -1;
    };
    t.blocks[pos] = pn;
    o.track = std::ptr::null_mut();
    o.prev = std::ptr::null_mut();
    o.next = std::ptr::null_mut();
    n.track = pt;
    if pos > 0 {
        n.prev = t.blocks[pos - 1];
    } else {
        n.prev = std::ptr::null_mut();
    }
    n.next = t.blocks.get(pos + 1).copied().unwrap_or(std::ptr::null_mut());
    if !n.prev.is_null() {
        // SAFETY: `n.prev` is a block box that remains alive.
        unsafe { (*(n.prev)).next = pn };
    }
    if !n.next.is_null() {
        // SAFETY: `n.next` is a block box that remains alive.
        unsafe { (*(n.next)).prev = pn };
    }
    0
}

/// `oaknode_sequence_as_node`: a borrowed generic-node view of a sequence.
#[no_mangle]
pub extern "C" fn oaknode_sequence_as_node(sequence: CHandle) -> CHandle {
    ref_clone(&sequence)
}

/// `oaknode_sequence_from_node`: a borrowed sequence view of a generic node.
#[no_mangle]
pub extern "C" fn oaknode_sequence_from_node(node: CHandle) -> CHandle {
    ref_clone(&node)
}

/// `oaknode_node_get_project`: write a borrowed handle to the owning project.
#[no_mangle]
pub extern "C" fn oaknode_node_get_project(node: CHandle, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(n) = (unsafe { get::<MockNode>(&node) }) else {
        return -1;
    };
    write_ptr_handle(out, n.project);
    0
}

/// `oaknode_node_output_connection_count`: read the output connection count.
#[no_mangle]
pub extern "C" fn oaknode_node_output_connection_count(node: CHandle, out_count: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(n) = (unsafe { get::<MockNode>(&node) }) else {
        return -1;
    };
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *out_count = n.output_conns };
    0
}

/// `oaknode_node_get_markers`: write a borrowed handle to the node's markers.
#[no_mangle]
pub extern "C" fn oaknode_node_get_markers(node: CHandle, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(n) = (unsafe { get::<MockNode>(&node) }) else {
        return -1;
    };
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *out = ref_clone(&n.markers) };
    0
}

/// `oaknode_node_get_work_area`: write a borrowed handle to the node's work area.
#[no_mangle]
pub extern "C" fn oaknode_node_get_work_area(node: CHandle, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(n) = (unsafe { get::<MockNode>(&node) }) else {
        return -1;
    };
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *out = ref_clone(&n.work_area) };
    0
}

/// `oaknode_project_add_node`: adopt a node into a project.
#[no_mangle]
pub extern "C" fn oaknode_project_add_node(project: CHandle, node: CHandle) -> c_int {
    // SAFETY: handles box MockNodes.
    let (pp, pn) = unsafe {
        match (get_mut::<MockNode>(&project), get_mut::<MockNode>(&node)) {
            (Some(mp), Some(mn)) => (mp as *mut MockNode, mn as *mut MockNode),
            _ => return -1,
        }
    };
    // SAFETY: boxes remain alive.
    unsafe { (*pn).project = pp };
    0
}

/// `oaknode_project_remove_node`: remove a node from a project.
#[no_mangle]
pub extern "C" fn oaknode_project_remove_node(project: CHandle, node: CHandle) -> c_int {
    // SAFETY: handles box MockNodes.
    let (pp, pn) = unsafe {
        match (get_mut::<MockNode>(&project), get_mut::<MockNode>(&node)) {
            (Some(mp), Some(mn)) => (mp as *mut MockNode, mn as *mut MockNode),
            _ => return -1,
        }
    };
    // SAFETY: boxes remain alive.
    unsafe {
        if (*pn).project == pp {
            (*pn).project = std::ptr::null_mut();
        }
    }
    0
}

/// `oaknode_command_create_remove_node`: a command that removes a node. The
/// mock returns a command handle whose redo/undo no-op, but whose free drops
/// the wrapped node handle it borrows.
#[no_mangle]
pub extern "C" fn oaknode_command_create_remove_node(node: CHandle) -> CHandle {
    let node_owned = ref_clone(&node);
    make_owned(MockUndoCommand {
        vtable: super::undo::OakUndoCommandVtable {
            redo: None,
            undo: None,
            free_fn: Some(free_owned_handle),
        },
        userdata: Box::into_raw(Box::new(node_owned)) as *mut c_void,
    })
}

/// Free callback used by `oaknode_command_create_remove_node`: drop the
/// borrowed node handle.
extern "C" fn free_owned_handle(userdata: *mut c_void) {
    // SAFETY: userdata is the boxed CHandle we created.
    unsafe { drop(Box::from_raw(userdata as *mut CHandle)) };
}

/// `oaknode_block_get_kind`: map a block's kind to an `OAKNODE_BLOCK_*` int.
#[no_mangle]
pub extern "C" fn oaknode_block_get_kind(block: CHandle, out_kind: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(b) = (unsafe { get::<MockNode>(&block) }) else {
        return -1;
    };
    let k = match b.kind {
        MockKind::Clip => 1,        // OAKNODE_BLOCK_CLIP
        MockKind::Gap => 2,         // OAKNODE_BLOCK_GAP
        _ => 0,                     // OAKNODE_BLOCK_OTHER
    };
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *out_kind = k };
    0
}

/// `oaknode_block_from_node`: a borrowed block view of a generic node (empty
/// when the node is not a block).
#[no_mangle]
pub extern "C" fn oaknode_block_from_node(node: CHandle) -> CHandle {
    // SAFETY: handle boxes a MockNode.
    let Some(n) = (unsafe { get::<MockNode>(&node) }) else {
        return CHandle::null();
    };
    match n.kind {
        MockKind::Clip | MockKind::Gap => ref_clone(&node),
        _ => CHandle::null(),
    }
}

/// `oaknode_block_are_linked`: whether `a` and `b` are linked (`linked` gets
/// 1/0).
#[no_mangle]
pub extern "C" fn oaknode_block_are_linked(a: CHandle, b: CHandle, linked: *mut c_int) -> c_int {
    // SAFETY: both handles box MockNodes.
    let (pa, pb) = unsafe {
        match (get::<MockNode>(&a), get::<MockNode>(&b)) {
            (Some(ma), Some(mb)) => (
                ma as *const MockNode as *mut MockNode,
                mb as *const MockNode as *mut MockNode,
            ),
            _ => return -1,
        }
    };
    // SAFETY: boxes remain alive (handles held).
    let (pa, pb) = unsafe { (pa, pb) };
    // SAFETY: `a` is alive (handle held).
    let linked_flag = unsafe { (*pa).links.contains(&pb) } as c_int;
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *linked = linked_flag };
    0
}

/// `oaknode_clip_add_cache_passthrough_from`: copy render-cache passthroughs
/// from `other`. The mock has no cache model, so this is a no-op.
#[no_mangle]
pub extern "C" fn oaknode_clip_add_cache_passthrough_from(_clip: CHandle, _other: CHandle) -> c_int {
    0
}

/// `oaknode_clip_get_media_in`: read a clip's media-in as an int pair.
#[no_mangle]
pub extern "C" fn oaknode_clip_get_media_in(
    clip: CHandle,
    numerator: *mut c_int,
    denominator: *mut c_int,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(c) = (unsafe { get::<MockNode>(&clip) }) else {
        return -1;
    };
    if c.kind != MockKind::Clip {
        return -1;
    }
    write_pair(numerator, denominator, c.media_in);
    0
}

/// `oaknode_clip_set_media_in`: set a clip's media-in as an int pair.
#[no_mangle]
pub extern "C" fn oaknode_clip_set_media_in(
    clip: CHandle,
    numerator: c_int,
    denominator: c_int,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(c) = (unsafe { get_mut::<MockNode>(&clip) }) else {
        return -1;
    };
    if c.kind != MockKind::Clip {
        return -1;
    }
    c.media_in = (numerator, denominator);
    0
}

/// `oaknode_track_create`: a new detached track of the given type.
#[no_mangle]
pub extern "C" fn oaknode_track_create(kind: c_int) -> CHandle {
    make_owned(MockNode {
        kind: MockKind::Track,
        track_type: kind,
        ..Default::default()
    })
}

/// `oaknode_track_get_locked`: read a track's locked flag (`locked` gets 1/0).
#[no_mangle]
pub extern "C" fn oaknode_track_get_locked(track: CHandle, locked: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get::<MockNode>(&track) }) else {
        return -1;
    };
    if t.kind != MockKind::Track {
        return -1;
    }
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *locked = t.locked as c_int };
    0
}

/// `oaknode_track_set_locked`: set a track's locked flag.
#[no_mangle]
pub extern "C" fn oaknode_track_set_locked(track: CHandle, locked: c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get_mut::<MockNode>(&track) }) else {
        return -1;
    };
    if t.kind != MockKind::Track {
        return -1;
    }
    t.locked = locked != 0;
    0
}

/// `oaknode_track_get_block_count`: number of blocks on a track.
#[no_mangle]
pub extern "C" fn oaknode_track_get_block_count(track: CHandle, count: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get::<MockNode>(&track) }) else {
        return -1;
    };
    if t.kind != MockKind::Track {
        return -1;
    }
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *count = t.blocks.len() as c_int };
    0
}

/// `oaknode_track_get_block_at`: borrowed block at `index` on a track.
#[no_mangle]
pub extern "C" fn oaknode_track_get_block_at(track: CHandle, index: c_int, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get::<MockNode>(&track) }) else {
        return -1;
    };
    if t.kind != MockKind::Track {
        return -1;
    }
    let Some(&p) = t.blocks.get(index as usize) else {
        return -1;
    };
    write_ptr_handle(out, p);
    0
}

/// `oaknode_track_get_block_containing_time`: the block strictly containing
/// `time` (in < t < out); writes null and returns -1 when none.
#[no_mangle]
pub extern "C" fn oaknode_track_get_block_containing_time(
    track: CHandle,
    numerator: c_int,
    denominator: c_int,
    out: *mut CHandle,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get::<MockNode>(&track) }) else {
        return -1;
    };
    if t.kind != MockKind::Track {
        return -1;
    }
    let time = (numerator, denominator);
    for &p in &t.blocks {
        // SAFETY: block pointers reference alive boxes.
        let b = unsafe { &*p };
        if pair_cmp(b.in_, time) == std::cmp::Ordering::Less
            && pair_cmp(time, b.out) == std::cmp::Ordering::Less
        {
            write_ptr_handle(out, p);
            return 0;
        }
    }
    write_ptr_handle(out, std::ptr::null_mut());
    -1
}

/// `oaknode_track_get_nearest_block_before_or_at`: the block whose in point is
/// at or before `time` (the latest such); null when none.
#[no_mangle]
pub extern "C" fn oaknode_track_get_nearest_block_before_or_at(
    track: CHandle,
    numerator: c_int,
    denominator: c_int,
    out: *mut CHandle,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get::<MockNode>(&track) }) else {
        return -1;
    };
    if t.kind != MockKind::Track {
        return -1;
    }
    let time = (numerator, denominator);
    let mut best: *mut MockNode = std::ptr::null_mut();
    for &p in &t.blocks {
        // SAFETY: block pointers reference alive boxes.
        let b = unsafe { &*p };
        if pair_cmp(b.in_, time) != std::cmp::Ordering::Greater {
            if best.is_null() || pair_cmp(b.in_, unsafe { &*best }.in_) == std::cmp::Ordering::Greater {
                best = p;
            }
        }
    }
    write_ptr_handle(out, best);
    0
}

/// `oaknode_track_get_nearest_block_after_or_at`: the block whose in point is
/// at or after `time` (the earliest such); null when none.
#[no_mangle]
pub extern "C" fn oaknode_track_get_nearest_block_after_or_at(
    track: CHandle,
    numerator: c_int,
    denominator: c_int,
    out: *mut CHandle,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(t) = (unsafe { get::<MockNode>(&track) }) else {
        return -1;
    };
    if t.kind != MockKind::Track {
        return -1;
    }
    let time = (numerator, denominator);
    let mut best: *mut MockNode = std::ptr::null_mut();
    for &p in &t.blocks {
        // SAFETY: block pointers reference alive boxes.
        let b = unsafe { &*p };
        if pair_cmp(b.in_, time) != std::cmp::Ordering::Less {
            if best.is_null() || pair_cmp(b.in_, unsafe { &*best }.in_) == std::cmp::Ordering::Less {
                best = p;
            }
        }
    }
    write_ptr_handle(out, best);
    0
}

/// `oaknode_tracklist_get_type`: the track list's track type.
#[no_mangle]
pub extern "C" fn oaknode_tracklist_get_type(list: CHandle, kind: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(l) = (unsafe { get::<MockNode>(&list) }) else {
        return -1;
    };
    if l.kind != MockKind::TrackList {
        return -1;
    }
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *kind = l.track_type };
    0
}

/// `oaknode_tracklist_get_track_count`: number of tracks in a track list.
#[no_mangle]
pub extern "C" fn oaknode_tracklist_get_track_count(list: CHandle, count: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(l) = (unsafe { get::<MockNode>(&list) }) else {
        return -1;
    };
    if l.kind != MockKind::TrackList {
        return -1;
    }
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *count = l.blocks.len() as c_int };
    0
}

/// `oaknode_tracklist_get_track_at`: borrowed track at `index` in a track list.
#[no_mangle]
pub extern "C" fn oaknode_tracklist_get_track_at(list: CHandle, index: c_int, out: *mut CHandle) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(l) = (unsafe { get::<MockNode>(&list) }) else {
        return -1;
    };
    if l.kind != MockKind::TrackList {
        return -1;
    }
    let Some(&p) = l.blocks.get(index as usize) else {
        return -1;
    };
    write_ptr_handle(out, p);
    0
}

/// `oaknode_tracklist_array_append`: append a track-array element on the
/// parent sequence. The mock keeps track lists as plain vectors, so this is a
/// no-op.
#[no_mangle]
pub extern "C" fn oaknode_tracklist_array_append(_list: CHandle) -> c_int {
    0
}

/// `oaknode_tracklist_array_remove_last`: remove the last track-array element.
/// No-op in the mock (see `oaknode_tracklist_array_append`).
#[no_mangle]
pub extern "C" fn oaknode_tracklist_array_remove_last(_list: CHandle) -> c_int {
    0
}

/// `oaknode_sequence_get_track_list`: the borrowed per-type track list, or
/// null when the sequence has no list of that type.
#[no_mangle]
pub extern "C" fn oaknode_sequence_get_track_list(
    sequence: CHandle,
    kind: c_int,
    out: *mut CHandle,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(s) = (unsafe { get::<MockNode>(&sequence) }) else {
        return -1;
    };
    if s.kind != MockKind::Sequence {
        return -1;
    }
    let mut found: *mut MockNode = std::ptr::null_mut();
    for &p in &s.blocks {
        // SAFETY: track-list pointers reference alive boxes.
        let l = unsafe { &*p };
        if l.kind == MockKind::TrackList && l.track_type == kind {
            found = p;
            break;
        }
    }
    write_ptr_handle(out, found);
    0
}

/// `oaknode_sequence_get_all_track_count`: total connected tracks across all
/// types.
#[no_mangle]
pub extern "C" fn oaknode_sequence_get_all_track_count(sequence: CHandle, count: *mut c_int) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(s) = (unsafe { get::<MockNode>(&sequence) }) else {
        return -1;
    };
    if s.kind != MockKind::Sequence {
        return -1;
    }
    let mut total = 0;
    for &p in &s.blocks {
        // SAFETY: track-list pointers reference alive boxes.
        let l = unsafe { &*p };
        if l.kind == MockKind::TrackList {
            total += l.blocks.len();
        }
    }
    // SAFETY: caller guarantees a valid pointer.
    unsafe { *count = total as c_int };
    0
}

/// `oaknode_sequence_get_all_track_at`: borrowed track at `index` across the
/// flat, all-types track list.
#[no_mangle]
pub extern "C" fn oaknode_sequence_get_all_track_at(
    sequence: CHandle,
    index: c_int,
    out: *mut CHandle,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(s) = (unsafe { get::<MockNode>(&sequence) }) else {
        return -1;
    };
    if s.kind != MockKind::Sequence {
        return -1;
    }
    let mut flat: Vec<*mut MockNode> = Vec::new();
    for &p in &s.blocks {
        // SAFETY: track-list pointers reference alive boxes.
        let l = unsafe { &*p };
        if l.kind == MockKind::TrackList {
            flat.extend_from_slice(&l.blocks);
        }
    }
    let Some(&p) = flat.get(index as usize) else {
        return -1;
    };
    write_ptr_handle(out, p);
    0
}

/// `oaknode_node_connect`: connect `output_node` to `input_node`'s `input_id`.
/// The mock tracks only the output connection count (input edge table is not
/// modelled), so this bumps `output_conns`.
#[no_mangle]
pub extern "C" fn oaknode_node_connect(
    output_node: CHandle,
    _input_node: CHandle,
    _input_id: *const c_char,
) -> c_int {
    // SAFETY: handle boxes a MockNode.
    let Some(o) = (unsafe { get_mut::<MockNode>(&output_node) }) else {
        return -1;
    };
    o.output_conns += 1;
    0
}

/// `oaknode_node_disconnect`: remove the edge feeding `input_node`'s
/// `input_id`. The mock has no input-edge table, so this is a no-op.
#[no_mangle]
pub extern "C" fn oaknode_node_disconnect(_input_node: CHandle, _input_id: *const c_char) -> c_int {
    0
}

/// `oaknode_node_copy_in_graph`: clone `node` in its graph; `*out_command`
/// receives an owned undo handle. The mock clones the node box and hands back
/// a no-op command.
#[no_mangle]
pub extern "C" fn oaknode_node_copy_in_graph(node: CHandle, out_command: *mut CHandle) -> CHandle {
    // SAFETY: handle boxes a MockNode.
    let Some(n) = (unsafe { get::<MockNode>(&node) }) else {
        return CHandle::null();
    };
    let copy = n.clone();
    let copy_handle = make_owned(copy);
    if !out_command.is_null() {
        // SAFETY: caller guarantees a valid out pointer.
        unsafe {
            *out_command = make_owned(MockUndoCommand {
                vtable: super::undo::OakUndoCommandVtable {
                    redo: None,
                    undo: None,
                    free_fn: None,
                },
                userdata: std::ptr::null_mut(),
            });
        };
    }
    copy_handle
}

// ---------------------------------------------------------------------------
// shared helpers
// ---------------------------------------------------------------------------

/// Compare two `(num, den)` pairs as fractions (denominators assumed
/// positive).
fn pair_cmp(a: (i32, i32), b: (i32, i32)) -> std::cmp::Ordering {
    let lhs = a.0 as i64 * b.1 as i64;
    let rhs = b.0 as i64 * a.1 as i64;
    lhs.cmp(&rhs)
}

/// Write a `(num, den)` pair into the out params (NULL-safe).
fn write_pair(num: *mut c_int, den: *mut c_int, pair: (i32, i32)) {
    // SAFETY: caller passes optional valid pointers.
    unsafe {
        if !num.is_null() {
            *num = pair.0;
        }
        if !den.is_null() {
            *den = pair.1;
        }
    }
}

/// Write a NUL-terminated copy of `s` into `buf` (two-stage contract: caller
/// calls once with a too-small buffer to get the size, then with enough).
fn write_cstr(buf: *mut c_char, buf_size: c_int, s: &str) {
    if buf.is_null() {
        return;
    }
    let bytes = s.as_bytes();
    let cap = buf_size as usize;
    let copy_len = bytes.len().min(cap.max(1) - 1);
    // SAFETY: caller guarantees `buf` points to at least `buf_size` bytes.
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, copy_len);
        *buf.add(copy_len) = 0;
    }
}

/// Read a C string into a `String` (empty on null).
///
/// # Safety
/// `p` must be a valid NUL-terminated string or null.
unsafe fn cstr(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    // SAFETY: caller guarantees a NUL-terminated string.
    unsafe { std::ffi::CStr::from_ptr(p) }
        .to_string_lossy()
        .into_owned()
}

/// Add two `(num, den)` pairs as fractions (crude but sufficient for the
/// mock's small integer test values).
fn add_pair(a: (i32, i32), b: (i32, i32)) -> (i32, i32) {
    if a.1 == b.1 {
        (a.0 + b.0, a.1)
    } else {
        let num = a.0 as i64 * b.1 as i64 + b.0 as i64 * a.1 as i64;
        let den = a.1 as i64 * b.1 as i64;
        (num as i32, den as i32)
    }
}

/// Subtract `b` from `a` as fractions.
fn sub_pair(a: (i32, i32), b: (i32, i32)) -> (i32, i32) {
    if a.1 == b.1 {
        (a.0 - b.0, a.1)
    } else {
        let num = a.0 as i64 * b.1 as i64 - b.0 as i64 * a.1 as i64;
        let den = a.1 as i64 * b.1 as i64;
        (num as i32, den as i32)
    }
}

/// Return a reference-counted copy of a handle (increments the box count and
/// returns the same `ctx`), modelling a borrowed handle.
fn ref_clone(h: &CHandle) -> CHandle {
    if h.ctx.is_null() {
        return CHandle::null();
    }
    // SAFETY: h.addref is the addref callback for the boxed type.
    if let Some(a) = h.addref {
        unsafe { a(h.ctx) };
    }
    CHandle {
        ctx: h.ctx,
        addref: h.addref,
        release: h.release,
        abi_version: h.abi_version,
    }
}

/// Write a borrowed handle for a raw box pointer into `out`, or a null handle
/// when the pointer is null.
fn write_ptr_handle(out: *mut CHandle, p: *mut MockNode) {
    if out.is_null() {
        return;
    }
    // SAFETY: caller guarantees a valid out pointer.
    unsafe {
        if p.is_null() {
            *out = CHandle::null();
        } else {
            // The raw pointer is the heap address of the box; build a
            // borrowed handle around it.
            *out = CHandle {
                ctx: p as *mut c_void,
                addref: None,
                release: None,
                abi_version: crate::handle::OAKTIMELINE_ABI_VERSION,
            };
        }
    }
}

/// Release a handle's box and clear it (used by the `*_free` mocks); NULL /
/// empty no-op.
fn free_box<T: 'static>(h: *mut CHandle) {
    if h.is_null() {
        return;
    }
    // SAFETY: caller passes a valid pointer.
    let handle = unsafe { &mut *h };
    if handle.ctx.is_null() {
        return;
    }
    // SAFETY: handle.ctx is a RefBox<T> with a single reference.
    unsafe { drop(Box::from_raw(handle.ctx as *mut RefBox<T>)) };
    handle.ctx = std::ptr::null_mut();
    handle.addref = None;
    handle.release = None;
    handle.abi_version = 0;
}
