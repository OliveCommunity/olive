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

//! C ABI contract tests (ffi.rs). One normal + one error path per
//! export family; the exhaustive matrix is driven from the existing
//! C++ gtest suite (`src/node/tests`, unchanged) running against this
//! crate — these tests only pin Rust-side specifics.
//!
//! Handles follow the C by-value contract: every function call that
//! consumes a handle receives `dup(&h)` (addref + copy), and every dup
//! is eventually released. Tests serialize on [`ffi_lock`] because they
//! mutate the process-wide debug alive counter.

use std::ffi::{c_char, CString};
use std::sync::{Mutex, MutexGuard};

use oaknode::error::{OAKNODE_E_INVALID, OAKNODE_E_NOT_FOUND, OAKNODE_OK};
use oaknode::ffi::factory::{
	oaknode_factory_create_from_id, oaknode_factory_id_at, oaknode_factory_id_count,
	oaknode_factory_initialize, oaknode_factory_name_from_id, oaknode_factory_node_at,
};
use oaknode::ffi::keyframe::oaknode_keyframe_opposing_bezier_type;
use oaknode::ffi::node::{
	oaknode_debug_alive_count, oaknode_node_are_linked, oaknode_node_connect,
	oaknode_node_context_count, oaknode_node_context_node_at, oaknode_node_copy_inputs,
	oaknode_node_create_copy, oaknode_node_disconnect, oaknode_node_free,
	oaknode_node_from_identity, oaknode_node_get_context_position, oaknode_node_get_id,
	oaknode_node_get_input, oaknode_node_get_input_at_time, oaknode_node_get_input_name,
	oaknode_node_get_input_string, oaknode_node_get_label, oaknode_node_get_name,
	oaknode_node_get_override_color, oaknode_node_get_project, oaknode_node_identity,
	oaknode_node_input_array_insert, oaknode_node_input_array_remove,
	oaknode_node_input_count, oaknode_node_input_get_connected_node,
	oaknode_node_input_get_type, oaknode_node_input_id, oaknode_node_input_is_connectable,
	oaknode_node_input_is_connected, oaknode_node_is_enabled, oaknode_node_link,
	oaknode_node_link_at, oaknode_node_link_count, oaknode_node_output_connection_count,
	oaknode_node_output_connection_element_at, oaknode_node_output_connection_input_id_at,
	oaknode_node_output_connection_node_at, oaknode_node_remove_from_context,
	oaknode_node_set_context_position, oaknode_node_set_enabled, oaknode_node_set_input,
	oaknode_node_set_input_string, oaknode_node_set_label, oaknode_node_set_override_color,
	oaknode_node_set_value_hint_track, oaknode_node_unlink,
};
use oaknode::ffi::project::{
	oaknode_project_add_node, oaknode_project_cache_path, oaknode_project_clear,
	oaknode_project_copy_settings, oaknode_project_filename, oaknode_project_free,
	oaknode_project_get_cache_location_setting, oaknode_project_get_custom_cache_path,
	oaknode_project_get_uuid, oaknode_project_init, oaknode_project_initialize,
	oaknode_project_is_modified, oaknode_project_is_new, oaknode_project_name,
	oaknode_project_node_at, oaknode_project_node_count, oaknode_project_pretty_filename,
	oaknode_project_remove_node, oaknode_project_root, oaknode_project_set_cache_location_setting,
	oaknode_project_set_custom_cache_path, oaknode_project_set_filename,
	oaknode_project_set_modified,
};
use oaknode::handle::CHandle;
use oaknode::value::{oak, OakNodeValue};

/// Serialize all tests touching the global alive counter / factory.
static FFI_LOCK: Mutex<()> = Mutex::new(());

fn ffi_lock() -> MutexGuard<'static, ()> {
	FFI_LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

/// By-value handle copy. The ffi callees never release the handles they
/// receive, so a bitwise clone is safe here: every box is released
/// exactly once, via the original handle's free call.
fn dup(h: &CHandle) -> CHandle {
	h.clone()
}

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

/// Read a two-stage getter's result into a String; the caller passes
/// the getter closure (returns required size / error code).
fn two_stage<F: Fn(*mut c_char, i32) -> i32>(getter: F) -> Result<String, i32> {
	let needed = getter(std::ptr::null_mut(), 0);
	if needed < 0 {
		return Err(needed);
	}
	let mut buf = vec![0u8; needed as usize];
	let rc = getter(buf.as_mut_ptr() as *mut c_char, needed);
	if rc < 0 {
		return Err(rc);
	}
	buf.pop(); // trailing NUL
	Ok(String::from_utf8(buf).unwrap())
}

fn init_project() -> CHandle {
	let p = unsafe { oaknode_project_init() };
	assert!(!p.ctx.is_null());
	assert_eq!(p.abi_version, 1);
	assert!(p.addref.is_some() && p.release.is_some());
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK);
	p
}

