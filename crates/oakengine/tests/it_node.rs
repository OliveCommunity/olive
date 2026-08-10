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

//! Integration tests for the node family: every `oakengine_*` export of
//! the facade's node module (src/node.rs; module C contract
//! `include/node/{node,project,footage,keyframe,dragger,group,multicam,...}.h`),
//! exercised end to end against the REAL `oaknode` crate — no mocks.
//!
//! Coverage contract (see the sibling it_plugin.rs):
//! - every export is called on a legal path with the result asserted;
//! - value-range inputs (indices, type ordinals, sizes, track counts) get
//!   the meaningful combinations;
//! - illegal inputs (NULL / empty `CHandle` boxes, out-of-range indices,
//!   zero/negative sizes, garbage enum ordinals) return a clean negative
//!   code or the documented no-op — never a crash;
//! - destroy contracts: facade `free`/`dispose` are NULL/empty no-ops, and
//!   the module destroy paths they delegate to are double-free-safe;
//! - the family's debug alive counter (`oaknode_debug_alive_count`) returns
//!   to baseline after every owned-object round trip.
//!
//! The facade owns a process-wide undo stack, so every test that pushes
//! undoable commands (or asserts undo state) runs inside [`with_owned`],
//! the same global mutex that guards the alive-counter assertions (owned
//! objects are the only ALIVE sources). Tests that only touch borrowed
//! handles or static helpers run in parallel.
//!
//! The node family is 327 exports. The facade divergences found while
//! exercising the family (documented in the block below the tests) are all
//! fixed in src/node.rs and asserted as correct behavior here.

#[path = "common/mod.rs"]
mod common;

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::sync::Mutex;

use oakengine::common::OakVideoParamsPod;
use oakengine::handle::{
	CHandle, OakEngineFootage, OakEngineKeyframe, OakEngineNode, OakEngineNodeDragger,
	OakEngineProject,
};
use oakengine::node::*;
use oakengine::node::value_type as vt;
use oakengine::undo::oakengine_undo_command_free;
use oaknode::ffi::dragger::oaknode_dragger_free;
use oaknode::ffi::factory::oaknode_factory_create_from_id;
use oaknode::ffi::keyframe::oaknode_keyframe_create as oaknode_kf_create;
use oaknode::ffi::keyframe::oaknode_keyframe_free;
use oaknode::ffi::node::oaknode_debug_alive_count;
use oaknode::ffi::node::oaknode_node_free;
use oaknode::ffi::project::oaknode_project_free;

/// Facade error codes (src/error.rs).
const E_INVALID: c_int = -1;
const E_STATE: c_int = -2;
const E_NOT_FOUND: c_int = -4;
/// Module error codes (include/node/error.h) — passed through untranslated.
const NODE_E_INVALID: c_int = -30001;
const NODE_E_STATE: c_int = -30002;
const NODE_E_NOT_FOUND: c_int = -30004;

/// Registered node type ids used by the tests (NUL-terminated `&CStr` so
/// `.as_ptr()` is a valid C string, matching the engine's `c"..."` usage).
const TYPE_VALUE: &std::ffi::CStr = c"org.olivevideoeditor.Olive.value";
const TYPE_SOLID: &std::ffi::CStr = c"org.olivevideoeditor.Olive.solidgenerator";
const TYPE_TRANSFORM: &std::ffi::CStr = c"org.olivevideoeditor.Olive.transform";
const TYPE_TEXT: &std::ffi::CStr = c"org.olivevideoeditor.Olive.textgenerator";
const TYPE_GROUP: &std::ffi::CStr = c"org.olivevideoeditor.Olive.group";
const TYPE_MULTICAM: &std::ffi::CStr = c"org.olivevideoeditor.Olive.multicam";
const TYPE_FOOTAGE: &std::ffi::CStr = c"org.olivevideoeditor.Olive.footage";

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

/// Serialize the owned-object / undo-stack tests: the alive counter and
/// the process-wide undo stack are shared across tests in this binary, so
/// every test that creates owned objects or pushes commands holds the same
/// mutex. Tests that only use borrowed handles run unguarded.
fn with_owned(f: impl FnOnce()) {
	static LOCK: Mutex<()> = Mutex::new(());
	let _g = LOCK.lock().unwrap_or_else(|e| e.into_inner());
	f();
}

/// Live owned objects (projects, factory/group/copy nodes).
fn alive() -> c_int {
	unsafe { oaknode_debug_alive_count() }
}

/// Read a two-stage facade string out of a fixed buffer.
unsafe fn read_buf(buf: &[c_char]) -> String {
	if buf.first().copied().unwrap_or(0) == 0 {
		String::new()
	} else {
		CStr::from_ptr(buf.as_ptr()).to_string_lossy().into_owned()
	}
}

/// Read a NUL-terminated C string.
unsafe fn read_cstr(s: *const c_char) -> String {
	unsafe { CStr::from_ptr(s).to_string_lossy().into_owned() }
}

/// A float POD value.
fn float_value(x: f64) -> OakNodeValue {
	OakNodeValue { kind: vt::FLOAT, num: 0, den: 0, f: [x, 0.0, 0.0, 0.0] }
}

/// An int (combo-compatible) POD value.
fn int_value(x: i64) -> OakNodeValue {
	OakNodeValue { kind: vt::INT, num: x, den: 0, f: [0.0; 4] }
}

/// A vec2 POD value.
fn vec2_value(x: f64, y: f64) -> OakNodeValue {
	OakNodeValue { kind: vt::VEC2, num: 0, den: 0, f: [x, y, 0.0, 0.0] }
}

/// A fresh project box (owned; caller frees with `oakengine_project_free`).
fn new_project() -> *mut OakEngineProject {
	let p = oakengine_project_create();
	assert!(!p.is_null());
	assert_eq!(unsafe { oakengine_project_new(p) }, 0);
	p
}

/// A box wrapping an empty (null-ctx) node handle — the "invalid handle"
/// class distinct from a NULL pointer.
fn empty_node_box() -> *mut OakEngineNode {
	Box::into_raw(Box::new(OakEngineNode { handle: CHandle::null() }))
}

/// A live box holding an addref'd copy of `node`'s handle, to hand to
/// `oakengine_node_group_get_inner` (that export replaces the box with the
/// resolved node; the replaced shell is leaked by design of the API). The
/// copy is addref'd so freeing this shell and the original handle later
/// both release their own reference (no double-free).
unsafe fn group_inner_slot(node: *mut OakEngineNode) -> *mut OakEngineNode {
	let mut handle = unsafe { (*node).handle };
	if let Some(addref) = handle.addref {
		unsafe { addref(handle.ctx) };
	}
	Box::into_raw(Box::new(OakEngineNode { handle }))
}

/// Fresh file under the system temp dir with a unique name.
fn fresh_temp_file(name: &str, contents: &[u8]) -> std::path::PathBuf {
	let p = std::env::temp_dir().join(format!("oak-it-node-{}-{name}", std::process::id()));
	std::fs::write(&p, contents).expect("write temp file");
	p
}

/// The index of the first project node whose type id matches `id`, or -1.
unsafe fn find_node(project: *mut OakEngineProject, id: &str) -> c_int {
	let count = oakengine_project_node_count(project);
	for i in 0..count {
		let node = oakengine_project_node_at(project, i);
		if node.is_null() {
			continue;
		}
		let mut buf = [0 as c_char; 256];
		let len = oakengine_node_get_type_id(node, buf.as_mut_ptr(), 256);
		if len > 0 && unsafe { read_buf(&mut buf) } == id {
			return i;
		}
	}
	-1
}

/// Force the oakundo command / oakcommon xml dlsym targets into the link
/// (the oaknode serializer bridge resolves them at runtime; same helper as
/// the sibling tests/node.rs).
fn force_oakundo_command_link() -> usize {
	let fns: [usize; 3] = [
		oakundo::ffi::command::oakundo_command_init as *const () as usize,
		oakcommon::ffi::xmlutils::oakcommon_xml_writer_init as *const () as usize,
		oakcommon::ffi::xmlutils::oakcommon_xml_reader_init as *const () as usize,
	];
	fns.iter().sum()
}

// ---------------------------------------------------------------------------
// Static helpers and pure functions (no handles; parallel-safe)
// ---------------------------------------------------------------------------