/// Every implemented handle-returning function returns ctx==NULL on
/// failure and a valid refcounted handle on success (abi_version
/// stamped).
#[test]
fn handle_contract_all_exports() {
	let _g = ffi_lock();
	let mut p = init_project();

	// Borrowed root folder handle.
	let mut root = unsafe { oaknode_project_root(dup(&p)) };
	assert!(!root.ctx.is_null());
	assert_eq!(root.abi_version, 1);

	// node_at: valid index -> borrowed handle; out-of-range / negative ->
	// empty; empty project handle -> empty.
	let mut n0 = unsafe { oaknode_project_node_at(dup(&p), 0) };
	assert!(!n0.ctx.is_null());
	assert!(unsafe { oaknode_project_node_at(dup(&p), 1) }.ctx.is_null());
	assert!(unsafe { oaknode_project_node_at(dup(&p), -1) }.ctx.is_null());
	assert!(unsafe { oaknode_project_node_at(CHandle::null(), 0) }.ctx.is_null());

	// Factory node handle: owned, valid.
	let mut math = unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.math").as_ptr()) };
	assert!(!math.ctx.is_null());
	assert_eq!(math.abi_version, 1);

	// create_copy: owned copy of an orphan node.
	let mut copy = unsafe { oaknode_node_create_copy(dup(&math)) };
	assert!(!copy.ctx.is_null());

	// from_identity of a live node -> valid; garbage identity -> empty.
	let id = unsafe { oaknode_node_identity(dup(&math)) };
	let mut back = unsafe { oaknode_node_from_identity(id) };
	assert!(!back.ctx.is_null());
	assert!(unsafe { oaknode_node_from_identity(0xdead) }.ctx.is_null());

	unsafe { oaknode_node_free(&mut back) };
	unsafe { oaknode_node_free(&mut copy) };
	unsafe { oaknode_node_free(&mut math) };
	unsafe { oaknode_node_free(&mut n0) };
	unsafe { oaknode_node_free(&mut root) };
	unsafe { oaknode_project_free(&mut p) };
}

/// free(NULL)/free(empty) are no-ops across every free export.
#[test]
fn free_null_noop_all_exports() {
	let _g = ffi_lock();
	unsafe {
		oaknode_project_free(std::ptr::null_mut());
		let mut empty = CHandle::null();
		oaknode_project_free(&mut empty);
		assert!(empty.ctx.is_null());

		oaknode_node_free(std::ptr::null_mut());
		let mut empty = CHandle::null();
		oaknode_node_free(&mut empty);
		assert!(empty.ctx.is_null());
	}

	// A live project frees cleanly and clears the handle.
	let mut p = init_project();
	unsafe { oaknode_project_free(&mut p) };
	assert!(p.ctx.is_null());
}

/// Two-stage string functions: size query, short buffer truncation
/// rule, and exact-fit write — for every string getter.
#[test]
fn two_stage_string_contract() {
	let _g = ffi_lock();
	let mut p = init_project();

	// project_name: "(untitled)" -> 10 + 1 bytes.
	let name = two_stage(|buf, size| unsafe { oaknode_project_name(dup(&p), buf, size) }).unwrap();
	assert_eq!(name, "(untitled)");

	// Short buffer truncates and NUL-terminates.
	let mut buf = [0u8; 4];
	let rc = unsafe { oaknode_project_name(dup(&p), buf.as_mut_ptr() as *mut c_char, buf.len() as i32) };
	assert_eq!(rc, "(untitled)".len() as i32 + 1, "required size regardless of buffer");
	assert_eq!(&buf[..3], b"(un");
	assert_eq!(buf[3], 0);

	// Exact-fit write (required size) is NUL-terminated.
	let needed = unsafe { oaknode_project_name(dup(&p), std::ptr::null_mut(), 0) };
	let mut buf = vec![0u8; needed as usize];
	assert_eq!(
		unsafe { oaknode_project_name(dup(&p), buf.as_mut_ptr() as *mut c_char, needed) },
		needed
	);
	assert_eq!(buf[needed as usize - 1], 0, "terminating NUL");
	buf.pop();
	assert_eq!(String::from_utf8(buf).unwrap(), "(untitled)");

	// filename: "" initially (size 1); after set_filename -> full path.
	let filename = two_stage(|buf, size| unsafe { oaknode_project_filename(dup(&p), buf, size) }).unwrap();
	assert_eq!(filename, "");
	unsafe { oaknode_project_set_filename(dup(&p), cs("/tmp/demo.ove").as_ptr()) };
	let filename = two_stage(|buf, size| unsafe { oaknode_project_filename(dup(&p), buf, size) }).unwrap();
	assert_eq!(filename, "/tmp/demo.ove");
	let pretty =
		two_stage(|buf, size| unsafe { oaknode_project_pretty_filename(dup(&p), buf, size) }).unwrap();
	assert_eq!(pretty, "/tmp/demo.ove");

	// uuid is the 36-char braced format.
	let uuid = two_stage(|buf, size| unsafe { oaknode_project_get_uuid(dup(&p), buf, size) }).unwrap();
	assert_eq!(uuid.len(), 38);
	assert!(uuid.starts_with('{') && uuid.ends_with('}'));

	// cache_path is a two-stage getter too.
	let _ = two_stage(|buf, size| unsafe { oaknode_project_cache_path(dup(&p), buf, size) }).unwrap();

	// Node string getters.
	let mut math = unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.math").as_ptr()) };
	let id = two_stage(|buf, size| unsafe { oaknode_node_get_id(dup(&math), buf, size) }).unwrap();
	assert_eq!(id, "org.olivevideoeditor.Olive.math");
	let name = two_stage(|buf, size| unsafe { oaknode_node_get_name(dup(&math), buf, size) }).unwrap();
	assert_eq!(name, "Math");
	let label = two_stage(|buf, size| unsafe { oaknode_node_get_label(dup(&math), buf, size) }).unwrap();
	assert_eq!(label, "");
	let iname = two_stage(|buf, size| {
		unsafe { oaknode_node_get_input_name(dup(&math), cs("enabled_in").as_ptr(), buf, size) }
	})
	.unwrap();
	assert_eq!(iname, "Enabled");

	unsafe { oaknode_node_free(&mut math) };
	unsafe { oaknode_project_free(&mut p) };
}

/// Identity registry: node_identity / node_from_identity round-trip;
/// freed nodes are rejected by from_identity.
#[test]
fn identity_registry_roundtrip() {
	let _g = ffi_lock();
	let mut math = unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.math").as_ptr()) };
	let id = unsafe { oaknode_node_identity(dup(&math)) };

	// Round-trip: the looked-up node has the same type id.
	let mut back = unsafe { oaknode_node_from_identity(id) };
	assert!(!back.ctx.is_null());
	let bid = two_stage(|buf, size| unsafe { oaknode_node_get_id(dup(&back), buf, size) }).unwrap();
	assert_eq!(bid, "org.olivevideoeditor.Olive.math");
	unsafe { oaknode_node_free(&mut back) };

	// Move the node into a project: its identity is re-registered and
	// still resolvable while the project lives.
	let mut p = unsafe { oaknode_project_init() };
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&math)) }, OAKNODE_OK);
	let id2 = unsafe { oaknode_node_identity(dup(&math)) };
	let mut back2 = unsafe { oaknode_node_from_identity(id2) };
	assert!(!back2.ctx.is_null());
	unsafe { oaknode_node_free(&mut back2) };

	// Free the node handle (drops its project reference) and the project:
	// both identities must then reject.
	unsafe { oaknode_node_free(&mut math) };
	unsafe { oaknode_project_free(&mut p) };
	let ghost = unsafe { oaknode_node_from_identity(id) };
	assert!(ghost.ctx.is_null(), "freed node must be rejected by from_identity");
	let ghost2 = unsafe { oaknode_node_from_identity(id2) };
	assert!(ghost2.ctx.is_null());
}

/// alive count: project create/destroy moves oaknode_debug_alive_count
/// predictably and returns to baseline.
#[test]
fn alive_count_accounting() {
	let _g = ffi_lock();
	let base = unsafe { oaknode_debug_alive_count() };

	let mut p = init_project();
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base + 1);

	let mut math =
		unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.math").as_ptr()) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base + 2);

	// A borrowed view does not count.
	let mut n0 = unsafe { oaknode_project_node_at(dup(&p), 0) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base + 2);
	unsafe { oaknode_node_free(&mut n0) };

	// Adding the owned node to the project transfers its accounting to
	// the project.
	unsafe { oaknode_project_add_node(dup(&p), dup(&math)) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base + 1);
	// The node handle is now a borrowed view: freeing it changes nothing.
	unsafe { oaknode_node_free(&mut math) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base + 1);

	unsafe { oaknode_project_free(&mut p) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base);

	// Detach: remove_node returns ownership (counted again), free
	// returns to baseline.
	let mut p2 = init_project();
	let mut node =
		unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.math").as_ptr()) };
	unsafe { oaknode_project_add_node(dup(&p2), dup(&node)) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base + 1);
	unsafe { oaknode_project_remove_node(dup(&p2), dup(&node)) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base + 2);
	unsafe { oaknode_node_free(&mut node) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base + 1);
	unsafe { oaknode_project_free(&mut p2) };
	assert_eq!(unsafe { oaknode_debug_alive_count() }, base);
}