/// Static node ids, category names, value-type names and value math.
#[test]
fn static_ids_and_pure_helpers() {
	common::force_link();
	let _ = force_oakundo_command_link();

	// Static input-id strings.
	assert_eq!(unsafe { read_cstr(oakengine_folder_child_input_key()) }, "child_in");
	assert_eq!(unsafe { read_cstr(oakengine_node_enabled_input_id()) }, "enabled_in");
	assert_eq!(unsafe { read_cstr(oakengine_volume_samples_input_id()) }, "samples_in");
	assert_eq!(unsafe { read_cstr(oakengine_transform_texture_input_id()) }, "tex_in");
	assert_eq!(unsafe { read_cstr(oakengine_transition_in_block_input_id()) }, "in_block_in");
	assert_eq!(unsafe { read_cstr(oakengine_transition_out_block_input_id()) }, "out_block_in");
	assert_eq!(unsafe { read_cstr(oakengine_subtitle_text_input_id()) }, "text_in");
	assert_eq!(unsafe { read_cstr(oakengine_project_item_mime_type()) },
		"application/x-oliveprojectitemdata");
	assert_eq!(unsafe { read_cstr(oakengine_multicam_input_current()) }, "current_in");
	assert_eq!(unsafe { read_cstr(oakengine_multicam_input_sources()) }, "sources_in");
	assert_eq!(unsafe { read_cstr(oakengine_multicam_input_sequence()) }, "sequence_in");
	assert_eq!(unsafe { read_cstr(oakengine_multicam_input_sequence_type()) }, "sequence_type_in");

	// Static flags.
	assert_eq!(oakengine_node_flag_dont_show_in_param_view(), 0x1);
	assert_eq!(oakengine_node_flag_video_effect(), 0x2);
	assert_eq!(oakengine_node_flag_audio_effect(), 0x4);
	assert_eq!(oakengine_node_flag_dont_show_in_create_menu(), 0x8);

	// Static scalars.
	assert_eq!(oakengine_audio_waveform_max_sample_rate(), 1024.0);
	assert_eq!(oakengine_keyframe_default_type(), 0);
	assert_eq!(oakengine_keyframe_opposing_bezier_type(0), 1);
	assert_eq!(oakengine_keyframe_opposing_bezier_type(1), 0);
	// Garbage type ordinal → module INVALID, never a crash.
	assert_eq!(oakengine_keyframe_opposing_bezier_type(2), NODE_E_INVALID);
	assert_eq!(oakengine_keyframe_opposing_bezier_type(-7), NODE_E_INVALID);

	// Category name matrix: 0..=11 named, everything else "Uncategorized".
	let mut buf = [0 as c_char; 64];
	for (cat, expected) in [
		(0, "Output"), (1, "Generator"), (2, "Math"), (3, "Keying"),
		(4, "Filter"), (5, "Color"), (6, "Time"), (7, "Timeline"),
		(8, "Transition"), (9, "Distort"), (10, "Project"), (11, "OpenFX"),
	] {
		let len = unsafe { oakengine_node_category_name(cat, buf.as_mut_ptr(), 64) };
		assert_eq!(len, expected.len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, expected);
	}
	assert_eq!(unsafe { oakengine_node_category_name(12, buf.as_mut_ptr(), 64) }, 13);
	assert_eq!(unsafe { read_buf(&mut buf) }, "Uncategorized");
	assert_eq!(unsafe { oakengine_node_category_name(-1, buf.as_mut_ptr(), 64) }, 13);
	assert_eq!(unsafe { read_buf(&mut buf) }, "Uncategorized");

	// Value keyframe track counts: 1 scalar, 2/3/4 vectors, 4 color, 6 bezier.
	assert_eq!(oakengine_node_value_keyframe_track_count(vt::FLOAT), 1);
	assert_eq!(oakengine_node_value_keyframe_track_count(vt::INT), 1);
	assert_eq!(oakengine_node_value_keyframe_track_count(vt::VEC2), 2);
	assert_eq!(oakengine_node_value_keyframe_track_count(vt::VEC3), 3);
	assert_eq!(oakengine_node_value_keyframe_track_count(vt::VEC4), 4);
	assert_eq!(oakengine_node_value_keyframe_track_count(vt::COLOR), 4);
	assert_eq!(oakengine_node_value_keyframe_track_count(vt::BEZIER), 6);
	assert_eq!(oakengine_node_value_keyframe_track_count(999), 1);

	// Pretty type names for every legal ordinal.
	for ty in 1..=19 {
		let len = unsafe { oakengine_node_value_pretty_type_name(ty, buf.as_mut_ptr(), 64) };
		assert!(len > 0, "pretty type name for {ty} must not be empty");
	}
	// NONE (0), negative and > AUDIO_PARAMS are invalid.
	assert_eq!(unsafe { oakengine_node_value_pretty_type_name(0, buf.as_mut_ptr(), 64) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_value_pretty_type_name(-1, buf.as_mut_ptr(), 64) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_value_pretty_type_name(20, buf.as_mut_ptr(), 64) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_value_pretty_type_name(999, buf.as_mut_ptr(), 64) }, E_INVALID);

	// split_to_tracks: vec2 → two per-component track values; combine back.
	let mut tracks = [unsafe { std::mem::zeroed::<OakNodeValue>() }; 2];
	assert_eq!(unsafe { oakengine_node_value_split_to_tracks(vt::VEC2, &vec2_value(1.0, 2.0), tracks.as_mut_ptr(), 2) }, 0);
	assert_eq!(tracks[0].kind, vt::VEC2);
	// Track `i` carries component `i`; combine reassembles them.
	assert!((tracks[0].f[0] - 1.0).abs() < 1e-9);
	assert!((tracks[1].f[0] - 2.0).abs() < 1e-9);
	let mut out = unsafe { std::mem::zeroed::<OakNodeValue>() };
	assert_eq!(unsafe { oakengine_node_value_combine_tracks(vt::VEC2, tracks.as_ptr(), 2, &mut out) }, 0);
	assert!((out.f[0] - 1.0).abs() < 1e-9);
	assert!((out.f[1] - 2.0).abs() < 1e-9);

	// split/combine of a float keeps a single track; track_count mismatch is
	// clamped by split and illegal (<= 0) for both.
	let mut ft = [unsafe { std::mem::zeroed::<OakNodeValue>() }; 4];
	assert_eq!(unsafe { oakengine_node_value_split_to_tracks(vt::FLOAT, &float_value(3.5), ft.as_mut_ptr(), 4) }, 0);
	assert!((ft[0].f[0] - 3.5).abs() < 1e-9);
	assert_eq!(unsafe { oakengine_node_value_split_to_tracks(vt::FLOAT, &float_value(1.0), std::ptr::null_mut(), 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_value_split_to_tracks(vt::FLOAT, &float_value(1.0), ft.as_mut_ptr(), 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_value_split_to_tracks(vt::FLOAT, std::ptr::null(), ft.as_mut_ptr(), 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_value_combine_tracks(vt::FLOAT, ft.as_ptr(), 0, &mut out) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_value_combine_tracks(vt::FLOAT, std::ptr::null(), 1, &mut out) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_value_combine_tracks(vt::FLOAT, ft.as_ptr(), 1, std::ptr::null_mut()) }, E_INVALID);

	// Multicam grid math legal matrix + illegal inputs.
	let mut rows: c_int = 0;
	let mut cols: c_int = 0;
	assert_eq!(unsafe { oakengine_multicam_get_rows_and_columns(1, &mut rows, &mut cols) }, 0);
	assert_eq!((rows, cols), (1, 1));
	assert_eq!(unsafe { oakengine_multicam_get_rows_and_columns(4, &mut rows, &mut cols) }, 0);
	assert_eq!((rows, cols), (2, 2));
	assert_eq!(unsafe { oakengine_multicam_get_rows_and_columns(0, &mut rows, &mut cols) }, 0);
	assert_eq!((rows, cols), (1, 1));
	assert_eq!(unsafe { oakengine_multicam_get_rows_and_columns(-1, &mut rows, &mut cols) }, E_INVALID);
	assert_eq!(unsafe { oakengine_multicam_get_rows_and_columns(4, std::ptr::null_mut(), &mut cols) }, E_INVALID);
	assert_eq!(unsafe { oakengine_multicam_get_rows_and_columns(4, &mut rows, std::ptr::null_mut()) }, E_INVALID);
	let mut r: c_int = 0;
	let mut c: c_int = 0;
	assert_eq!(unsafe { oakengine_multicam_index_to_row_cols(0, 2, 2, &mut r, &mut c) }, 0);
	assert_eq!((r, c), (0, 0));
	assert_eq!(unsafe { oakengine_multicam_index_to_row_cols(3, 2, 2, &mut r, &mut c) }, 0);
	assert_eq!((r, c), (1, 1));
	assert_eq!(unsafe { oakengine_multicam_index_to_row_cols(-1, 2, 2, &mut r, &mut c) }, E_INVALID);
	assert_eq!(unsafe { oakengine_multicam_index_to_row_cols(0, 0, 2, &mut r, &mut c) }, E_INVALID);
	assert_eq!(unsafe { oakengine_multicam_index_to_row_cols(0, 2, 0, &mut r, &mut c) }, E_INVALID);
	assert_eq!(unsafe { oakengine_multicam_index_to_row_cols(0, 2, 2, std::ptr::null_mut(), &mut c) }, E_INVALID);
	assert_eq!(unsafe { oakengine_multicam_index_to_row_cols(0, 2, 2, &mut r, std::ptr::null_mut()) }, E_INVALID);
	assert_eq!(oakengine_multicam_rows_cols_to_index(1, 1, 2, 2), 3);
	assert_eq!(oakengine_multicam_rows_cols_to_index(0, 0, 2, 2), 0);
	assert_eq!(oakengine_multicam_rows_cols_to_index(-1, 0, 2, 2), E_INVALID);
	assert_eq!(oakengine_multicam_rows_cols_to_index(0, -1, 2, 2), E_INVALID);
	assert_eq!(oakengine_multicam_rows_cols_to_index(2, 0, 2, 2), E_INVALID);
	assert_eq!(oakengine_multicam_rows_cols_to_index(0, 2, 2, 2), E_INVALID);
	assert_eq!(oakengine_multicam_rows_cols_to_index(0, 0, 0, 2), E_INVALID);
	assert_eq!(oakengine_multicam_rows_cols_to_index(0, 0, 2, 0), E_INVALID);

	// Footage stream type names.
	for (t, name) in [(0, "Video"), (1, "Audio"), (2, "Subtitle"), (3, "Unknown"), (-1, "Unknown")] {
		let len = unsafe { oakengine_footage_stream_type_name(t, buf.as_mut_ptr(), 64) };
		assert_eq!(len, name.len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, name);
	}
}

// ---------------------------------------------------------------------------
// Serialized legal paths (undo-stack mutating; owned objects)
// ---------------------------------------------------------------------------

/// Project lifecycle, factory, node metadata/params/graph edits, keyframes,
/// dragger, group, multicam, folder, bulk delete and footage — the full
/// legal matrix of the node family in one serialized test (the facade's
/// undo stack is process-wide, and owned-object creation feeds the alive
/// counter, both guarded by [`with_owned`]).
#[test]
fn node_family_legal_paths() {
	with_owned(|| {
		common::force_link();
		let _ = force_oakundo_command_link();
		let base = alive();
		let mut buf = [0 as c_char; 512];

		// ---- project shell: create → new → name/filename/cache --------
		let project = oakengine_project_create();
		assert!(!project.is_null());
		assert_eq!(alive(), base + 1, "an owned project must be alive-counted");

		// Freeing NULL is a no-op.
		unsafe { oakengine_project_free(std::ptr::null_mut()) };

		// A fresh project is untitled.
		let len = unsafe { oakengine_project_name(project, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "(untitled)".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "(untitled)");
		assert_eq!(unsafe { oakengine_project_filename(project, buf.as_mut_ptr(), 512) }, 0);

		// A second new on the same project is rejected with E_STATE.
		assert_eq!(unsafe { oakengine_project_new(project) }, 0);
		assert_eq!(unsafe { oakengine_project_new(project) }, E_STATE);

		// Modified flag round trip.
		assert_eq!(unsafe { oakengine_project_is_modified(project) }, 0);
		assert_eq!(unsafe { oakengine_project_set_modified(project, 1) }, 0);
		assert_eq!(unsafe { oakengine_project_is_modified(project) }, 1);
		assert_eq!(unsafe { oakengine_project_set_modified(project, 0) }, 0);
		assert_eq!(unsafe { oakengine_project_is_modified(project) }, 0);

		// Filename drives the display name.
		assert_eq!(unsafe { oakengine_project_set_filename(project, c"/tmp/oak_it_node.ovexml".as_ptr()) }, 0);
		let len = unsafe { oakengine_project_filename(project, buf.as_mut_ptr(), 512) };
		assert!(len > 0);
		assert!(unsafe { read_buf(&mut buf) }.ends_with("oak_it_node.ovexml"));
		let len = unsafe { oakengine_project_pretty_filename(project, buf.as_mut_ptr(), 512) };
		assert!(len > 0);
		let len = unsafe { oakengine_project_name(project, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "oak_it_node".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "oak_it_node");

		// Cache path surfaces.
		let rc = unsafe { oakengine_project_cache_path(project, buf.as_mut_ptr(), 512) };
		assert!(rc >= 0);
		assert_eq!(unsafe { oakengine_project_get_cache_location_setting(project) }, 0);
		assert_eq!(unsafe { oakengine_project_get_custom_cache_path(project, buf.as_mut_ptr(), 512) }, 0);
		assert_eq!(unsafe { oakengine_project_set_custom_cache_path(project, c"/tmp/oak_it_cache".as_ptr()) }, 0);
		let len = unsafe { oakengine_project_get_custom_cache_path(project, buf.as_mut_ptr(), 512) };
		assert!(len > 0);
		assert_eq!(unsafe { read_buf(&mut buf) }, "/tmp/oak_it_cache");
		// NULL path clears the custom cache path.
		assert_eq!(unsafe { oakengine_project_set_custom_cache_path(project, std::ptr::null()) }, 0);
		assert_eq!(unsafe { oakengine_project_get_custom_cache_path(project, buf.as_mut_ptr(), 512) }, 0);
		// Alongside path is a documented stub returning an empty string.
		assert_eq!(unsafe { oakengine_project_cache_alongside_path(project, buf.as_mut_ptr(), 512) }, 0);

		// Color reference space is a documented stub ("" / OK).
		assert_eq!(unsafe { oakengine_project_get_color_reference_space(project, buf.as_mut_ptr(), 512) }, 0);
		assert_eq!(unsafe { oakengine_project_set_color_reference_space(project, c"rec709".as_ptr()) }, 0);

		// Sequence enumeration: a fresh project has none.
		assert_eq!(unsafe { oakengine_project_sequence_count(project) }, 0);
		assert!(unsafe { oakengine_project_sequence_at(project, 0) }.is_null());

		// Root folder + node enumeration.
		assert_eq!(unsafe { oakengine_project_node_count(project) }, 1);
		let root = unsafe { oakengine_project_root(project) };
		assert!(!root.is_null());
		assert_eq!(unsafe { oakengine_node_is_folder(root) }, 1);
		assert_eq!(unsafe { oakengine_node_is_item(root) }, 1);
		assert_eq!(unsafe { oakengine_project_node_at(project, 0) }.is_null(), false);
		assert!(unsafe { oakengine_project_node_at(project, 999) }.is_null());
		unsafe { oakengine_node_free(root) }; // borrowed shell

		// ---- factory ----------------------------------------------------
		let factory_count = oakengine_node_factory_id_count();
		assert!(factory_count > 0);
		let proto = unsafe { oakengine_node_factory_node_at(0) };
		assert!(!proto.is_null());
		let len = unsafe { oakengine_node_get_type_id(proto, buf.as_mut_ptr(), 512) };
		assert!(len > 0);
		let type_id = unsafe { read_buf(&mut buf) };
		let type_id_c = CString::new(type_id.as_str()).unwrap();
		let name_len = unsafe { oakengine_node_factory_name_from_id(type_id_c.as_ptr(), buf.as_mut_ptr(), 512) };
		assert!(name_len > 0, "factory name for a registered id must resolve");
		// Factory node index bounds.
		assert!(unsafe { oakengine_node_factory_node_at(-1) }.is_null());
		assert!(unsafe { oakengine_node_factory_node_at(1_000_000) }.is_null());
		// NULL type id → empty name (length 0), NULL → NULL node.
		assert_eq!(unsafe { oakengine_node_factory_name_from_id(std::ptr::null(), buf.as_mut_ptr(), 512) }, 0);
		assert!(unsafe { oakengine_node_factory_create_from_id(std::ptr::null()) }.is_null());
		// Unknown type id → NULL + a non-empty last error.
		assert!(unsafe { oakengine_node_factory_create_from_id(c"org.oak.no.such.node".as_ptr()) }.is_null());
		let err_len = unsafe { oakengine_node_last_error(buf.as_mut_ptr(), 512) };
		assert!(err_len > 0, "node_last_error must be non-empty after a failed factory lookup");
		unsafe { oakengine_node_free(proto) }; // borrowed prototype shell

		// Category / description / flags surfaces are stubs with stable results.
		let orphan = unsafe { oakengine_node_factory_create_from_id(TYPE_VALUE.as_ptr()) };
		assert!(!orphan.is_null());
		assert_eq!(alive(), base + 2, "an owned factory node must be alive-counted");
		assert_eq!(unsafe { oakengine_node_category_count(orphan) }, 0);
		assert_eq!(unsafe { oakengine_node_category_at(orphan, 0) }, -1);
		assert_eq!(unsafe { oakengine_node_get_flags(orphan) }, 0);
		assert_eq!(unsafe { oakengine_node_get_sub_category(orphan, buf.as_mut_ptr(), 512) }, 0);
		assert_eq!(unsafe { oakengine_node_get_description(orphan, buf.as_mut_ptr(), 512) }, 0);
		unsafe { oakengine_node_retranslate(orphan) }; // void no-op
		unsafe { oakengine_node_delete_later(orphan) }; // void no-op
		unsafe { oakengine_node_get_brush(orphan, 0.0, 1.0, std::ptr::null_mut()) }; // void no-op

		// ---- metadata: type id / name / short name / label --------------
		let len = unsafe { oakengine_node_get_type_id(orphan, buf.as_mut_ptr(), 512) };
		assert_eq!(len, TYPE_VALUE.to_bytes().len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, TYPE_VALUE.to_str().unwrap());
		let len = unsafe { oakengine_node_get_name(orphan, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "Value".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "Value");
		let len = unsafe { oakengine_node_get_short_name(orphan, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "Value".len() as c_int, "short name falls back to the name");
		// Label is empty until set.
		assert_eq!(unsafe { oakengine_node_get_label(orphan, buf.as_mut_ptr(), 512) }, 0);

		// ---- standalone copy (owned; free back to baseline) -------------
		let copy = unsafe { oakengine_node_create_copy(orphan) };
		assert!(!copy.is_null());
		assert_eq!(alive(), base + 3);
		let len = unsafe { oakengine_node_get_type_id(copy, buf.as_mut_ptr(), 512) };
		assert_eq!(len, TYPE_VALUE.to_bytes().len() as c_int);
		unsafe { oakengine_node_free(copy) };
		assert_eq!(alive(), base + 2, "freeing an owned copy must return the alive counter");
		assert!(unsafe { oakengine_node_create_copy(std::ptr::null()) }.is_null());

		// ---- project add nodes ------------------------------------------
		let solid = unsafe { oakengine_project_add_node(project, TYPE_SOLID.as_ptr()) };
		assert!(!solid.is_null());
		let transform = unsafe { oakengine_project_add_node(project, TYPE_TRANSFORM.as_ptr()) };
		assert!(!transform.is_null());
		let value = unsafe { oakengine_project_add_node(project, TYPE_VALUE.as_ptr()) };
		assert!(!value.is_null());
		let value2 = unsafe { oakengine_project_add_node(project, TYPE_VALUE.as_ptr()) };
		assert!(!value2.is_null());
		let group = unsafe { oakengine_project_add_node(project, TYPE_GROUP.as_ptr()) };
		assert!(!group.is_null());
		let multicam = unsafe { oakengine_project_add_node(project, TYPE_MULTICAM.as_ptr()) };
		assert!(!multicam.is_null());
		// 6 added nodes + the root folder.
		assert_eq!(unsafe { oakengine_project_node_count(project) }, 7);
		// Unknown id → NULL + last error.
		assert!(unsafe { oakengine_project_add_node(project, c"org.oak.nope".as_ptr()) }.is_null());
		assert!(unsafe { oakengine_project_add_node(std::ptr::null_mut(), TYPE_VALUE.as_ptr()) }.is_null());

		// The added nodes report their owning project.
		let owned_project = unsafe { oakengine_node_get_project(value) };
		assert!(!owned_project.is_null());
		unsafe { oakengine_project_free(owned_project) }; // borrowed project shell
		let owned_project2 = unsafe { oakengine_project_from_object(value) };
		assert!(!owned_project2.is_null());
		unsafe { oakengine_project_free(owned_project2) };

		// ---- node type queries ------------------------------------------
		assert_eq!(unsafe { oakengine_node_is_clip(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_track(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_viewer_output(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_footage(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_sequence(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_folder(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_group(group) }, 1);
		assert_eq!(unsafe { oakengine_node_is_group(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_multicam(multicam) }, 1);
		assert_eq!(unsafe { oakengine_node_is_multicam(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_item(solid) }, 0);
		assert_eq!(unsafe { oakengine_node_is_item(group) }, 1, "group is a project-tree item");

		// ---- label operations (undoable) --------------------------------
		assert_eq!(unsafe { oakengine_node_set_label(value, c"My Value".as_ptr()) }, 0);
		let len = unsafe { oakengine_node_get_label(value, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "My Value".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "My Value");
		// NULL label → empty label (documented).
		assert_eq!(unsafe { oakengine_node_set_label(value, std::ptr::null()) }, 0);
		assert_eq!(unsafe { oakengine_node_get_label(value, buf.as_mut_ptr(), 512) }, 0);

		// set_label_ex matrix: undoable (1) and live (0).
		assert_eq!(unsafe { oakengine_node_set_label_ex(value, c"Ex".as_ptr(), 1) }, 0);
		assert_eq!(unsafe { oakengine_node_set_label_ex(value, c"Ex2".as_ptr(), 0) }, 0);
		let len = unsafe { oakengine_node_get_label(value, buf.as_mut_ptr(), 512) };
		assert_eq!(len, 3);
		assert_eq!(unsafe { read_buf(&mut buf) }, "Ex2");

		// label_and_name: label wins over the name.
		let len = unsafe { oakengine_node_get_label_and_name(value, buf.as_mut_ptr(), 512) };
		assert_eq!(len, 3);
		assert_eq!(unsafe { read_buf(&mut buf) }, "Ex2");
		// No label → the name.
		let len = unsafe { oakengine_node_get_label_and_name(solid, buf.as_mut_ptr(), 512) };
		assert!(len > 0);

		// rename_many: one multi command for several nodes.
		let mut nodes = [value, value2];
		assert_eq!(unsafe { oakengine_node_rename_many(nodes.as_mut_ptr(), 2, c"Renamed".as_ptr(), std::ptr::null_mut()) }, 0);
		let len = unsafe { oakengine_node_get_label(value, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "Renamed".len() as c_int);
		// count 0 is a no-op; count < 0 or a NULL entry is invalid.
		assert_eq!(unsafe { oakengine_node_rename_many(nodes.as_mut_ptr(), 0, c"x".as_ptr(), std::ptr::null_mut()) }, 0);
		assert_eq!(unsafe { oakengine_node_rename_many(nodes.as_mut_ptr(), -1, c"x".as_ptr(), std::ptr::null_mut()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_rename_many(std::ptr::null_mut(), 2, c"x".as_ptr(), std::ptr::null_mut()) }, E_INVALID);
		nodes[1] = std::ptr::null_mut();
		assert_eq!(unsafe { oakengine_node_rename_many(nodes.as_mut_ptr(), 2, c"x".as_ptr(), std::ptr::null_mut()) }, E_INVALID);
		// set_label_many delegates to rename_many.
		nodes = [value, value2];
		assert_eq!(unsafe { oakengine_node_set_label_many(nodes.as_mut_ptr(), 2, c"Many".as_ptr()) }, 0);

		// rename_command: opaque command pointer (caller owns → free).
		let cmd = unsafe { oakengine_node_rename_command(value, c"Cmd".as_ptr()) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		assert!(unsafe { oakengine_node_rename_command(std::ptr::null_mut(), c"x".as_ptr()) }.is_null());
		// NULL label on the command is legal (empty label).
		let cmd = unsafe { oakengine_node_rename_command(value, std::ptr::null()) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };

		// ---- color labels -----------------------------------------------
		assert_eq!(unsafe { oakengine_node_get_color_label(value) }, -1);
		assert_eq!(unsafe { oakengine_node_get_effective_color_label(value) }, -1);
		let cmd = unsafe { oakengine_node_set_color_label_command(value, 3) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		let mut cl_nodes = [value, value2];
		assert_eq!(unsafe { oakengine_node_set_color_label(cl_nodes.as_mut_ptr(), 2, 2) }, 0);
		assert_eq!(unsafe { oakengine_node_set_color_label(cl_nodes.as_mut_ptr(), 0, 2) }, 0);
		assert_eq!(unsafe { oakengine_node_set_color_label(cl_nodes.as_mut_ptr(), -1, 2) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_set_color_label(std::ptr::null_mut(), 1, 2) }, E_INVALID);

		// ---- input introspection ----------------------------------------
		// Every node carries the standard enabled_in plus the value node's
		// type_in/value_in.
		assert_eq!(unsafe { oakengine_node_input_count(value) }, 3);
		let len = unsafe { oakengine_node_input_id(value, 0, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "enabled_in".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "enabled_in");
		let len = unsafe { oakengine_node_input_id(value, 1, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "type_in".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "type_in");
		let len = unsafe { oakengine_node_input_id(value, 2, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "value_in".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "value_in");
		assert_eq!(unsafe { oakengine_node_input_id(value, 999, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_input_id(value, -1, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_input_get_type(value, c"value_in".as_ptr()) }, vt::FLOAT);
		// Texture inputs have no POD type code; the module reports NONE.
		assert_eq!(unsafe { oakengine_node_input_get_type(transform, c"tex_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_is_connectable(transform, c"tex_in".as_ptr()) }, 1);
		assert_eq!(unsafe { oakengine_node_input_is_connectable(value, c"value_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_is_keyframable(value, c"value_in".as_ptr()) }, 1);
		assert_eq!(unsafe { oakengine_node_input_is_connected(transform, c"tex_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_is_connected(value, c"tex_in".as_ptr()) }, NODE_E_NOT_FOUND, "unknown input on a valid node");
		// Input-name lookup (the value node's localized names).
		let len = unsafe { oakengine_node_get_input_name(value, c"value_in".as_ptr(), buf.as_mut_ptr(), 512) };
		assert_eq!(len, "Value".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "Value");
		let len = unsafe { oakengine_node_get_input_name(value, c"type_in".as_ptr(), buf.as_mut_ptr(), 512) };
		assert_eq!(len, "Type".len() as c_int);
		assert_eq!(unsafe { oakengine_node_get_input_name(value, c"nope_in".as_ptr(), buf.as_mut_ptr(), 512) }, NODE_E_NOT_FOUND);
		// Stub input flags are stable.
		assert_eq!(unsafe { oakengine_node_input_is_array(value, c"value_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_array_size(value, c"value_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_get_flags(value, c"value_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_get_data_type(value, c"value_in".as_ptr()) }, -1);
		assert_eq!(unsafe { oakengine_node_input_is_hidden(value, c"value_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_is_keyframed(value, c"value_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_is_keyframed_ex(value, c"value_in".as_ptr(), 0) }, 0);

		// ---- standard parameter access ----------------------------------
		assert_eq!(unsafe { oakengine_node_set_input(value, c"value_in".as_ptr(), &float_value(3.5)) }, 0);
		let mut out: OakNodeValue = unsafe { std::mem::zeroed() };
		assert_eq!(unsafe { oakengine_node_get_input(value, c"value_in".as_ptr(), &mut out) }, 0);
		assert_eq!(out.kind, vt::FLOAT);
		assert!((out.f[0] - 3.5).abs() < 1e-6);
		// Type mismatch (INT kind into a float input) → module INVALID.
		assert_eq!(unsafe { oakengine_node_set_input(value, c"value_in".as_ptr(), &int_value(7)) }, NODE_E_INVALID);
		// Unknown input → module NOT_FOUND.
		assert_eq!(unsafe { oakengine_node_set_input(value, c"nope_in".as_ptr(), &float_value(1.0)) }, NODE_E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_get_input(value, c"nope_in".as_ptr(), &mut out) }, NODE_E_NOT_FOUND);

		// set_standard_value_command → opaque command pointer (free).
		let cmd = unsafe { oakengine_node_set_standard_value_command(value, c"value_in".as_ptr(), -1, -1, &float_value(4.0)) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		assert!(unsafe { oakengine_node_set_standard_value_command(value, c"value_in".as_ptr(), -1, -1, std::ptr::null()) }.is_null());

		// set_input_video_params_command is a documented stub → NULL.
		let params = unsafe { std::mem::zeroed::<OakVideoParamsPod>() };
		assert!(unsafe { oakengine_node_set_input_video_params_command(value, c"value_in".as_ptr(), &params) }.is_null());

		// ---- string parameter access (text generator) -------------------
		let textgen = unsafe { oakengine_node_factory_create_from_id(TYPE_TEXT.as_ptr()) };
		assert!(!textgen.is_null());
		// project + orphan + textgen owned (the added-node views are
		// borrowed; the facade releases the factory's owned handle).
		let alive_now = alive();
		assert_eq!(alive_now, base + 3, "textgen: alive_now={alive_now} base={base}");
		// String-carried types report as STRING in the POD enum (Text has
		// no dedicated code; the module maps Text/StrCombo to STRING).
		assert_eq!(unsafe { oakengine_node_input_get_type(textgen, c"text_in".as_ptr()) }, vt::STRING);
		// The generator's default text is "Sample Text" (11 chars).
		let len = unsafe { oakengine_node_get_input_string(textgen, c"text_in".as_ptr(), buf.as_mut_ptr(), 512) };
		assert_eq!(len, "Sample Text".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "Sample Text");
		assert_eq!(unsafe { oakengine_node_set_input_string(textgen, c"text_in".as_ptr(), c"Hello Text".as_ptr()) }, 0);
		let len2 = unsafe { oakengine_node_get_input_string(textgen, c"text_in".as_ptr(), buf.as_mut_ptr(), 512) };
		assert_eq!(len2, "Hello Text".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "Hello Text");
		// String write on a non-string input → module INVALID.
		assert_eq!(unsafe { oakengine_node_set_input_string(value, c"value_in".as_ptr(), c"x".as_ptr()) }, NODE_E_INVALID);
		// get_input_string on a non-string input → module INVALID.
		assert_eq!(unsafe { oakengine_node_get_input_string(value, c"value_in".as_ptr(), buf.as_mut_ptr(), 512) }, NODE_E_INVALID);
		// Unknown input → NOT_FOUND for the string getter.
		assert_eq!(unsafe { oakengine_node_get_input_string(value, c"nope_in".as_ptr(), buf.as_mut_ptr(), 512) }, NODE_E_NOT_FOUND);
		unsafe { oakengine_node_free(textgen) };
		// project + orphan owned.
		assert_eq!(alive(), base + 2);

		// ---- at-time values ---------------------------------------------
		assert_eq!(unsafe { oakengine_node_frame_time_base(value, std::ptr::null_mut(), std::ptr::null_mut()) }, 0);
		let mut tb_num: c_int = 0;
		let mut tb_den: c_int = 0;
		assert_eq!(unsafe { oakengine_node_frame_time_base(value, &mut tb_num, &mut tb_den) }, 0);
		assert_eq!((tb_num, tb_den), (1001, 30000), "engine default time base without a sequence");

		assert_eq!(unsafe { oakengine_node_set_input_at_time(value, c"value_in".as_ptr(), -1, 0, -1, &float_value(0.5), 0) }, 0);
		let mut at: OakNodeValue = unsafe { std::mem::zeroed() };
		assert_eq!(unsafe { oakengine_node_get_input_at_time(value, c"value_in".as_ptr(), -1, -1, 0, 0, &mut at) }, 0);
		assert_eq!(at.kind, vt::FLOAT);
		assert!((at.f[0] - 0.5).abs() < 1e-6);
		// At-time read on an unknown input → module NOT_FOUND.
		assert_eq!(unsafe { oakengine_node_get_input_at_time(value, c"nope_in".as_ptr(), -1, -1, 0, 0, &mut at) }, NODE_E_NOT_FOUND);
		// At-time write on an unknown input → module NOT_FOUND.
		assert_eq!(unsafe { oakengine_node_set_input_at_time(value, c"nope_in".as_ptr(), -1, 0, -1, &float_value(1.0), 0) }, NODE_E_NOT_FOUND);

		// set_value_at_time_command → opaque command pointer (free).
		let cmd = unsafe { oakengine_node_set_value_at_time_command(
			value as *mut c_void, c"value_in".as_ptr(), -1, 0, 1, &float_value(0.75), -1, 0) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		// Zero denominator → NULL, never a crash.
		assert!(unsafe { oakengine_node_set_value_at_time_command(
			value as *mut c_void, c"value_in".as_ptr(), -1, 0, 0, &float_value(0.75), -1, 0) }.is_null());

		// String at-time path is a documented stub → Invalid.
		assert_eq!(unsafe { oakengine_node_set_input_string_at_time(value, c"value_in".as_ptr(), -1, 0, c"x".as_ptr()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_get_input_string_at_time(value, c"value_in".as_ptr(), -1, 0, -1, buf.as_mut_ptr(), 512) }, E_INVALID);
		// Bezier/binary at-time reads are documented stubs → Invalid.
		let mut six = [0.0f64; 6];
		assert_eq!(unsafe { oakengine_node_get_input_bezier_at_time(value, c"value_in".as_ptr(), -1, 0, -1, six.as_mut_ptr()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_get_input_binary_at_time(value, c"value_in".as_ptr(), -1, 0, -1, buf.as_mut_ptr(), 512) }, E_INVALID);

		// ---- input default value (stub) ---------------------------------
		assert_eq!(unsafe { oakengine_node_input_get_default_value(value, c"value_in".as_ptr(), 0, &mut out) }, E_NOT_FOUND);

		// ---- input properties (stubs) -----------------------------------
		assert_eq!(unsafe { oakengine_node_input_has_property(value, c"value_in".as_ptr(), c"key".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_set_input_property_string(value, c"value_in".as_ptr(), c"key".as_ptr(), c"v".as_ptr(), 1) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_input_get_property_string(value, c"value_in".as_ptr(), c"key".as_ptr(), buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_input_get_property_number(value, c"value_in".as_ptr(), c"key".as_ptr(), 0, std::ptr::null_mut()) }, E_INVALID);
		let mut dbl: f64 = 0.0;
		assert_eq!(unsafe { oakengine_node_input_get_property_number(value, c"value_in".as_ptr(), c"key".as_ptr(), 0, &mut dbl) }, E_NOT_FOUND);
		let mut i64out: i64 = 0;
		assert_eq!(unsafe { oakengine_node_input_get_property_int(value, c"value_in".as_ptr(), c"key".as_ptr(), &mut i64out) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_input_get_property_rational(value, c"value_in".as_ptr(), c"key".as_ptr(), std::ptr::null_mut(), std::ptr::null_mut()) }, E_NOT_FOUND, "stub NotFound with a valid node");
		assert_eq!(unsafe { oakengine_node_input_get_property_track_number(value, c"value_in".as_ptr(), c"key".as_ptr(), 0, &mut dbl) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_input_get_property_count(value, c"value_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_get_property_key(value, c"value_in".as_ptr(), 0, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_input_get_property_string_list_count(value, c"value_in".as_ptr(), c"key".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_get_property_string_list(value, c"value_in".as_ptr(), c"key".as_ptr(), 0, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);

		// ---- copy inputs -------------------------------------------------
		assert_eq!(unsafe { oakengine_node_copy_inputs(value2, value) }, 0);
		assert_eq!(unsafe { oakengine_node_copy_inputs(std::ptr::null_mut(), value) }, E_INVALID);

		// ---- value hint -------------------------------------------------
		assert_eq!(unsafe { oakengine_node_set_value_hint(value, c"value_in".as_ptr(), 0, 0, 0, c"".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_set_value_hint(value, c"nope_in".as_ptr(), 0, 0, 0, c"".as_ptr()) }, NODE_E_NOT_FOUND);

		// ---- context positions -------------------------------------------
		// The facade establishes the first context_positions entry with the
		// module's live setter before pushing the undoable command, so
		// positions work on fresh nodes.
		let root = unsafe { oakengine_project_root(project) };
		let mut x: f64 = 0.0;
		let mut y: f64 = 0.0;
		let mut expanded: c_int = 0;
		assert_eq!(unsafe { oakengine_node_context_node_count(root) }, 0);
		assert_eq!(unsafe { oakengine_node_context_contains_node(root, value) }, 0);
		assert!(unsafe { oakengine_node_context_node_at(root, 0, &mut x, &mut y, &mut expanded) }.is_null());
		// A fresh node gains its first context entry through the facade.
		assert_eq!(unsafe { oakengine_node_set_context_position(root, value, 10.0, 20.0) }, 0);
		assert_eq!(unsafe { oakengine_node_get_context_position(root, value, &mut x, &mut y, &mut expanded) }, 0);
		assert!((x - 10.0).abs() < 1e-9, "x={x}");
		assert!((y - 20.0).abs() < 1e-9, "y={y}");
		assert_eq!(expanded, 0);
		// Expanded flag flips through the same path.
		assert_eq!(unsafe { oakengine_node_set_context_expanded(root, value, 1) }, 0);
		assert_eq!(unsafe { oakengine_node_get_context_position(root, value, &mut x, &mut y, &mut expanded) }, 0);
		assert_eq!(expanded, 1);
		unsafe { oakengine_node_free(root) };

		// ---- array inputs (multicam sources_in is an array) --------------
		assert_eq!(unsafe { oakengine_node_array_insert_at(multicam, c"sources_in".as_ptr(), 0) }, 0);
		assert_eq!(unsafe { oakengine_node_array_insert_at(multicam, c"sources_in".as_ptr(), 1) }, 0);
		assert_eq!(unsafe { oakengine_multicam_get_source_count(multicam) }, 2);
		assert_eq!(unsafe { oakengine_node_array_remove_at(multicam, c"sources_in".as_ptr(), 1) }, 0);
		assert_eq!(unsafe { oakengine_multicam_get_source_count(multicam) }, 1);
		assert_eq!(unsafe { oakengine_node_array_remove_at(multicam, c"sources_in".as_ptr(), 0) }, 0);
		assert_eq!(unsafe { oakengine_multicam_get_source_count(multicam) }, 0);
		// Non-array input → module INVALID; negative index → facade INVALID.
		assert_eq!(unsafe { oakengine_node_array_insert_at(value, c"value_in".as_ptr(), 0) }, NODE_E_INVALID);
		assert_eq!(unsafe { oakengine_node_array_insert_at(value, c"value_in".as_ptr(), -1) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_array_remove_at(value, c"value_in".as_ptr(), 0) }, NODE_E_INVALID);
		assert_eq!(unsafe { oakengine_multicam_get_source_count(value) }, E_INVALID, "non-multicam node");

		// ---- graph editing: connect / disconnect -------------------------
		assert_eq!(unsafe { oakengine_node_connect(solid, transform, c"tex_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_is_connected(transform, c"tex_in".as_ptr()) }, 1);
		// A second connect on an already-connected input is rejected with the
		// module STATE error, mirroring the live `oaknode_node_connect`.
		assert_eq!(unsafe { oakengine_node_connect(solid, transform, c"tex_in".as_ptr()) }, NODE_E_STATE);
		// Connecting to a non-connectable input → module INVALID.
		assert_eq!(unsafe { oakengine_node_connect(solid, value, c"value_in".as_ptr()) }, NODE_E_INVALID);
		// Connecting to an unknown input → module NOT_FOUND.
		assert_eq!(unsafe { oakengine_node_connect(solid, value, c"nope_in".as_ptr()) }, NODE_E_NOT_FOUND);

		// Connected-node + output-connection introspection.
		let conn = unsafe { oakengine_node_input_get_connected_node(transform, c"tex_in".as_ptr(), -1) };
		assert!(!conn.is_null());
		let len = unsafe { oakengine_node_get_type_id(conn, buf.as_mut_ptr(), 512) };
		assert_eq!(len, TYPE_SOLID.to_bytes().len() as c_int);
		unsafe { oakengine_node_free(conn) };
		assert!(unsafe { oakengine_node_input_get_connected_node(transform, c"nope_in".as_ptr(), -1) }.is_null());
		assert_eq!(unsafe { oakengine_node_output_connection_count(solid) }, 1);
		let mut conn_node: *mut OakEngineNode = std::ptr::null_mut();
		let mut elem: c_int = -1;
		assert_eq!(unsafe { oakengine_node_output_connection_at(solid, 0, &mut conn_node, buf.as_mut_ptr(), 512, &mut elem) }, 0);
		assert!(!conn_node.is_null());
		assert_eq!(unsafe { read_buf(&mut buf) }, "tex_in");
		assert_eq!(elem, -1);
		unsafe { oakengine_node_free(conn_node) };
		// _ex variant reports the module's hidden flag (always 0).
		let mut hidden: c_int = 1;
		assert_eq!(unsafe { oakengine_node_output_connection_at_ex(solid, 0, &mut conn_node, buf.as_mut_ptr(), 512, &mut elem, &mut hidden) }, 0);
		assert_eq!(hidden, 0);
		unsafe { oakengine_node_free(conn_node) };
		// Out-of-range output index.
		assert_eq!(unsafe { oakengine_node_output_connection_at(solid, 1, &mut conn_node, buf.as_mut_ptr(), 512, &mut elem) }, E_NOT_FOUND);

		// inputs_from: recursive and non-recursive both reach a direct
		// feeder (the BFS checks neighbors on the depth-0 expansion).
		assert_eq!(unsafe { oakengine_node_inputs_from(transform, solid, 1) }, 1);
		assert_eq!(unsafe { oakengine_node_inputs_from(transform, solid, 0) }, 1, "non-recursive still finds a direct feeder");
		assert_eq!(unsafe { oakengine_node_inputs_from(value, solid, 1) }, 0);
		assert_eq!(unsafe { oakengine_node_inputs_from(std::ptr::null(), solid, 1) }, 0);
		assert_eq!(unsafe { oakengine_node_inputs_from(transform, std::ptr::null(), 1) }, 0);

		// Input-connection surfaces are stubs (outputs only).
		assert_eq!(unsafe { oakengine_node_input_connection_count_all(transform) }, 0);
		assert_eq!(unsafe { oakengine_node_input_connection_count(transform, c"tex_in".as_ptr(), -1) }, 0);
		assert_eq!(unsafe { oakengine_node_input_connection_at_all(transform, 0, &mut conn_node, buf.as_mut_ptr(), 512, &mut elem, &mut conn_node, &mut hidden) }, E_NOT_FOUND);
		assert!(unsafe { oakengine_node_input_connection_at(transform, c"tex_in".as_ptr(), -1, 0) }.is_null());

		// Disconnect + command variants.
		assert_eq!(unsafe { oakengine_node_disconnect(transform, c"tex_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_input_is_connected(transform, c"tex_in".as_ptr()) }, 0);
		// Disconnecting a disconnected input → module NOT_FOUND.
		assert_eq!(unsafe { oakengine_node_disconnect(transform, c"tex_in".as_ptr()) }, NODE_E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_disconnect_ex(transform, c"tex_in".as_ptr(), -1) }, NODE_E_NOT_FOUND);
		let cmd = unsafe { oakengine_node_connect_command(solid, transform, c"tex_in".as_ptr(), -1) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		assert_eq!(unsafe { oakengine_node_connect(solid, transform, c"tex_in".as_ptr()) }, 0);
		let cmd = unsafe { oakengine_node_disconnect_command(transform, c"tex_in".as_ptr(), -1) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };

		// block_link: link/unlink round trip (1 = changed, 0 = no-op).
		assert_eq!(unsafe { oakengine_block_link(solid as *mut c_void, value as *mut c_void, 1) }, 1);
		assert_eq!(unsafe { oakengine_block_link(solid as *mut c_void, value as *mut c_void, 1) }, 0, "already linked");
		assert_eq!(unsafe { oakengine_block_link(solid as *mut c_void, value as *mut c_void, 0) }, 1);
		assert_eq!(unsafe { oakengine_block_link(solid as *mut c_void, value as *mut c_void, 0) }, 0, "already unlinked");
		assert_eq!(unsafe { oakengine_block_link(std::ptr::null_mut(), value as *mut c_void, 1) }, E_INVALID);
		// link_command → opaque command pointer (free).
		let cmd = unsafe { oakengine_node_link_command(solid, value, 1) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		assert!(unsafe { oakengine_node_link_command(std::ptr::null_mut(), value, 1) }.is_null());

		// ---- copy in graph ----------------------------------------------
		// copy_in_graph pushes a "Copy Node" command and returns an owned
		// copy in a scratch project (free to return the alive counter).
		let count_before = unsafe { oakengine_project_node_count(project) };
		let copied = unsafe { oakengine_node_copy_in_graph(value, std::ptr::null_mut()) };
		assert!(!copied.is_null());
		assert_eq!(alive(), base + 3, "copy-in-graph: owned copy + project + orphan");
		assert_eq!(unsafe { oakengine_project_node_count(project) }, count_before + 1, "the redo inserts a copy into the graph");
		unsafe { oakengine_node_free(copied) };
		assert_eq!(alive(), base + 2);

		// add_to_project_command: opaque AddNode command for an orphan.
		let orphan2 = unsafe { oakengine_node_factory_create_from_id(TYPE_VALUE.as_ptr()) };
		assert!(!orphan2.is_null());
		let cmd = unsafe { oakengine_node_add_to_project_command(project, orphan2) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		unsafe { oakengine_node_free(orphan2) };
		// Adding a node already in the project → NULL command.
		assert!(unsafe { oakengine_node_add_to_project_command(project, value) }.is_null());

		// copy_dependency_graph is a documented stub → Invalid.
		let mut copies: *mut OakEngineNode = std::ptr::null_mut();
		let mut srcs = [value];
		assert_eq!(unsafe { oakengine_node_copy_dependency_graph(srcs.as_mut_ptr(), &mut copies, 1, std::ptr::null_mut()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_copy_dependency_graph(std::ptr::null_mut(), &mut copies, 1, std::ptr::null_mut()) }, E_INVALID);

		// connect_command_string is a documented stub → empty description.
		assert_eq!(unsafe { oakengine_node_connect_command_string(solid, value, c"value_in".as_ptr(), -1, buf.as_mut_ptr(), 512) }, 0);

		// transform_time_to is a documented identity stub.
		let mut rin: i64 = 0;
		let mut rind: i64 = 0;
		let mut rout: i64 = 0;
		let mut routd: i64 = 0;
		assert_eq!(unsafe { oakengine_node_transform_time_to(solid, value, 0, 0, 5, 1, 7, 2, &mut rin, &mut rind, &mut rout, &mut routd) }, 0);
		assert_eq!((rin, rind, rout, routd), (5, 1, 7, 2));
		assert_eq!(unsafe { oakengine_node_transform_time_to(std::ptr::null_mut(), value, 0, 0, 1, 1, 1, 1, &mut rin, &mut rind, &mut rout, &mut routd) }, E_INVALID);

		// ---- keyframes (facade stubs + the real handle API) -------------
		assert_eq!(unsafe { oakengine_node_keyframe_count(value, c"value_in".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_node_keyframe_count_on_track(value, c"value_in".as_ptr(), 0, 0) }, 0);
		assert_eq!(unsafe { oakengine_node_keyframe_at(value, c"value_in".as_ptr(), 0, std::ptr::null_mut(), std::ptr::null_mut()) }, E_NOT_FOUND, "stub NotFound with a valid node");
		let mut kf_time: i64 = 0;
		assert_eq!(unsafe { oakengine_node_keyframe_at(value, c"value_in".as_ptr(), 0, &mut kf_time, &mut out) }, E_NOT_FOUND);
		let mut f1: f32 = 0.0;
		let mut f2: f32 = 0.0;
		let mut f3: f32 = 0.0;
		let mut f4: f32 = 0.0;
		let mut kty: c_int = 0;
		assert_eq!(unsafe { oakengine_node_keyframe_get_easing(value, c"value_in".as_ptr(), 0, &mut f1, &mut f2, &mut f3, &mut f4, &mut kty) }, E_NOT_FOUND);
		// keyframe_add pushes the value-at-time path (undoable).
		assert_eq!(unsafe { oakengine_node_keyframe_add(value, c"value_in".as_ptr(), 0, &float_value(1.5), 0, 0.0, 0.0, 0.0, 0.0) }, 0);
		// Invalid easing type ordinal → Invalid.
		assert_eq!(unsafe { oakengine_node_keyframe_add(value, c"value_in".as_ptr(), 0, &float_value(1.5), 3, 0.0, 0.0, 0.0, 0.0) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_keyframe_add(value, c"value_in".as_ptr(), 0, &float_value(1.5), -1, 0.0, 0.0, 0.0, 0.0) }, E_INVALID);
		// keyframe_remove is a documented stub → NOT_FOUND.
		assert_eq!(unsafe { oakengine_node_keyframe_remove(value, c"value_in".as_ptr(), 0) }, E_NOT_FOUND);
		// Keyframe stub enumerators.
		assert_eq!(unsafe { oakengine_node_has_keyframe_at_time(value, c"value_in".as_ptr(), -1, 0, 0) }, 0);
		assert_eq!(unsafe { oakengine_node_keyframe_earliest_time(value, c"value_in".as_ptr(), -1, &mut i64out, std::ptr::null_mut()) }, 0);
		assert_eq!(unsafe { oakengine_node_keyframe_latest_time(value, c"value_in".as_ptr(), -1, std::ptr::null_mut(), &mut i64out) }, 0);
		assert_eq!(unsafe { oakengine_node_keyframe_closest_time_before(value, c"value_in".as_ptr(), -1, 0, 0, &mut i64out, &mut i64out) }, 0);
		assert_eq!(unsafe { oakengine_node_keyframe_closest_time_after(value, c"value_in".as_ptr(), -1, 0, 0, &mut i64out, &mut i64out) }, 0);
		assert!(unsafe { oakengine_node_keyframe_handle_on_track(value, c"value_in".as_ptr(), -1, 0, 0) }.is_null());
		assert!(unsafe { oakengine_node_keyframe_handle_at_time(value, c"value_in".as_ptr(), -1, 0, 0, 1) }.is_null());
		assert_eq!(unsafe { oakengine_node_keyframes_at_time(value, c"value_in".as_ptr(), -1, 0, 1, std::ptr::null_mut(), 0) }, 0);
		assert_eq!(unsafe { oakengine_node_keyframes_toggle_at_time(value, c"value_in".as_ptr(), -1, 0, 0, 1, c"t".as_ptr()) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_set_input_keyframing(value, c"value_in".as_ptr(), -1, 1, 0, 0, c"k".as_ptr()) }, E_INVALID);
		assert!(unsafe { oakengine_node_set_input_keyframing_command(value, c"value_in".as_ptr(), -1, 1) }.is_null());
		assert_eq!(unsafe { oakengine_node_keyframes_paste(value, std::ptr::null_mut(), 1, c"p".as_ptr()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_keyframe_best_type_at_time(value, c"value_in".as_ptr(), -1, 0, 0, 2) }, 2, "caller default passes through");
		assert_eq!(unsafe { oakengine_node_keyframe_track_count(value, c"value_in".as_ptr(), -1) }, 1);
		assert_eq!(unsafe { oakengine_node_keyframe_set_easing(value, c"value_in".as_ptr(), 0, 0, 0.0, 0.0, 0.0, 0.0) }, E_NOT_FOUND);
		let ts: [i64; 1] = [0];
		let trk: [c_int; 1] = [0];
		assert_eq!(unsafe { oakengine_node_keyframes_set_type_many(value, c"value_in".as_ptr(), -1, ts.as_ptr(), trk.as_ptr(), 1, 0) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_keyframes_set_time_many(value, c"value_in".as_ptr(), -1, ts.as_ptr(), trk.as_ptr(), 1, 1) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_keyframes_set_value_many(value, c"value_in".as_ptr(), -1, ts.as_ptr(), trk.as_ptr(), 1, &float_value(1.0), &float_value(0.0)) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_keyframes_set_bezier_many(value, c"value_in".as_ptr(), -1, ts.as_ptr(), trk.as_ptr(), 1, 0.0, 0.0, 1.0, 1.0) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_keyframe_set_bezier_point(value, c"value_in".as_ptr(), -1, 0, 0, 0, 0.0, 0.0, 0.0, 0.0) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_keyframes_clear(value, c"value_in".as_ptr()) }, 0, "documented no-op");
		// insert_keyframe_command → opaque command pointer (free).
		let cmd = unsafe { oakengine_node_insert_keyframe_command(value, c"value_in".as_ptr(), -1, 0, 0, &float_value(2.0), 0, 0.0, 0.0, 0.0, 0.0) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		assert!(unsafe { oakengine_node_insert_keyframe_command(value, c"value_in".as_ptr(), -1, 0, 0, &float_value(2.0), 3, 0.0, 0.0, 0.0, 0.0) }.is_null(), "garbage easing type");
		// remove_keyframe_command is a documented stub → NULL.
		let dummy_kf = Box::into_raw(Box::new(OakEngineKeyframe { handle: CHandle::null() }));
		assert!(unsafe { oakengine_node_remove_keyframe_command(dummy_kf) }.is_null());
		unsafe { oakengine_keyframe_dispose(dummy_kf) };

		// ---- OakEngineKeyframe handle API --------------------------------
		let kf = unsafe { oakengine_keyframe_create(value, c"value_in".as_ptr(), -1, 0, 0, 0, &float_value(1.0), 0) };
		assert!(!kf.is_null());
		let mut num: i64 = 0;
		let mut den: i64 = 0;
		assert_eq!(unsafe { oakengine_keyframe_get_time(kf, &mut num, &mut den) }, 0);
		assert_eq!(num, 0);
		assert_eq!(den, 1);
		let len = unsafe { oakengine_keyframe_get_input_id(kf, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "value_in".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "value_in");
		assert_eq!(unsafe { oakengine_keyframe_get_track(kf) }, 0);
		assert_eq!(unsafe { oakengine_keyframe_get_element(kf) }, -1);
		let parent = unsafe { oakengine_keyframe_get_node(kf) };
		assert!(!parent.is_null());
		let len = unsafe { oakengine_node_get_type_id(parent, buf.as_mut_ptr(), 512) };
		assert_eq!(len, TYPE_VALUE.to_bytes().len() as c_int);
		unsafe { oakengine_node_free(parent) };
		assert_eq!(unsafe { oakengine_keyframe_get_type(kf) }, 0);
		let mut kout: OakNodeValue = unsafe { std::mem::zeroed() };
		assert_eq!(unsafe { oakengine_keyframe_get_value(kf, &mut kout) }, 0);
		assert_eq!(kout.kind, vt::FLOAT);
		assert!((kout.f[0] - 1.0).abs() < 1e-9);

		// Live setters + readback.
		assert_eq!(unsafe { oakengine_keyframe_set_value_live(kf, &float_value(2.0)) }, 0);
		assert_eq!(unsafe { oakengine_keyframe_set_time_live(kf, 1, 1) }, 0);
		assert_eq!(unsafe { oakengine_keyframe_get_time(kf, &mut num, &mut den) }, 0);
		assert_eq!((num, den), (1, 1));
		assert_eq!(unsafe { oakengine_keyframe_set_bezier_point_live(kf, 0, 0.5, 0.25) }, 0);
		let mut bx: f64 = 0.0;
		let mut by: f64 = 0.0;
		assert_eq!(unsafe { oakengine_keyframe_get_bezier_point(kf, 0, &mut bx, &mut by) }, 0);
		assert!((bx - 0.5).abs() < 1e-9);
		assert!((by - 0.25).abs() < 1e-9);
		assert_eq!(unsafe { oakengine_keyframe_get_valid_bezier_point(kf, 1, &mut bx, &mut by) }, 0);
		// Bezier point index bounds.
		assert_eq!(unsafe { oakengine_keyframe_set_bezier_point_live(kf, 2, 0.0, 0.0) }, E_INVALID);
		assert_eq!(unsafe { oakengine_keyframe_get_bezier_point(kf, -1, &mut bx, &mut by) }, E_INVALID);
		// compute_paste_value against the target node.
		let mut pv: OakNodeValue = unsafe { std::mem::zeroed() };
		assert_eq!(unsafe { oakengine_keyframe_compute_paste_value(value, kf, &mut pv) }, 0);
		// Orphaned keyframe has no sibling.
		assert_eq!(unsafe { oakengine_keyframe_has_sibling_at_time(kf, 1, 1) }, 0);
		assert_eq!(unsafe { oakengine_keyframe_has_sibling_at_time(kf, 1, -1) }, 0, "negative track ignored");
		// Undoable command creators over a keyframe handle.
		let cmd = unsafe { oakengine_keyframe_set_time_command(kf, 3) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		let cmd = unsafe { oakengine_keyframe_set_value_command(kf, &float_value(3.0)) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		assert!(unsafe { oakengine_keyframe_set_value_command(kf, std::ptr::null()) }.is_null());
		// keyframes_remove_many is a documented stub → Invalid.
		let mut kf_ptr = kf;
		assert_eq!(unsafe { oakengine_keyframes_remove_many(&mut kf_ptr, 1, c"r".as_ptr()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_keyframes_remove_many(std::ptr::null_mut(), 1, c"r".as_ptr()) }, E_INVALID);
		// Dispose + NULL/empty dispose.
		unsafe { oakengine_keyframe_dispose(kf) };
		unsafe { oakengine_keyframe_dispose(std::ptr::null_mut()) };
		let empty_kf = Box::into_raw(Box::new(OakEngineKeyframe { handle: CHandle::null() }));
		unsafe { oakengine_keyframe_dispose(empty_kf) };

		// ---- dragger -----------------------------------------------------
		let drag = unsafe { oakengine_dragger_create(value, c"value_in".as_ptr(), -1, 1) };
		assert!(!drag.is_null());
		assert_eq!(unsafe { oakengine_dragger_is_started(drag) }, 0);
		assert_eq!(unsafe { oakengine_dragger_start(drag, 0, 1, 0) }, 0);
		assert_eq!(unsafe { oakengine_dragger_is_started(drag) }, 1);
		assert_eq!(unsafe { oakengine_dragger_drag(drag, &float_value(2.5)) }, 0);
		assert_eq!(unsafe { oakengine_dragger_end(drag, c"Drag Value".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_dragger_is_started(drag) }, 0, "end resets the dragger");
		// The dragged value landed in the standard value.
		let mut dv: OakNodeValue = unsafe { std::mem::zeroed() };
		assert_eq!(unsafe { oakengine_node_get_input(value, c"value_in".as_ptr(), &mut dv) }, 0);
		assert!((dv.f[0] - 2.5).abs() < 1e-6);
		// drag before start → module STATE; start twice → module STATE.
		assert_eq!(unsafe { oakengine_dragger_drag(drag, &float_value(1.0)) }, NODE_E_STATE, "drag without start");
		assert_eq!(unsafe { oakengine_dragger_start(drag, 1, 1, 0) }, 0);
		assert_eq!(unsafe { oakengine_dragger_start(drag, 2, 1, 0) }, NODE_E_STATE);
		assert_eq!(unsafe { oakengine_dragger_end(drag, c"x".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_dragger_end(drag, c"x".as_ptr()) }, NODE_E_STATE, "end without start");
		unsafe { oakengine_dragger_free(drag) };
		unsafe { oakengine_dragger_free(std::ptr::null_mut()) };
		let empty_drag = Box::into_raw(Box::new(OakEngineNodeDragger { handle: CHandle::null() }));
		unsafe { oakengine_dragger_free(empty_drag) };
		assert!(unsafe { oakengine_dragger_create(std::ptr::null_mut(), c"value_in".as_ptr(), -1, 1) }.is_null());

		// ---- group passthrough ------------------------------------------
		assert_eq!(unsafe { oakengine_group_input_passthrough_count(group) }, 0);
		let id_len = unsafe { oakengine_group_add_input_passthrough(group, value, c"value_in".as_ptr(), -1, c"".as_ptr(), buf.as_mut_ptr(), 512) };
		assert!(id_len > 0, "a passthrough id must be generated");
		let passthrough_id = unsafe { read_buf(&mut buf) };
		assert_eq!(unsafe { oakengine_group_input_passthrough_count(group) }, 1);
		// passthrough_at returns the id, the inner node, input and element.
		let mut pt_node: *mut OakEngineNode = std::ptr::null_mut();
		let mut pt_elem: c_int = 0;
		let len = unsafe { oakengine_group_input_passthrough_at(group, 0, buf.as_mut_ptr(), 512, &mut pt_node, buf.as_mut_ptr(), 512, &mut pt_elem) };
		assert!(len > 0);
		assert!(!pt_node.is_null());
		assert_eq!(unsafe { read_buf(&mut buf) }, "value_in");
		assert_eq!(pt_elem, -1);
		unsafe { oakengine_node_free(pt_node) };
		// Out-of-range passthrough index → module NOT_FOUND.
		assert_eq!(unsafe { oakengine_group_input_passthrough_at(group, 5, buf.as_mut_ptr(), 512, &mut pt_node, buf.as_mut_ptr(), 512, &mut pt_elem) }, NODE_E_NOT_FOUND);
		// id_of_passthrough round trip: the module getters return the copied
		// string length (>= 0) on success, and the facade treats only
		// negative codes as failures.
		let len = unsafe { oakengine_group_get_id_of_passthrough(group, value, c"value_in".as_ptr(), -1, buf.as_mut_ptr(), 512) };
		assert!(len > 0, "the passthrough id must be returned");
		assert_eq!(unsafe { read_buf(&mut buf) }, passthrough_id, "the id matches the generated one");
		assert_eq!(unsafe { oakengine_group_get_id_of_passthrough(group, value, c"nope_in".as_ptr(), -1, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		// get_passthrough_from_id writes the inner node, input and element.
		let passthrough_id_c = CString::new(passthrough_id.as_str()).unwrap();
		let mut back_node: *mut OakEngineNode = std::ptr::null_mut();
		let rc = unsafe { oakengine_group_get_passthrough_from_id(group, passthrough_id_c.as_ptr(), &mut back_node, buf.as_mut_ptr(), 512, &mut pt_elem) };
		assert_eq!(rc, 0);
		assert!(!back_node.is_null(), "out_node must be written");
		assert_eq!(unsafe { read_buf(&mut buf) }, "value_in");
		assert_eq!(pt_elem, -1);
		unsafe { oakengine_node_free(back_node) };
		assert_eq!(unsafe { oakengine_group_get_passthrough_from_id(group, c"no-such-id".as_ptr(), &mut back_node, buf.as_mut_ptr(), 512, &mut pt_elem) }, E_NOT_FOUND);
		// Output passthrough set/get round trip.
		assert!(unsafe { oakengine_group_get_output_passthrough(group) }.is_null());
		assert_eq!(unsafe { oakengine_group_set_output_passthrough(group, value) }, 0);
		let op = unsafe { oakengine_group_get_output_passthrough(group) };
		assert!(!op.is_null());
		unsafe { oakengine_node_free(op) };
		// resolve_input resolves the passthrough to its inner node/input.
		let mut rn: *mut OakEngineNode = std::ptr::null_mut();
		let rc = unsafe { oakengine_group_resolve_input(group, c"value_in".as_ptr(), -1, &mut rn, buf.as_mut_ptr(), 512, &mut pt_elem) };
		assert_eq!(rc, 0);
		assert!(!rn.is_null(), "the resolved node must be written");
		assert_eq!(unsafe { read_buf(&mut buf) }, "value_in");
		assert_eq!(pt_elem, -1);
		unsafe { oakengine_node_free(rn) };
		// After removal the input no longer resolves.
		assert_eq!(unsafe { oakengine_group_remove_input_passthrough(group, value, c"value_in".as_ptr(), -1) }, 0);
		assert_eq!(unsafe { oakengine_group_input_passthrough_count(group) }, 0);
		assert_eq!(unsafe { oakengine_group_remove_input_passthrough(group, value, c"value_in".as_ptr(), -1) }, NODE_E_NOT_FOUND, "already removed");
		// Undoable add + set output.
		assert_eq!(unsafe { oakengine_group_add_input_passthrough_undoable(group, value, c"value_in".as_ptr(), -1, c"".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_group_input_passthrough_count(group) }, 1);
		assert_eq!(unsafe { oakengine_group_set_output_passthrough_undoable(group, value) }, 0);
		// Opaque group command creators (free).
		let cmd = unsafe { oakengine_group_add_input_passthrough_command(group, value, c"value_in".as_ptr(), -1, c"".as_ptr()) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		let cmd = unsafe { oakengine_group_set_output_passthrough_command(group, value) };
		assert!(!cmd.is_null());
		unsafe { oakengine_undo_command_free(cmd) };
		// group_get_inner walks one passthrough level (the input id is
		// pre-filled in the inout buffer).
		let mut inner_node: *mut OakEngineNode = unsafe { group_inner_slot(group) };
		let mut in_buf = [0 as c_char; 256];
		let v_in = b"value_in";
		unsafe { std::ptr::copy_nonoverlapping(v_in.as_ptr() as *const c_char, in_buf.as_mut_ptr(), v_in.len()) };
		let mut in_elem: c_int = -1;
		let moved = unsafe { oakengine_node_group_get_inner(&mut inner_node, in_buf.as_mut_ptr(), 256, &mut in_elem) };
		assert_eq!(moved, 1, "one passthrough level is walked");
		assert_eq!(unsafe { read_buf(&mut in_buf) }, "value_in");
		assert_eq!(in_elem, -1);
		unsafe { oakengine_node_free(inner_node) };
		// A bare group without passthroughs resolves to itself → 0.
		let bare = unsafe { oakengine_project_add_node(project, TYPE_GROUP.as_ptr()) };
		assert!(!bare.is_null());
		let mut bare_node: *mut OakEngineNode = unsafe { group_inner_slot(bare) };
		assert_eq!(unsafe { oakengine_node_group_get_inner(&mut bare_node, in_buf.as_mut_ptr(), 256, &mut in_elem) }, 0);
		unsafe { oakengine_node_free(bare_node) };

		// ---- multicam node surfaces -------------------------------------
		assert_eq!(unsafe { oakengine_multicam_get_source_count(multicam) }, 0);
		assert_eq!(unsafe { oakengine_multicam_get_current_source(multicam) }, 0);

		// ---- gizmo surfaces (stubs) -------------------------------------
		assert_eq!(unsafe { oakengine_node_has_gizmos(value) }, 0);
		assert_eq!(unsafe { oakengine_node_gizmo_count(value) }, 0);
		assert!(unsafe { oakengine_node_gizmo_at(value, 0) }.is_null());
		assert_eq!(unsafe { oakengine_node_update_gizmo_positions(value, std::ptr::null_mut(), 1920, 1080, 0, 1) }, 0);
		assert_eq!(unsafe { oakengine_node_update_gizmo_positions(std::ptr::null_mut(), std::ptr::null_mut(), 0, 0, 0, 1) }, E_INVALID);

		// ---- node data (stub table) -------------------------------------
		let mut oty: c_int = 9;
		let mut oi: i64 = 9;
		for role in 0..=5 {
			assert_eq!(unsafe { oakengine_node_get_data(value, role, &mut oty, &mut oi, buf.as_mut_ptr(), 512) }, 0);
			assert_eq!(oty, 0, "stub reports no data");
		}
		assert_eq!(unsafe { oakengine_node_get_data(value, 6, &mut oty, &mut oi, buf.as_mut_ptr(), 512) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_get_data(value, -1, &mut oty, &mut oi, buf.as_mut_ptr(), 512) }, E_INVALID);
		assert_eq!(unsafe { oakengine_node_get_data(std::ptr::null(), 0, &mut oty, &mut oi, buf.as_mut_ptr(), 512) }, E_INVALID);

		// ---- exclusive dependencies / plugin messages / caches (stubs) --
		assert_eq!(unsafe { oakengine_node_get_exclusive_dependency_count(value) }, 0);
		assert!(unsafe { oakengine_node_get_exclusive_dependency_at(value, 0) }.is_null());
		assert_eq!(unsafe { oakengine_node_has_plugin(value) }, 0);
		assert_eq!(unsafe { oakengine_node_plugin_message_count(value) }, 0);
		assert_eq!(unsafe { oakengine_node_plugin_message_at(value, 0, &mut oty, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_node_plugin_clear_messages(value) }, E_NOT_FOUND);
		assert!(unsafe { oakengine_node_get_thumbnail_cache(value) }.is_null());
		assert!(unsafe { oakengine_node_get_waveform_cache(value) }.is_null());
		assert!(unsafe { oakengine_node_get_video_frame_cache(value) }.is_null(), "no cache object on a plain node");

		// ---- viewer / block / clip / track surfaces ---------------------
		assert!(unsafe { oakengine_viewer_output_get_connected_texture(value) }.is_null());
		assert!(unsafe { oakengine_clip_get_track(value) }.is_null(), "not a clip");
		assert_eq!(unsafe { oakengine_track_get_type(value) }, -1, "not a track");
		assert_eq!(unsafe { oakengine_track_get_index(value) }, -1);
		assert!(unsafe { oakengine_track_get_sequence(value) }.is_null());
		assert_eq!(unsafe { oakengine_block_get_length_rational(value, &mut tb_num, &mut tb_den) }, E_INVALID, "not a block");
		assert_eq!(unsafe { oakengine_block_get_in_rational(value, &mut tb_num, &mut tb_den) }, E_INVALID);
		assert_eq!(unsafe { oakengine_block_get_out_rational(value, &mut tb_num, &mut tb_den) }, E_INVALID);

		// ---- subtitle / shape (stubs) -----------------------------------
		assert_eq!(unsafe { oakengine_subtitle_get_text(value, buf.as_mut_ptr(), 512) }, E_INVALID);
		assert_eq!(unsafe { oakengine_subtitle_set_text(value, c"sub".as_ptr()) }, E_INVALID);
		let params = unsafe { std::mem::zeroed::<OakVideoParamsPod>() };
		assert_eq!(unsafe { oakengine_shape_set_rect_undoable(value, 0.0, 0.0, 10.0, 10.0, &params, std::ptr::null_mut()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_shape_set_rect_undoable(std::ptr::null_mut(), 0.0, 0.0, 10.0, 10.0, &params, std::ptr::null_mut()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_shape_set_rect_undoable(value, 0.0, 0.0, 10.0, 10.0, std::ptr::null(), std::ptr::null_mut()) }, E_INVALID);
		// effect-input surface is a documented stub.
		assert_eq!(unsafe { oakengine_node_get_effect_input(value, buf.as_mut_ptr(), 512, &mut oty) }, E_NOT_FOUND);

		// ---- bulk delete -------------------------------------------------
		// The solid → transform edge from the connect section is still live,
		// so a redundant reconnect is rejected with the module STATE error;
		// the edge is then deleted together with the node in one multi
		// command.
		assert_eq!(unsafe { oakengine_node_connect(solid, transform, c"tex_in".as_ptr()) }, NODE_E_STATE, "the edge from the connect section is still live");
		let del_nodes = [transform];
		let edge_outputs = [solid];
		let edge_inputs = [transform];
		let edge_ids = [c"tex_in".as_ptr() as *const c_char];
		let edge_elems: [c_int; 1] = [-1];
		let count_before = unsafe { oakengine_project_node_count(project) };
		assert_eq!(unsafe { oakengine_nodes_delete_many(
			del_nodes.as_ptr() as *mut *mut OakEngineNode,
			std::ptr::null_mut(),
			1,
			edge_outputs.as_ptr() as *mut *mut OakEngineNode,
			edge_inputs.as_ptr() as *mut *mut OakEngineNode,
			edge_ids.as_ptr() as *mut *const c_char,
			edge_elems.as_ptr(),
			1,
		) }, 0);
		assert_eq!(unsafe { oakengine_project_node_count(project) }, count_before - 1, "delete must remove the node");
		// Empty delete → Invalid ("nothing to delete").
		assert_eq!(unsafe { oakengine_nodes_delete_many(std::ptr::null_mut(), std::ptr::null_mut(), 0, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null(), 0) }, E_INVALID);
		// _ex with a null node entry → Invalid.
		let mut null_nodes = [std::ptr::null_mut()];
		assert_eq!(unsafe { oakengine_nodes_delete_many_ex(
			null_nodes.as_mut_ptr(),
			std::ptr::null_mut(),
			1,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null(),
			0,
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null_mut(),
			std::ptr::null(),
			0,
		) }, E_INVALID);

		// ---- folders -----------------------------------------------------
		let root = unsafe { oakengine_project_root(project) };
		let folder = unsafe { oakengine_folder_create(project, root, c"Bin".as_ptr()) };
		assert!(!folder.is_null());
		let len = unsafe { oakengine_node_get_label(folder, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "Bin".len() as c_int);
		assert_eq!(unsafe { oakengine_node_is_folder(folder) }, 1);
		// Root now has the folder as a child.
		let root2 = unsafe { oakengine_project_root(project) };
		assert_eq!(unsafe { oakengine_folder_item_child_count(root2) }, 1);
		assert_eq!(unsafe { oakengine_folder_has_child_recursive(root2, folder) }, 1);
		assert_eq!(unsafe { oakengine_folder_index_of_child(root2, folder) }, 0);
		let child = unsafe { oakengine_folder_item_child(root2, 0) };
		assert!(!child.is_null());
		let len = unsafe { oakengine_node_get_type_id(child, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "org.olivevideoeditor.Olive.folder".len() as c_int);
		unsafe { oakengine_node_free(child) };
		assert!(unsafe { oakengine_folder_item_child(root2, 5) }.is_null());
		assert_eq!(unsafe { oakengine_folder_item_child_count(std::ptr::null()) }, 0);
		assert_eq!(unsafe { oakengine_folder_has_child_recursive(std::ptr::null(), folder) }, 0);
		assert_eq!(unsafe { oakengine_folder_has_child_recursive(root2, std::ptr::null()) }, 0);
		assert_eq!(unsafe { oakengine_folder_index_of_child(std::ptr::null(), folder) }, E_INVALID);
		assert_eq!(unsafe { oakengine_folder_index_of_child(root2, std::ptr::null()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_folder_index_of_child(root2, value) }, E_NOT_FOUND, "value is not in the root");
		// folder_add_child is undoable; add the value node under the folder.
		assert_eq!(unsafe { oakengine_folder_add_child(folder, value) }, 0);
		assert_eq!(unsafe { oakengine_folder_item_child_count(folder) }, 1);
		assert_eq!(unsafe { oakengine_folder_item_child_count(root2) }, 1, "moved out of the root");
		// A node already in one folder cannot be added to a second one: the
		// facade mirrors the module's live one-folder-per-node check (its
		// UNDOABLE command creator skips it) and rejects with STATE.
		assert_eq!(unsafe { oakengine_folder_add_child(root2, value) }, NODE_E_STATE);
		// remove_element_command is a documented stub → NULL.
		assert!(unsafe { oakengine_folder_remove_element_command(root2, value) }.is_null());
		// move_children: move the value node back into the root.
		let mut mv = [value];
		assert_eq!(unsafe { oakengine_folder_move_children(mv.as_mut_ptr(), 1, root2, c"move".as_ptr()) }, 0);
		assert_eq!(unsafe { oakengine_folder_item_child_count(root2) }, 2);
		assert_eq!(unsafe { oakengine_folder_move_child(value, root2) }, 0);
		assert_eq!(unsafe { oakengine_folder_move_children(std::ptr::null_mut(), 1, root2, c"x".as_ptr()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_folder_move_children(mv.as_mut_ptr(), 0, root2, c"x".as_ptr()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_folder_move_children(mv.as_mut_ptr(), 1, std::ptr::null_mut(), c"x".as_ptr()) }, E_INVALID);
		// folder_create requires a folder parent.
		assert!(unsafe { oakengine_folder_create(project, value, c"x".as_ptr()) }.is_null());
		unsafe { oakengine_node_free(folder) };
		unsafe { oakengine_node_free(root2) };

		// ---- undo/redo over the global stack ----------------------------
		assert_eq!(unsafe { oakengine_project_can_undo(project) }, 1, "commands were pushed above");
		assert_eq!(unsafe { oakengine_project_undo(project) }, 0);
		assert_eq!(unsafe { oakengine_project_can_redo(project) }, 1);
		assert_eq!(unsafe { oakengine_project_redo(project) }, 0);
		assert_eq!(unsafe { oakengine_project_can_undo(std::ptr::null_mut()) }, 0);
		assert_eq!(unsafe { oakengine_project_can_redo(std::ptr::null_mut()) }, 0);
		assert_eq!(unsafe { oakengine_project_undo(std::ptr::null_mut()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_project_redo(std::ptr::null_mut()) }, E_INVALID);

		// ---- save → fresh load round trip --------------------------------
		let save_path = std::env::temp_dir().join(format!("oak_it_node_save-{}.ovexml", std::process::id()));
		let _ = std::fs::remove_file(&save_path);
		let sp = CString::new(save_path.to_string_lossy().into_owned()).unwrap();
		assert_eq!(unsafe { oakengine_project_save(project, sp.as_ptr()) }, 0);
		assert!(save_path.exists());
		// Save with NULL path uses the project filename.
		assert_eq!(unsafe { oakengine_project_save(project, std::ptr::null()) }, 0);
		assert_eq!(unsafe { oakengine_project_save(std::ptr::null_mut(), sp.as_ptr()) }, E_INVALID);

		let project2 = oakengine_project_create();
		let mut err = [0 as c_char; 512];
		let rc = unsafe { oakengine_project_load(project2, sp.as_ptr(), err.as_mut_ptr(), 512) };
		if rc == 0 {
			assert!(unsafe { oakengine_project_node_count(project2) } >= 1);
		} else {
			let err_len = unsafe { CStr::from_ptr(err.as_ptr()) }.to_bytes().len();
			assert!(err_len > 0, "load failure must fill the error buffer");
		}
		unsafe { oakengine_project_free(project2) };
		// Load on an already-initialized project → E_STATE.
		assert_eq!(unsafe { oakengine_project_load(project, sp.as_ptr(), err.as_mut_ptr(), 512) }, E_STATE);
		// Load with a NULL path → Invalid.
		let project3 = oakengine_project_create();
		assert_eq!(unsafe { oakengine_project_load(project3, std::ptr::null(), err.as_mut_ptr(), 512) }, E_INVALID);
		// Load of a nonexistent file → failure + non-empty err buffer.
		let rc = unsafe { oakengine_project_load(project3, c"/no/such/project-file.ove".as_ptr(), err.as_mut_ptr(), 512) };
		assert!(rc < 0);
		assert!(unsafe { CStr::from_ptr(err.as_ptr()) }.to_bytes().len() > 0);
		unsafe { oakengine_project_free(project3) };
		let _ = std::fs::remove_file(&save_path);

		// ---- footage: probe / import / proxy / relink -------------------
		let media_a = fresh_temp_file("media-a.mp4", b"not-real-media");
		let media_a_c = CString::new(media_a.to_string_lossy().into_owned()).unwrap();

		// Probe a real file (module records the filename only; no decoder).
		let footage = unsafe { oakengine_footage_probe(media_a_c.as_ptr()) };
		assert!(!footage.is_null());
		let len = unsafe { oakengine_footage_get_filename(footage, buf.as_mut_ptr(), 512) };
		assert!(len > 0);
		assert_eq!(unsafe { read_buf(&mut buf) }, media_a.to_string_lossy().into_owned());
		assert_eq!(unsafe { oakengine_footage_get_decoder_name(footage, buf.as_mut_ptr(), 512) }, 0);
		assert_eq!(unsafe { oakengine_footage_get_video_stream_count(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_get_audio_stream_count(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_get_subtitle_stream_count(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_is_online(footage) }, 1);
		let mut secs: f64 = -1.0;
		assert_eq!(unsafe { oakengine_footage_get_duration(footage, &mut secs) }, 0);
		assert_eq!(secs, 0.0, "module footage has no media duration");
		// No streams → stream accessors report NOT_FOUND.
		let mut vinfo: OakFootageVideoInfo = unsafe { std::mem::zeroed() };
		let mut ainfo: OakFootageAudioInfo = unsafe { std::mem::zeroed() };
		assert_eq!(unsafe { oakengine_footage_get_video_stream_info(footage, 0, &mut vinfo) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_get_audio_stream_info(footage, 0, &mut ainfo) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_get_video_stream_info(footage, -1, &mut vinfo) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_get_video_stream_info(std::ptr::null_mut(), 0, &mut vinfo) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_get_video_stream_info(footage, 0, std::ptr::null_mut()) }, E_INVALID);
		let mut cr: c_int = 0;
		let mut il: c_int = 0;
		let mut pm: c_int = 0;
		assert_eq!(unsafe { oakengine_footage_get_video_stream_overrides(footage, 0, buf.as_mut_ptr(), 512, &mut cr, &mut il, &mut pm) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_set_video_stream_overrides(footage, 0, c"".as_ptr(), 0, 0, 0) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_get_pixel_aspect(footage, 0, &mut tb_num, &mut tb_den) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_set_pixel_aspect(footage, 0, 1, 1) }, E_NOT_FOUND);
		// Bad pixel-aspect ratio is rejected before the stream lookup.
		assert_eq!(unsafe { oakengine_footage_set_pixel_aspect(footage, 0, 0, 1) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_set_pixel_aspect(footage, 0, 1, 0) }, E_INVALID);
		let mut si: i64 = 0;
		let mut dur: i64 = 0;
		assert_eq!(unsafe { oakengine_footage_get_image_sequence_params(footage, 0, &mut si, &mut dur, &mut tb_num, &mut tb_den) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_set_image_sequence_params(footage, 0, 0, 1, 25, 1) }, E_NOT_FOUND);
		// Bad image-sequence params are rejected first.
		assert_eq!(unsafe { oakengine_footage_set_image_sequence_params(footage, 0, -1, 1, 25, 1) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_set_image_sequence_params(footage, 0, 0, 0, 25, 1) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_set_image_sequence_params(footage, 0, 0, 1, 0, 1) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_set_image_sequence_params(footage, 0, 0, 1, 25, 0) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_get_stream_enabled(footage, 0, 0) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_get_stream_enabled(footage, 3, 0) }, E_NOT_FOUND, "garbage track type");
		assert_eq!(unsafe { oakengine_footage_set_stream_enabled(footage, 0, 0, 1) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_get_stream_reference(footage, 0, &mut oty, &mut tb_num) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_get_stream_reference(footage, -1, &mut oty, &mut tb_num) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_describe_video_stream(footage, 0, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_footage_describe_audio_stream(footage, 0, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		// Source start time is a documented stub (0/1).
		assert_eq!(unsafe { oakengine_footage_get_source_start_time(footage, &mut tb_num, &mut tb_den) }, 0);
		assert_eq!((tb_num, tb_den), (0, 1));
		assert_eq!(unsafe { oakengine_footage_set_source_start_time(footage, 1, 0, 1) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_get_source_start_time_source(footage, buf.as_mut_ptr(), 512) }, 0);
		// Colorspace candidates are stubs.
		assert_eq!(unsafe { oakengine_footage_colorspace_count(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_colorspace_at(footage, 0, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		// Proxy state: defaults, set round trip, delete/clear.
		assert_eq!(unsafe { oakengine_footage_proxy_get_state(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_proxy_is_enabled(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_proxy_get_path(footage, buf.as_mut_ptr(), 512) }, 0);
		assert_eq!(unsafe { oakengine_footage_proxy_generate(footage) }, E_STATE, "documented stub");
		assert_eq!(unsafe { oakengine_footage_set_proxy(footage, c"/tmp/proxy.mp4".as_ptr(), 1, 0, 1, 1) }, 0);
		assert_eq!(unsafe { oakengine_footage_proxy_get_state(footage) }, 1);
		assert_eq!(unsafe { oakengine_footage_proxy_is_enabled(footage) }, 1);
		let len = unsafe { oakengine_footage_proxy_get_path(footage, buf.as_mut_ptr(), 512) };
		assert_eq!(len, "/tmp/proxy.mp4".len() as c_int);
		assert_eq!(unsafe { read_buf(&mut buf) }, "/tmp/proxy.mp4");
		assert_eq!(unsafe { oakengine_footage_proxy_delete(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_proxy_get_state(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_proxy_set_enabled(footage, 1) }, 0);
		assert_eq!(unsafe { oakengine_footage_proxy_is_enabled(footage) }, 1);
		assert_eq!(unsafe { oakengine_footage_clear_proxy(footage) }, 0);
		assert_eq!(unsafe { oakengine_footage_proxy_is_enabled(footage) }, 0);
		// Custom proxy params stubs.
		assert_eq!(unsafe { oakengine_footage_has_custom_proxy_params(footage) }, 0);
		let mut pp: OakProxyParams = unsafe { std::mem::zeroed() };
		assert_eq!(unsafe { oakengine_footage_get_effective_proxy_params(footage, &mut pp) }, 0);
		assert_eq!(pp.width, 0);
		assert_eq!(unsafe { oakengine_footage_set_custom_proxy_params(footage, &pp) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_clear_custom_proxy_params(footage) }, E_INVALID);
		assert_eq!(unsafe { oakengine_footage_invalidate(footage) }, 0, "documented no-op");
		// Relink requires an existing file.
		let media_b = fresh_temp_file("media-b.mp4", b"other");
		let media_b_c = CString::new(media_b.to_string_lossy().into_owned()).unwrap();
		assert_eq!(unsafe { oakengine_footage_relink(footage, media_b_c.as_ptr()) }, 0);
		let len = unsafe { oakengine_footage_get_filename(footage, buf.as_mut_ptr(), 512) };
		assert_eq!(unsafe { read_buf(&mut buf) }, media_b.to_string_lossy().into_owned());
		let _ = len;
		// Relink to a nonexistent file → NOT_FOUND + footage error text.
		assert_eq!(unsafe { oakengine_footage_relink(footage, c"/no/such/file.mp4".as_ptr()) }, E_NOT_FOUND);
		let err_len = unsafe { oakengine_footage_last_error(buf.as_mut_ptr(), 512) };
		assert!(err_len > 0, "footage_last_error must be non-empty after a failed relink");
		// NULL path → Invalid.
		assert_eq!(unsafe { oakengine_footage_relink(footage, std::ptr::null()) }, E_INVALID);
		unsafe { oakengine_footage_free(footage) };
		unsafe { oakengine_footage_free(std::ptr::null_mut()) };
		let empty_ftg = Box::into_raw(Box::new(OakEngineFootage { handle: CHandle::null() }));
		unsafe { oakengine_footage_free(empty_ftg) };

		// Probe failure paths.
		let probe_null = unsafe { oakengine_footage_probe(std::ptr::null()) };
		assert!(probe_null.is_null());
		let probe_missing = unsafe { oakengine_footage_probe(c"/no/such/media.mp4".as_ptr()) };
		assert!(probe_missing.is_null());
		let err_len = unsafe { oakengine_footage_last_error(buf.as_mut_ptr(), 512) };
		assert!(err_len > 0);

		// Import into the project (real file).
		let imported = unsafe { oakengine_project_import_footage(project, media_b_c.as_ptr()) };
		assert!(!imported.is_null());
		assert_eq!(unsafe { oakengine_project_footage_count(project) }, 1);
		let len = unsafe { oakengine_project_footage_filename(project, 0, buf.as_mut_ptr(), 512) };
		assert!(len > 0);
		assert_eq!(unsafe { oakengine_project_footage_is_online(project, 0) }, 1);
		assert_eq!(unsafe { oakengine_project_footage_filename(project, 5, buf.as_mut_ptr(), 512) }, E_NOT_FOUND);
		assert_eq!(unsafe { oakengine_project_footage_is_online(project, 5) }, E_NOT_FOUND);
		// The footage node is in the graph: borrow + validity.
		let footage_idx = unsafe { find_node(project, TYPE_FOOTAGE.to_str().unwrap()) };
		assert!(footage_idx >= 0);
		let footage_node = unsafe { oakengine_project_node_at(project, footage_idx) };
		assert_eq!(unsafe { oakengine_node_is_footage(footage_node) }, 1);
		assert_eq!(unsafe { oakengine_footage_is_valid(footage_node) }, 0, "module footage is never probed");
		// `oakengine_footage_borrow` takes its OWN addref'd reference, so the
		// borrow and the source node shell can BOTH be freed (no double-free).
		let borrowed = unsafe { oakengine_footage_borrow(footage_node) };
		assert!(!borrowed.is_null());
		let len = unsafe { oakengine_footage_get_filename(borrowed, buf.as_mut_ptr(), 512) };
		assert!(len > 0);
		unsafe { oakengine_footage_free(borrowed) };
		unsafe { oakengine_node_free(footage_node) };
		// Borrow of a non-footage node → NULL.
		assert!(unsafe { oakengine_footage_borrow(value) }.is_null());
		assert_eq!(unsafe { oakengine_footage_is_valid(value) }, 0);
		// Import failure paths.
		assert!(unsafe { oakengine_project_import_footage(project, c"/no/such/media.mp4".as_ptr()) }.is_null());
		assert!(unsafe { oakengine_project_import_footage(std::ptr::null_mut(), media_b_c.as_ptr()) }.is_null());
		assert!(unsafe { oakengine_project_import_footage(project, std::ptr::null()) }.is_null());

		// find_offline_footage: make the imported footage offline and
		// relink it from a search directory.
		let media_b_basename = media_b.file_name().unwrap().to_string_lossy().into_owned();
		let _ = std::fs::remove_file(&media_b); // now offline
		assert_eq!(unsafe { oakengine_project_footage_is_online(project, 0) }, 0);
		let search_dir = std::env::temp_dir().join(format!("oak-it-node-search-{}", std::process::id()));
		let _ = std::fs::remove_dir_all(&search_dir);
		std::fs::create_dir_all(&search_dir).unwrap();
		std::fs::write(search_dir.join(&media_b_basename), b"found").unwrap();
		let search_c = CString::new(search_dir.to_string_lossy().into_owned()).unwrap();
		assert_eq!(unsafe { oakengine_project_find_offline_footage(project, search_c.as_ptr()) }, 1);
		assert_eq!(unsafe { oakengine_project_footage_is_online(project, 0) }, 1);
		assert_eq!(unsafe { oakengine_project_find_offline_footage(project, std::ptr::null()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_project_find_offline_footage(std::ptr::null_mut(), search_c.as_ptr()) }, E_INVALID);
		assert_eq!(unsafe { oakengine_project_find_offline_footage(project, c"/no/such/dir".as_ptr()) }, E_INVALID);

		// ---- remove a node + free the project ---------------------------
		assert_eq!(unsafe { oakengine_project_remove_node(project, value2) }, 0);
		assert_eq!(unsafe { oakengine_project_remove_node(project, value2) }, E_INVALID, "already removed");
		assert_eq!(unsafe { oakengine_project_remove_node(std::ptr::null_mut(), value) }, E_INVALID);

		// Free borrowed shells captured above.
		unsafe { oakengine_node_free(value) };
		unsafe { oakengine_node_free(value2) };
		unsafe { oakengine_node_free(solid) };
		unsafe { oakengine_node_free(group) };
		unsafe { oakengine_node_free(bare) };
		unsafe { oakengine_node_free(multicam) };
		unsafe { oakengine_node_free(orphan) };
		unsafe { oakengine_footage_free(imported) };
		unsafe { oakengine_project_free(project) };
		// One intentional process-lifetime leak remains: the hidden probe
		// project created by the first `oakengine_footage_probe` (leaked
		// like the C++ EngineCore shell). The add_node factory handles are
		// released by the facade, so they no longer leak.
		assert_eq!(alive(), base + 1, "only the probe project leak remains");
	});
}

// ---------------------------------------------------------------------------
// Illegal inputs: NULL / empty handles / bad sizes / garbage (parallel-safe)
// ---------------------------------------------------------------------------

/// NULL and empty-`CHandle` boxes for the handle-taking exports: every
/// call must return a clean negative code or the documented no-op, never a
/// crash. Runs in parallel (no undo-stack or alive-counter access).
#[test]
fn null_and_empty_handle_failure_paths() {
	common::force_link();
	let _ = force_oakundo_command_link();
	let mut buf = [0 as c_char; 512];
	let mut out: OakNodeValue = unsafe { std::mem::zeroed() };
	let mut num: i64 = 0;
	let mut den: i64 = 0;
	// Scratch out-params reused across the failure calls below.
	let mut f64a: f64 = 0.0;
	let mut f64b: f64 = 0.0;
	let mut f32a: f32 = 0.0;
	let mut f32b: f32 = 0.0;
	let mut f32c: f32 = 0.0;
	let mut f32d: f32 = 0.0;
	let mut i32a: c_int = 0;
	let mut i32b: c_int = 0;
	let mut i32c: c_int = 0;
	let mut i64a: i64 = 0;
	let mut i64b: i64 = 0;

	// Empty-handle boxes (non-NULL pointers wrapping CHandle::null()).
	let node = empty_node_box();
	let project = Box::into_raw(Box::new(OakEngineProject { handle: CHandle::null() }));
	let keyframe = Box::into_raw(Box::new(OakEngineKeyframe { handle: CHandle::null() }));
	let dragger = Box::into_raw(Box::new(OakEngineNodeDragger { handle: CHandle::null() }));
	let footage = Box::into_raw(Box::new(OakEngineFootage { handle: CHandle::null() }));

	// project family
	assert_eq!(unsafe { oakengine_project_new(project) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_is_modified(project) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_project_set_modified(project, 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_name(project, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_filename(project, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_pretty_filename(project, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_set_filename(project, c"/x.ove".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_cache_path(project, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_cache_alongside_path(project, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_set_custom_cache_path(project, c"/x".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_get_custom_cache_path(project, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_get_cache_location_setting(project) }, -1, "NULL → -1 documented");
	assert_eq!(unsafe { oakengine_project_get_color_reference_space(project, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_set_color_reference_space(project, c"x".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_footage_count(project) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_project_footage_filename(project, 0, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_footage_is_online(project, 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_sequence_count(project) }, E_INVALID, "empty handle, not a NULL pointer");
	assert!(unsafe { oakengine_project_sequence_at(project, 0) }.is_null());
	assert_eq!(unsafe { oakengine_project_node_count(project) }, E_INVALID, "empty handle, not a NULL pointer");
	assert!(unsafe { oakengine_project_node_at(project, 0) }.is_null());
	assert!(unsafe { oakengine_project_root(project) }.is_null());
	assert!(unsafe { oakengine_project_from_object(node) }.is_null());
	assert_eq!(unsafe { oakengine_project_save(project, c"/x.ove".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_project_load(project, c"/x.ove".as_ptr(), buf.as_mut_ptr(), 512) }, E_INVALID);

	// folder family
	assert!(unsafe { oakengine_folder_create(project, node, c"x".as_ptr()) }.is_null());
	assert_eq!(unsafe { oakengine_folder_has_child_recursive(node, node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_folder_index_of_child(node, node) }, E_INVALID);
	assert_eq!(unsafe { oakengine_folder_item_child_count(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert!(unsafe { oakengine_folder_item_child(node, 0) }.is_null());
	assert_eq!(unsafe { oakengine_folder_add_child(node, node) }, E_INVALID);
	assert!(unsafe { oakengine_folder_remove_element_command(node, node) }.is_null());
	assert_eq!(unsafe { oakengine_folder_move_child(node, node) }, E_INVALID);
	assert_eq!(unsafe { oakengine_folder_move_children(std::ptr::null_mut(), 1, node, c"x".as_ptr()) }, E_INVALID);

	// node factory / metadata / params
	assert!(unsafe { oakengine_node_factory_create_from_id(c"x".as_ptr()) }.is_null());
	assert_eq!(unsafe { oakengine_node_category_count(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_category_at(node, 0) }, -1);
	// Empty handle and NULL both report 0 flags (no error sentinel leaks
	// through as u64::MAX).
	assert_eq!(unsafe { oakengine_node_get_flags(node) }, 0);
	assert_eq!(unsafe { oakengine_node_get_sub_category(node, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_description(node, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert!(unsafe { oakengine_node_create_copy(node) }.is_null());
	assert_eq!(unsafe { oakengine_node_get_type_id(node, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_name(node, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_short_name(node, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_label(node, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_set_label(node, c"x".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_set_label_ex(node, c"x".as_ptr(), 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_label_and_name(node, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_color_label(node) }, -1, "NULL → -1 documented");
	assert_eq!(unsafe { oakengine_node_get_effective_color_label(node) }, -1);
	assert!(unsafe { oakengine_node_set_color_label_command(node, 0) }.is_null());
	assert_eq!(unsafe { oakengine_node_input_count(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_id(node, 0, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_type(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_is_connected(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_is_connectable(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_is_keyframable(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_is_keyframed(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_is_keyframed_ex(node, c"x".as_ptr(), 0) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_is_array(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_array_size(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_get_flags(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_get_data_type(node, c"x".as_ptr()) }, -1);
	assert_eq!(unsafe { oakengine_node_input_is_hidden(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_get_input(node, c"x".as_ptr(), &mut out) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_set_input(node, c"x".as_ptr(), &float_value(1.0)) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_input_string(node, c"x".as_ptr(), buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_set_input_string(node, c"x".as_ptr(), c"s".as_ptr()) }, E_INVALID);
	assert!(unsafe { oakengine_node_set_standard_value_command(node, c"x".as_ptr(), -1, -1, &float_value(1.0)) }.is_null(), "stub-null");
	assert!(unsafe { oakengine_node_set_input_video_params_command(node, c"x".as_ptr(), &unsafe { std::mem::zeroed::<OakVideoParamsPod>() }) }.is_null(), "stub-null");
	assert!(unsafe { oakengine_node_set_value_at_time_command(node as *mut c_void, c"x".as_ptr(), -1, 0, 1, &float_value(1.0), -1, 0) }.is_null(), "empty handle");
	assert_eq!(unsafe { oakengine_node_frame_time_base(node, &mut i32a, &mut i32b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_set_input_at_time(node, c"x".as_ptr(), -1, 0, -1, &float_value(1.0), 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_input_at_time(node, c"x".as_ptr(), -1, -1, 0, 0, &mut out) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_set_input_string_at_time(node, c"x".as_ptr(), -1, 0, c"s".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_input_string_at_time(node, c"x".as_ptr(), -1, 0, -1, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_input_bezier_at_time(node, c"x".as_ptr(), -1, 0, -1, std::ptr::null_mut()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_input_binary_at_time(node, c"x".as_ptr(), -1, 0, -1, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_array_insert_at(node, c"x".as_ptr(), 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_array_remove_at(node, c"x".as_ptr(), 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_has_property(node, c"x".as_ptr(), c"k".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_set_input_property_string(node, c"x".as_ptr(), c"k".as_ptr(), c"v".as_ptr(), 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_property_string(node, c"x".as_ptr(), c"k".as_ptr(), buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_property_number(node, c"x".as_ptr(), c"k".as_ptr(), 0, &mut f64a) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_property_int(node, c"x".as_ptr(), c"k".as_ptr(), &mut num) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_property_rational(node, c"x".as_ptr(), c"k".as_ptr(), std::ptr::null_mut(), std::ptr::null_mut()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_property_track_number(node, c"x".as_ptr(), c"k".as_ptr(), 0, &mut f64a) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_property_count(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_get_property_key(node, c"x".as_ptr(), 0, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_property_string_list_count(node, c"x".as_ptr(), c"k".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_get_property_string_list(node, c"x".as_ptr(), c"k".as_ptr(), 0, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_get_default_value(node, c"x".as_ptr(), 0, &mut out) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_input_name(node, c"x".as_ptr(), buf.as_mut_ptr(), 512) }, E_INVALID);

	// graph editing
	assert_eq!(unsafe { oakengine_node_connect(node, node, c"x".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_disconnect(node, c"x".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_disconnect_ex(node, c"x".as_ptr(), -1) }, E_INVALID);
	assert!(unsafe { oakengine_node_connect_command(node, node, c"x".as_ptr(), -1) }.is_null());
	assert!(unsafe { oakengine_node_disconnect_command(node, c"x".as_ptr(), -1) }.is_null());
	assert_eq!(unsafe { oakengine_node_copy_inputs(node, node) }, E_INVALID);
	assert!(unsafe { oakengine_node_copy_in_graph(node, std::ptr::null_mut()) }.is_null());
	assert!(unsafe { oakengine_node_input_get_connected_node(node, c"x".as_ptr(), -1) }.is_null());
	assert_eq!(unsafe { oakengine_node_output_connection_count(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_output_connection_at(node, 0, std::ptr::null_mut(), buf.as_mut_ptr(), 512, std::ptr::null_mut()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_output_connection_at_ex(node, 0, std::ptr::null_mut(), buf.as_mut_ptr(), 512, std::ptr::null_mut(), std::ptr::null_mut()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_connection_count_all(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_input_connection_at_all(node, 0, std::ptr::null_mut(), buf.as_mut_ptr(), 512, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_input_connection_count(node, c"x".as_ptr(), -1) }, E_INVALID, "empty handle, not a NULL pointer");
	assert!(unsafe { oakengine_node_input_connection_at(node, c"x".as_ptr(), -1, 0) }.is_null());
	assert_eq!(unsafe { oakengine_node_inputs_from(node, node, 1) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_set_value_hint(node, c"x".as_ptr(), 0, 0, 0, c"".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_set_context_position(node, node, 0.0, 0.0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_context_position(node, node, &mut f64a, &mut f64b, &mut i32a) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_set_context_expanded(node, node, 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_context_node_count(node) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_context_contains_node(node, node) }, E_INVALID);
	assert!(unsafe { oakengine_node_context_node_at(node, 0, &mut f64a, &mut f64b, &mut i32a) }.is_null());
	assert_eq!(unsafe { oakengine_node_get_effect_input(node, buf.as_mut_ptr(), 512, &mut i32a) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframe_count(node, c"x".as_ptr()) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_keyframe_count_on_track(node, c"x".as_ptr(), 0, 0) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_keyframe_at(node, c"x".as_ptr(), 0, &mut num, &mut out) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframe_get_easing(node, c"x".as_ptr(), 0, &mut f32a, &mut f32b, &mut f32c, &mut f32d, &mut i32a) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframe_add(node, c"x".as_ptr(), 0, &float_value(1.0), 0, 0.0, 0.0, 0.0, 0.0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframe_remove(node, c"x".as_ptr(), 0) }, E_INVALID);
	assert!(unsafe { oakengine_node_insert_keyframe_command(node, c"x".as_ptr(), -1, 0, 0, &float_value(1.0), 0, 0.0, 0.0, 0.0, 0.0) }.is_null());
	assert_eq!(unsafe { oakengine_node_keyframe_set_easing(node, c"x".as_ptr(), 0, 0, 0.0, 0.0, 0.0, 0.0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframes_set_type_many(node, c"x".as_ptr(), -1, std::ptr::null(), std::ptr::null(), 0, 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframes_set_time_many(node, c"x".as_ptr(), -1, std::ptr::null(), std::ptr::null(), 0, 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframes_set_value_many(node, c"x".as_ptr(), -1, std::ptr::null(), std::ptr::null(), 0, std::ptr::null(), std::ptr::null()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframes_set_bezier_many(node, c"x".as_ptr(), -1, std::ptr::null(), std::ptr::null(), 0, 0.0, 0.0, 0.0, 0.0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframe_set_bezier_point(node, c"x".as_ptr(), -1, 0, 0, 0, 0.0, 0.0, 0.0, 0.0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframes_clear(node, c"x".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_keyframe_best_type_at_time(node, c"x".as_ptr(), -1, 0, 0, 5) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_keyframe_track_count(node, c"x".as_ptr(), -1) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_keyframes_toggle_at_time(node, c"x".as_ptr(), -1, 0, 0, 1, c"t".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_has_keyframe_at_time(node, c"x".as_ptr(), -1, 0, 0) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_keyframe_earliest_time(node, c"x".as_ptr(), -1, &mut num, &mut den) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_keyframe_latest_time(node, c"x".as_ptr(), -1, &mut num, &mut den) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_keyframe_closest_time_before(node, c"x".as_ptr(), -1, 0, 0, &mut num, &mut den) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_keyframe_closest_time_after(node, c"x".as_ptr(), -1, 0, 0, &mut num, &mut den) }, E_INVALID, "empty handle, not a NULL pointer");
	assert!(unsafe { oakengine_node_keyframe_handle_on_track(node, c"x".as_ptr(), -1, 0, 0) }.is_null());
	assert!(unsafe { oakengine_node_keyframe_handle_at_time(node, c"x".as_ptr(), -1, 0, 0, 1) }.is_null());
	assert_eq!(unsafe { oakengine_node_keyframes_at_time(node, c"x".as_ptr(), -1, 0, 1, std::ptr::null_mut(), 0) }, 0, "stub ignores its handle");
	assert_eq!(unsafe { oakengine_node_set_input_keyframing(node, c"x".as_ptr(), -1, 1, 0, 0, c"k".as_ptr()) }, E_INVALID);
	assert!(unsafe { oakengine_node_set_input_keyframing_command(node, c"x".as_ptr(), -1, 1) }.is_null());
	assert_eq!(unsafe { oakengine_node_keyframes_paste(node, std::ptr::null_mut(), 1, c"p".as_ptr()) }, E_INVALID);

	// keyframe handle family
	assert_eq!(unsafe { oakengine_keyframe_get_time(keyframe, &mut num, &mut den) }, E_INVALID);
	assert_eq!(unsafe { oakengine_keyframe_get_input_id(keyframe, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_keyframe_get_track(keyframe) }, -1);
	assert_eq!(unsafe { oakengine_keyframe_get_element(keyframe) }, -1);
	assert!(unsafe { oakengine_keyframe_get_node(keyframe) }.is_null());
	assert_eq!(unsafe { oakengine_keyframe_get_type(keyframe) }, -1);
	assert_eq!(unsafe { oakengine_keyframe_get_value(keyframe, &mut out) }, E_INVALID);
	assert_eq!(unsafe { oakengine_keyframe_compute_paste_value(node, keyframe, &mut out) }, E_INVALID);
	assert_eq!(unsafe { oakengine_keyframe_has_sibling_at_time(keyframe, 0, 0) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_keyframe_set_bezier_point_live(keyframe, 0, 0.0, 0.0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_keyframe_get_bezier_point(keyframe, 0, &mut f64a, &mut f64b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_keyframe_get_valid_bezier_point(keyframe, 0, &mut f64a, &mut f64b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_keyframe_set_value_live(keyframe, &float_value(1.0)) }, E_INVALID);
	assert_eq!(unsafe { oakengine_keyframe_set_time_live(keyframe, 1, 1) }, E_INVALID);
	assert!(unsafe { oakengine_keyframe_set_time_command(keyframe, 1) }.is_null());
	assert!(unsafe { oakengine_keyframe_set_value_command(keyframe, &float_value(1.0)) }.is_null());
	assert!(unsafe { oakengine_keyframe_create(node, c"x".as_ptr(), -1, 0, 0, 0, &float_value(1.0), 0) }.is_null(), "empty node");
	assert!(unsafe { oakengine_keyframe_create(std::ptr::null_mut(), c"x".as_ptr(), -1, 0, 0, 0, &float_value(1.0), 0) }.is_null());

	// dragger family
	assert!(unsafe { oakengine_dragger_create(node, c"x".as_ptr(), -1, 1) }.is_null(), "empty node");
	assert_eq!(unsafe { oakengine_dragger_start(dragger, 0, 1, 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_dragger_drag(dragger, &float_value(1.0)) }, E_INVALID);
	assert_eq!(unsafe { oakengine_dragger_end(dragger, c"x".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_dragger_is_started(dragger) }, E_INVALID, "empty handle, not a NULL pointer");

	// group family
	assert_eq!(unsafe { oakengine_group_input_passthrough_count(node) }, E_INVALID);
	assert_eq!(unsafe { oakengine_group_add_input_passthrough(node, node, c"x".as_ptr(), -1, c"".as_ptr(), buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_group_input_passthrough_at(node, 0, buf.as_mut_ptr(), 512, std::ptr::null_mut(), buf.as_mut_ptr(), 512, &mut i32a) }, E_INVALID);
	assert_eq!(unsafe { oakengine_group_get_id_of_passthrough(node, node, c"x".as_ptr(), -1, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_group_get_passthrough_from_id(node, c"id".as_ptr(), std::ptr::null_mut(), buf.as_mut_ptr(), 512, &mut i32a) }, E_INVALID);
	assert!(unsafe { oakengine_group_get_output_passthrough(node) }.is_null());
	assert_eq!(unsafe { oakengine_group_set_output_passthrough(node, node) }, E_INVALID);
	assert_eq!(unsafe { oakengine_group_resolve_input(node, c"x".as_ptr(), -1, std::ptr::null_mut(), buf.as_mut_ptr(), 512, &mut i32a) }, E_INVALID);
	assert_eq!(unsafe { oakengine_group_remove_input_passthrough(node, node, c"x".as_ptr(), -1) }, E_INVALID);
	assert!(unsafe { oakengine_group_add_input_passthrough_command(node, node, c"x".as_ptr(), -1, c"".as_ptr()) }.is_null());
	assert!(unsafe { oakengine_group_set_output_passthrough_command(node, node) }.is_null());
	assert_eq!(unsafe { oakengine_group_add_input_passthrough_undoable(node, node, c"x".as_ptr(), -1, c"".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_group_set_output_passthrough_undoable(node, node) }, E_INVALID);

	// multicam family
	assert_eq!(unsafe { oakengine_multicam_get_source_count(node) }, E_INVALID);
	assert_eq!(unsafe { oakengine_multicam_get_current_source(node) }, E_INVALID);

	// subtree / caches / data
	assert_eq!(unsafe { oakengine_node_has_gizmos(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_gizmo_count(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert!(unsafe { oakengine_node_gizmo_at(node, 0) }.is_null());
	assert_eq!(unsafe { oakengine_node_update_gizmo_positions(node, std::ptr::null_mut(), 0, 0, 0, 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_data(node, 0, &mut i32a, &mut num, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_get_exclusive_dependency_count(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert!(unsafe { oakengine_node_get_exclusive_dependency_at(node, 0) }.is_null());
	assert_eq!(unsafe { oakengine_node_has_plugin(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_plugin_message_count(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_plugin_message_at(node, 0, &mut i32a, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_node_plugin_clear_messages(node) }, E_INVALID);
	assert!(unsafe { oakengine_node_get_thumbnail_cache(node) }.is_null());
	assert!(unsafe { oakengine_node_get_waveform_cache(node) }.is_null());
	assert!(unsafe { oakengine_node_get_video_frame_cache(node) }.is_null());
	assert_eq!(unsafe { oakengine_node_is_clip(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_is_track(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_is_viewer_output(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_is_footage(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_is_sequence(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_is_folder(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_is_group(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_is_multicam(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_node_is_item(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert!(unsafe { oakengine_node_get_project(node) }.is_null());
	assert!(unsafe { oakengine_node_parent(node) }.is_null());
	assert!(unsafe { oakengine_node_folder(node) }.is_null());
	assert!(unsafe { oakengine_clip_get_track(node) }.is_null());
	assert_eq!(unsafe { oakengine_track_get_type(node) }, -1);
	assert_eq!(unsafe { oakengine_track_get_index(node) }, -1);
	assert!(unsafe { oakengine_track_get_sequence(node) }.is_null());
	assert_eq!(unsafe { oakengine_block_get_length_rational(node, &mut i32a, &mut i32b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_block_get_in_rational(node, &mut i32a, &mut i32b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_block_get_out_rational(node, &mut i32a, &mut i32b) }, E_INVALID);
	assert!(unsafe { oakengine_viewer_output_get_connected_texture(node) }.is_null());
	assert_eq!(unsafe { oakengine_shape_set_rect_undoable(node, 0.0, 0.0, 1.0, 1.0, &unsafe { std::mem::zeroed::<OakVideoParamsPod>() }, std::ptr::null_mut()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_subtitle_get_text(node, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_subtitle_set_text(node, c"x".as_ptr()) }, E_INVALID);

	// footage family
	assert!(unsafe { oakengine_footage_borrow(node) }.is_null(), "not a footage node");
	assert_eq!(unsafe { oakengine_footage_is_valid(node) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_footage_get_decoder_name(footage, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_video_stream_count(footage) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_footage_get_audio_stream_count(footage) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_footage_get_subtitle_stream_count(footage) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_footage_get_duration(footage, &mut f64a) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_is_online(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_source_start_time(footage, &mut i32a, &mut i32b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_relink(footage, c"/x.mp4".as_ptr()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_proxy_get_state(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_proxy_generate(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_proxy_delete(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_proxy_is_enabled(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_proxy_set_enabled(footage, 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_proxy_get_path(footage, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_filename(footage, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_colorspace_count(footage) }, E_INVALID, "empty handle, not a NULL pointer");
	assert_eq!(unsafe { oakengine_footage_colorspace_at(footage, 0, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_has_custom_proxy_params(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_effective_proxy_params(footage, std::ptr::null_mut()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_set_custom_proxy_params(footage, std::ptr::null()) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_clear_custom_proxy_params(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_set_proxy(footage, c"/x.mp4".as_ptr(), 0, 0, 0, 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_clear_proxy(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_invalidate(footage) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_set_source_start_time(footage, 1, 0, 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_source_start_time_source(footage, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_set_stream_enabled(footage, 0, 0, 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_stream_enabled(footage, 0, 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_stream_reference(footage, 0, &mut i32a, &mut i32b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_describe_video_stream(footage, 0, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_describe_audio_stream(footage, 0, buf.as_mut_ptr(), 512) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_video_stream_overrides(footage, 0, buf.as_mut_ptr(), 512, &mut i32a, &mut i32b, &mut i32c) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_set_video_stream_overrides(footage, 0, c"".as_ptr(), 0, 0, 0) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_pixel_aspect(footage, 0, &mut i32a, &mut i32b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_set_pixel_aspect(footage, 0, 1, 1) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_get_image_sequence_params(footage, 0, &mut i64a, &mut i64b, &mut i32a, &mut i32b) }, E_INVALID);
	assert_eq!(unsafe { oakengine_footage_set_image_sequence_params(footage, 0, 0, 1, 25, 1) }, E_INVALID);
	assert!(unsafe { oakengine_project_import_footage(project, c"/x.mp4".as_ptr()) }.is_null(), "empty project");
	assert_eq!(unsafe { oakengine_project_find_offline_footage(project, c"/tmp".as_ptr()) }, E_INVALID);

	// buffer-size edge cases: NULL buf / zero / negative size never crash.
	let mut small = [0 as c_char; 4];
	assert_eq!(unsafe { oakengine_node_get_type_id(node, small.as_mut_ptr(), 4) }, E_INVALID); // empty handle wins
	let _ = unsafe { oakengine_node_last_error(std::ptr::null_mut(), 0) };
	let _ = unsafe { oakengine_node_last_error(std::ptr::null_mut(), -5) };
	let _ = unsafe { oakengine_footage_last_error(std::ptr::null_mut(), 0) };
	let _ = unsafe { oakengine_footage_last_error(small.as_mut_ptr(), -1) };
	// Two-stage getters with a NULL buf only report the length (legal).
	let _ = unsafe { oakengine_node_category_name(0, std::ptr::null_mut(), 0) };

	// Free the empty boxes (no-op release paths).
	unsafe { oakengine_node_free(node) };
	unsafe { oakengine_project_free(project) };
	unsafe { oakengine_keyframe_dispose(keyframe) };
	unsafe { oakengine_dragger_free(dragger) };
	unsafe { oakengine_footage_free(footage) };
}

// ---------------------------------------------------------------------------
// Module destroy contracts + alive counter
// ---------------------------------------------------------------------------

/// Facade `free`/`dispose`/NULL/empty contracts plus the module destroy
/// paths they delegate to: `free(NULL)` and `free(empty)` are no-ops, the
/// module-level free of an already-emptied handle is double-free-safe, and
/// every owned-object round trip restores the debug alive counter.
#[test]
fn destroy_contracts_and_alive_count() {
	with_owned(|| {
		common::force_link();
		let _ = force_oakundo_command_link();
		let base = alive();

		// Facade-level free(NULL) / free(empty box) are no-ops.
		unsafe { oakengine_project_free(std::ptr::null_mut()) };
		unsafe { oakengine_node_free(std::ptr::null_mut()) };
		unsafe { oakengine_keyframe_dispose(std::ptr::null_mut()) };
		unsafe { oakengine_dragger_free(std::ptr::null_mut()) };
		unsafe { oakengine_footage_free(std::ptr::null_mut()) };

		// Owned round trips: create +1, free back to baseline.
		let project = oakengine_project_create();
		assert!(!project.is_null());
		assert_eq!(alive(), base + 1);
		unsafe { oakengine_project_free(project) };
		assert_eq!(alive(), base, "project free must return the alive counter");

		let node = unsafe { oakengine_node_factory_create_from_id(TYPE_VALUE.as_ptr()) };
		assert!(!node.is_null());
		assert_eq!(alive(), base + 1);
		unsafe { oakengine_node_free(node) };
		assert_eq!(alive(), base);

		let group = unsafe { oakengine_node_group_create() };
		assert!(!group.is_null());
		assert_eq!(alive(), base + 1);
		unsafe { oakengine_node_free(group) };
		assert_eq!(alive(), base);

		// Module-level double-free of an already-emptied handle is a no-op
		// (the destroy path the facade wraps clears ctx before returning).
		let mut h = unsafe { oaknode_factory_create_from_id(TYPE_VALUE.as_ptr()) };
		assert!(!h.is_null());
		assert_eq!(alive(), base + 1);
		unsafe { oaknode_node_free(&mut h) };
		assert!(h.is_null());
		unsafe { oaknode_node_free(&mut h) }; // double free: no-op
		assert_eq!(alive(), base);

		let mut ph = unsafe { oaknode::ffi::project::oaknode_project_init() };
		assert!(!ph.is_null());
		assert_eq!(alive(), base + 1);
		unsafe { oaknode_project_free(&mut ph) };
		assert!(ph.is_null());
		unsafe { oaknode_project_free(&mut ph) }; // double free: no-op
		assert_eq!(alive(), base);

		// Keyframe handles are not alive-counted but their free is
		// double-free-safe at the module level.
		let mut kh = unsafe { oaknode_kf_create(0, 1, &float_value(1.0), 0, 0, -1, c"value_in".as_ptr(), CHandle::null()) };
		assert!(!kh.is_null());
		unsafe { oaknode_keyframe_free(&mut kh) };
		assert!(kh.is_null());
		unsafe { oaknode_keyframe_free(&mut kh) }; // double free: no-op
		assert_eq!(alive(), base);

		// Dragger module free: create against a real node handle (freed
		// separately), then double-free the dragger handle.
		let mut nh = unsafe { oaknode_factory_create_from_id(TYPE_VALUE.as_ptr()) };
		assert!(!nh.is_null());
		let mut dh = unsafe { oaknode::ffi::dragger::oaknode_dragger_create(nh, c"value_in".as_ptr(), -1, 0) };
		assert!(!dh.is_null());
		unsafe { oaknode_node_free(&mut nh) };
		unsafe { oaknode_dragger_free(&mut dh) };
		assert!(dh.is_null());
		unsafe { oaknode_dragger_free(&mut dh) }; // double free: no-op
		assert_eq!(alive(), base);
	});
}

/// `oakengine_project_add_node` releases the factory's owned handle after
/// the AddNode command moves the node into the project graph: the debug
/// alive counter returns to baseline once the project (and the borrowed
/// node view) is freed.
#[test]
fn project_add_node_owned_handle_leak() {
	with_owned(|| {
		common::force_link();
		let _ = force_oakundo_command_link();
		let base = alive();

		let project = oakengine_project_create();
		assert_eq!(alive(), base + 1);

		let node = unsafe { oakengine_project_add_node(project, TYPE_VALUE.as_ptr()) };
		assert!(!node.is_null());
		// The added-node view is borrowed; the factory's owned handle was
		// released by the facade, so only the project is alive-counted.
		assert_eq!(alive(), base + 1, "project only; add_node returns a borrowed view");

		// Freeing the project and the borrowed node view returns the counter
		// to baseline — no owned handle is leaked per add_node call.
		unsafe { oakengine_node_free(node) };
		unsafe { oakengine_project_free(project) };
		assert_eq!(alive(), base, "no leak: the owned factory handle was released");
	});
}

// ---------------------------------------------------------------------------
// Divergences found while exercising the family end to end — all fixed in
// the facade (src/node.rs); each item below states the fixed behavior.
// ---------------------------------------------------------------------------
//
// 1. `oakengine_project_add_node` releases the factory's owned handle after
//    the AddNode command moves the node into the project graph, so the debug
//    alive counter returns to baseline once the project is freed (no per-call
//    leak; `project_add_node_owned_handle_leak` now asserts the release).
//
// 2. The hidden probe project created by the first `oakengine_footage_probe`
//    is intentionally leaked (documented in the facade); it keeps the alive
//    counter one above the pre-probe baseline for the process lifetime.
//
// 3. `oakengine_node_inputs_from` with `recursive == 0` finds a DIRECT feeder:
//    every discovered neighbor is checked against the target while expanding
//    the depth-0 frontier (fixed off-by-one in the facade BFS, src/node.rs
//    `inputs_from`).
//
// 4. `oakengine_group_get_id_of_passthrough`, `oakengine_group_get_passthrough_from_id`
//    and `oakengine_group_resolve_input` treat the module's two-stage string
//    length (>= 0, e.g. 9 for "value_in") as a SUCCESS, only negative codes as
//    failures. `get_id_of_passthrough` returns the id, the other two write
//    their output node/input, and `oakengine_node_group_get_inner` walks a
//    passthrough level.
//
// 5. `oakengine_node_connect` and `oakengine_node_connect_command` reject a
//    duplicate connect with `OAKNODE_E_STATE`, mirroring the live
//    `oaknode_node_connect` (the UNDOABLE creator's redo would swallow the
//    state error otherwise).
//
// 6. `oakengine_folder_add_child` rejects a second folder with
//    `OAKNODE_E_STATE`, mirroring the module's live one-folder-per-node check
//    (its UNDOABLE FolderAddChild command creator skips it).
//
// 7. `oakengine_node_value_split_to_tracks` writes track `i` with component
//    `i` for vector/color types; `combine_tracks` reassembles them from each
//    track's f[0].
//
// 8. Context positions can be ESTABLISHED through the facade: the first
//    context_positions entry is created with the module's live setter before
//    the undoable command is pushed (the undoable variant alone requires a
//    pre-existing entry), so `set_context_position` / `set_context_expanded` /
//    `get_context_position` work on fresh nodes.
//
// 9. `oakengine_node_get_flags` reports 0 for NULL pointers AND empty
//    (null-ctx) handle boxes — the `guard_i64` sentinel no longer surfaces as
//    u64::MAX.
//
// 10. `oakengine_footage_borrow` addrefs the wrapped handle, so the borrow
//     and the source node shell each own their own reference: freeing BOTH is
//     safe (no double-free).
// ---------------------------------------------------------------------------