/// Project family: success + failure path for every implemented export.
#[test]
fn project_family_contract() {
	let _g = ffi_lock();
	let mut p = init_project();

	// initialize: E_STATE on second call; E_INVALID on empty handle.
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, oaknode::error::OAKNODE_E_STATE);
	assert_eq!(unsafe { oaknode_project_initialize(CHandle::null()) }, OAKNODE_E_INVALID);

	// root: valid after initialize.
	let mut root = unsafe { oaknode_project_root(dup(&p)) };
	assert!(!root.ctx.is_null());
	assert!(unsafe { oaknode_project_root(CHandle::null()) }.ctx.is_null());
	unsafe { oaknode_node_free(&mut root) };

	// name/filename/pretty/is_modified/is_new + empty-handle failures.
	assert_eq!(
		unsafe { oaknode_project_name(CHandle::null(), std::ptr::null_mut(), 0) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_project_filename(CHandle::null(), std::ptr::null_mut(), 0) },
		OAKNODE_E_INVALID
	);
	assert_eq!(unsafe { oaknode_project_is_modified(CHandle::null()) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_project_is_new(CHandle::null()) }, OAKNODE_E_INVALID);

	// set_filename: success; NULL failure.
	assert_eq!(
		unsafe { oaknode_project_set_filename(dup(&p), cs("/tmp/x.ove").as_ptr()) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oaknode_project_set_filename(dup(&p), std::ptr::null()) }, OAKNODE_E_INVALID);
	assert_eq!(
		unsafe { oaknode_project_set_filename(CHandle::null(), cs("x").as_ptr()) },
		OAKNODE_E_INVALID
	);
	assert_eq!(unsafe { oaknode_project_is_new(dup(&p)) }, 0, "has a filename now");

	// modified flag.
	assert_eq!(unsafe { oaknode_project_set_modified(dup(&p), 1) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_project_is_modified(dup(&p)) }, 1);
	assert_eq!(unsafe { oaknode_project_set_modified(CHandle::null(), 0) }, OAKNODE_E_INVALID);

	// cache settings.
	assert_eq!(unsafe { oaknode_project_get_cache_location_setting(dup(&p)) }, 0);
	assert_eq!(
		unsafe { oaknode_project_set_cache_location_setting(dup(&p), 2) },
		OAKNODE_OK
	);
	assert_eq!(
		unsafe { oaknode_project_set_cache_location_setting(dup(&p), 99) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_project_set_cache_location_setting(CHandle::null(), 0) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_project_set_custom_cache_path(dup(&p), cs("/tmp/cache").as_ptr()) },
		OAKNODE_OK
	);
	let custom = two_stage(|buf, size| unsafe { oaknode_project_get_custom_cache_path(dup(&p), buf, size) })
		.unwrap();
	assert_eq!(custom, "/tmp/cache");
	unsafe { oaknode_project_set_custom_cache_path(dup(&p), std::ptr::null()) }; // NULL clears
	let custom = two_stage(|buf, size| unsafe { oaknode_project_get_custom_cache_path(dup(&p), buf, size) })
		.unwrap();
	assert_eq!(custom, "");

	// copy_settings: dst inherits src settings; empty handle fails.
	let mut p2 = unsafe { oaknode_project_init() };
	unsafe { oaknode_project_set_cache_location_setting(dup(&p2), 1) };
	assert_eq!(unsafe { oaknode_project_copy_settings(dup(&p), dup(&p2)) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_project_get_cache_location_setting(dup(&p)) }, 1);
	assert_eq!(
		unsafe { oaknode_project_copy_settings(CHandle::null(), dup(&p2)) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_project_copy_settings(dup(&p), CHandle::null()) },
		OAKNODE_E_INVALID
	);

	// node add/remove/count/at.
	let base_count = unsafe { oaknode_project_node_count(dup(&p)) };
	let mut node =
		unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.solidgenerator").as_ptr()) };
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), dup(&node)) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_project_node_count(dup(&p)) }, base_count + 1);
	assert_eq!(unsafe { oaknode_project_add_node(CHandle::null(), dup(&node)) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_project_add_node(dup(&p), CHandle::null()) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_project_remove_node(dup(&p), dup(&node)) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_project_node_count(dup(&p)) }, base_count);
	assert_eq!(
		unsafe { oaknode_project_remove_node(dup(&p), dup(&node)) },
		OAKNODE_E_NOT_FOUND,
		"double remove fails"
	);
	assert_eq!(unsafe { oaknode_project_remove_node(CHandle::null(), dup(&node)) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_project_node_count(CHandle::null()) }, OAKNODE_E_INVALID);
	unsafe { oaknode_node_free(&mut node) };

	// clear: resets; empty handle fails.
	assert_eq!(unsafe { oaknode_project_clear(CHandle::null()) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_project_clear(dup(&p)) }, OAKNODE_OK);
	assert!(unsafe { oaknode_project_root(dup(&p)) }.ctx.is_null(), "root gone after clear");
	assert_eq!(unsafe { oaknode_project_initialize(dup(&p)) }, OAKNODE_OK, "re-initializable");

	unsafe { oaknode_project_free(&mut p) };
	unsafe { oaknode_project_free(&mut p2) };
}

/// Node family: metadata, inputs, params, graph editing, links,
/// contexts, arrays, copies — success + failure per export.
#[test]
fn node_family_contract() {
	let _g = ffi_lock();
	let mut p = init_project();

	// Two factory nodes in the same project.
	let mut a =
		unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.math").as_ptr()) };
	let mut b =
		unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.ortho").as_ptr()) };
	unsafe { oaknode_project_add_node(dup(&p), dup(&a)) };
	unsafe { oaknode_project_add_node(dup(&p), dup(&b)) };

	// Metadata.
	assert_eq!(unsafe { oaknode_node_get_id(CHandle::null(), std::ptr::null_mut(), 0) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_node_get_name(CHandle::null(), std::ptr::null_mut(), 0) }, OAKNODE_E_INVALID);
	let id = two_stage(|buf, size| unsafe { oaknode_node_get_id(dup(&a), buf, size) }).unwrap();
	assert_eq!(id, "org.olivevideoeditor.Olive.math");

	// Label.
	assert_eq!(unsafe { oaknode_node_set_label(dup(&a), cs("my node").as_ptr()) }, OAKNODE_OK);
	let label = two_stage(|buf, size| unsafe { oaknode_node_get_label(dup(&a), buf, size) }).unwrap();
	assert_eq!(label, "my node");
	assert_eq!(unsafe { oaknode_node_set_label(CHandle::null(), cs("x").as_ptr()) }, OAKNODE_E_INVALID);
	assert_eq!(unsafe { oaknode_node_set_label(dup(&a), std::ptr::null()) }, OAKNODE_E_INVALID);

	// Override color.
	let mut oc = 0;
	assert_eq!(unsafe { oaknode_node_get_override_color(dup(&a), &mut oc) }, OAKNODE_OK);
	assert_eq!(oc, -1);
	assert_eq!(unsafe { oaknode_node_set_override_color(dup(&a), 3) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_node_get_override_color(dup(&a), &mut oc) }, OAKNODE_OK);
	assert_eq!(oc, 3);
	assert_eq!(unsafe { oaknode_node_get_override_color(CHandle::null(), &mut oc) }, OAKNODE_E_INVALID);
	assert_eq!(
		unsafe { oaknode_node_get_override_color(dup(&a), std::ptr::null_mut()) },
		OAKNODE_E_INVALID
	);

	// Enabled.
	let mut en = 0;
	assert_eq!(unsafe { oaknode_node_is_enabled(dup(&a), &mut en) }, OAKNODE_OK);
	assert_eq!(en, 1);
	assert_eq!(unsafe { oaknode_node_set_enabled(dup(&a), 0) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_node_is_enabled(dup(&a), &mut en) }, OAKNODE_OK);
	assert_eq!(en, 0);
	assert_eq!(unsafe { oaknode_node_is_enabled(CHandle::null(), &mut en) }, OAKNODE_E_INVALID);

	// Input introspection.
	let mut count = 0;
	assert_eq!(unsafe { oaknode_node_input_count(dup(&a), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 4, "enabled_in + method_in + param_a_in + param_b_in");
	let first = two_stage(|buf, size| unsafe { oaknode_node_input_id(dup(&a), 0, buf, size) }).unwrap();
	assert_eq!(first, "enabled_in");
	assert_eq!(
		unsafe { oaknode_node_input_id(dup(&a), 99, std::ptr::null_mut(), 0) },
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(
		unsafe { oaknode_node_input_id(dup(&a), -1, std::ptr::null_mut(), 0) },
		OAKNODE_E_NOT_FOUND
	);

	let mut ty = 0;
	assert_eq!(
		unsafe { oaknode_node_input_get_type(dup(&a), cs("enabled_in").as_ptr(), &mut ty) },
		OAKNODE_OK
	);
	assert_eq!(ty, oak::BOOL);
	assert_eq!(
		unsafe { oaknode_node_input_get_type(dup(&a), cs("nope").as_ptr(), &mut ty) },
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(
		unsafe { oaknode_node_input_get_type(dup(&a), cs("enabled_in").as_ptr(), std::ptr::null_mut()) },
		OAKNODE_E_INVALID
	);

	let mut conn = 0;
	assert_eq!(
		unsafe { oaknode_node_input_is_connected(dup(&a), cs("param_a_in").as_ptr(), &mut conn) },
		OAKNODE_OK
	);
	assert_eq!(conn, 0);
	assert_eq!(
		unsafe { oaknode_node_input_is_connected(dup(&a), cs("nope").as_ptr(), &mut conn) },
		OAKNODE_E_NOT_FOUND
	);

	let mut connectable = 0;
	assert_eq!(
		unsafe { oaknode_node_input_is_connectable(dup(&a), cs("param_a_in").as_ptr(), &mut connectable) },
		OAKNODE_OK
	);
	assert_eq!(connectable, 1);
	assert_eq!(
		unsafe { oaknode_node_input_is_connectable(dup(&a), cs("method_in").as_ptr(), &mut connectable) },
		OAKNODE_OK
	);
	assert_eq!(connectable, 0, "method_in is not connectable");
	assert_eq!(
		unsafe { oaknode_node_input_is_connectable(dup(&a), cs("nope").as_ptr(), &mut connectable) },
		OAKNODE_E_NOT_FOUND
	);

	let iname = two_stage(|buf, size| {
		unsafe { oaknode_node_get_input_name(dup(&a), cs("enabled_in").as_ptr(), buf, size) }
	})
	.unwrap();
	assert_eq!(iname, "Enabled");
	assert_eq!(
		unsafe { oaknode_node_get_input_name(dup(&a), cs("nope").as_ptr(), std::ptr::null_mut(), 0) },
		OAKNODE_E_NOT_FOUND
	);

	// Params: get/set float, unknown id, string input, bad POD type.
	let mut pod = OakNodeValue::none();
	assert_eq!(
		unsafe { oaknode_node_get_input(dup(&a), cs("param_a_in").as_ptr(), &mut pod) },
		OAKNODE_OK
	);
	assert_eq!(pod.kind, oak::FLOAT);
	assert_eq!(pod.f[0], 0.0);
	let v = OakNodeValue {
		kind: oak::FLOAT,
		num: 0,
		den: 0,
		f: [3.5, 0.0, 0.0, 0.0],
	};
	assert_eq!(
		unsafe { oaknode_node_set_input(dup(&a), cs("param_a_in").as_ptr(), &v) },
		OAKNODE_OK
	);
	assert_eq!(
		unsafe { oaknode_node_get_input(dup(&a), cs("param_a_in").as_ptr(), &mut pod) },
		OAKNODE_OK
	);
	assert_eq!(pod.f[0], 3.5);
	assert_eq!(
		unsafe { oaknode_node_get_input(dup(&a), cs("nope").as_ptr(), &mut pod) },
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(
		unsafe { oaknode_node_set_input(dup(&a), cs("nope").as_ptr(), &v) },
		OAKNODE_E_NOT_FOUND
	);
	let wrong_type = OakNodeValue {
		kind: oak::INT,
		num: 1,
		den: 0,
		f: [0.0; 4],
	};
	assert_eq!(
		unsafe { oaknode_node_set_input(dup(&a), cs("param_a_in").as_ptr(), &wrong_type) },
		OAKNODE_E_INVALID,
		"POD type must match the declared input type"
	);

	// String input: the timeformat node's format_in.
	let mut tf = unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.timeformat").as_ptr()) };
	unsafe { oaknode_project_add_node(dup(&p), dup(&tf)) };
	let s = two_stage(|buf, size| unsafe {
		oaknode_node_get_input_string(dup(&tf), cs("format_in").as_ptr(), buf, size)
	})
	.unwrap();
	assert_eq!(s, "hh:mm:ss");
	assert_eq!(
		unsafe { oaknode_node_set_input_string(dup(&tf), cs("format_in").as_ptr(), cs("yyyy").as_ptr()) },
		OAKNODE_OK
	);
	let s = two_stage(|buf, size| unsafe {
		oaknode_node_get_input_string(dup(&tf), cs("format_in").as_ptr(), buf, size)
	})
	.unwrap();
	assert_eq!(s, "yyyy");
	// Non-string input -> E_INVALID; unknown -> E_NOT_FOUND.
	assert_eq!(
		unsafe { oaknode_node_get_input_string(dup(&tf), cs("time_in").as_ptr(), std::ptr::null_mut(), 0) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_node_set_input_string(dup(&tf), cs("time_in").as_ptr(), cs("x").as_ptr()) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_node_get_input_string(dup(&tf), cs("nope").as_ptr(), std::ptr::null_mut(), 0) },
		OAKNODE_E_NOT_FOUND
	);

	// Graph editing: connect a -> b.param_a_in.
	assert_eq!(
		unsafe { oaknode_node_connect(dup(&a), dup(&b), cs("rot_in").as_ptr()) },
		OAKNODE_OK
	);
	let mut connected = 0;
	assert_eq!(
		unsafe { oaknode_node_input_is_connected(dup(&b), cs("rot_in").as_ptr(), &mut connected) },
		OAKNODE_OK
	);
	assert_eq!(connected, 1);
	// Duplicate connect -> E_STATE.
	assert_eq!(
		unsafe { oaknode_node_connect(dup(&a), dup(&b), cs("rot_in").as_ptr()) },
		oaknode::error::OAKNODE_E_STATE
	);
	// Not connectable -> E_INVALID.
	assert_eq!(
		unsafe { oaknode_node_connect(dup(&a), dup(&a), cs("method_in").as_ptr()) },
		OAKNODE_E_INVALID
	);
	// Unknown input -> E_NOT_FOUND.
	assert_eq!(
		unsafe { oaknode_node_connect(dup(&a), dup(&b), cs("nope").as_ptr()) },
		OAKNODE_E_NOT_FOUND
	);
	// Empty handle -> E_INVALID.
	assert_eq!(
		unsafe { oaknode_node_connect(CHandle::null(), dup(&b), cs("param_a_in").as_ptr()) },
		OAKNODE_E_INVALID
	);

	// input_get_connected_node.
	let mut src = CHandle::null();
	assert_eq!(
		unsafe { oaknode_node_input_get_connected_node(dup(&b), cs("rot_in").as_ptr(), &mut src) },
		OAKNODE_OK
	);
	assert!(!src.ctx.is_null());
	let src_id = two_stage(|buf, size| unsafe { oaknode_node_get_id(dup(&src), buf, size) }).unwrap();
	assert_eq!(src_id, "org.olivevideoeditor.Olive.math");
	assert_eq!(
		unsafe { oaknode_node_input_get_connected_node(dup(&b), cs("nope").as_ptr(), &mut src) },
		OAKNODE_E_NOT_FOUND
	);

	// Output connections.
	let mut count = 0;
	assert_eq!(unsafe { oaknode_node_output_connection_count(dup(&a), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 1);
	let mut node_out = CHandle::null();
	assert_eq!(
		unsafe { oaknode_node_output_connection_node_at(dup(&a), 0, &mut node_out) },
		OAKNODE_OK
	);
	assert!(!node_out.ctx.is_null());
	assert_eq!(
		unsafe { oaknode_node_output_connection_node_at(dup(&a), 1, &mut node_out) },
		OAKNODE_E_NOT_FOUND
	);
	let input_id =
		two_stage(|buf, size| unsafe { oaknode_node_output_connection_input_id_at(dup(&a), 0, buf, size) })
			.unwrap();
	assert_eq!(input_id, "rot_in");
	let mut element = 0;
	assert_eq!(
		unsafe { oaknode_node_output_connection_element_at(dup(&a), 0, &mut element) },
		OAKNODE_OK
	);
	assert_eq!(element, -1);

	// Disconnect.
	assert_eq!(
		unsafe { oaknode_node_disconnect(dup(&b), cs("rot_in").as_ptr()) },
		OAKNODE_OK
	);
	assert_eq!(
		unsafe { oaknode_node_disconnect(dup(&b), cs("rot_in").as_ptr()) },
		OAKNODE_E_NOT_FOUND,
		"disconnect of an unconnected input fails"
	);
	assert_eq!(
		unsafe { oaknode_node_disconnect(dup(&b), cs("nope").as_ptr()) },
		OAKNODE_E_NOT_FOUND
	);

	// Links.
	let mut linked = 0;
	assert_eq!(unsafe { oaknode_node_link(dup(&a), dup(&b), &mut linked) }, OAKNODE_OK);
	assert_eq!(linked, 1);
	assert_eq!(unsafe { oaknode_node_link(dup(&a), dup(&b), &mut linked) }, OAKNODE_OK);
	assert_eq!(linked, 0, "already linked");
	let mut linked_ok = 0;
	assert_eq!(unsafe { oaknode_node_are_linked(dup(&a), dup(&b), &mut linked_ok) }, OAKNODE_OK);
	assert_eq!(linked_ok, 1);
	assert_eq!(unsafe { oaknode_node_link_count(dup(&a), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 1);
	let mut link_node = CHandle::null();
	assert_eq!(unsafe { oaknode_node_link_at(dup(&a), 0, &mut link_node) }, OAKNODE_OK);
	assert!(!link_node.ctx.is_null());
	assert_eq!(unsafe { oaknode_node_link_at(dup(&a), 1, &mut link_node) }, OAKNODE_E_NOT_FOUND);
	assert_eq!(unsafe { oaknode_node_unlink(dup(&a), dup(&b), &mut linked) }, OAKNODE_OK);
	assert_eq!(linked, 1);
	assert_eq!(unsafe { oaknode_node_unlink(dup(&a), dup(&b), &mut linked) }, OAKNODE_OK);
	assert_eq!(linked, 0, "already unlinked");

	// Context positions.
	assert_eq!(
		unsafe { oaknode_node_set_context_position(dup(&a), dup(&b), 10.0, 20.0, 1) },
		OAKNODE_OK
	);
	assert_eq!(unsafe { oaknode_node_context_count(dup(&a), &mut count) }, OAKNODE_OK);
	assert_eq!(count, 1);
	let mut x = 0.0;
	let mut y = 0.0;
	let mut expanded = 0;
	assert_eq!(
		unsafe { oaknode_node_get_context_position(dup(&a), dup(&b), &mut x, &mut y, &mut expanded) },
		OAKNODE_OK
	);
	assert_eq!((x, y, expanded), (10.0, 20.0, 1));
	assert_eq!(
		unsafe { oaknode_node_get_context_position(dup(&a), dup(&a), &mut x, &mut y, &mut expanded) },
		OAKNODE_E_NOT_FOUND
	);
	let mut ctx_node = CHandle::null();
	assert_eq!(unsafe { oaknode_node_context_node_at(dup(&a), 0, &mut ctx_node) }, OAKNODE_OK);
	assert!(!ctx_node.ctx.is_null());
	assert_eq!(unsafe { oaknode_node_remove_from_context(dup(&a), dup(&b)) }, OAKNODE_OK);
	assert_eq!(
		unsafe { oaknode_node_remove_from_context(dup(&a), dup(&b)) },
		OAKNODE_E_NOT_FOUND
	);

	// get_project.
	let mut proj = CHandle::null();
	assert_eq!(unsafe { oaknode_node_get_project(dup(&a), &mut proj) }, OAKNODE_OK);
	assert!(!proj.ctx.is_null());
	assert_eq!(unsafe { oaknode_node_get_project(CHandle::null(), &mut proj) }, OAKNODE_E_INVALID);

	// Array input insert/remove on a non-array input fails E_INVALID;
	// on an unknown input E_NOT_FOUND.
	assert_eq!(
		unsafe { oaknode_node_input_array_insert(dup(&a), cs("param_a_in").as_ptr(), 0) },
		OAKNODE_E_INVALID
	);
	assert_eq!(
		unsafe { oaknode_node_input_array_insert(dup(&a), cs("nope").as_ptr(), 0) },
		OAKNODE_E_NOT_FOUND
	);

	// copy_inputs: dst (matrix) takes src (math)'s values where the ids
	// match (enabled_in only) and preserves its own otherwise.
	assert_eq!(unsafe { oaknode_node_copy_inputs(dup(&b), dup(&a), 0) }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_node_copy_inputs(CHandle::null(), dup(&a), 0) }, OAKNODE_E_INVALID);

	// set_value_hint_track.
	assert_eq!(
		unsafe { oaknode_node_set_value_hint_track(dup(&a), cs("param_a_in").as_ptr(), 0, 1) },
		OAKNODE_OK
	);
	assert_eq!(
		unsafe { oaknode_node_set_value_hint_track(dup(&a), cs("nope").as_ptr(), 0, 1) },
		OAKNODE_E_NOT_FOUND
	);

	// get_input_at_time: no keyframes -> standard value.
	let mut at = OakNodeValue::none();
	assert_eq!(
		unsafe { oaknode_node_get_input_at_time(dup(&a), cs("param_a_in").as_ptr(), 5, 1, &mut at) },
		OAKNODE_OK
	);
	assert_eq!(at.kind, oak::FLOAT);
	assert!((at.f[0] - 3.5).abs() < 1e-9);
	assert_eq!(
		unsafe { oaknode_node_get_input_at_time(dup(&a), cs("nope").as_ptr(), 5, 1, &mut at) },
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(
		unsafe {
			oaknode_node_get_input_at_time(dup(&a), cs("param_a_in").as_ptr(), 5, 1, std::ptr::null_mut())
		},
		OAKNODE_E_INVALID
	);

	// create_copy of a graph-owned node.
	let mut copy = unsafe { oaknode_node_create_copy(dup(&a)) };
	assert!(!copy.ctx.is_null());
	let copy_id = two_stage(|buf, size| unsafe { oaknode_node_get_id(dup(&copy), buf, size) }).unwrap();
	assert_eq!(copy_id, "org.olivevideoeditor.Olive.math");

	// Cleanup (free every borrowed view and the owned nodes).
	unsafe { oaknode_node_free(&mut copy) };
	unsafe { oaknode_node_free(&mut src) };
	unsafe { oaknode_node_free(&mut node_out) };
	unsafe { oaknode_node_free(&mut link_node) };
	unsafe { oaknode_node_free(&mut ctx_node) };
	unsafe { oaknode_node_free(&mut proj) };
	unsafe { oaknode_node_free(&mut tf) };
	unsafe { oaknode_node_free(&mut b) };
	unsafe { oaknode_node_free(&mut a) };
	unsafe { oaknode_project_free(&mut p) };
}

/// Factory family + the pure keyframe helper.
#[test]
fn factory_and_keyframe_helpers() {
	let _g = ffi_lock();
	assert_eq!(unsafe { oaknode_factory_initialize() }, OAKNODE_OK);
	assert_eq!(unsafe { oaknode_factory_initialize() }, OAKNODE_OK, "idempotent");

	let mut count = 0;
	assert_eq!(unsafe { oaknode_factory_id_count(&mut count) }, OAKNODE_OK);
	assert!(count >= 4, "at least the implemented node types registered");

	let first = two_stage(|buf, size| unsafe { oaknode_factory_id_at(0, buf, size) }).unwrap();
	assert_eq!(first, "org.olivevideoeditor.Olive.polygon");
	assert_eq!(
		unsafe { oaknode_factory_id_at(-1, std::ptr::null_mut(), 0) },
		OAKNODE_E_NOT_FOUND
	);
	assert_eq!(
		unsafe { oaknode_factory_id_at(count, std::ptr::null_mut(), 0) },
		OAKNODE_E_NOT_FOUND
	);

	let name = two_stage(|buf, size| {
		unsafe { oaknode_factory_name_from_id(cs("org.olivevideoeditor.Olive.pan").as_ptr(), buf, size) }
	})
	.unwrap();
	assert_eq!(name, "Pan");
	let missing = two_stage(|buf, size| {
		unsafe { oaknode_factory_name_from_id(cs("org.example.missing").as_ptr(), buf, size) }
	})
	.unwrap();
	assert_eq!(missing, "", "unknown id -> empty string");

	// create_from_id: known -> valid, unknown -> empty.
	let mut node =
		unsafe { oaknode_factory_create_from_id(cs("org.olivevideoeditor.Olive.solidgenerator").as_ptr()) };
	assert!(!node.ctx.is_null());
	assert!(
		unsafe { oaknode_factory_create_from_id(cs("org.example.missing").as_ptr()) }
			.ctx
			.is_null()
	);
	assert!(unsafe { oaknode_factory_create_from_id(std::ptr::null()) }.ctx.is_null());

	// node_at: borrowed prototype (index 5 = math, whose constructor is
	// implemented; prototypes at todo!()-constructor indices are only
	// reachable once Phase 3 lands); out-of-range -> E_NOT_FOUND.
	let mut proto = CHandle::null();
	assert_eq!(unsafe { oaknode_factory_node_at(5, &mut proto) }, OAKNODE_OK);
	assert!(!proto.ctx.is_null());
	assert_eq!(unsafe { oaknode_factory_node_at(count, &mut proto) }, OAKNODE_E_NOT_FOUND);

	// Pure keyframe helper: IN(0) <-> OUT(1), invalid -> E_INVALID.
	assert_eq!(unsafe { oaknode_keyframe_opposing_bezier_type(0) }, 1);
	assert_eq!(unsafe { oaknode_keyframe_opposing_bezier_type(1) }, 0);
	assert_eq!(
		unsafe { oaknode_keyframe_opposing_bezier_type(2) },
		OAKNODE_E_INVALID
	);

	unsafe { oaknode_node_free(&mut node) };
	unsafe { oaknode_node_free(&mut proto) };
}
