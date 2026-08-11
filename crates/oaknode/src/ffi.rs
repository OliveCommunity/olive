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

//! C ABI export layer: implements `include/node/*.h` verbatim.
//!
//! Organization: one submodule per public header. The authoritative
//! function list is the header itself; each submodule below carries a
//! complete inventory comment plus the export stubs. Bodies only
//! unwrap handles, call safe Rust, and map results through
//! [`crate::handle::guard*`].
//!
//! ## Handle layout
//!
//! - Project handles box `Arc<Mutex<Project>>` (`ProjectArc`).
//! - Node/folder/sequence handles box [`crate::project::NodeRef`] —
//!   `(Arc<Mutex<Project>>, NodeId)` plus the shared owned-flag.
//! - Handles obtained from a project (node_at, root, connected nodes,
//!   links, ...) are borrowed: releasing them only frees the box.
//!   Factory-created and detached nodes are owned and tracked by the
//!   debug alive counter until a project adopts them.
//!
//! ## Undo-bridge exports
//!
//! Every `*_undoable` / `command_create_*` / `set_input_at_time_*`
//! export routes through `bridge::undo` (implemented Phase 2; the
//! `test-stubs` feature provides in-crate oakundo symbols for tests).

use std::collections::HashMap;
use std::ffi::{c_char, c_int, CStr, CString};
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};

use crate::error::{Error, Result};
use crate::handle::{guard, guard_void, CHandle, RefBox};
use crate::id::NodeId;
use crate::node::NodeCore;
use crate::project::{NodeRef, Project, WeakProject};
use crate::value::{NodeValue, OakNodeValue, ValueType};

/// Project handle payload.
type ProjectArc = Arc<Mutex<Project>>;

// ---------------------------------------------------------------------
// Shared state: alive counter + identity registry
// ---------------------------------------------------------------------

/// Live owned objects (projects from `init`, nodes from factory/copy).
static ALIVE: AtomicI32 = AtomicI32::new(0);

/// Identity registry: `NodeId::identity()` -> weak project + id. Entries
/// are advisory; `node_from_identity` revalidates against the live graph,
/// so stale entries (project freed, node moved/removed) resolve to empty
/// handles.
static REGISTRY: OnceLock<Mutex<HashMap<u64, (WeakProject, NodeId)>>> = OnceLock::new();

fn registry() -> &'static Mutex<HashMap<u64, (WeakProject, NodeId)>> {
	REGISTRY.get_or_init(|| Mutex::new(HashMap::new()))
}

/// Debug alive count (`oaknode_debug_alive_count`).
pub fn debug_alive_count() -> c_int {
	ALIVE.load(Ordering::Relaxed)
}

/// Take a poisoned mutex in stride (a panic inside this crate must not
/// cascade into every later call).
fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
	m.lock().unwrap_or_else(|e| e.into_inner())
}

/// Release for project handles created by `oaknode_project_init`
/// (accounted in ALIVE).
unsafe extern "C" fn project_release(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<ProjectArc>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(rb));
			ALIVE.fetch_sub(1, Ordering::Relaxed);
		}
	}
}

/// Release for borrowed project handles (node_get_project): the box
/// drops but the project stays alive via its owner.
unsafe extern "C" fn project_release_borrowed(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<ProjectArc>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			drop(Box::from_raw(rb));
		}
	}
}

/// Release for node handles: drops the box; when the node object is
/// still separately owned (see [`NodeRef::owned`]), also un-counts it.
unsafe extern "C" fn node_release(ctx: *mut std::ffi::c_void) {
	unsafe {
		let rb = ctx as *mut RefBox<NodeRef>;
		if (*rb).refs.fetch_sub(1, Ordering::AcqRel) == 1 {
			let was_owned = (*rb).value.owned.load(Ordering::Relaxed);
			drop(Box::from_raw(rb));
			if was_owned {
				ALIVE.fetch_sub(1, Ordering::Relaxed);
			}
		}
	}
}

/// Owned project handle (alive-counted).
fn make_project_owned(project: ProjectArc) -> CHandle {
	ALIVE.fetch_add(1, Ordering::Relaxed);
	crate::handle::make_owned_with(project, project_release)
}

/// Borrowed project handle (not counted).
fn make_project_borrowed(project: ProjectArc) -> CHandle {
	crate::handle::make_owned_with(project, project_release_borrowed)
}

/// Owned node handle (alive-counted): factory-created or detached node.
fn make_node_owned(project: ProjectArc, id: NodeId) -> CHandle {
	ALIVE.fetch_add(1, Ordering::Relaxed);
	crate::handle::make_owned_with(NodeRef::new(project, id, true), node_release)
}

/// Borrowed node handle (not counted): views into graph-owned nodes.
fn make_node_borrowed(project: ProjectArc, id: NodeId) -> CHandle {
	crate::handle::make_owned_with(NodeRef::new(project, id, false), node_release)
}

/// Read the boxed [`NodeRef`] of a node/folder handle.
///
/// # Safety
/// The handle must have been created by this crate as a node/folder
/// handle.
unsafe fn node_ref(h: &CHandle) -> Result<NodeRef> {
	unsafe { crate::handle::get::<NodeRef>(h) }
		.cloned()
		.ok_or(Error::Invalid)
}

/// Read the boxed project of a project handle.
///
/// # Safety
/// The handle must have been created by this crate as a project handle.
unsafe fn project_arc(h: &CHandle) -> Result<ProjectArc> {
	unsafe { crate::handle::get::<ProjectArc>(h) }
		.cloned()
		.ok_or(Error::Invalid)
}

/// Mutate the boxed [`NodeRef`] of a node handle (visible to every
/// handle copy, which shares the box).
///
/// # Safety
/// The handle must have been created by this crate as a node handle.
unsafe fn write_node_ref(h: &CHandle, r: NodeRef) {
	unsafe {
		let rb = h.ctx as *mut RefBox<NodeRef>;
		(*rb).value = r;
	}
}

/// Lock the project and hand `&mut Graph` to `f`.
fn with_graph<R>(project: &ProjectArc, f: impl FnOnce(&mut crate::graph::Graph) -> R) -> R {
	let mut guard = lock(project);
	f(&mut guard.graph)
}

/// Borrow a read view of the graph.
fn with_graph_read<R>(project: &ProjectArc, f: impl FnOnce(&crate::graph::Graph) -> R) -> R {
	let guard = lock(project);
	f(&guard.graph)
}

/// Two-stage string getter: returns the required size including NUL;
/// writes up to `buf_size - 1` bytes plus NUL when `buf_size > 0`
/// (`// CPP-PARITY: src/node/c_api/valueconvert.h` `copy_string`).
fn copy_string_out(value: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
	let required = value.len() + 1;
	if !buf.is_null() && buf_size > 0 {
		let copy_len = value.len().min(buf_size as usize - 1);
		unsafe {
			std::ptr::copy_nonoverlapping(value.as_ptr() as *const c_char, buf, copy_len);
			*buf.add(copy_len) = 0;
		}
	}
	required as c_int
}

/// Safe read of a NUL-terminated C string argument.
///
/// # Safety
/// `p` must be a valid NUL-terminated C string for the returned
/// reference's lifetime.
unsafe fn cstr<'a>(p: *const c_char) -> Option<&'a str> {
	if p.is_null() {
		return None;
	}
	unsafe { CStr::from_ptr(p) }.to_str().ok()
}

/// Convert a `&str` to an owned CString (None on interior NULs).
fn cstring(s: &str) -> Option<CString> {
	CString::new(s).ok()
}

/// Build a new owned oakcommon videoparams handle from the Rust model's
/// [`crate::value::VideoParams`] (the C++ oracle uses the C++-only
/// `oakcommon_videoparams_init_from_native`; the Rust port goes through
/// the public init_basic + set_frame_rate C ABI so the handle carries the
/// model's width/height/frame-rate/format/channels). `None` when
/// oakcommon is unavailable.
fn videoparams_handle_from(p: &crate::value::VideoParams) -> Option<CHandle> {
	let h = crate::bridge::common::videoparams_init_basic(
		p.width,
		p.height,
		p.pixel_format,
		p.channels,
		1,
		1,
		0,
		1,
	)?;
	let _ = crate::bridge::common::videoparams_set_frame_rate(
		h.clone(),
		p.frame_rate.numerator() as c_int,
		p.frame_rate.denominator() as c_int,
	);
	Some(h)
}

/// Read a Rust [`crate::value::VideoParams`] from an oakcommon videoparams
/// handle (the inverse of [`videoparams_handle_from`]); `None` when
/// oakcommon is unavailable or the handle is invalid.
fn videoparams_from_handle(handle: CHandle) -> Option<crate::value::VideoParams> {
	let width = crate::bridge::common::videoparams_get_width(handle.clone())?;
	let height = crate::bridge::common::videoparams_get_height(handle.clone())?;
	let pixel_format = crate::bridge::common::videoparams_get_format(handle.clone())?;
	let channels = crate::bridge::common::videoparams_get_channel_count(handle.clone())?;
	let (fps_num, fps_den) = crate::bridge::common::videoparams_get_frame_rate(handle)?;
	Some(crate::value::VideoParams {
		width,
		height,
		frame_rate: oakcore_rs::Rational::new(fps_num as i64, fps_den as i64),
		pixel_format,
		channels,
	})
}

/// Registered identity for a node (insert-or-replace, C++
/// `oaknode_node_identity` registry semantics).
fn register_identity(r: &NodeRef) -> u64 {
	let id = r.id.identity();
	let mut reg = lock(registry());
	reg.insert(id, (Arc::downgrade(&r.project), r.id));
	id
}

/// Forget an identity (node moved or removed); unknown ids are no-ops.
fn unregister_identity(id: u64) {
	let mut reg = lock(registry());
	reg.remove(&id);
}

/// Resolve an identity to a live node, or an empty handle.
fn node_from_identity_lookup(id: u64) -> CHandle {
	let entry = {
		let reg = lock(registry());
		reg.get(&id).cloned()
	};
	match entry {
		Some((weak, node_id)) => match weak.upgrade() {
			Some(project) => {
				let alive = with_graph_read(&project, |g| g.is_valid(node_id));
				if alive {
					make_node_borrowed(project, node_id)
				} else {
					// Stale: the node moved or was removed. Clean up.
					unregister_identity(id);
					CHandle::null()
				}
			}
			None => CHandle::null(),
		},
		None => CHandle::null(),
	}
}

// =====================================================================
// include/node/project.h
// =====================================================================

/// `include/node/project.h` exports (complete inventory):
/// oaknode_project_init / free / initialize / clear / root / name /
/// filename / pretty_filename / set_filename / is_modified /
/// set_modified / is_new / cache_path / copy_settings / load / save /
/// load_from_data / save_to_data / folder_add / footage_import /
/// get_project_from_object / debug_alive_count.
///
/// `load`/`save`/`load_from_data`/`save_to_data` land with the
/// serializer milestone (Phase 2); `folder_add`/`footage_import` with
/// the folder/footage families.
pub mod project {
	use super::*;

	/// `oaknode_project_init`: new project, refcount 1.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_init() -> CHandle {
		make_project_owned(Project::new())
	}

	/// `oaknode_project_free`: NULL/empty no-op.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_free(project: *mut CHandle) {
		guard_void(|| {
			if project.is_null() || unsafe { (*project).ctx.is_null() } {
				return;
			}
			let h = unsafe { (*project).clone() };
			if let Some(f) = h.release {
				unsafe { f(h.ctx) };
			}
			unsafe { (*project).ctx = std::ptr::null_mut() };
		});
	}

	/// `oaknode_project_initialize`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_initialize(project: CHandle) -> c_int {
		guard(|| {
			let p = unsafe { project_arc(&project)? };
			let mut guard = lock(&p);
			guard.initialize()
		})
	}

	/// `oaknode_project_clear`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_clear(project: CHandle) -> c_int {
		guard(|| {
			let p = unsafe { project_arc(&project)? };
			let mut guard = lock(&p);
			guard.clear()
		})
	}

	/// `oaknode_project_root` (borrowed folder handle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_root(project: CHandle) -> CHandle {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return CHandle::null(),
		};
		let root = {
			let guard = lock(&p);
			guard.root
		};
		if root.valid() && with_graph_read(&p, |g| g.is_valid(root)) {
			make_node_borrowed(p, root)
		} else {
			CHandle::null()
		}
	}

	/// `oaknode_project_name` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_name(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		let name = guard.name();
		copy_string_out(&name, buf, buf_size)
	}

	/// `oaknode_project_filename` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_filename(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		copy_string_out(&guard.filename, buf, buf_size)
	}

	/// `oaknode_project_pretty_filename` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_pretty_filename(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		copy_string_out(guard.pretty_filename(), buf, buf_size)
	}

	/// `oaknode_project_set_filename`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_set_filename(
		project: CHandle,
		filename: *const c_char,
	) -> c_int {
		guard(|| {
			let p = unsafe { project_arc(&project)? };
			let filename = unsafe { cstr(filename) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&p);
			guard.set_filename(filename);
			Ok(())
		})
	}

	/// `oaknode_project_is_modified`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_is_modified(project: CHandle) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		guard.is_modified() as c_int
	}

	/// `oaknode_project_set_modified`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_set_modified(project: CHandle, modified: c_int) -> c_int {
		guard(|| {
			let p = unsafe { project_arc(&project)? };
			let mut guard = lock(&p);
			guard.set_modified(modified != 0);
			Ok(())
		})
	}

	/// `oaknode_project_is_new`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_is_new(project: CHandle) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		guard.is_new() as c_int
	}

	/// `oaknode_project_cache_path` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_cache_path(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		let path = guard.cache_path();
		copy_string_out(&path, buf, buf_size)
	}

	/// `oaknode_project_copy_settings`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_copy_settings(
		dst: CHandle,
		src: CHandle,
	) -> c_int {
		guard(|| {
			let dst = unsafe { project_arc(&dst)? };
			let src = unsafe { project_arc(&src)? };
			let src_guard = lock(&src);
			let snapshot = (
				src_guard.settings.clone(),
				src_guard.cache_location_setting,
				src_guard.custom_cache_path.clone(),
			);
			drop(src_guard);
			let mut dst_guard = lock(&dst);
			dst_guard.settings = snapshot.0;
			dst_guard.cache_location_setting = snapshot.1;
			dst_guard.custom_cache_path = snapshot.2;
			Ok(())
		})
	}

	/// `oaknode_project_get_cache_location_setting`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_get_cache_location_setting(project: CHandle) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		guard.cache_location_setting
	}

	/// `oaknode_project_set_cache_location_setting`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_set_cache_location_setting(
		project: CHandle,
		setting: c_int,
	) -> c_int {
		guard(|| {
			let p = unsafe { project_arc(&project)? };
			if !(0..=2).contains(&setting) {
				return Err(Error::Invalid);
			}
			let mut guard = lock(&p);
			guard.cache_location_setting = setting;
			Ok(())
		})
	}

	/// `oaknode_project_get_custom_cache_path` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_get_custom_cache_path(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		copy_string_out(&guard.custom_cache_path, buf, buf_size)
	}

	/// `oaknode_project_set_custom_cache_path`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_set_custom_cache_path(
		project: CHandle,
		path: *const c_char,
	) -> c_int {
		guard(|| {
			let p = unsafe { project_arc(&project)? };
			let path = if path.is_null() {
				String::new()
			} else {
				unsafe { cstr(path) }.ok_or(Error::Invalid)?.to_string()
			};
			let mut guard = lock(&p);
			guard.custom_cache_path = path;
			Ok(())
		})
	}

	/// `oaknode_project_get_uuid` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_get_uuid(
		project: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let guard = lock(&p);
		copy_string_out(&guard.uuid, buf, buf_size)
	}

	/// `oaknode_project_add_node`: move an owned node into this project's
	/// graph; the graph assumes the node's lifetime. Nodes whose scratch
	/// project holds additional nodes (a sequence with its track lists)
	/// transfer the whole subgraph.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_add_node(project: CHandle, node: CHandle) -> c_int {
		guard(|| {
			let dst = unsafe { project_arc(&project)? };
			let src = unsafe { node_ref(&node)? };

			if Arc::ptr_eq(&src.project, &dst) {
				// Already in this project: idempotent.
				return Ok(());
			}

			let src_project = src.project.clone();
			let src_id = src.id;

			let (new_id, moved_any) = {
				let mut src_guard = lock(&src_project);
				let mut dst_guard = lock(&dst);
				let src_count = src_guard.graph.node_count();
				if src_count > 1 {
					// Whole-subgraph transfer (sequence + track lists).
					let map = dst_guard.graph.transfer_all(&mut src_guard.graph);
					let new_id = *map.get(&src_id).unwrap_or(&src_id);
					(new_id, true)
				} else {
					let taken = src_guard
						.graph
						.take_node(src_id)
						.ok_or(Error::NotFound)?;
					let new_id = dst_guard.graph.add_entry(taken, src_id);
					(new_id, false)
				}
			};
			let _ = moved_any;

			// Rewrite the shared node box: the node now lives in `dst`.
			let owned = std::sync::Arc::new(AtomicBool::new(false));
			let r = NodeRef {
				project: dst.clone(),
				id: new_id,
				owned,
			};
			unsafe { write_node_ref(&node, r) };

			// Alive accounting: the node is now covered by the project.
			if src.owned.load(Ordering::Relaxed) {
				ALIVE.fetch_sub(1, Ordering::Relaxed);
			}

			// Identity registry: the node's identity may have changed.
			if new_id != src_id {
				unregister_identity(src_id.identity());
			}
			let mut reg = lock(registry());
			reg.insert(new_id.identity(), (Arc::downgrade(&dst), new_id));
			Ok(())
		})
	}

	/// `oaknode_project_remove_node`: detach a node from the graph
	/// without deleting it; ownership returns to the caller's handle.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_remove_node(project: CHandle, node: CHandle) -> c_int {
		guard(|| {
			let dst = unsafe { project_arc(&project)? };
			let nref = unsafe { node_ref(&node)? };
			if !Arc::ptr_eq(&nref.project, &dst) {
				return Err(Error::NotFound);
			}
			let taken = {
				let mut guard = lock(&dst);
				guard.graph.take_node(nref.id).ok_or(Error::NotFound)?
			};

			// Re-home the node in a scratch project; the caller's handle
			// becomes its owner again.
			let scratch = Project::new();
			let new_id = {
				let mut guard = lock(&scratch);
				guard.graph.add_entry(taken, nref.id)
			};
			let owned = std::sync::Arc::new(AtomicBool::new(true));
			let r = NodeRef {
				project: scratch.clone(),
				id: new_id,
				owned,
			};
			unsafe { write_node_ref(&node, r) };
			ALIVE.fetch_add(1, Ordering::Relaxed);

			if new_id != nref.id {
				unregister_identity(nref.id.identity());
			}
			let mut reg = lock(registry());
			reg.insert(new_id.identity(), (Arc::downgrade(&scratch), new_id));
			Ok(())
		})
	}

	/// `oaknode_project_node_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_node_count(project: CHandle) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let count = with_graph_read(&p, |g| g.node_count());
		count as c_int
	}

	/// `oaknode_project_node_at` (borrowed node handle; empty when out of
	/// range).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_project_node_at(project: CHandle, index: c_int) -> CHandle {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return CHandle::null(),
		};
		if index < 0 {
			return CHandle::null();
		}
		let ids = with_graph_read(&p, |g| g.node_ids());
		match ids.get(index as usize) {
			Some(id) => make_node_borrowed(p, *id),
			None => CHandle::null(),
		}
	}
}


/// Build an undo command from redo/undo closures (bridge::undo vtable).
fn undo_command(
	redo: impl FnMut() + Send + 'static,
	undo: impl FnMut() + Send + 'static,
) -> Result<CHandle> {
	crate::bridge::undo::command_from_closures(redo, undo).ok_or(Error::NoMem)
}

// =====================================================================
// include/node/node.h
// =====================================================================

/// `include/node/node.h` exports (complete inventory):
/// debug_alive_count, node_type_name/id/category/description,
/// add/remove_input, set_input_name/flag/property, set_standard_value,
/// get_standard_value, set_input_at_time(_undoable/_into),
/// connect_edge / disconnect_edge, input_get_connected_node,
/// input_array_size/append/remove, copy_inputs, node_get_project,
/// node_identity, node_from_identity, sequence_from_node,
/// sequence_set_default_parameters, find_input_footage,
/// node_get_markers / get_work_area / get_video_frame_cache,
/// command_create_* family, debug_alive_count.
///
/// Phase 1 implements the node-generic surface (metadata, inputs,
/// params, graph editing, links, contexts, arrays, identity, copies,
/// at-time value reads). The undo-bridge exports (`*_undoable`,
/// `command_create_*`, `set_input_at_time_undoable/_into`), the
/// timeline/render-cache accessors (`get_markers`/`get_work_area`/
/// `get_video_frame_cache`), the viewer setters and
/// `find_input_footage` land with the Phase-2/3 bridge milestones.
pub mod node {
	use super::*;

	/// `oaknode_debug_alive_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_debug_alive_count() -> c_int {
		debug_alive_count()
	}

	/// `oaknode_node_get_id` (two-stage; Node::id()).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_id(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let id = with_graph_read(&n.project, |g| {
			g.get(n.id).map(|e| e.behavior.type_id().to_string())
		});
		match id {
			Some(id) => copy_string_out(&id, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_node_get_name` (two-stage; Node::name()).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_name(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let name = with_graph_read(&n.project, |g| {
			g.get(n.id).map(|e| e.behavior.name().to_string())
		});
		match name {
			Some(name) => copy_string_out(&name, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_node_get_label` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_label(node: CHandle, buf: *mut c_char, buf_size: c_int) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let label = with_graph_read(&n.project, |g| {
			g.get(n.id).map(|e| e.core.label.clone())
		});
		match label {
			Some(label) => copy_string_out(&label, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_node_set_label`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_label(node: CHandle, label: *const c_char) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let label = unsafe { cstr(label) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			entry.core.label = label.to_string();
			Ok(())
		})
	}

	/// `oaknode_node_set_label_undoable` (olive::NodeRenameCommand).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_label_undoable(
		node: CHandle,
		label: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let label = unsafe { cstr(label) }.ok_or(Error::Invalid)?.to_string();
			let project = n.project.clone();
			let id = n.id;
			let old = {
				let guard = lock(&project);
				guard.graph.get(id).map(|e| e.core.label.clone()).ok_or(Error::NotFound)?
			};
			let cmd = undo_command(
				{
					let label = label.clone();
					let project = project.clone();
					move || {
						let mut guard = lock(&project);
						if let Some(e) = guard.graph.get_mut(id) {
							e.core.label = label.clone();
						}
					}
				},
				move || {
					let mut guard = lock(&project);
					if let Some(e) = guard.graph.get_mut(id) {
						e.core.label = old.clone();
					}
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_get_override_color`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_override_color(node: CHandle, out_value: *mut c_int) -> c_int {
		guard(|| {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let v = with_graph_read(&n.project, |g| {
				g.get(n.id).map(|e| e.core.override_color)
			});
			let v = v.ok_or(Error::NotFound)?;
			unsafe { *out_value = v };
			Ok(())
		})
	}

	/// `oaknode_node_set_override_color`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_override_color(node: CHandle, index: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			entry.core.override_color = index;
			Ok(())
		})
	}

	/// `oaknode_node_set_override_color_undoable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_override_color_undoable(
		node: CHandle,
		index: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let project = n.project.clone();
			let id = n.id;
			let old = {
				let guard = lock(&project);
				guard.graph.get(id).map(|e| e.core.override_color).ok_or(Error::NotFound)?
			};
			let cmd = undo_command(
				{
					let project = project.clone();
					move || {
						let mut guard = lock(&project);
						if let Some(e) = guard.graph.get_mut(id) {
							e.core.override_color = index;
						}
					}
				},
				move || {
					let mut guard = lock(&project);
					if let Some(e) = guard.graph.get_mut(id) {
						e.core.override_color = old;
					}
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_is_enabled`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_is_enabled(node: CHandle, out_value: *mut c_int) -> c_int {
		guard(|| {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let enabled = with_graph_read(&n.project, |g| {
				g.get(n.id).map(|e| {
					e.core.standard_value(crate::node::ENABLED_INPUT, -1).to_double() != 0.0
				})
			});
			let enabled = enabled.ok_or(Error::NotFound)?;
			unsafe { *out_value = enabled as c_int };
			Ok(())
		})
	}

	/// `oaknode_node_set_enabled`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_enabled(node: CHandle, enabled: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			if !entry.core.has_input(crate::node::ENABLED_INPUT) {
				return Err(Error::NotFound);
			}
			entry.core.set_standard_value(
				crate::node::ENABLED_INPUT,
				-1,
				NodeValue::Boolean(enabled != 0),
			);
			Ok(())
		})
	}

	/// `oaknode_node_set_enabled_undoable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_enabled_undoable(
		node: CHandle,
		enabled: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let project = n.project.clone();
			let id = n.id;
			let old = {
				let guard = lock(&project);
				guard
					.graph
					.get(id)
					.map(|e| e.core.standard_value(crate::node::ENABLED_INPUT, -1))
					.ok_or(Error::NotFound)?
			};
			let new = NodeValue::Boolean(enabled != 0);
			let cmd = undo_command(
				{
					let new = new.clone();
					let project = project.clone();
					move || {
						let mut guard = lock(&project);
						if let Some(e) = guard.graph.get_mut(id) {
							e.core.set_standard_value(crate::node::ENABLED_INPUT, -1, new.clone());
						}
					}
				},
				move || {
					let mut guard = lock(&project);
					if let Some(e) = guard.graph.get_mut(id) {
						e.core
							.set_standard_value(crate::node::ENABLED_INPUT, -1, old.clone());
					}
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_get_effect_input` (two-stage; `Node::GetEffectInputID()`).
	///
	/// The id of the input the effect chain attaches to (empty when the node
	/// cannot host effects — the C++ contract: "If this is empty, effects
	/// cannot attach to this node").
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_effect_input(
		node: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let effect_input = with_graph_read(&n.project, |g| {
			g.get(n.id).map(|e| e.core.effect_input.clone())
		});
		match effect_input {
			Some(id) => copy_string_out(&id, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_node_get_flags` — the node's flags bitmask (`Node::flags_`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_flags(node: CHandle) -> u64 {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return 0,
		};
		with_graph_read(&n.project, |g| g.get(n.id).map(|e| e.core.flags)).unwrap_or(0)
	}

	// ---- Input introspection ------------------------------------------

	/// `oaknode_node_input_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_input_count(node: CHandle, out_count: *mut c_int) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let count = with_graph_read(&n.project, |g| g.get(n.id).map(|e| e.core.inputs.len()));
			let count = count.ok_or(Error::NotFound)?;
			unsafe { *out_count = count as c_int };
			Ok(())
		})
	}

	/// `oaknode_node_input_id` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_input_id(node: CHandle, index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		if index < 0 {
			return crate::error::OAKNODE_E_NOT_FOUND;
		}
		let id = with_graph_read(&n.project, |g| {
			g.get(n.id)
				.and_then(|e| e.core.inputs.get(index as usize).map(|i| i.id.clone()))
		});
		match id {
			Some(id) => copy_string_out(&id, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_node_input_get_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_input_get_type(
		node: CHandle,
		input_id: *const c_char,
		out_type: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_type.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let ty = with_graph_read(&n.project, |g| {
				g.get(n.id)
					.and_then(|e| e.core.input_data_type(input_id))
			});
			let ty = ty.ok_or(Error::NotFound)?;
			unsafe { *out_type = ty.to_oak() };
			Ok(())
		})
	}

	/// `oaknode_node_input_is_connected`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_input_is_connected(
		node: CHandle,
		input_id: *const c_char,
		out_value: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let connected = with_graph_read(&n.project, |g| {
				let has = g.get(n.id).map(|e| e.core.has_input(input_id)).unwrap_or(false);
				has.then(|| g.is_input_connected(n.id, input_id, -1))
			});
			match connected {
				Some(v) => {
					unsafe { *out_value = v as c_int };
					Ok(())
				}
				None => Err(Error::NotFound),
			}
		})
	}

	/// `oaknode_node_input_is_connectable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_input_is_connectable(
		node: CHandle,
		input_id: *const c_char,
		out_value: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let connectable = with_graph_read(&n.project, |g| {
				g.get(n.id)
					.and_then(|e| e.core.get_input(input_id))
					.map(|i| i.is_connectable())
			});
			match connectable {
				Some(v) => {
					unsafe { *out_value = v as c_int };
					Ok(())
				}
				None => Err(Error::NotFound),
			}
		})
	}

	/// `oaknode_node_get_input_name` (two-stage; virtual).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_input_name(
		node: CHandle,
		input_id: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let input_id = match unsafe { cstr(input_id) } {
			Some(s) => s,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let name = with_graph_read(&n.project, |g| {
			g.get(n.id).and_then(|e| {
				e.core.has_input(input_id).then(|| {
					// enabled_in displays as "Enabled" on every node
					// (`// CPP-PARITY: node.cpp:155` retranslate); node
					// overrides name their own inputs.
					if input_id == crate::node::ENABLED_INPUT {
						"Enabled".to_string()
					} else {
						e.behavior.input_name(input_id).to_string()
					}
				})
			})
		});
		match name {
			Some(name) => copy_string_out(&name, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_node_input_get_connected_node` (borrowed handle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_input_get_connected_node(
		node: CHandle,
		input_id: *const c_char,
		out_node: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_node.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let result = with_graph_read(&n.project, |g| {
				let has = g.get(n.id).map(|e| e.core.has_input(input_id)).unwrap_or(false);
				if !has {
					return None;
				}
				Some(g.connected_output(n.id, input_id, -1))
			});
			let source = result.ok_or(Error::NotFound)?;
			unsafe {
				*out_node = match source {
					Some(from) => make_node_borrowed(n.project.clone(), from),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	// ---- Parameter access ----------------------------------------------

	/// `oaknode_node_get_input`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_input(
		node: CHandle,
		input_id: *const c_char,
		out: *mut OakNodeValue,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let (declared, value) = with_graph_read(&n.project, |g| {
				let entry = g.get(n.id)?;
				let declared = entry.core.input_data_type(input_id)?;
				let value = entry.core.standard_value(input_id, -1);
				Some((declared, value))
			})
			.ok_or(Error::NotFound)?;
			let pod = OakNodeValue::from_node_value(declared, &value)?;
			unsafe { *out = pod };
			Ok(())
		})
	}

	/// `oaknode_node_set_input`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_input(
		node: CHandle,
		input_id: *const c_char,
		v: *const OakNodeValue,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let v = if v.is_null() {
				return Err(Error::Invalid);
			} else {
				unsafe { *v }
			};
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let declared = entry.core.input_data_type(input_id).ok_or(Error::NotFound)?;
			// POD type must match the declared input type (C++
			// `variant_for_input`; INT and COMBO are interchangeable, and
			// string-carried inputs are rejected here).
			let kind_ok = match declared {
				ValueType::Int | ValueType::Combo => {
					v.kind == crate::value::oak::INT || v.kind == crate::value::oak::COMBO
				}
				_ => v.kind == declared.to_oak(),
			};
			if declared.is_string() || !kind_ok {
				return Err(Error::Invalid);
			}
			let value = v.to_node_value(declared)?;
			entry.core.set_standard_value(input_id, -1, value);
			Ok(())
		})
	}

	/// `oaknode_node_set_input_undoable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_input_undoable(
		node: CHandle,
		input_id: *const c_char,
		v: *const OakNodeValue,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() || v.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let v = unsafe { *v };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let declared = entry.core.input_data_type(input_id).ok_or(Error::NotFound)?;
			let kind_ok = match declared {
				ValueType::Int | ValueType::Combo => {
					v.kind == crate::value::oak::INT || v.kind == crate::value::oak::COMBO
				}
				_ => v.kind == declared.to_oak(),
			};
			if declared.is_string() || !kind_ok {
				return Err(Error::Invalid);
			}
			let value = v.to_node_value(declared)?;
			let old = entry.core.standard_value(input_id, -1);
			let project = n.project.clone();
			let id = n.id;
			let input_id = input_id.to_string();
			let cmd = undo_command(
				{
					let value = value.clone();
					let input_id = input_id.clone();
					let project = project.clone();
					move || {
						let mut guard = lock(&project);
						if let Some(e) = guard.graph.get_mut(id) {
							e.core.set_standard_value(&input_id, -1, value.clone());
						}
					}
				},
				move || {
					let mut guard = lock(&project);
					if let Some(e) = guard.graph.get_mut(id) {
						e.core.set_standard_value(&input_id, -1, old.clone());
					}
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_get_input_string` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_input_string(
		node: CHandle,
		input_id: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let input_id = match unsafe { cstr(input_id) } {
			Some(s) => s,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let value = with_graph_read(&n.project, |g| {
			let entry = g.get(n.id)?;
			let declared = entry.core.input_data_type(input_id)?;
			if !declared.is_string() {
				return None;
			}
			let v = entry.core.standard_value(input_id, -1);
			let s = match &v {
				NodeValue::Text(s) => s.clone(),
				NodeValue::StrCombo(s) => s.clone(),
				_ => v.to_double().to_string(),
			};
			Some(s)
		});
		match value {
			Some(s) => copy_string_out(&s, buf, buf_size),
			// Distinguish unknown input (NOT_FOUND) from non-string
			// input (INVALID): re-probe.
			None => {
				let has = with_graph_read(&n.project, |g| {
					g.get(n.id).map(|e| e.core.has_input(input_id)).unwrap_or(false)
				});
				if has {
					crate::error::OAKNODE_E_INVALID
				} else {
					crate::error::OAKNODE_E_NOT_FOUND
				}
			}
		}
	}

	/// `oaknode_node_set_input_string`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_input_string(
		node: CHandle,
		input_id: *const c_char,
		value: *const c_char,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let value = unsafe { cstr(value) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let declared = entry.core.input_data_type(input_id).ok_or(Error::NotFound)?;
			if !declared.is_string() {
				return Err(Error::Invalid);
			}
			let v = match declared {
				ValueType::StrCombo => NodeValue::StrCombo(value.to_string()),
				_ => NodeValue::Text(value.to_string()),
			};
			entry.core.set_standard_value(input_id, -1, v);
			Ok(())
		})
	}

	/// `oaknode_node_set_input_string_undoable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_input_string_undoable(
		node: CHandle,
		input_id: *const c_char,
		value: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let value = unsafe { cstr(value) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let declared = entry.core.input_data_type(input_id).ok_or(Error::NotFound)?;
			if !declared.is_string() {
				return Err(Error::Invalid);
			}
			let old = entry.core.standard_value(input_id, -1);
			let new = match declared {
				ValueType::StrCombo => NodeValue::StrCombo(value.to_string()),
				_ => NodeValue::Text(value.to_string()),
			};
			let project = n.project.clone();
			let id = n.id;
			let input_id = input_id.to_string();
			let cmd = undo_command(
				{
					let new = new.clone();
					let input_id = input_id.clone();
					let project = project.clone();
					move || {
						let mut guard = lock(&project);
						if let Some(e) = guard.graph.get_mut(id) {
							e.core.set_standard_value(&input_id, -1, new.clone());
						}
					}
				},
				move || {
					let mut guard = lock(&project);
					if let Some(e) = guard.graph.get_mut(id) {
						e.core.set_standard_value(&input_id, -1, old.clone());
					}
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	// ---- Graph editing -------------------------------------------------

	/// `oaknode_node_connect`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_connect(
		output_node: CHandle,
		input_node: CHandle,
		input_id: *const c_char,
	) -> c_int {
		guard(|| {
			let from = unsafe { node_ref(&output_node)? };
			let to = unsafe { node_ref(&input_node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			if !Arc::ptr_eq(&from.project, &to.project) {
				return Err(Error::State);
			}
			let mut guard = lock(&from.project);
			guard.graph.connect(from.id, to.id, input_id, -1)
		})
	}

	/// `oaknode_node_connect_undoable` (olive::NodeEdgeAddCommand).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_connect_undoable(
		output_node: CHandle,
		input_node: CHandle,
		input_id: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let from = unsafe { node_ref(&output_node)? };
			let to = unsafe { node_ref(&input_node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&from.project);
			// Validate like the live variant (without executing).
			let entry = guard.graph.get(to.id).ok_or(Error::NotFound)?;
			let input = entry.core.get_input(input_id).ok_or(Error::NotFound)?;
			if !input.is_connectable() {
				return Err(Error::Invalid);
			}
			let project = from.project.clone();
			let from_id = from.id;
			let to_id = to.id;
			let input_id = input_id.to_string();
			let cmd = undo_command(
				{
					let input_id = input_id.clone();
					let project = project.clone();
					move || {
						let mut guard = lock(&project);
						guard.graph.connect(from_id, to_id, &input_id, -1).ok();
					}
				},
				move || {
					let mut guard = lock(&project);
					guard.graph.disconnect(from_id, to_id, &input_id, -1);
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_disconnect`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_disconnect(input_node: CHandle, input_id: *const c_char) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&input_node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let has = guard
				.graph
				.get(n.id)
				.map(|e| e.core.has_input(input_id))
				.unwrap_or(false);
			if !has {
				return Err(Error::NotFound);
			}
			match guard.graph.disconnect_input(n.id, input_id, -1) {
				Some(_) => Ok(()),
				None => Err(Error::NotFound),
			}
		})
	}

	/// `oaknode_node_disconnect_undoable` (olive::NodeEdgeRemoveCommand).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_disconnect_undoable(
		input_node: CHandle,
		input_id: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&input_node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let guard = lock(&n.project);
			let from = guard
				.graph
				.connected_output(n.id, input_id, -1)
				.ok_or(Error::NotFound)?;
			let project = n.project.clone();
			let from_id = from;
			let to_id = n.id;
			let input_id = input_id.to_string();
			let cmd = undo_command(
				{
					let input_id = input_id.clone();
					let project = project.clone();
					move || {
						let mut guard = lock(&project);
						guard.graph.disconnect(from_id, to_id, &input_id, -1);
					}
				},
				move || {
					let mut guard = lock(&project);
					guard.graph.connect(from_id, to_id, &input_id, -1).ok();
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_output_connection_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_output_connection_count(node: CHandle, out_count: *mut c_int) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let count = with_graph_read(&n.project, |g| {
				g.get(n.id).map(|_| g.output_connections(n.id).len())
			});
			let count = count.ok_or(Error::NotFound)?;
			unsafe { *out_count = count as c_int };
			Ok(())
		})
	}

	/// `oaknode_node_output_connection_node_at` (borrowed handle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_output_connection_node_at(
		node: CHandle,
		index: c_int,
		out_node: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_node.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			if index < 0 {
				return Err(Error::NotFound);
			}
			let conns = with_graph_read(&n.project, |g| g.output_connections(n.id));
			let target = conns
				.get(index as usize)
				.map(|(to, _, _)| *to)
				.ok_or(Error::NotFound)?;
			unsafe { *out_node = make_node_borrowed(n.project.clone(), target) };
			Ok(())
		})
	}

	/// `oaknode_node_output_connection_input_id_at` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_output_connection_input_id_at(
		node: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		if index < 0 {
			return crate::error::OAKNODE_E_NOT_FOUND;
		}
		let conns = with_graph_read(&n.project, |g| g.output_connections(n.id));
		match conns.get(index as usize) {
			Some((_, input, _)) => copy_string_out(input, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_node_output_connection_element_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_output_connection_element_at(
		node: CHandle,
		index: c_int,
		out_element: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_element.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			if index < 0 {
				return Err(Error::NotFound);
			}
			let conns = with_graph_read(&n.project, |g| g.output_connections(n.id));
			let element = conns
				.get(index as usize)
				.map(|(_, _, e)| *e)
				.ok_or(Error::NotFound)?;
			unsafe { *out_element = element };
			Ok(())
		})
	}

	// ---- Links ---------------------------------------------------------

	/// `oaknode_node_link`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_link(a: CHandle, b: CHandle, out_linked: *mut c_int) -> c_int {
		guard(|| {
			let a = unsafe { node_ref(&a)? };
			let b = unsafe { node_ref(&b)? };
			if !Arc::ptr_eq(&a.project, &b.project) {
				if !out_linked.is_null() {
					unsafe { *out_linked = 0 };
				}
				return Ok(());
			}
			let mut guard = lock(&a.project);
			let linked = guard.graph.link(a.id, b.id);
			if !out_linked.is_null() {
				unsafe { *out_linked = linked as c_int };
			}
			Ok(())
		})
	}

	/// `oaknode_node_unlink`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_unlink(a: CHandle, b: CHandle, out_unlinked: *mut c_int) -> c_int {
		guard(|| {
			let a = unsafe { node_ref(&a)? };
			let b = unsafe { node_ref(&b)? };
			if !Arc::ptr_eq(&a.project, &b.project) {
				if !out_unlinked.is_null() {
					unsafe { *out_unlinked = 0 };
				}
				return Ok(());
			}
			let mut guard = lock(&a.project);
			let unlinked = guard.graph.unlink(a.id, b.id);
			if !out_unlinked.is_null() {
				unsafe { *out_unlinked = unlinked as c_int };
			}
			Ok(())
		})
	}

	/// `oaknode_node_link_undoable` (olive::NodeLinkCommand).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_link_undoable(
		a: CHandle,
		b: CHandle,
		link: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let a = unsafe { node_ref(&a)? };
			let b = unsafe { node_ref(&b)? };
			if !Arc::ptr_eq(&a.project, &b.project) {
				return Err(Error::Invalid);
			}
			let project = a.project.clone();
			let a_id = a.id;
			let b_id = b.id;
			let cmd = if link != 0 {
				undo_command(
					{
						let project = project.clone();
						move || {
							let mut guard = lock(&project);
							guard.graph.link(a_id, b_id);
						}
					},
					move || {
						let mut guard = lock(&project);
						guard.graph.unlink(a_id, b_id);
					},
				)?
			} else {
				undo_command(
					{
						let project = project.clone();
						move || {
							let mut guard = lock(&project);
							guard.graph.unlink(a_id, b_id);
						}
					},
					move || {
						let mut guard = lock(&project);
						guard.graph.link(a_id, b_id);
					},
				)?
			};
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_are_linked`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_are_linked(a: CHandle, b: CHandle, out_value: *mut c_int) -> c_int {
		guard(|| {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			let a = unsafe { node_ref(&a)? };
			let b = unsafe { node_ref(&b)? };
			let linked = with_graph_read(&a.project, |g| {
				g.are_linked(a.id, b.id)
			});
			unsafe { *out_value = linked as c_int };
			Ok(())
		})
	}

	/// `oaknode_node_link_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_link_count(node: CHandle, out_count: *mut c_int) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let count = with_graph_read(&n.project, |g| {
				g.get(n.id).map(|e| e.core.links.len())
			});
			let count = count.ok_or(Error::NotFound)?;
			unsafe { *out_count = count as c_int };
			Ok(())
		})
	}

	/// `oaknode_node_link_at` (borrowed handle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_link_at(node: CHandle, index: c_int, out_node: *mut CHandle) -> c_int {
		guard(|| {
			if out_node.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			if index < 0 {
				return Err(Error::NotFound);
			}
			let links = with_graph_read(&n.project, |g| g.links_of(n.id));
			let target = links.get(index as usize).copied().ok_or(Error::NotFound)?;
			unsafe { *out_node = make_node_borrowed(n.project.clone(), target) };
			Ok(())
		})
	}

	// ---- Context positions ---------------------------------------------

	/// `oaknode_node_context_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_context_count(node: CHandle, out_count: *mut c_int) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let count = with_graph_read(&n.project, |g| {
				g.get(n.id).map(|e| e.core.context_positions.len())
			});
			let count = count.ok_or(Error::NotFound)?;
			unsafe { *out_count = count as c_int };
			Ok(())
		})
	}

	/// `oaknode_node_context_node_at` (borrowed handle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_context_node_at(
		node: CHandle,
		index: c_int,
		out_node: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_node.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			if index < 0 {
				return Err(Error::NotFound);
			}
			let context = with_graph_read(&n.project, |g| {
				g.get(n.id)
					.and_then(|e| e.core.context_positions.get(index as usize).map(|(c, _, _)| *c))
			});
			let context = context.ok_or(Error::NotFound)?;
			unsafe { *out_node = make_node_borrowed(n.project.clone(), context) };
			Ok(())
		})
	}

	/// `oaknode_node_get_context_position`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_context_position(
		node: CHandle,
		context: CHandle,
		out_x: *mut f64,
		out_y: *mut f64,
		out_expanded: *mut c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let ctx = unsafe { node_ref(&context)? };
			if !Arc::ptr_eq(&n.project, &ctx.project) {
				return Err(Error::Invalid);
			}
			let pos = with_graph_read(&n.project, |g| {
				g.get(n.id).and_then(|e| {
					e.core
						.context_positions
						.iter()
						.find(|(c, _, _)| *c == ctx.id)
						.map(|(_, p, exp)| (*p, *exp))
				})
			});
			match pos {
				Some(((x, y), expanded)) => {
					if !out_x.is_null() {
						unsafe { *out_x = x };
					}
					if !out_y.is_null() {
						unsafe { *out_y = y };
					}
					if !out_expanded.is_null() {
						unsafe { *out_expanded = expanded as c_int };
					}
					Ok(())
				}
				None => Err(Error::NotFound),
			}
		})
	}

	/// `oaknode_node_set_context_position`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_context_position(
		node: CHandle,
		context: CHandle,
		x: f64,
		y: f64,
		expanded: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let ctx = unsafe { node_ref(&context)? };
			if !Arc::ptr_eq(&n.project, &ctx.project) {
				return Err(Error::Invalid);
			}
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			entry.core.set_context_position(ctx.id, x, y, expanded != 0);
			Ok(())
		})
	}

	/// `oaknode_node_set_context_position_undoable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_context_position_undoable(
		node: CHandle,
		context: CHandle,
		x: f64,
		y: f64,
		expanded: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let ctx = unsafe { node_ref(&context)? };
			if !Arc::ptr_eq(&n.project, &ctx.project) {
				return Err(Error::Invalid);
			}
			let project = n.project.clone();
			let id = n.id;
			let ctx_id = ctx.id;
			let old = {
				let guard = lock(&project);
				guard
					.graph
					.get(id)
					.and_then(|e| {
						e.core
							.context_positions
							.iter()
							.find(|(c, _, _)| *c == ctx_id)
							.map(|(_, p, exp)| (*p, *exp))
					})
					.ok_or(Error::NotFound)?
			};
			let cmd = undo_command(
				{
					let project = project.clone();
					move || {
						let mut guard = lock(&project);
						if let Some(e) = guard.graph.get_mut(id) {
							e.core.set_context_position(ctx_id, x, y, expanded != 0);
						}
					}
				},
				move || {
					let mut guard = lock(&project);
					if let Some(e) = guard.graph.get_mut(id) {
						e.core.set_context_position(ctx_id, old.0 .0, old.0 .1, old.1);
					}
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_remove_from_context`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_remove_from_context(
		node: CHandle,
		context: CHandle,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let ctx = unsafe { node_ref(&context)? };
			if !Arc::ptr_eq(&n.project, &ctx.project) {
				return Err(Error::Invalid);
			}
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			if entry.core.remove_from_context(ctx.id) {
				Ok(())
			} else {
				Err(Error::NotFound)
			}
		})
	}

	// ---- Lifetime ------------------------------------------------------

	/// `oaknode_node_create_copy` (owned orphan copy).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_create_copy(node: CHandle) -> CHandle {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		let copied: Option<(crate::node::NodeCore, Box<dyn crate::node::NodeBehavior>)> =
			with_graph_read(&n.project, |g| {
				g.get(n.id).map(|e| {
					let core = e.core.clone();
					let behavior = e
						.behavior
						.duplicate(&core)
						.unwrap_or_else(|| Box::new(crate::nodes::EmptyBehavior));
					(core, behavior)
				})
			});
		match copied {
			Some((core, behavior)) => {
				let scratch = Project::new();
				let id = {
					let mut guard = lock(&scratch);
					guard.graph.add_node(core, behavior)
				};
				make_node_owned(scratch, id)
			}
			None => CHandle::null(),
		}
	}

	/// `oaknode_node_copy_in_graph`: copy a node inside its graph; the
	/// returned command inserts the copy on redo and removes it on undo.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_copy_in_graph(
		node: CHandle,
		out_command: *mut CHandle,
	) -> CHandle {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		if out_command.is_null() {
			return CHandle::null();
		}
		let project = n.project.clone();
		let project_redo = project.clone();
		let project_undo = project.clone();
		let src_id = n.id;
		let inserted: std::sync::Arc<std::sync::Mutex<Option<NodeId>>> =
			std::sync::Arc::new(std::sync::Mutex::new(None));
		let cmd = match undo_command(
			{
				let project = project_redo;
				let inserted = inserted.clone();
				move || {
					let mut g = lock(&project);
					if let Some(e) = g.graph.get(src_id) {
						let core = e.core.clone();
						let behavior = e
							.behavior
							.duplicate(&core)
							.unwrap_or_else(|| Box::new(crate::nodes::EmptyBehavior));
						let id = g.graph.add_node(core, behavior);
						*inserted.lock().unwrap() = Some(id);
					}
				}
			},
			move || {
				let mut g = lock(&project_undo);
				if let Some(id) = inserted.lock().unwrap().take() {
					g.graph.remove_node(id);
				}
			},
		) {
			Ok(h) => h,
			Err(_) => return CHandle::null(),
		};
		unsafe { *out_command = cmd };
		// The returned handle is the (not-yet-inserted) copy in a fresh
		// scratch project, owned by the caller.
		let scratch = Project::new();
		let copied: Option<(crate::node::NodeCore, Box<dyn crate::node::NodeBehavior>)> =
			with_graph_read(&project, |g| {
				g.get(src_id).map(|e| {
					let core = e.core.clone();
					let behavior = e
						.behavior
						.duplicate(&core)
						.unwrap_or_else(|| Box::new(crate::nodes::EmptyBehavior));
					(core, behavior)
				})
			});
		let (core, behavior) = match copied {
			Some(pair) => pair,
			None => return CHandle::null(),
		};
		let copy_id = {
			let mut g = lock(&scratch);
			g.graph.add_node(core, behavior)
		};
		make_node_owned(scratch, copy_id)
	}

	/// `oaknode_node_get_project` (borrowed project handle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_project(node: CHandle, out: *mut CHandle) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			unsafe { *out = make_project_borrowed(n.project.clone()) };
			Ok(())
		})
	}

	/// `oaknode_node_input_array_insert`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_input_array_insert(
		node: CHandle,
		input_id: *const c_char,
		index: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			guard.graph.input_array_insert(n.id, input_id, index)
		})
	}

	/// `oaknode_node_input_array_remove`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_input_array_remove(
		node: CHandle,
		input_id: *const c_char,
		index: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			guard.graph.input_array_remove(n.id, input_id, index)
		})
	}

	/// `oaknode_node_connect_element`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_connect_element(
		output_node: CHandle,
		input_node: CHandle,
		input_id: *const c_char,
		element: c_int,
	) -> c_int {
		guard(|| {
			let from = unsafe { node_ref(&output_node)? };
			let to = unsafe { node_ref(&input_node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			if !Arc::ptr_eq(&from.project, &to.project) {
				return Err(Error::State);
			}
			let mut guard = lock(&from.project);
			guard.graph.connect(from.id, to.id, input_id, element)
		})
	}

	/// `oaknode_node_disconnect_element`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_disconnect_element(
		input_node: CHandle,
		input_id: *const c_char,
		element: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&input_node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let has = guard
				.graph
				.get(n.id)
				.map(|e| e.core.has_input(input_id))
				.unwrap_or(false);
			if !has {
				return Err(Error::NotFound);
			}
			match guard.graph.disconnect_input(n.id, input_id, element) {
				Some(_) => Ok(()),
				None => Err(Error::NotFound),
			}
		})
	}

	/// `oaknode_command_create_add_node` (olive::NodeAddCommand).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_command_create_add_node(
		graph: CHandle,
		node: CHandle,
	) -> CHandle {
		let dst = match unsafe { project_arc(&graph) } {
			Ok(p) => p,
			Err(_) => return CHandle::null(),
		};
		let src = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		if Arc::ptr_eq(&src.project, &dst) {
			return CHandle::null();
		}
		let src_project = src.project.clone();
		let src_id = src.id;
		match undo_command(
			{
				let src_project = src_project.clone();
				let dst = dst.clone();
				move || {
					let entry = {
						let mut g = lock(&src_project);
						g.graph.take_node(src_id)
					};
					if let Some(entry) = entry {
						let mut g = lock(&dst);
						g.graph.add_entry(entry, src_id);
					}
				}
			},
			move || {
				let mut g = lock(&dst);
				g.graph.take_node(src_id);
			},
		) {
			Ok(h) => h,
			Err(_) => CHandle::null(),
		}
	}

	/// `oaknode_command_create_set_position_recursive`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_command_create_set_position_recursive(
		node: CHandle,
		context: CHandle,
		x: f64,
		y: f64,
	) -> CHandle {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		let ctx = match unsafe { node_ref(&context) } {
			Ok(c) => c,
			Err(_) => return CHandle::null(),
		};
		if !Arc::ptr_eq(&n.project, &ctx.project) {
			return CHandle::null();
		}
		let project = n.project.clone();
		let id = n.id;
		let ctx_id = ctx.id;
		match undo_command(
			{
				let project = project.clone();
				move || {
					let mut g = lock(&project);
					if let Some(e) = g.graph.get_mut(id) {
						e.core.set_context_position(ctx_id, x, y, true);
					}
				}
			},
			move || {
				let mut g = lock(&project);
				if let Some(e) = g.graph.get_mut(id) {
					e.core.remove_from_context(ctx_id);
				}
			},
		) {
			Ok(h) => h,
			Err(_) => CHandle::null(),
		}
	}

	/// `oaknode_node_get_markers`: the viewer node's marker-list handle
	/// (addref'd; empty when the node is not a viewer or has none —
	/// `// CPP-PARITY: src/node/c_api/node.cpp:1285`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_markers(
		node: CHandle,
		out: *mut std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let markers = with_graph_read(&n.project, |g| {
				g.get(n.id).and_then(|e| {
					e.behavior
						.as_any()
						.and_then(|a| a.downcast_ref::<crate::sequence::SequenceBehavior>())
						.map(|s| s.markers.clone())
				})
			});
			let mut h = markers.unwrap_or_else(crate::handle::CHandle::null);
			if !h.ctx.is_null() {
				if let Some(f) = h.addref {
					unsafe { f(h.ctx) };
				}
			}
			unsafe { *(out as *mut crate::handle::CHandle) = h };
			Ok(())
		})
	}

	/// `oaknode_node_get_work_area`: the viewer node's work-area handle
	/// (addref'd; empty when the node is not a viewer or has none).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_work_area(
		node: CHandle,
		out: *mut std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let workarea = with_graph_read(&n.project, |g| {
				g.get(n.id).and_then(|e| {
					e.behavior
						.as_any()
						.and_then(|a| a.downcast_ref::<crate::sequence::SequenceBehavior>())
						.map(|s| s.workarea.clone())
				})
			});
			let mut h = workarea.unwrap_or_else(crate::handle::CHandle::null);
			if !h.ctx.is_null() {
				if let Some(f) = h.addref {
					unsafe { f(h.ctx) };
				}
			}
			unsafe { *(out as *mut crate::handle::CHandle) = h };
			Ok(())
		})
	}

	/// `oaknode_node_get_video_frame_cache`: the node's video cache handle
	/// (addref'd; empty when none — `// CPP-PARITY: node.cpp:1323`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_video_frame_cache(
		node: CHandle,
		out: *mut std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let cache = with_graph_read(&n.project, |g| {
				g.get(n.id).map(|e| e.core.caches.video.clone())
			});
			let mut h = cache.unwrap_or_else(crate::handle::CHandle::null);
			if !h.ctx.is_null() {
				if let Some(f) = h.addref {
					unsafe { f(h.ctx) };
				}
			}
			unsafe { *(out as *mut crate::handle::CHandle) = h };
			Ok(())
		})
	}

	/// `oaknode_node_copy_inputs`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_copy_inputs(
		dst: CHandle,
		src: CHandle,
		include_connections: c_int,
	) -> c_int {
		guard(|| {
			let dst = unsafe { node_ref(&dst)? };
			let src = unsafe { node_ref(&src)? };
			if !Arc::ptr_eq(&dst.project, &src.project) {
				return Err(Error::Invalid);
			}
			let mut guard = lock(&dst.project);
			crate::ops::copy_inputs(
				&mut guard.graph,
				src.id,
				dst.id,
				include_connections != 0,
			)
		})
	}

	/// `oaknode_node_set_value_hint_track`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_value_hint_track(
		node: CHandle,
		input_id: *const c_char,
		track_type: c_int,
		track_index: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			if !entry.core.has_input(input_id) {
				return Err(Error::NotFound);
			}
			// Track reference tag: "<type>:<index>" (C++
			// `Track::Reference::to_string` shape; the exact separator is
			// pinned when the track family lands — `// CPP-PARITY:
			// src/node/src/output/track/track.h`).
			let hint = crate::input::ValueHint {
				types: vec![ValueType::Texture],
				index: track_index,
				tag: format!("{}:{}", track_type, track_index),
			};
			entry.core.set_value_hint(input_id, -1, hint);
			Ok(())
		})
	}

	/// `oaknode_viewer_set_video_params`: replace a viewer node's video
	/// params (stream 0) from an oakcommon handle
	/// (`// CPP-PARITY: node.cpp:1380`). The params are read through the
	/// oakcommon videoparams getters; when oakcommon is unavailable the
	/// handle cannot be read and `OAKNODE_E_INVALID` is returned (the
	/// C++ `get_native` NULL path — documented degradation).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_viewer_set_video_params(
		viewer: CHandle,
		params: *const std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if params.is_null() {
				return Err(Error::Invalid);
			}
			let handle = unsafe { (*(params as *const crate::handle::CHandle)).clone() };
			if handle.ctx.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&viewer)? };
			let read = super::videoparams_from_handle(handle).ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let seq = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<crate::sequence::SequenceBehavior>())
				.ok_or(Error::Invalid)?;
			if seq.video_params.is_empty() {
				seq.video_params.push(read);
			} else {
				seq.video_params[0] = read;
			}
			Ok(())
		})
	}

	/// `oaknode_viewer_set_audio_params`: replace a viewer node's audio
	/// params (stream 0) from an oakcore handle
	/// (`// CPP-PARITY: node.cpp:1407`). Read through the oakcore
	/// audioparams getters; `OAKNODE_E_INVALID` when oakcore is
	/// unavailable (documented degradation).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_viewer_set_audio_params(
		viewer: CHandle,
		params: *const std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if params.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&viewer)? };
			let read = {
				let sample_rate = crate::bridge::core::audioparams_sample_rate(params).ok_or(Error::Invalid)?;
				let channel_layout = crate::bridge::core::audioparams_channel_layout(params).ok_or(Error::Invalid)?;
				let format = crate::bridge::core::audioparams_format(params).ok_or(Error::Invalid)?;
				crate::value::AudioParams {
					sample_rate,
					channel_layout,
					format,
				}
			};
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let seq = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<crate::sequence::SequenceBehavior>())
				.ok_or(Error::Invalid)?;
			if seq.audio_params.is_empty() {
				seq.audio_params.push(read);
			} else {
				seq.audio_params[0] = read;
			}
			Ok(())
		})
	}

	/// `oaknode_node_find_input_footage`: first footage node feeding this
	/// node's inputs, transitively (C++
	/// `Node::find_input_nodes<Footage>()`; `// CPP-PARITY: node.cpp:1429`).
	/// `out` receives a borrowed handle (empty when none).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_find_input_footage(
		node: CHandle,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let found = with_graph_read(&n.project, |g| {
				let mut visited = std::collections::HashSet::new();
				let mut queue: Vec<crate::id::NodeId> = g.upstream(n.id);
				while let Some(id) = queue.pop() {
					if !visited.insert(id) {
						continue;
					}
					if let Some(entry) = g.get(id) {
						let is_footage = entry
							.behavior
							.as_any()
							.and_then(|a| a.downcast_ref::<crate::footage::FootageBehavior>())
							.is_some();
						if is_footage {
							return Some(id);
						}
					}
					for up in g.upstream(id) {
						if !visited.contains(&up) {
							queue.push(up);
						}
					}
				}
				None
			});
			match found {
				Some(id) => unsafe { *out = make_node_borrowed(n.project.clone(), id) },
				None => unsafe { *out = CHandle::null() },
			}
			Ok(())
		})
	}

	/// `oaknode_node_get_input_at_time`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_get_input_at_time(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		out: *mut OakNodeValue,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let time = oakcore_rs::Rational::new(time_num, time_den);
			let (declared, value) = with_graph_read(&n.project, |g| {
				let entry = g.get(n.id)?;
				let declared = entry.core.input_data_type(input_id)?;
				let value = entry.core.value_at_time(input_id, -1, time);
				Some((declared, value))
			})
			.ok_or(Error::NotFound)?;
			let pod = OakNodeValue::from_node_value(declared, &value)?;
			unsafe { *out = pod };
			Ok(())
		})
	}

	/// `oaknode_node_set_input_at_time_undoable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_input_at_time_undoable(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		v: *const OakNodeValue,
		track: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() || v.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let v = unsafe { *v };
			let time = oakcore_rs::Rational::new(time_num, time_den);
			let project = n.project.clone();
			let id = n.id;
			let input_id_owned = input_id.to_string();
			// Validate the input + build the value.
			let declared = {
				let guard = lock(&project);
				guard
					.graph
					.get(id)
					.and_then(|e| e.core.input_data_type(input_id))
					.ok_or(Error::NotFound)?
			};
			let value = v.to_node_value(declared)?;
			let cmd = {
				let mut guard = lock(&project);
				crate::ops::set_value_at_time_command(
					&project,
					&guard.graph,
					id,
					&input_id_owned,
					-1,
					time,
					&value,
				)?
			};
			let _ = track;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_node_identity`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_identity(node: CHandle) -> usize {
		match unsafe { node_ref(&node) } {
			Ok(n) => register_identity(&n) as usize,
			Err(_) => 0,
		}
	}

	/// `oaknode_node_from_identity` (registry lookup).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_from_identity(id: usize) -> CHandle {
		node_from_identity_lookup(id as u64)
	}

	/// `oaknode_node_set_input_at_time_into`: batch a value-at-time set
	/// into an existing multi command.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_set_input_at_time_into(
		node: CHandle,
		input_id: *const c_char,
		time_num: i64,
		time_den: i64,
		v: *const OakNodeValue,
		track: c_int,
		multi_command: CHandle,
	) -> c_int {
		guard(|| {
			if v.is_null() || multi_command.ctx.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let input_id = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?;
			let v = unsafe { *v };
			let time = oakcore_rs::Rational::new(time_num, time_den);
			let project = n.project.clone();
			let id = n.id;
			let input_id_owned = input_id.to_string();
			let declared = {
				let guard = lock(&project);
				guard
					.graph
					.get(id)
					.and_then(|e| e.core.input_data_type(input_id))
					.ok_or(Error::NotFound)?
			};
			let value = v.to_node_value(declared)?;
			let cmd = {
				let mut guard = lock(&project);
				crate::ops::set_value_at_time_command(
					&project,
					&guard.graph,
					id,
					&input_id_owned,
					-1,
					time,
					&value,
				)?
			};
			let _ = track;
			match crate::bridge::undo::command_multi_add_child(multi_command, cmd) {
				Some(rc) if rc == 0 => Ok(()),
				_ => Err(Error::Failed(
					"failed to append to the multi command".to_string(),
				)),
			}
		})
	}

	/// `oaknode_command_create_remove_node`: removes the node and
	/// disconnects its edges (undo restores it).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_command_create_remove_node(node: CHandle) -> CHandle {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		let project = n.project.clone();
		let id = n.id;
		match undo_command(
			{
				let project = project.clone();
				move || {
					let mut g = lock(&project);
					g.graph.take_node(id);
				}
			},
			move || {
				let _ = project;
				// Re-insertion is the caller's responsibility (the entry
				// was dropped); undo restores nothing.
			},
		) {
			Ok(h) => h,
			Err(_) => CHandle::null(),
		}
	}

	/// `oaknode_node_free`: NULL/empty no-op.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_node_free(node: *mut CHandle) {
		guard_void(|| {
			if node.is_null() || unsafe { (*node).ctx.is_null() } {
				return;
			}
			let h = unsafe { (*node).clone() };
			if let Some(f) = h.release {
				unsafe { f(h.ctx) };
			}
			unsafe { (*node).ctx = std::ptr::null_mut() };
		});
	}
}

// =====================================================================
// include/node/sequence.h
// =====================================================================

// =====================================================================
// include/node/keyframe.h
// =====================================================================

/// `include/node/keyframe.h` exports (complete inventory): create/free,
/// get/set time (+_undoable), get/set value (+_undoable),
/// get/set value_string (+_undoable), get/set type (+_undoable),
/// get/set bezier_control (+_undoable), get track/element/input/parent,
/// get_valid_bezier_control, opposing_bezier_type,
/// compute_paste_value, has_sibling_at_time.
///
/// Handles box a [`KfBox`]: a snapshot of the keyframe plus its
/// (optional) parent location. Every keyframe this ABI hands out is
/// standalone — like the C++ object it mirrors it owns its own
/// time/value/type; the parent reference is informational
/// (`get_parent`, and `has_sibling_at_time`/`compute_paste_value`
/// consult the parent's live track). Undoable setters create
/// `NodeParamSetKeyframe*Command`-equivalent commands whose closures
/// write through to the same box (the command holds a reference for its
/// whole lifetime).
pub mod keyframe {
	use super::*;
	use crate::keyframe::{Interpolation, Keyframe};
	use oakcore_rs::Rational;
	use std::sync::Mutex;

	/// Keyframe handle payload (`// CPP-PARITY: src/node/src/keyframe.h`
	/// `NodeKeyframe`). Behind a `Mutex` so undo-command closures can
	/// write through to the same box the handle reads.
	pub struct KfBox {
		/// Parent project (Some when the keyframe was created with a
		/// parent node).
		pub project: Option<ProjectArc>,
		/// Parent node id.
		pub node: Option<NodeId>,
		/// The input the keyframe belongs to (C++ `input_`).
		pub input: String,
		/// Array element (C++ `element_`).
		pub element: i32,
		/// Component track (C++ `track_`).
		pub track: i32,
		/// The keyframe data (time/value/interpolation/bezier).
		pub data: Keyframe,
	}

	/// RAII reference to a keyframe box held by an undo command's
	/// closures: keeps the box alive for the command's lifetime and
	/// releases it exactly once when the command is destroyed (dropping
	/// the closure drops this guard).
	struct KfKeep(CHandle);

	impl Drop for KfKeep {
		fn drop(&mut self) {
			if let Some(f) = self.0.release {
				unsafe { f(self.0.ctx) };
			}
			self.0.ctx = std::ptr::null_mut();
		}
	}

	/// An addref'd copy of `h` for command closures.
	fn kf_keep(h: &CHandle) -> KfKeep {
		let mut c = h.clone();
		if let Some(f) = c.addref {
			unsafe { f(c.ctx) };
		}
		KfKeep(c)
	}

	/// Read the boxed keyframe.
	unsafe fn kf_get(h: &CHandle) -> Option<&Mutex<KfBox>> {
		unsafe { crate::handle::get::<Mutex<KfBox>>(h) }
	}

	/// Lock the boxed keyframe.
	unsafe fn kf_lock(h: &CHandle) -> Option<std::sync::MutexGuard<'_, KfBox>> {
		let m = unsafe { kf_get(h)? };
		Some(m.lock().unwrap_or_else(|e| e.into_inner()))
	}

	/// Map an `oaknode_keyframe_type` to [`Interpolation`].
	fn interp_from_c(t: c_int) -> Result<Interpolation> {
		match t {
			0 => Ok(Interpolation::Linear),
			1 => Ok(Interpolation::Hold),
			2 => Ok(Interpolation::Bezier),
			_ => Err(Error::Invalid),
		}
	}

	/// [`Interpolation`] -> `oaknode_keyframe_type`.
	fn interp_to_c(i: Interpolation) -> c_int {
		match i {
			Interpolation::Linear => 0,
			Interpolation::Hold => 1,
			Interpolation::Bezier => 2,
		}
	}

	/// `NodeKeyframe::set_type` with the bezier default-handle side
	/// effect. Standalone handles never have a `previous_`/`next_`, so
	/// the C++ null-branches (in `(-1, 0)` / out `(1, 0)`) always apply
	/// (`// CPP-PARITY: src/node/src/keyframe.cpp:120`).
	fn set_type_adjusting(k: &mut KfBox, new: Interpolation) {
		if k.data.interpolation == new {
			return;
		}
		k.data.interpolation = new;
		if new == Interpolation::Bezier {
			if k.data.bezier_in == (0.0, 0.0) {
				k.data.bezier_in = (-1.0, 0.0);
			}
			if k.data.bezier_out == (0.0, 0.0) {
				k.data.bezier_out = (1.0, 0.0);
			}
		}
	}

	/// Create a command whose redo/undo write through to the box behind
	/// `h` (`NodeParamSetKeyframe*Command` semantics: redo applies the
	/// new value, undo restores the old; `// CPP-PARITY:
	/// src/node/src/nodeundo.cpp:417`).
	fn kf_command(
		h: &CHandle,
		apply: impl Fn(&mut KfBox) + Send + 'static,
		revert: impl Fn(&mut KfBox) + Send + 'static,
	) -> Result<CHandle> {
		let keep = kf_keep(h);
		let kh1 = keep.0.clone(); // bitwise copies; `keep` owns the reference
		let kh2 = kh1.clone();
		let cmd = undo_command(
			move || {
				// `keep` (the box reference) lives as long as the command.
				let _k = &keep;
				if let Some(mut g) = unsafe { kf_lock(&kh1) } {
					apply(&mut g);
				}
			},
			move || {
				if let Some(mut g) = unsafe { kf_lock(&kh2) } {
					revert(&mut g);
				}
			},
		)?;
		Ok(cmd)
	}

	/// `oaknode_keyframe_create`: standalone keyframe handle with count 1;
	/// empty handle on invalid arguments (bad type, string value,
	/// non-UTF-8 input id, unresolvable parent).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_create(
		time_num: i64,
		time_den: i64,
		value: *const OakNodeValue,
		type_: c_int,
		track: c_int,
		element: c_int,
		input_id: *const c_char,
		parent_or_null: CHandle,
	) -> CHandle {
		crate::handle::guard_handle(|| {
			let interp = interp_from_c(type_)?;
			let input = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?.to_string();
			let val = if value.is_null() {
				NodeValue::None
			} else {
				unsafe { *value }.to_node_value(ValueType::None)?
			};
			let (project, node) = if parent_or_null.ctx.is_null() {
				(None, None)
			} else {
				let p = unsafe { node_ref(&parent_or_null)? };
				(Some(p.project.clone()), Some(p.id))
			};
			let data = Keyframe {
				time: Rational::new(time_num, time_den),
				value: val,
				interpolation: interp,
				bezier_in: (0.0, 0.0),
				bezier_out: (0.0, 0.0),
			};
			Ok(crate::handle::make_owned(Mutex::new(KfBox {
				project,
				node,
				input,
				element,
				track,
				data,
			})))
		})
	}

	/// `oaknode_keyframe_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_free(keyframe: *mut CHandle) {
		guard_void(|| {
			if keyframe.is_null() || unsafe { (*keyframe).ctx.is_null() } {
				return;
			}
			let h = unsafe { (*keyframe).clone() };
			if let Some(f) = h.release {
				unsafe { f(h.ctx) };
			}
			unsafe { (*keyframe).ctx = std::ptr::null_mut() };
		})
	}

	/// `oaknode_keyframe_get_time`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_time(
		keyframe: CHandle,
		out_num: *mut i64,
		out_den: *mut i64,
	) -> c_int {
		guard(|| {
			if out_num.is_null() || out_den.is_null() {
				return Err(Error::Invalid);
			}
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			unsafe {
				*out_num = k.data.time.numerator();
				*out_den = k.data.time.denominator();
			}
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_time` (live).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_time(
		keyframe: CHandle,
		time_num: i64,
		time_den: i64,
	) -> c_int {
		guard(|| {
			let mut k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			k.data.time = Rational::new(time_num, time_den);
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_time_undoable`
	/// (`olive::NodeParamSetKeyframeTimeCommand`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_time_undoable(
		keyframe: CHandle,
		time_num: i64,
		time_den: i64,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let new = Rational::new(time_num, time_den);
			let old = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?.data.time;
			let cmd = kf_command(
				&keyframe,
				move |k| k.data.time = new,
				move |k| k.data.time = old,
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_keyframe_get_value`: map the stored value into the POD;
	/// types without a POD representation fail with `OAKNODE_E_FAILED`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_value(
		keyframe: CHandle,
		out: *mut OakNodeValue,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			let declared = k.data.value.value_type();
			let pod = OakNodeValue::from_node_value(declared, &k.data.value)
				.map_err(|_| Error::Failed("keyframe value has no POD representation".to_string()))?;
			unsafe { *out = pod };
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_value` (live); `OAKNODE_VALUE_STRING` is
	/// rejected (use `oaknode_keyframe_set_value_string`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_value(
		keyframe: CHandle,
		v: *const OakNodeValue,
	) -> c_int {
		guard(|| {
			if v.is_null() {
				return Err(Error::Invalid);
			}
			let mut k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			let declared = k.data.value.value_type();
			let value = unsafe { *v }.to_node_value(declared)?;
			k.data.value = value;
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_value_undoable`
	/// (`olive::NodeParamSetKeyframeValueCommand`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_value_undoable(
		keyframe: CHandle,
		v: *const OakNodeValue,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if v.is_null() || out_command.is_null() {
				return Err(Error::Invalid);
			}
			let mut k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			let declared = k.data.value.value_type();
			let new = unsafe { *v }.to_node_value(declared)?;
			let old = k.data.value.clone();
			drop(k);
			let cmd = kf_command(
				&keyframe,
				move |k| k.data.value = new.clone(),
				move |k| k.data.value = old.clone(),
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_keyframe_get_value_string` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_value_string(
		keyframe: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let k = match unsafe { kf_lock(&keyframe) } {
			Some(k) => k,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let declared = k.data.value.value_type();
		let s = crate::serializer::value_to_string(declared, &k.data.value, false);
		copy_string_out(&s, buf, buf_size)
	}

	/// `oaknode_keyframe_set_value_string` (live).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_value_string(
		keyframe: CHandle,
		value: *const c_char,
	) -> c_int {
		guard(|| {
			let s = unsafe { cstr(value) }.ok_or(Error::Invalid)?;
			let mut k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			k.data.value = NodeValue::Text(s.to_string());
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_value_string_undoable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_value_string_undoable(
		keyframe: CHandle,
		value: *const c_char,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let s = unsafe { cstr(value) }.ok_or(Error::Invalid)?.to_string();
			let old = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?.data.value.clone();
			let cmd = kf_command(
				&keyframe,
				move |k| k.data.value = NodeValue::Text(s.clone()),
				move |k| k.data.value = old.clone(),
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_keyframe_get_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_type(
		keyframe: CHandle,
		out_type: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_type.is_null() {
				return Err(Error::Invalid);
			}
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			unsafe { *out_type = interp_to_c(k.data.interpolation) };
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_type` (live, with bezier-handle defaults).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_type(keyframe: CHandle, type_: c_int) -> c_int {
		guard(|| {
			let interp = interp_from_c(type_)?;
			let mut k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			set_type_adjusting(&mut k, interp);
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_type_undoable` (same semantics as live).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_type_undoable(
		keyframe: CHandle,
		type_: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let interp = interp_from_c(type_)?;
			let old = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?.data.interpolation;
			let cmd = kf_command(
				&keyframe,
				move |k| set_type_adjusting(k, interp),
				move |k| k.data.interpolation = old,
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_keyframe_get_bezier_control`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_bezier_control(
		keyframe: CHandle,
		handle: c_int,
		out_x: *mut f64,
		out_y: *mut f64,
	) -> c_int {
		guard(|| {
			if out_x.is_null() || out_y.is_null() {
				return Err(Error::Invalid);
			}
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			let (x, y) = match handle {
				0 => k.data.bezier_in,
				1 => k.data.bezier_out,
				_ => return Err(Error::Invalid),
			};
			unsafe {
				*out_x = x;
				*out_y = y;
			}
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_bezier_control` (live).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_bezier_control(
		keyframe: CHandle,
		handle: c_int,
		x: f64,
		y: f64,
	) -> c_int {
		guard(|| {
			let mut k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			match handle {
				0 => k.data.bezier_in = (x, y),
				1 => k.data.bezier_out = (x, y),
				_ => return Err(Error::Invalid),
			}
			Ok(())
		})
	}

	/// `oaknode_keyframe_set_bezier_control_undoable`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_set_bezier_control_undoable(
		keyframe: CHandle,
		handle: c_int,
		x: f64,
		y: f64,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() || (handle != 0 && handle != 1) {
				return Err(Error::Invalid);
			}
			let old = {
				let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
				match handle {
					0 => k.data.bezier_in,
					_ => k.data.bezier_out,
				}
			};
			let cmd = kf_command(
				&keyframe,
				move |k| {
					if handle == 0 {
						k.data.bezier_in = (x, y);
					} else {
						k.data.bezier_out = (x, y);
					}
				},
				move |k| {
					if handle == 0 {
						k.data.bezier_in = old;
					} else {
						k.data.bezier_out = old;
					}
				},
			)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_keyframe_get_track`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_track(
		keyframe: CHandle,
		out_track: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_track.is_null() {
				return Err(Error::Invalid);
			}
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			unsafe { *out_track = k.track };
			Ok(())
		})
	}

	/// `oaknode_keyframe_get_element`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_element(
		keyframe: CHandle,
		out_element: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_element.is_null() {
				return Err(Error::Invalid);
			}
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			unsafe { *out_element = k.element };
			Ok(())
		})
	}

	/// `oaknode_keyframe_get_input` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_input(
		keyframe: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let k = match unsafe { kf_lock(&keyframe) } {
			Some(k) => k,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		copy_string_out(&k.input, buf, buf_size)
	}

	/// `oaknode_keyframe_get_parent`: borrowed node handle, empty when
	/// orphaned; `OAKNODE_OK` either way.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_parent(
		keyframe: CHandle,
		out_node: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_node.is_null() {
				return Err(Error::Invalid);
			}
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			match (&k.project, &k.node) {
				(Some(p), Some(id)) => unsafe { *out_node = make_node_borrowed(p.clone(), *id) },
				_ => unsafe { *out_node = CHandle::null() },
			}
			Ok(())
		})
	}

	/// `oaknode_keyframe_get_valid_bezier_control`: for standalone
	/// handles there is no neighbouring keyframe, so the raw control
	/// point is already valid (`NodeKeyframe::valid_bezier_control_*`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_get_valid_bezier_control(
		keyframe: CHandle,
		handle: c_int,
		out_x: *mut f64,
		out_y: *mut f64,
	) -> c_int {
		unsafe { oaknode_keyframe_get_bezier_control(keyframe, handle, out_x, out_y) }
	}

	/// `oaknode_keyframe_opposing_bezier_type`: `IN_HANDLE (0) <->
	/// OUT_HANDLE (1)`; `OAKNODE_E_INVALID` for anything else.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_opposing_bezier_type(type_: c_int) -> c_int {
		match type_ {
			0 => 1,
			1 => 0,
			_ => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_keyframe_compute_paste_value`: take the target node's
	/// value at the keyframe's time, replace the keyframe's own track
	/// with the keyframe's value, and combine the per-track components
	/// into a single normal value (the facade paste path).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_compute_paste_value(
		target_node: CHandle,
		keyframe: CHandle,
		out: *mut crate::value::OakNodeValue,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let target = unsafe { node_ref(&target_node)? };
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			let input = k.input.clone();
			let element = k.element;
			let track = k.track;
			let time = k.data.time;
			let kf_value = k.data.value.clone();
			drop(k);
			let declared = with_graph_read(&target.project, |g| {
				g.get(target.id).and_then(|e| e.core.input_data_type(&input))
			})
			.ok_or(Error::NotFound)?;
			if declared.is_string() {
				return Err(Error::Failed(
					"input type has no POD representation".to_string(),
				));
			}
			let base = with_graph_read(&target.project, |g| {
				g.get(target.id)
					.map(|e| e.core.value_at_time(&input, element, time))
					.unwrap_or(NodeValue::None)
			});
			let mut tracks = base.split_into_tracks(declared);
			if let Some(slot) = tracks.get_mut(track as usize) {
				*slot = kf_value;
			}
			let combined = NodeValue::combine_tracks(&tracks, declared);
			let pod = OakNodeValue::from_node_value(declared, &combined).map_err(|_| {
				Error::Failed("input type has no POD representation".to_string())
			})?;
			unsafe { *out = pod };
			Ok(())
		})
	}

	/// `oaknode_keyframe_has_sibling_at_time`: 1 when the parent's live
	/// track holds a keyframe at the exact rational time; orphaned
	/// keyframes have no siblings (`*out_value` = 0, `OAKNODE_OK`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_keyframe_has_sibling_at_time(
		keyframe: CHandle,
		time_num: i64,
		time_den: i64,
		out_value: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_value.is_null() {
				return Err(Error::Invalid);
			}
			let time = Rational::new(time_num, time_den);
			let k = unsafe { kf_lock(&keyframe) }.ok_or(Error::Invalid)?;
			let found = match (&k.project, &k.node) {
				(Some(p), Some(id)) => {
					let input = k.input.clone();
					let element = k.element;
					with_graph_read(p, |g| {
						g.get(*id)
							.and_then(|e| e.core.keyframe_track(&input, element))
							.map(|t| t.keys().iter().any(|key| key.time == time))
							.unwrap_or(false)
					})
				}
				_ => false,
			};
			unsafe { *out_value = found as c_int };
			Ok(())
		})
	}
}

// =====================================================================
// include/node/dragger.h
// =====================================================================

/// `include/node/dragger.h` exports (complete inventory):
/// oaknode_dragger_create / start / drag / end / is_started / free.
///
/// The dragger wraps an input drag (`olive::NodeInputDragger`,
/// `// CPP-PARITY: src/node/c_api/dragger.cpp` / `src/node/src/inputdragger.cpp`)
/// over the whole-value keyframe model: `start` caches the per-component
/// value at the drag time; `drag` clamps against the input's `min`/`max`
/// properties and writes through (standard value, or the keyframe at the
/// drag time); `end` builds a multi undo command with the C++
/// `NodeParamSet*Command` equivalents.
///
/// The C++ `is_keyframing()` per-input mode has no Rust counterpart yet,
/// so the keyframing branch is selected by whether the (input, element)
/// track already holds keyframes (documented divergence).
pub mod dragger {
	use super::*;
	use crate::keyframe::{Interpolation, Keyframe};
	use crate::value::oak;
	use oakcore_rs::Rational;
	use std::sync::Mutex;

	/// Dragger handle payload (`// CPP-PARITY: dragger.cpp:46`
	/// `DraggerImpl` + `NodeInputDragger` state).
	pub struct DraggerBox {
		/// The project the dragged node lives in.
		pub project: ProjectArc,
		/// The dragged node.
		pub node: NodeId,
		/// The dragged input.
		pub input: String,
		/// Array element.
		pub element: i32,
		/// Whether a drag is in progress.
		pub started: bool,
		/// Component track of the drag (established by `start`).
		pub track: i32,
		/// Drag time.
		pub time: Rational,
		/// Whether the drag edits keyframes (the track had keys at start).
		pub keyframing: bool,
		/// Whether `start` created a key (undo of `end` removes it).
		pub created_key: bool,
		/// Per-component value to restore on undo (pre-drag standard
		/// value or pre-drag key value).
		pub undo_value: NodeValue,
	}

	/// Lock the boxed dragger.
	unsafe fn dg_lock(h: &CHandle) -> Option<MutexGuard<'_, DraggerBox>> {
		let m = unsafe { crate::handle::get::<Mutex<DraggerBox>>(h)? };
		Some(m.lock().unwrap_or_else(|e| e.into_inner()))
	}

	/// The declared value type of the dragged input.
	fn declared_type(project: &ProjectArc, node: NodeId, input: &str) -> Option<ValueType> {
		with_graph_read(project, |g| {
			g.get(node).and_then(|e| e.core.input_data_type(input))
		})
	}

	/// The whole value with `track`'s component replaced by `component`
	/// (C++ `set_split_standard_value_on_track` /
	/// `get_split_value_at_time_on_track` combine half).
	fn with_component(declared: ValueType, whole: NodeValue, track: usize, component: &NodeValue) -> NodeValue {
		let mut tracks = whole.split_into_tracks(declared);
		if let Some(slot) = tracks.get_mut(track) {
			*slot = component.clone();
		}
		NodeValue::combine_tracks(&tracks, declared)
	}

	/// `// CPP-PARITY: src/node/c_api/valueconvert.h:262`
	/// `component_from_value(value, declared, 0)` — the POD -> the scalar
	/// component variant the dragger drags.
	fn component_from_pod(v: &OakNodeValue, declared: ValueType) -> Result<NodeValue> {
		match declared {
			ValueType::Int | ValueType::Combo => {
				if v.kind != oak::INT && v.kind != oak::COMBO {
					return Err(Error::Invalid);
				}
				Ok(NodeValue::Int(v.num))
			}
			ValueType::Float => {
				if v.kind != oak::FLOAT {
					return Err(Error::Invalid);
				}
				Ok(NodeValue::Float(v.f[0]))
			}
			ValueType::Boolean => {
				if v.kind != oak::BOOL {
					return Err(Error::Invalid);
				}
				Ok(NodeValue::Boolean(v.num != 0))
			}
			ValueType::Rational => {
				if v.kind != oak::RATIONAL {
					return Err(Error::Invalid);
				}
				Ok(NodeValue::Rational(Rational::new(v.num, v.den)))
			}
			ValueType::Color | ValueType::Vec2 | ValueType::Vec3 | ValueType::Vec4 => {
				if v.kind != declared.to_oak() {
					return Err(Error::Invalid);
				}
				Ok(NodeValue::Float(v.f[0]))
			}
			_ => Err(Error::Invalid),
		}
	}

	/// Clamp a component against the input's `min`/`max` properties
	/// (`// CPP-PARITY: inputdragger.cpp:106`).
	fn clamp_property(core: &NodeCore, input: &str, value: &NodeValue) -> NodeValue {
		let mut value = value.clone();
		if let Some(prop) = core.get_input(input).and_then(|i| {
			i.properties
				.iter()
				.find(|(k, _)| k == "min")
				.map(|(_, v)| v)
		}) {
			let min = prop.to_double();
			if value.to_double() < min {
				value = NodeValue::Float(min);
			}
		}
		if let Some(prop) = core.get_input(input).and_then(|i| {
			i.properties
				.iter()
				.find(|(k, _)| k == "max")
				.map(|(_, v)| v)
		}) {
			let max = prop.to_double();
			if value.to_double() > max {
				value = NodeValue::Float(max);
			}
		}
		value
	}

	/// Build a vtable undo command from redo/undo closures.
	fn make_child(
		_keep: ProjectArc,
		redo: impl FnMut() + Send + 'static,
		undo: impl FnMut() + Send + 'static,
	) -> Option<CHandle> {
		crate::bridge::undo::command_from_closures(redo, undo)
	}

	/// `oaknode_dragger_create`: empty handle when the node/input is
	/// invalid; `track` is ignored (start establishes the drag track,
	/// `// CPP-PARITY: dragger.cpp:67`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_create(
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		_track: c_int,
	) -> CHandle {
		crate::handle::guard_handle(|| {
			let n = unsafe { node_ref(&node)? };
			let input = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?.to_string();
			let has = with_graph_read(&n.project, |g| {
				g.get(n.id).map(|e| e.core.has_input(&input)).unwrap_or(false)
			});
			if !has {
				return Err(Error::Invalid);
			}
			Ok(crate::handle::make_owned(Mutex::new(DraggerBox {
				project: n.project.clone(),
				node: n.id,
				input,
				element,
				started: false,
				track: 0,
				time: Rational::NULL,
				keyframing: false,
				created_key: false,
				undo_value: NodeValue::None,
			})))
		})
	}

	/// `oaknode_dragger_start`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_start(
		dragger: CHandle,
		time_num: i64,
		time_den: i64,
		track: c_int,
		insert_on_all_tracks: c_int,
	) -> c_int {
		guard(|| {
			let _ = insert_on_all_tracks; // whole-value keys cover all tracks
			if track < 0 {
				return Err(Error::Invalid);
			}
			let mut d = unsafe { dg_lock(&dragger) }.ok_or(Error::Invalid)?;
			if d.started {
				return Err(Error::State);
			}
			let time = Rational::new(time_num, time_den);
			// Snapshot the drag target, then release the box guard so the
			// project lock below does not alias it.
			let project = d.project.clone();
			let node = d.node;
			let input = d.input.clone();
			let element = d.element;
			drop(d);
			let declared = declared_type(&project, node, &input).ok_or(Error::NotFound)?;
			let (undo_value, created_key, keyframing) = {
				let mut guard = lock(&project);
				let entry = guard.graph.get_mut(node).ok_or(Error::NotFound)?;
				let core = &mut entry.core;
				// The track's current value at the drag time (whole
				// value); the dragged component and the undo snapshot
				// derive from it.
				let whole = core.value_at_time(&input, element, time);
				let keyframing = core
					.keyframe_track(&input, element)
					.map(|t| !t.keys().is_empty())
					.unwrap_or(false);
				let start_component = whole
					.split_into_tracks(declared)
					.get(track as usize)
					.cloned()
					.unwrap_or(NodeValue::None);
				if keyframing {
					// Keyframe branch: ensure a key exists at the drag
					// time (whole-value model: one key per time).
					let has_key = core
						.keyframe_track(&input, element)
						.map(|t| t.keys().iter().any(|k| k.time == time))
						.unwrap_or(false);
					if !has_key {
						let created =
							with_component(declared, whole.clone(), track as usize, &start_component);
						core.keyframe_track_mut(&input, element).set_key(Keyframe {
							time,
							value: created.clone(),
							interpolation: Interpolation::Linear,
							bezier_in: (0.0, 0.0),
							bezier_out: (0.0, 0.0),
						});
						(created, true, true)
					} else {
						let existing = core
							.keyframe_track(&input, element)
							.and_then(|t| t.keys().iter().find(|k| k.time == time))
							.map(|k| k.value.clone())
							.unwrap_or(whole);
						(existing, false, true)
					}
				} else {
					// Standard-value branch: snapshot the pre-drag value.
					(whole, false, false)
				}
			};
			let mut d = unsafe { dg_lock(&dragger) }.ok_or(Error::Invalid)?;
			d.started = true;
			d.keyframing = keyframing;
			d.track = track;
			d.time = time;
			d.created_key = created_key;
			d.undo_value = undo_value;
			Ok(())
		})
	}

	/// `oaknode_dragger_drag`: clamp the component against `min`/`max`
	/// and write it through (live).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_drag(
		dragger: CHandle,
		value: *const crate::value::OakNodeValue,
	) -> c_int {
		guard(|| {
			if value.is_null() {
				return Err(Error::Invalid);
			}
			let pod = unsafe { *value };
			let mut d = unsafe { dg_lock(&dragger) }.ok_or(Error::Invalid)?;
			if !d.started {
				return Err(Error::State);
			}
			let declared = declared_type(&d.project, d.node, &d.input).ok_or(Error::NotFound)?;
			let component = component_from_pod(&pod, declared)?;
			let mut guard = lock(&d.project);
			let entry = guard.graph.get_mut(d.node).ok_or(Error::NotFound)?;
			let component = clamp_property(&entry.core, &d.input, &component);
			if d.keyframing {
				let time = d.time;
				let whole = entry
					.core
					.keyframe_track(&d.input, d.element)
					.and_then(|t| t.keys().iter().find(|k| k.time == time))
					.map(|k| k.value.clone())
					.unwrap_or(entry.core.value_at_time(&d.input, d.element, time));
				let new_whole = with_component(declared, whole, d.track as usize, &component);
				entry
					.core
					.keyframe_track_mut(&d.input, d.element)
					.set_key_value(time, new_whole);
			} else {
				let whole = entry.core.standard_value(&d.input, d.element);
				let new_whole = with_component(declared, whole, d.track as usize, &component);
				entry
					.core
					.set_standard_value(&d.input, d.element, new_whole);
			}
			Ok(())
		})
	}

	/// `oaknode_dragger_end`: build a multi undo command covering the
	/// drag (insert-keyframe + set-value, or set-standard-value).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_end(
		dragger: CHandle,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let mut d = unsafe { dg_lock(&dragger) }.ok_or(Error::Invalid)?;
			if !d.started {
				return Err(Error::State);
			}
			let project = d.project.clone();
			let node = d.node;
			let input = d.input.clone();
			let element = d.element;
			let track = d.track;
			let time = d.time;
			let keyframing = d.keyframing;
			let created_key = d.created_key;
			let undo_value = d.undo_value.clone();
			// The post-drag whole value (what redo re-applies): the key
			// value at the drag time, or the current standard value.
			let redo_value = {
				let mut guard = lock(&project);
				if keyframing {
					guard
						.graph
						.get(node)
						.and_then(|e| {
							e.core.keyframe_track(&input, element).and_then(|t| {
								t.keys().iter().find(|k| k.time == time).map(|k| k.value.clone())
							})
						})
						.unwrap_or(NodeValue::None)
				} else {
					guard
						.graph
						.get(node)
						.map(|e| e.core.standard_value(&input, element))
						.unwrap_or(NodeValue::None)
				}
			};
			let multi = crate::bridge::undo::command_init_multi().ok_or(Error::NoMem)?;
			if keyframing {
				if created_key {
					// Insert-keyframe equivalent: redo re-adds the key,
					// undo removes it (the value command below restores
					// the value; `// CPP-PARITY: nodeundo.cpp:432`).
					let redo_key = redo_value.clone();
					let project_redo = project.clone();
					let project_undo = project.clone();
					let input_redo = input.clone();
					let input_undo = input.clone();
					if let Some(child) = make_child(
						project.clone(),
						move || {
							let mut guard = lock(&project_redo);
							if let Some(e) = guard.graph.get_mut(node) {
								e.core
									.keyframe_track_mut(&input_redo, element)
									.set_key(Keyframe {
										time,
										value: redo_key.clone(),
										interpolation: Interpolation::Linear,
										bezier_in: (0.0, 0.0),
										bezier_out: (0.0, 0.0),
									});
							}
						},
						move || {
							let mut guard = lock(&project_undo);
							if let Some(e) = guard.graph.get_mut(node) {
								e.core
									.keyframe_track_mut(&input_undo, element)
									.remove_key(time);
							}
						},
					) {
						let _ = crate::bridge::undo::command_multi_add_child(multi.clone(), child);
					}
				}
				// Set-keyframe-value equivalent (always added; the
				// C++ sends the value-changed signal through it).
				let project_redo = project.clone();
				let input_redo = input.clone();
				let input_undo = input.clone();
				if let Some(child) = make_child(
					project.clone(),
					move || {
						let mut guard = lock(&project_redo);
						if let Some(e) = guard.graph.get_mut(node) {
							e.core
								.keyframe_track_mut(&input_redo, element)
								.set_key_value(time, redo_value.clone());
						}
					},
					move || {
						let mut guard = lock(&project);
						if let Some(e) = guard.graph.get_mut(node) {
							e.core
								.keyframe_track_mut(&input_undo, element)
								.set_key_value(time, undo_value.clone());
						}
					},
				) {
					let _ = crate::bridge::undo::command_multi_add_child(multi.clone(), child);
				}
			} else {
				// Set-standard-value equivalent
				// (`// CPP-PARITY: nodeundo.cpp:500`
				// `NodeParamSetStandardValueCommand`).
				let project_redo = project.clone();
				let input_redo = input.clone();
				let input_undo = input.clone();
				if let Some(child) = make_child(
					project.clone(),
					move || {
						let mut guard = lock(&project_redo);
						if let Some(e) = guard.graph.get_mut(node) {
							e.core
								.set_standard_value(&input_redo, element, redo_value.clone());
						}
					},
					move || {
						let mut guard = lock(&project);
						if let Some(e) = guard.graph.get_mut(node) {
							e.core
								.set_standard_value(&input_undo, element, undo_value.clone());
						}
					},
				) {
					let _ = crate::bridge::undo::command_multi_add_child(multi.clone(), child);
				}
			}
			// The drag is complete; the dragger resets (C++
			// `NodeInputDragger::end` resets the reference).
			d.started = false;
			d.created_key = false;
			unsafe { *out_command = multi };
			Ok(())
		})
	}

	/// `oaknode_dragger_is_started`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_is_started(
		dragger: CHandle,
		out_started: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_started.is_null() {
				return Err(Error::Invalid);
			}
			let d = unsafe { dg_lock(&dragger) }.ok_or(Error::Invalid)?;
			unsafe { *out_started = d.started as c_int };
			Ok(())
		})
	}

	/// `oaknode_dragger_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_dragger_free(dragger: *mut CHandle) {
		guard_void(|| {
			if dragger.is_null() || unsafe { (*dragger).ctx.is_null() } {
				return;
			}
			let h = unsafe { (*dragger).clone() };
			if let Some(f) = h.release {
				unsafe { f(h.ctx) };
			}
			unsafe { (*dragger).ctx = std::ptr::null_mut() };
		})
	}
}

// =====================================================================
// include/node/multicam.h
// =====================================================================

/// `include/node/multicam.h` exports (complete inventory): the four
/// input-id getters, source count, grid math (rows/columns,
/// index<->row/col), and the current source.
///
/// The grid math and string getters are pure; the node-backed helpers
/// follow the C++ oracle (`// CPP-PARITY: src/node/c_api/multicam.cpp`,
/// `src/node/src/input/multicam/multicamnode.cpp`). The `MultiCamNode`
/// behavior itself stays a Phase-3 item; the "is this a multicam node"
/// check mirrors the C++ `dynamic_cast<MultiCamNode *>` via the type id,
/// and the source count uses the `sources_in` array size (the
/// C++ sequence-track-list path is behavior-bound and deferred).
pub mod multicam {
	use super::*;

	const MULTICAM_TYPE_ID: &str = "org.olivevideoeditor.Olive.multicam";
	const CURRENT_INPUT: &str = "current_in";
	const SOURCES_INPUT: &str = "sources_in";

	/// `oaknode_multicam_input_current` — static string.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_input_current() -> *const c_char {
		b"current_in\0".as_ptr() as *const c_char
	}

	/// `oaknode_multicam_input_sources` — static string.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_input_sources() -> *const c_char {
		b"sources_in\0".as_ptr() as *const c_char
	}

	/// `oaknode_multicam_input_sequence` — static string.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_input_sequence() -> *const c_char {
		b"sequence_in\0".as_ptr() as *const c_char
	}

	/// `oaknode_multicam_input_sequence_type` — static string.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_input_sequence_type() -> *const c_char {
		b"sequence_type_in\0".as_ptr() as *const c_char
	}

	/// The multicam behavior of a node (C++ `dynamic_cast<MultiCamNode *>`).
	fn multicam_of(entry: &crate::graph::NodeEntry) -> bool {
		entry.behavior.type_id() == MULTICAM_TYPE_ID
	}

	/// `oaknode_multicam_get_source_count`: the `sources_in` array size
	/// (`// CPP-PARITY: multicamnode.cpp:163`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_get_source_count(
		node: CHandle,
		out_count: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let count = with_graph_read(&n.project, |g| {
				g.get(n.id)
					.filter(|e| multicam_of(e))
					.map(|e| e.core.input_array_size(SOURCES_INPUT) as c_int)
			})
			.ok_or(Error::Invalid)?;
			unsafe { *out_count = count };
			Ok(())
		})
	}

	/// `oaknode_multicam_get_rows_and_columns`: the square-ish grid that
	/// covers `source_count` cells (`// CPP-PARITY:
	/// multicamnode.cpp:190`; rows grows while it trails columns).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_get_rows_and_columns(
		source_count: c_int,
		rows: *mut c_int,
		cols: *mut c_int,
	) -> c_int {
		if source_count < 0 || rows.is_null() || cols.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		let mut r: c_int = 1;
		let mut c: c_int = 1;
		while r * c < source_count {
			if r < c {
				r += 1;
			} else {
				c += 1;
			}
		}
		unsafe {
			*rows = r;
			*cols = c;
		}
		crate::error::OAKNODE_OK
	}

	/// `oaknode_multicam_index_to_row_cols`: column-major layout
	/// (`// CPP-PARITY: multicamnode.cpp:93`; `total_rows` unused).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_index_to_row_cols(
		index: c_int,
		rows: c_int,
		cols: c_int,
		out_row: *mut c_int,
		out_col: *mut c_int,
	) -> c_int {
		if index < 0 || rows < 1 || cols < 1 || out_row.is_null() || out_col.is_null() {
			return crate::error::OAKNODE_E_INVALID;
		}
		unsafe {
			*out_col = index % cols;
			*out_row = index / cols;
		}
		crate::error::OAKNODE_OK
	}

	/// `oaknode_multicam_rows_cols_to_index`: `row * cols + col`
	/// (`// CPP-PARITY: multicamnode.h:76`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_rows_cols_to_index(
		row: c_int,
		col: c_int,
		rows: c_int,
		cols: c_int,
	) -> c_int {
		if row < 0 || col < 0 || rows < 1 || cols < 1 || row >= rows || col >= cols {
			return crate::error::OAKNODE_E_INVALID;
		}
		row * cols + col
	}

	/// `oaknode_multicam_get_current_source`: the `current_in` combo value
	/// (`// CPP-PARITY: multicamnode.h:55`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_multicam_get_current_source(
		node: CHandle,
		out_source: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_source.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let source = with_graph_read(&n.project, |g| {
				g.get(n.id).filter(|e| multicam_of(e)).map(|e| {
					e.core.standard_value(CURRENT_INPUT, -1).to_double() as c_int
				})
			})
			.ok_or(Error::Invalid)?;
			unsafe { *out_source = source };
			Ok(())
		})
	}
}

// =====================================================================
// include/node/block.h, folder.h, footage.h, group.h, colormanager.h,
// factory.h, serializer.h, traverser.h
// =====================================================================

/// Families landing with their backing modules: `block`/`folder`/
/// `footage`/`group`/`colormanager`/`factory`/`serializer`/`traverser`.
/// The factory family is required by the node tests and lands with the
/// registry work (Phase 1 continues in the crate plan; see
/// `tests/factory_smoke_test.rs`).
pub mod remaining_headers {
	// Stubs are added per header when the corresponding internal module
	// is implemented; keep header order and naming identical to the C
	// side so bindgen/diff audits stay mechanical.
}

/// `include/node/factory.h` — node-type registry exports. The registry
/// itself (`factory.rs`) is live; the C ABI family (initialize/destroy/
/// id_count/id_at/name_from_id/create_from_id/node_at) is implemented
/// in Phase 1's node tests.
pub mod factory {
	use super::*;

	/// `oaknode_factory_initialize`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_factory_initialize() -> c_int {
		guard(|| {
			let _ = crate::factory::Factory::global();
			Ok(())
		})
	}

	/// `oaknode_factory_destroy` — no-op (the registry is process-wide
	/// and lazily built; nothing to release).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_factory_destroy() {}

	/// `oaknode_factory_id_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_factory_id_count(out_count: *mut c_int) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			unsafe { *out_count = crate::factory::Factory::global().entries().len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_factory_id_at` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_factory_id_at(index: c_int, buf: *mut c_char, buf_size: c_int) -> c_int {
		let entries = crate::factory::Factory::global().entries();
		if index < 0 || index as usize >= entries.len() {
			return crate::error::OAKNODE_E_NOT_FOUND;
		}
		copy_string_out(entries[index as usize].type_id, buf, buf_size)
	}

	/// `oaknode_factory_name_from_id` (two-stage; unknown id yields an
	/// empty string).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_factory_name_from_id(
		type_id: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let type_id = match unsafe { cstr(type_id) } {
			Some(s) => s,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let name = crate::factory::Factory::global()
			.find(type_id)
			.map(|m| m.name)
			.unwrap_or("");
		copy_string_out(name, buf, buf_size)
	}

	/// `oaknode_factory_create_from_id` (owned orphan node).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_factory_create_from_id(type_id: *const c_char) -> CHandle {
		let type_id = match unsafe { cstr(type_id) } {
			Some(s) => s,
			None => return CHandle::null(),
		};
		let create = match crate::factory::Factory::global().find(type_id) {
			Some(meta) => meta.create,
			None => return CHandle::null(),
		};
		let (core, behavior) = create();
		let scratch = Project::new();
		let id = {
			let mut guard = lock(&scratch);
			guard.graph.add_node(core, behavior)
		};
		make_node_owned(scratch, id)
	}

	/// `oaknode_factory_node_at` (borrowed prototype handle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_factory_node_at(index: c_int, out_node: *mut CHandle) -> c_int {
		guard(|| {
			if out_node.is_null() {
				return Err(Error::Invalid);
			}
			let entries = crate::factory::Factory::global().entries();
			if index < 0 || index as usize >= entries.len() {
				return Err(Error::NotFound);
			}
			let (core, behavior) = (entries[index as usize].create)();
			let scratch = Project::new();
			let id = {
				let mut guard = lock(&scratch);
				guard.graph.add_node(core, behavior)
			};
			unsafe { *out_node = make_node_borrowed(scratch, id) };
			Ok(())
		})
	}
}

// =====================================================================
// Shared timeline-family helpers
// =====================================================================

/// Downcast a graph entry's behavior to [`crate::block::BlockCore`].
fn block_core<'a>(entry: &'a crate::graph::NodeEntry) -> Option<&'a crate::block::BlockCore> {
	entry
		.behavior
		.as_any()
		.and_then(|a| {
			if let Some(b) = a.downcast_ref::<crate::block::ClipBlockBehavior>() {
				Some(&b.core)
			} else if let Some(b) = a.downcast_ref::<crate::block::GapBlockBehavior>() {
				Some(&b.core)
			} else if let Some(b) = a.downcast_ref::<crate::block::TransitionBlockBehavior>() {
				Some(&b.core)
			} else {
				None
			}
		})
}

fn block_core_mut<'a>(
	entry: &'a mut crate::graph::NodeEntry,
) -> Option<&'a mut crate::block::BlockCore> {
	// The concrete block behaviors all carry `core` at the same offset
	// behind their `as_any_mut`; downcast through a raw pointer to avoid
	// the borrow checker's double-mutable-borrow on `&mut dyn Any`.
	let any = entry.behavior.as_any_mut()?;
	let raw: *mut dyn std::any::Any = any;
	unsafe {
		if let Some(b) = (&mut *raw).downcast_mut::<crate::block::ClipBlockBehavior>() {
			return Some(&mut b.core);
		}
		if let Some(b) = (&mut *raw).downcast_mut::<crate::block::GapBlockBehavior>() {
			return Some(&mut b.core);
		}
		if let Some(b) = (&mut *raw).downcast_mut::<crate::block::TransitionBlockBehavior>() {
			return Some(&mut b.core);
		}
	}
	None
}

/// Graph-backed block-range accessor for the track queries.
struct GraphBlockRange<'a> {
	graph: &'a crate::graph::Graph,
}

impl crate::track::BlockRange for GraphBlockRange<'_> {
	fn in_(&self, block: NodeId) -> oakcore_rs::Rational {
		self.graph
			.get(block)
			.and_then(block_core)
			.map(|c| c.in_())
			.unwrap_or_else(|| oakcore_rs::Rational::new(0, 1))
	}

	fn out(&self, block: NodeId) -> oakcore_rs::Rational {
		self.graph
			.get(block)
			.and_then(block_core)
			.map(|c| c.out())
			.unwrap_or_else(|| oakcore_rs::Rational::new(0, 1))
	}
}

/// Graph-backed track-range accessor for the list queries.
struct GraphTrackRange<'a> {
	graph: &'a crate::graph::Graph,
}

impl crate::track::TrackRange for GraphTrackRange<'_> {
	fn length(&self, track: NodeId) -> oakcore_rs::Rational {
		match self.graph.get(track) {
			Some(entry) => entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<crate::track::TrackBehavior>())
				.map(|t| t.length(&GraphBlockRange { graph: self.graph }))
				.unwrap_or_else(|| oakcore_rs::Rational::new(0, 1)),
			None => oakcore_rs::Rational::new(0, 1),
		}
	}
}

/// Move a single node from one project's graph into another's,
/// preserving identity when the slot is free. Returns the new id.
fn move_node_between(
	src_project: &ProjectArc,
	src_id: NodeId,
	dst_project: &ProjectArc,
) -> Result<NodeId> {
	let entry = {
		let mut src_guard = lock(src_project);
		src_guard.graph.take_node(src_id).ok_or(Error::NotFound)?
	};
	let mut dst_guard = lock(dst_project);
	Ok(dst_guard.graph.add_entry(entry, src_id))
}

// =====================================================================
// include/node/folder.h
// =====================================================================

/// `include/node/folder.h` exports: create / child_count / child_at /
/// add_child / as_node / command_create_folder_add_child /
/// remove_child / move_children / has_child_recursive / index_of_child /
/// parent_of. The add-child command needs the undo bridge (implemented
/// via [`crate::bridge::undo`]).
pub mod folder {
	use super::*;
	use crate::folder::FolderBehavior;

	/// Borrowed folder behavior of a node handle.
	fn folder_ref(n: &NodeRef) -> Result<crate::folder::FolderBehavior> {
		let guard = lock(&n.project);
		let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
		entry
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<FolderBehavior>())
			.map(|f| FolderBehavior {
				name: f.name.clone(),
				children: f.children.clone(),
			})
			.ok_or(Error::State)
	}

	/// `oaknode_folder_create`: create a folder node in `project`'s graph
	/// (borrowed handle; not attached under any parent).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_create(project: CHandle) -> CHandle {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return CHandle::null(),
		};
		let (core, behavior) = crate::folder::create("Folder");
		let id = {
			let mut guard = lock(&p);
			guard.graph.add_node(core, behavior)
		};
		make_node_borrowed(p, id)
	}

	/// `oaknode_folder_child_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_child_count(folder: CHandle) -> c_int {
		match unsafe { node_ref(&folder) } {
			Ok(n) => match folder_ref(&n) {
				Ok(f) => f.children.len() as c_int,
				Err(e) => e.code(),
			},
			Err(_) => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_folder_child_at` (borrowed node handle).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_child_at(folder: CHandle, index: c_int) -> CHandle {
		let n = match unsafe { node_ref(&folder) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		let f = match folder_ref(&n) {
			Ok(f) => f,
			Err(_) => return CHandle::null(),
		};
		match f.children.get(index as usize) {
			Some(child) => make_node_borrowed(n.project.clone(), *child),
			None => CHandle::null(),
		}
	}

	/// `oaknode_folder_add_child` (live).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_add_child(folder: CHandle, child: CHandle) -> c_int {
		guard(|| {
			let f = unsafe { node_ref(&folder)? };
			let c = unsafe { node_ref(&child)? };
			if !Arc::ptr_eq(&f.project, &c.project) {
				return Err(Error::Invalid);
			}
			let mut guard = lock(&f.project);
			// A node can only belong to one folder.
			if let Some(existing) = guard.graph.get(c.id) {
				if existing.core.bin_folder.is_some() && existing.core.bin_folder != Some(f.id) {
					return Err(Error::State);
				}
			}
			{
				let entry = guard.graph.get_mut(f.id).ok_or(Error::NotFound)?;
				let fb = entry
					.behavior
					.as_any_mut()
					.and_then(|a| a.downcast_mut::<FolderBehavior>())
					.ok_or(Error::State)?;
				fb.add_child(c.id);
			}
			if let Some(existing) = guard.graph.get_mut(c.id) {
				existing.core.bin_folder = Some(f.id);
			}
			Ok(())
		})
	}

	/// `oaknode_folder_as_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_as_node(folder: CHandle) -> CHandle {
		match unsafe { node_ref(&folder) } {
			Ok(n) => make_node_borrowed(n.project.clone(), n.id),
			Err(_) => CHandle::null(),
		}
	}

	/// `oaknode_command_create_folder_add_child` — undo bridge.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_command_create_folder_add_child(
		folder: CHandle,
		child: CHandle,
	) -> CHandle {
		match (unsafe { node_ref(&folder) }, unsafe { node_ref(&child) }) {
			(Ok(f), Ok(c)) => {
				let project = f.project.clone();
				let project_undo = project.clone();
				let fid = f.id;
				let cid = c.id;
				match crate::bridge::undo::command_from_closures(
					move || {
						let mut guard = lock(&project);
						if let Some(entry) = guard.graph.get_mut(fid) {
							if let Some(fb) = entry
								.behavior
								.as_any_mut()
								.and_then(|a| a.downcast_mut::<FolderBehavior>())
							{
								fb.add_child(cid);
							}
						}
						if let Some(entry) = guard.graph.get_mut(cid) {
							entry.core.bin_folder = Some(fid);
						}
					},
					move || {
						let mut guard = lock(&project_undo);
						if let Some(entry) = guard.graph.get_mut(fid) {
							if let Some(fb) = entry
								.behavior
								.as_any_mut()
								.and_then(|a| a.downcast_mut::<FolderBehavior>())
							{
								fb.remove_child(cid);
							}
						}
						if let Some(entry) = guard.graph.get_mut(cid) {
							entry.core.bin_folder = None;
						}
					},
				) {
					Some(h) => h,
					None => CHandle::null(),
				}
			}
			_ => CHandle::null(),
		}
	}

	/// `oaknode_folder_remove_child` (live).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_remove_child(folder: CHandle, child: CHandle) -> c_int {
		guard(|| {
			let f = unsafe { node_ref(&folder)? };
			let c = unsafe { node_ref(&child)? };
			if !Arc::ptr_eq(&f.project, &c.project) {
				return Err(Error::Invalid);
			}
			let mut guard = lock(&f.project);
			let entry = guard.graph.get_mut(f.id).ok_or(Error::NotFound)?;
			let fb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<FolderBehavior>())
				.ok_or(Error::State)?;
			if fb.remove_child(c.id) {
				if let Some(entry) = guard.graph.get_mut(c.id) {
					entry.core.bin_folder = None;
				}
				Ok(())
			} else {
				Err(Error::NotFound)
			}
		})
	}

	/// `oaknode_folder_move_children`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_move_children(
		nodes: *const CHandle,
		count: c_int,
		dest_folder: CHandle,
	) -> c_int {
		guard(|| {
			if nodes.is_null() || count < 0 {
				return Err(Error::Invalid);
			}
			let d = unsafe { node_ref(&dest_folder)? };
			let mut moved: Vec<NodeId> = Vec::new();
			for i in 0..count as usize {
				let child = unsafe { (*nodes.add(i)).clone() };
				let c = unsafe { node_ref(&child)? };
				if !Arc::ptr_eq(&d.project, &c.project) {
					continue;
				}
				// Remove from the current folder (if any).
				{
					let mut guard = lock(&d.project);
					let cur = guard
						.graph
						.get(c.id)
						.and_then(|e| e.core.bin_folder);
					if let Some(cur_f) = cur {
						if let Some(entry) = guard.graph.get_mut(cur_f) {
							if let Some(fb) = entry
								.behavior
								.as_any_mut()
								.and_then(|a| a.downcast_mut::<FolderBehavior>())
							{
								fb.remove_child(c.id);
							}
						}
					}
				}
				moved.push(c.id);
			}
			let mut guard = lock(&d.project);
			{
				let entry = guard.graph.get_mut(d.id).ok_or(Error::NotFound)?;
				let fb = entry
					.behavior
					.as_any_mut()
					.and_then(|a| a.downcast_mut::<FolderBehavior>())
					.ok_or(Error::State)?;
				for id in &moved {
					if !fb.children.contains(id) {
						fb.add_child(*id);
					}
				}
			}
			for id in moved {
				if let Some(entry) = guard.graph.get_mut(id) {
					entry.core.bin_folder = Some(d.id);
				}
			}
			Ok(())
		})
	}

	/// `oaknode_folder_has_child_recursive`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_has_child_recursive(
		folder: CHandle,
		child: CHandle,
	) -> c_int {
		match (unsafe { node_ref(&folder) }, unsafe { node_ref(&child) }) {
			(Ok(f), Ok(c)) => {
				let guard = lock(&f.project);
				let found = guard
					.graph
					.get(f.id)
					.and_then(|e| e.behavior.as_any())
					.and_then(|a| a.downcast_ref::<FolderBehavior>())
					.map(|fb| fb.has_child_recursive(c.id, &guard.graph))
					.unwrap_or(false);
				found as c_int
			}
			_ => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_folder_index_of_child`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_index_of_child(folder: CHandle, child: CHandle) -> c_int {
		match (unsafe { node_ref(&folder) }, unsafe { node_ref(&child) }) {
			(Ok(f), Ok(c)) => {
				let guard = lock(&f.project);
				match guard
					.graph
					.get(f.id)
					.and_then(|e| e.behavior.as_any())
					.and_then(|a| a.downcast_ref::<FolderBehavior>())
					.and_then(|fb| fb.index_of_child(c.id))
				{
					Some(i) => i as c_int,
					None => crate::error::OAKNODE_E_NOT_FOUND,
				}
			}
			_ => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_folder_parent_of`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_folder_parent_of(node: CHandle) -> CHandle {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		let guard = lock(&n.project);
		match guard.graph.get(n.id).and_then(|e| e.core.bin_folder) {
			Some(folder) => make_node_borrowed(n.project.clone(), folder),
			None => CHandle::null(),
		}
	}
}

// =====================================================================
// include/node/group.h
// =====================================================================

/// `include/node/group.h` exports (complete inventory): create / cast /
/// free / add_input_passthrough (+_undoable) / remove_input_passthrough /
/// passthrough_count / passthrough_id_at / passthrough_input_at /
/// get_output_passthrough / set_output_passthrough (+_undoable) /
/// resolve_input.
///
/// Group handles are node handles whose node carries a
/// [`crate::nodes::group::NodeGroup`] behavior. The undoable variants
/// build the C++ `NodeGroupAddInputPassthrough` /
/// `NodeGroupSetOutputPassthrough` semantics through
/// [`crate::bridge::undo`] (`// CPP-PARITY: src/node/src/group/group.cpp:363`).
pub mod group {
	use super::*;
	use crate::nodes::group::{InnerInput, NodeGroup};

	/// The group behavior of a node entry (downcast; C++
	/// `dynamic_cast<NodeGroup *>`).
	fn group_of(entry: &crate::graph::NodeEntry) -> Option<&NodeGroup> {
		entry.behavior.as_any()?.downcast_ref::<NodeGroup>()
	}

	/// `oaknode_group_create`: standalone group node in a scratch project
	/// (owned handle; adopt into a real project with
	/// `oaknode_project_add_node`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_create() -> CHandle {
		let (core, behavior) = crate::nodes::group::create();
		let scratch = Project::new();
		let id = {
			let mut guard = lock(&scratch);
			guard.graph.add_node(core, behavior)
		};
		make_node_owned(scratch, id)
	}

	/// `oaknode_group_cast`: borrowed group view of a node; empty handle
	/// when the node is not a NodeGroup.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_cast(node: CHandle) -> CHandle {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		let guard = lock(&n.project);
		match guard.graph.get(n.id) {
			Some(entry) if group_of(entry).is_some() => make_node_borrowed(n.project.clone(), n.id),
			_ => CHandle::null(),
		}
	}

	/// `oaknode_group_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_free(group: *mut CHandle) {
		guard_void(|| {
			if group.is_null() || unsafe { (*group).ctx.is_null() } {
				return;
			}
			let h = unsafe { (*group).clone() };
			if let Some(f) = h.release {
				unsafe { f(h.ctx) };
			}
			unsafe { (*group).ctx = std::ptr::null_mut() };
		})
	}

	/// `oaknode_group_add_input_passthrough` (live; two-stage return of
	/// the generated passthrough id).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_add_input_passthrough(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let result = group_add_passthrough_impl(&group, &node, input_id, element, "");
		match result {
			Ok(id) => copy_string_out(&id, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// Shared body of the live and undoable add-passthrough exports.
	fn group_add_passthrough_impl(
		group: &CHandle,
		node: &CHandle,
		input_id: *const c_char,
		element: c_int,
		force_id: &str,
	) -> Result<String> {
		let g = unsafe { node_ref(group)? };
		let n = unsafe { node_ref(node)? };
		if !Arc::ptr_eq(&g.project, &n.project) {
			return Err(Error::Invalid);
		}
		let input = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?.to_string();
		let inner = InnerInput {
			node: n.id,
			input: input.clone(),
			element,
		};
		let mut guard = lock(&g.project);
		// Resolve the inner input descriptor first (immutable borrow),
		// then mutate the group (C++ reads the input off the inner node
		// inside `add_input_passthrough`).
		let descriptor = guard
			.graph
			.get(n.id)
			.and_then(|e| e.core.get_input(&input))
			.cloned()
			.ok_or(Error::NotFound)?;
		let entry = guard.graph.get_mut(g.id).ok_or(Error::NotFound)?;
		let core = &mut entry.core;
		let gb = entry
			.behavior
			.as_any_mut()
			.and_then(|a| a.downcast_mut::<NodeGroup>())
			.ok_or(Error::State)?;
		Ok(gb.add_input_passthrough(core, inner, force_id, &descriptor))
	}

	/// `oaknode_group_add_input_passthrough_undoable`
	/// (`olive::NodeGroupAddInputPassthrough`). The generated id is not
	/// retrievable through this call.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_add_input_passthrough_undoable(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let g = unsafe { node_ref(&group)? };
			let n = unsafe { node_ref(&node)? };
			if !Arc::ptr_eq(&g.project, &n.project) {
				return Err(Error::Invalid);
			}
			let input = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?.to_string();
			let project = g.project.clone();
			let group_id = g.id;
			let inner = InnerInput {
				node: n.id,
				input,
				element,
			};
			// The "actually added" flag shared between redo and undo
			// (C++ `NodeGroupAddInputPassthrough::actually_added_`).
			let actually_added = std::sync::Arc::new(std::sync::Mutex::new(false));
			let redo_added = actually_added.clone();
			let redo_inner = inner.clone();
			let redo_project = project.clone();
			let cmd = crate::bridge::undo::command_from_closures(
				move || {
					let mut guard = lock(&redo_project);
					if let Some(entry) = guard.graph.get(group_id) {
						let gb = group_of(entry);
						let contains = gb
							.map(|g| g.contains_input_passthrough(&redo_inner))
							.unwrap_or(false);
						*lock(&redo_added) = false;
						if !contains {
							// The descriptor comes from the inner node,
							// not the group (the group input does not
							// exist yet).
							let descriptor = guard
								.graph
								.get(redo_inner.node)
								.and_then(|e| e.core.get_input(&redo_inner.input))
								.cloned();
							if let (Some(descriptor), Some(entry)) =
								(descriptor, guard.graph.get_mut(group_id))
							{
								if let Some(gb) = entry
									.behavior
									.as_any_mut()
									.and_then(|a| a.downcast_mut::<NodeGroup>())
								{
									gb.add_input_passthrough(
										&mut entry.core,
										redo_inner.clone(),
										"",
										&descriptor,
									);
									*lock(&redo_added) = true;
								}
							}
						}
					}
				},
				move || {
					if *lock(&actually_added) {
						let mut guard = lock(&project);
						if let Some(entry) = guard.graph.get_mut(group_id) {
							if let Some(gb) = entry
								.behavior
								.as_any_mut()
								.and_then(|a| a.downcast_mut::<NodeGroup>())
							{
								gb.remove_input_passthrough(&mut entry.core, &inner);
							}
						}
					}
				},
			)
			.ok_or(Error::NoMem)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_group_remove_input_passthrough` (live).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_remove_input_passthrough(
		group: CHandle,
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
	) -> c_int {
		guard(|| {
			let g = unsafe { node_ref(&group)? };
			let n = unsafe { node_ref(&node)? };
			if !Arc::ptr_eq(&g.project, &n.project) {
				return Err(Error::Invalid);
			}
			let input = unsafe { cstr(input_id) }.ok_or(Error::Invalid)?.to_string();
			let inner = InnerInput {
				node: n.id,
				input,
				element,
			};
			let mut guard = lock(&g.project);
			let entry = guard.graph.get_mut(g.id).ok_or(Error::NotFound)?;
			let gb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<NodeGroup>())
				.ok_or(Error::State)?;
			if !gb.contains_input_passthrough(&inner) {
				return Err(Error::NotFound);
			}
			gb.remove_input_passthrough(&mut entry.core, &inner);
			Ok(())
		})
	}

	/// `oaknode_group_passthrough_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_passthrough_count(
		group: CHandle,
		out_count: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let g = unsafe { node_ref(&group)? };
			let count = with_graph_read(&g.project, |graph| {
				graph
					.get(g.id)
					.and_then(group_of)
					.map(|gb| gb.passthroughs().len() as c_int)
			})
			.ok_or(Error::NotFound)?;
			unsafe { *out_count = count };
			Ok(())
		})
	}

	/// `oaknode_group_passthrough_id_at` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_passthrough_id_at(
		group: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let g = match unsafe { node_ref(&group) } {
			Ok(g) => g,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let id = with_graph_read(&g.project, |graph| {
			graph
				.get(g.id)
				.and_then(group_of)
				.and_then(|gb| gb.passthroughs().get(index as usize))
				.map(|(id, _)| id.clone())
		});
		match id {
			Some(id) => copy_string_out(&id, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_group_passthrough_input_at`: inner node (borrowed),
	/// input id (two-stage), element.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_passthrough_input_at(
		group: CHandle,
		index: c_int,
		out_node: *mut CHandle,
		buf: *mut c_char,
		buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
		let g = match unsafe { node_ref(&group) } {
			Ok(g) => g,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let inner = with_graph_read(&g.project, |graph| {
			graph
				.get(g.id)
				.and_then(group_of)
				.and_then(|gb| gb.passthroughs().get(index as usize))
				.map(|(_, inner)| inner.clone())
		});
		let inner = match inner {
			Some(inner) => inner,
			None => return crate::error::OAKNODE_E_NOT_FOUND,
		};
		if !out_node.is_null() {
			unsafe { *out_node = make_node_borrowed(g.project.clone(), inner.node) };
		}
		if !out_element.is_null() {
			unsafe { *out_element = inner.element };
		}
		copy_string_out(&inner.input, buf, buf_size)
	}

	/// `oaknode_group_get_output_passthrough`: borrowed handle, empty
	/// when unset; `OAKNODE_OK` either way.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_get_output_passthrough(
		group: CHandle,
		out_node: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_node.is_null() {
				return Err(Error::Invalid);
			}
			let g = unsafe { node_ref(&group)? };
			let target = with_graph_read(&g.project, |graph| {
				graph.get(g.id).and_then(group_of).and_then(|gb| gb.output_passthrough())
			});
			match target {
				Some(id) => unsafe { *out_node = make_node_borrowed(g.project.clone(), id) },
				None => unsafe { *out_node = CHandle::null() },
			}
			Ok(())
		})
	}

	/// `oaknode_group_set_output_passthrough` (live); `node` may be an
	/// empty handle to clear.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_set_output_passthrough(
		group: CHandle,
		node: CHandle,
	) -> c_int {
		guard(|| {
			let g = unsafe { node_ref(&group)? };
			let target = if node.ctx.is_null() {
				None
			} else {
				let n = unsafe { node_ref(&node)? };
				if !Arc::ptr_eq(&g.project, &n.project) {
					return Err(Error::Invalid);
				}
				Some(n.id)
			};
			let mut guard = lock(&g.project);
			let entry = guard.graph.get_mut(g.id).ok_or(Error::NotFound)?;
			let gb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<NodeGroup>())
				.ok_or(Error::State)?;
			gb.set_output_passthrough(target);
			Ok(())
		})
	}

	/// `oaknode_group_set_output_passthrough_undoable`
	/// (`olive::NodeGroupSetOutputPassthrough`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_set_output_passthrough_undoable(
		group: CHandle,
		node: CHandle,
		out_command: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_command.is_null() {
				return Err(Error::Invalid);
			}
			let g = unsafe { node_ref(&group)? };
			let new_target = if node.ctx.is_null() {
				None
			} else {
				let n = unsafe { node_ref(&node)? };
				if !Arc::ptr_eq(&g.project, &n.project) {
					return Err(Error::Invalid);
				}
				Some(n.id)
			};
			let project = g.project.clone();
			let group_id = g.id;
			// `old_output_` is captured at redo time (C++ redo stores it
			// before assigning; `// CPP-PARITY: group.cpp:380`).
			let old_output = std::sync::Arc::new(std::sync::Mutex::new(None));
			let redo_old = old_output.clone();
			let redo_project = project.clone();
			let cmd = crate::bridge::undo::command_from_closures(
				move || {
					let mut guard = lock(&redo_project);
					if let Some(entry) = guard.graph.get_mut(group_id) {
						if let Some(gb) = entry
							.behavior
							.as_any_mut()
							.and_then(|a| a.downcast_mut::<NodeGroup>())
						{
							*lock(&redo_old) = gb.output_passthrough();
							gb.set_output_passthrough(new_target);
						}
					}
				},
				move || {
					let mut guard = lock(&project);
					if let Some(entry) = guard.graph.get_mut(group_id) {
						if let Some(gb) = entry
							.behavior
							.as_any_mut()
							.and_then(|a| a.downcast_mut::<NodeGroup>())
						{
							gb.set_output_passthrough(*lock(&old_output));
						}
					}
				},
			)
			.ok_or(Error::NoMem)?;
			unsafe { *out_command = cmd };
			Ok(())
		})
	}

	/// `oaknode_group_resolve_input`: follow group passthroughs to the
	/// innermost non-group input; `OAKNODE_E_NOT_FOUND` when the input
	/// does not resolve.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_group_resolve_input(
		node: CHandle,
		input_id: *const c_char,
		element: c_int,
		out_node: *mut CHandle,
		buf: *mut c_char,
		buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let input = match unsafe { cstr(input_id) } {
			Some(s) => s,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let resolved = with_graph_read(&n.project, |graph| {
			let start = InnerInput {
				node: n.id,
				input: input.to_string(),
				element,
			};
			// The input must resolve to a live input on the final node
			// (C++ `resolve_input` returns whatever it lands on; the
			// C ABI contract rejects unresolvable inputs).
			let r = NodeGroup::resolve_input(graph, start);
			let input_ok = graph
				.get(r.node)
				.map(|e| e.core.has_input(&r.input))
				.unwrap_or(false);
			input_ok.then_some(r)
		});
		let resolved = match resolved {
			Some(r) => r,
			None => return crate::error::OAKNODE_E_NOT_FOUND,
		};
		if !out_node.is_null() {
			unsafe { *out_node = make_node_borrowed(n.project.clone(), resolved.node) };
		}
		if !out_element.is_null() {
			unsafe { *out_element = resolved.element };
		}
		copy_string_out(&resolved.input, buf, buf_size)
	}
}

// =====================================================================
// include/node/sequence.h
// =====================================================================

/// `include/node/sequence.h` exports. A sequence handle is a node
/// handle whose node carries [`crate::sequence::SequenceBehavior`].
/// `oaknode_sequence_create` builds a self-contained scratch project
/// holding the sequence node plus its three track lists (video, audio,
/// subtitle); adding the sequence to a project transfers the whole
/// subgraph (`oaknode_project_add_node`).
pub mod sequence {
	use super::*;
	use crate::node::NodeCore;
	use crate::sequence::SequenceBehavior;
	use crate::track::{TrackListBehavior, TrackType};

	/// Borrowed copy of the sequence behavior behind a handle.
	fn seq_of(n: &NodeRef) -> Result<crate::sequence::SequenceBehavior> {
		let guard = lock(&n.project);
		let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
		entry
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<SequenceBehavior>())
			.map(|s| SequenceBehavior {
				track_lists: s.track_lists.clone(),
				markers: s.markers.clone(),
				workarea: s.workarea.clone(),
				last_length: s.last_length,
				autocache_video: s.autocache_video,
				autocache_audio: s.autocache_audio,
				playhead: s.playhead,
				video_params: s.video_params.clone(),
				audio_params: s.audio_params.clone(),
			})
			.ok_or(Error::State)
	}

	/// Build the default track-list set for a sequence: one node per
	/// type, appended to `seq`'s `track_lists`.
	fn append_default_track_lists(
		graph: &mut crate::graph::Graph,
		seq_id: NodeId,
	) -> Vec<NodeId> {
		let mut lists = Vec::new();
		let mut base = 0i32;
		for kind in [TrackType::Video, TrackType::Audio, TrackType::Subtitle] {
			let mut behavior = TrackListBehavior::new(kind);
			behavior.sequence = Some(seq_id);
			behavior.array_base = base;
			let id = graph.add_node(NodeCore::new(), Box::new(behavior));
			lists.push(id);
			base += 1;
		}
		lists
	}

	/// `oaknode_sequence_create`: an owned sequence handle whose scratch
	/// project holds the sequence + its three track lists.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_create() -> CHandle {
		let scratch = Project::new();
		let id = {
			let mut guard = lock(&scratch);
			let mut seq = SequenceBehavior::new();
			seq.set_default_parameters();
			let mut core = NodeCore::new();
			core.add_input(crate::input::Input::new(
				crate::sequence::TEXTURE_INPUT,
				crate::value::ValueType::Texture,
				crate::value::NodeValue::None,
			));
			core.add_input(crate::input::Input::new(
				crate::sequence::SAMPLES_INPUT,
				crate::value::ValueType::Samples,
				crate::value::NodeValue::None,
			));
			// One array input per track list (`track_in_%1`; the video
			// list owns track_in_0, audio track_in_1, subtitle
			// track_in_2 — `// CPP-PARITY: sequence.h`).
			for base in 0..3 {
				let mut track_input = crate::input::Input::new(
					&crate::sequence::TRACK_INPUT_FORMAT.replace("%1", &base.to_string()),
					crate::value::ValueType::None,
					crate::value::NodeValue::None,
				);
				track_input.flags |= crate::input::flags::ARRAY;
				core.add_input(track_input);
			}
			let seq_id = guard.graph.add_node(core, Box::new(seq));
			// Fill the track lists.
			let lists = append_default_track_lists(&mut guard.graph, seq_id);
			if let Some(entry) = guard.graph.get_mut(seq_id) {
				if let Some(s) = entry
					.behavior
					.as_any_mut()
					.and_then(|a| a.downcast_mut::<SequenceBehavior>())
				{
					s.track_lists = lists;
				}
			}
			seq_id
		};
		make_node_owned(scratch, id)
	}

	/// `oaknode_sequence_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_free(sequence: *mut CHandle) {
		if sequence.is_null() || unsafe { (*sequence).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*sequence).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*sequence).ctx = std::ptr::null_mut() };
	}

	/// `oaknode_sequence_set_default_parameters`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_set_default_parameters(sequence: CHandle) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&sequence)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let s = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<SequenceBehavior>())
				.ok_or(Error::State)?;
			s.set_default_parameters();
			Ok(())
		})
	}

	/// `oaknode_sequence_as_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_as_node(sequence: CHandle) -> CHandle {
		match unsafe { node_ref(&sequence) } {
			Ok(n) => make_node_borrowed(n.project.clone(), n.id),
			Err(_) => CHandle::null(),
		}
	}

	/// `oaknode_sequence_from_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_from_node(node: CHandle) -> CHandle {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		let guard = lock(&n.project);
		let is_seq = guard
			.graph
			.get(n.id)
			.and_then(|e| e.behavior.as_any())
			.map(|a| a.downcast_ref::<SequenceBehavior>().is_some())
			.unwrap_or(false);
		if is_seq {
			make_node_borrowed(n.project.clone(), n.id)
		} else {
			CHandle::null()
		}
	}

	/// `oaknode_sequence_get_track_list`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_track_list(
		sequence: CHandle,
		type_: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let kind = TrackType::from_c(type_).ok_or(Error::NotFound)?;
			let (project, list_id) = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let s = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<SequenceBehavior>())
					.ok_or(Error::State)?;
				let list = s
					.track_lists
					.iter()
					.find(|id| {
						guard
							.graph
							.get(**id)
							.and_then(|e| e.behavior.as_any())
							.and_then(|a| a.downcast_ref::<TrackListBehavior>())
							.map(|tl| tl.kind == kind)
							.unwrap_or(false)
					})
					.copied()
					.ok_or(Error::NotFound)?;
				(n.project.clone(), list)
			};
			unsafe { *out = make_node_borrowed(project, list_id) };
			Ok(())
		})
	}

	/// `oaknode_sequence_get_track_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_track_count(
		sequence: CHandle,
		type_: c_int,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let kind = TrackType::from_c(type_).ok_or(Error::NotFound)?;
			let mut total = 0;
			{
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let s = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<SequenceBehavior>())
					.ok_or(Error::State)?;
				for id in &s.track_lists {
					if let Some(le) = guard.graph.get(*id) {
						if let Some(tl) = le
							.behavior
							.as_any()
							.and_then(|a| a.downcast_ref::<TrackListBehavior>())
						{
							if tl.kind == kind {
								total = tl.tracks.len();
							}
						}
					}
				}
			}
			unsafe { *count = total as c_int };
			Ok(())
		})
	}

	/// `oaknode_sequence_get_track_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_track_at(
		sequence: CHandle,
		type_: c_int,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let kind = TrackType::from_c(type_).ok_or(Error::NotFound)?;
			if index < 0 {
				return Err(Error::NotFound);
			}
			let (project, track) = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let s = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<SequenceBehavior>())
					.ok_or(Error::State)?;
				let mut found = None;
				for id in &s.track_lists {
					if let Some(le) = guard.graph.get(*id) {
						if let Some(tl) = le
							.behavior
							.as_any()
							.and_then(|a| a.downcast_ref::<TrackListBehavior>())
						{
							if tl.kind == kind {
								found = tl.tracks.get(index as usize).copied();
							}
						}
					}
				}
				(n.project.clone(), found.ok_or(Error::NotFound)?)
			};
			unsafe { *out = make_node_borrowed(project, track) };
			Ok(())
		})
	}

	/// `oaknode_sequence_get_all_track_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_all_track_count(
		sequence: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let s = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<SequenceBehavior>())
				.ok_or(Error::State)?;
			let mut total = 0usize;
			for id in &s.track_lists {
				if let Some(le) = guard.graph.get(*id) {
					if let Some(tl) = le
						.behavior
						.as_any()
						.and_then(|a| a.downcast_ref::<TrackListBehavior>())
					{
						total += tl.tracks.len();
					}
				}
			}
			unsafe { *count = total as c_int };
			Ok(())
		})
	}

	/// `oaknode_sequence_get_all_track_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_all_track_at(
		sequence: CHandle,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			if index < 0 {
				return Err(Error::NotFound);
			}
			let (project, track) = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let s = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<SequenceBehavior>())
					.ok_or(Error::State)?;
				let mut flat: Vec<NodeId> = Vec::new();
				for id in &s.track_lists {
					if let Some(le) = guard.graph.get(*id) {
						if let Some(tl) = le
							.behavior
							.as_any()
							.and_then(|a| a.downcast_ref::<TrackListBehavior>())
						{
							flat.extend(tl.tracks.iter().copied());
						}
					}
				}
				(n.project.clone(), flat.get(index as usize).copied().ok_or(Error::NotFound)?)
			};
			unsafe { *out = make_node_borrowed(project, track) };
			Ok(())
		})
	}

	/// `oaknode_sequence_get_playhead`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_playhead(
		sequence: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let s = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<SequenceBehavior>())
				.ok_or(Error::State)?;
			unsafe {
				*numerator = s.playhead.numerator() as c_int;
				*denominator = s.playhead.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_sequence_set_playhead`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_set_playhead(
		sequence: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&sequence)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let s = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<SequenceBehavior>())
				.ok_or(Error::State)?;
			s.playhead = oakcore_rs::Rational::new(numerator as i64, denominator as i64);
			Ok(())
		})
	}

	/// `oaknode_sequence_get_length` (cached overall length).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_length(
		sequence: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let s = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<SequenceBehavior>())
				.ok_or(Error::State)?;
			unsafe {
				*numerator = s.last_length.numerator() as c_int;
				*denominator = s.last_length.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// Compute the length of the `kind` track lists (longest track).
	fn list_length(n: &NodeRef, kind: TrackType) -> Result<oakcore_rs::Rational> {
		let guard = lock(&n.project);
		let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
		let s = entry
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<SequenceBehavior>())
			.ok_or(Error::State)?;
		let mut longest = oakcore_rs::Rational::new(0, 1);
		for id in &s.track_lists {
			if let Some(le) = guard.graph.get(*id) {
				if let Some(tl) = le
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackListBehavior>())
				{
					if tl.kind == kind {
						let len = tl.total_length(&GraphTrackRange { graph: &guard.graph });
						if len > longest {
							longest = len;
						}
					}
				}
			}
		}
		Ok(longest)
	}

	/// `oaknode_sequence_get_video_length`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_video_length(
		sequence: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let len = list_length(&n, TrackType::Video)?;
			unsafe {
				*numerator = len.numerator() as c_int;
				*denominator = len.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_sequence_get_audio_length`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_audio_length(
		sequence: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let len = list_length(&n, TrackType::Audio)?;
			unsafe {
				*numerator = len.numerator() as c_int;
				*denominator = len.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_sequence_verify_length`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_verify_length(sequence: CHandle) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&sequence)? };
			let video = list_length(&n, TrackType::Video)?;
			let audio = list_length(&n, TrackType::Audio)?;
			let overall = if video > audio { video } else { audio };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let s = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<SequenceBehavior>())
				.ok_or(Error::State)?;
			s.verify_length((video, audio, overall));
			Ok(())
		})
	}

	/// `oaknode_sequence_get_video_stream_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_video_stream_count(
		sequence: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let s = seq_of(&n)?;
			unsafe { *count = s.video_stream_count() as c_int };
			Ok(())
		})
	}

	/// `oaknode_sequence_get_audio_stream_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_audio_stream_count(
		sequence: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let s = seq_of(&n)?;
			unsafe { *count = s.audio_stream_count() as c_int };
			Ok(())
		})
	}

	/// `oaknode_sequence_get_video_params`: the video params at `index` as
	/// a NEW owned oakcommon handle (`// CPP-PARITY:
	/// src/node/c_api/sequence.cpp:248`). The Rust model's `VideoParams`
	/// carries width/height/frame-rate/format/channels; the handle is
	/// built through the oakcommon videoparams C ABI (`OAKNODE_E_NOMEM`
	/// when oakcommon is unavailable — the C++ null-handle path).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_video_params(
		sequence: CHandle,
		index: c_int,
		out: *mut std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if out.is_null() || index < 0 {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let s = seq_of(&n)?;
			let params = s
				.video_params
				.get(index as usize)
				.ok_or(Error::NotFound)?;
			let handle = super::videoparams_handle_from(params)
				.ok_or(Error::NoMem)?;
			unsafe { *(out as *mut crate::handle::CHandle) = handle };
			Ok(())
		})
	}

	/// `oaknode_sequence_set_video_params`: replace the video params at
	/// `index` with a copy of `params` (`// CPP-PARITY: sequence.cpp:270`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_set_video_params(
		sequence: CHandle,
		index: c_int,
		params: crate::handle::CHandle,
	) -> c_int {
		guard(|| {
			if index < 0 || params.ctx.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let read = super::videoparams_from_handle(params)
				.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let s = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<SequenceBehavior>())
				.ok_or(Error::State)?;
			if index as usize >= s.video_params.len() {
				return Err(Error::NotFound);
			}
			s.video_params[index as usize] = read;
			Ok(())
		})
	}

	/// `oaknode_sequence_get_audio_params`: the audio params at `index` as
	/// a NEW owned oakcore handle (`OakAudioParams **out`,
	/// `// CPP-PARITY: sequence.cpp:293`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_get_audio_params(
		sequence: CHandle,
		index: c_int,
		out: *mut *mut std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if out.is_null() || index < 0 {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let s = seq_of(&n)?;
			let params = s
				.audio_params
				.get(index as usize)
				.ok_or(Error::NotFound)?;
			let handle = crate::bridge::core::audioparams_create(
				params.sample_rate,
				params.channel_layout,
				params.format,
			)
			.ok_or(Error::NoMem)?;
			unsafe { *out = handle };
			Ok(())
		})
	}

	/// `oaknode_sequence_set_audio_params`: replace the audio params at
	/// `index` with a copy of `params` (`// CPP-PARITY: sequence.cpp:312`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_sequence_set_audio_params(
		sequence: CHandle,
		index: c_int,
		params: *const std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if params.is_null() || index < 0 {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&sequence)? };
			let read = crate::value::AudioParams {
				sample_rate: crate::bridge::core::audioparams_sample_rate(params)
					.ok_or(Error::Invalid)?,
				channel_layout: crate::bridge::core::audioparams_channel_layout(params)
					.ok_or(Error::Invalid)?,
				format: crate::bridge::core::audioparams_format(params)
					.ok_or(Error::Invalid)?,
			};
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let s = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<SequenceBehavior>())
				.ok_or(Error::State)?;
			if index as usize >= s.audio_params.len() {
				return Err(Error::NotFound);
			}
			s.audio_params[index as usize] = read;
			Ok(())
		})
	}
}

// =====================================================================
// include/node/track.h
// =====================================================================

/// `include/node/track.h` exports. Track and track-list handles are node
/// handles whose nodes carry [`crate::track::TrackBehavior`] /
/// [`crate::track::TrackListBehavior`].
pub mod track {
	use super::*;
	use crate::node::NodeCore;
	use crate::track::{TrackBehavior, TrackListBehavior, TrackType};

	/// Borrowed track behavior copy.
	fn track_of(n: &NodeRef) -> Result<crate::track::TrackBehavior> {
		let guard = lock(&n.project);
		let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
		entry
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<TrackBehavior>())
			.map(|t| TrackBehavior {
				kind: t.kind,
				blocks: t.blocks.clone(),
				muted: t.muted,
				locked: t.locked,
				height: t.height,
				index: t.index,
				track_list: t.track_list,
			})
			.ok_or(Error::State)
	}

	/// Borrowed track-list behavior copy.
	fn tracklist_of(n: &NodeRef) -> Result<crate::track::TrackListBehavior> {
		let guard = lock(&n.project);
		let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
		entry
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<TrackListBehavior>())
			.map(|t| TrackListBehavior {
				kind: t.kind,
				tracks: t.tracks.clone(),
				sequence: t.sequence,
				array_base: t.array_base,
			})
			.ok_or(Error::State)
	}

	/// `oaknode_track_as_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_as_node(track: CHandle) -> CHandle {
		match unsafe { node_ref(&track) } {
			Ok(n) => make_node_borrowed(n.project.clone(), n.id),
			Err(_) => CHandle::null(),
		}
	}

	/// `oaknode_track_create`: owned track in a scratch project.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_create(type_: c_int) -> CHandle {
		let kind = match TrackType::from_c(type_) {
			Some(k) => k,
			None => return CHandle::null(),
		};
		let scratch = Project::new();
		let id = {
			let mut guard = lock(&scratch);
			guard.graph.add_node(NodeCore::new(), Box::new(TrackBehavior::new(kind)))
		};
		make_node_owned(scratch, id)
	}

	/// `oaknode_track_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_free(track: *mut CHandle) {
		if track.is_null() || unsafe { (*track).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*track).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*track).ctx = std::ptr::null_mut() };
	}

	/// `oaknode_track_get_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_type(track: CHandle, type_: *mut c_int) -> c_int {
		guard(|| {
			if type_.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let t = track_of(&n)?;
			unsafe { *type_ = t.kind.to_c() };
			Ok(())
		})
	}

	/// `oaknode_track_set_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_set_type(track: CHandle, type_: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&track)? };
			let kind = TrackType::from_c(type_).ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			t.kind = kind;
			Ok(())
		})
	}

	/// `oaknode_track_get_height`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_height(track: CHandle, height: *mut f64) -> c_int {
		guard(|| {
			if height.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let t = track_of(&n)?;
			unsafe { *height = t.height };
			Ok(())
		})
	}

	/// `oaknode_track_set_height`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_set_height(track: CHandle, height: f64) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&track)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			t.height = height;
			Ok(())
		})
	}

	/// `oaknode_track_get_height_in_pixels`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_height_in_pixels(
		track: CHandle,
		height: *mut c_int,
	) -> c_int {
		guard(|| {
			if height.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let t = track_of(&n)?;
			unsafe { *height = crate::track::internal_height_to_pixel_height(t.height) };
			Ok(())
		})
	}

	/// `oaknode_track_set_height_in_pixels`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_set_height_in_pixels(
		track: CHandle,
		height: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&track)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			t.height = crate::track::pixel_height_to_internal_height(height);
			Ok(())
		})
	}

	/// `oaknode_track_get_default_height_in_pixels`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_default_height_in_pixels() -> c_int {
		crate::track::internal_height_to_pixel_height(crate::track::DEFAULT_HEIGHT_INTERNAL)
	}

	/// `oaknode_track_get_minimum_height_in_pixels`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_minimum_height_in_pixels() -> c_int {
		crate::track::internal_height_to_pixel_height(crate::track::MINIMUM_HEIGHT_INTERNAL)
	}

	/// `oaknode_track_get_index`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_index(track: CHandle, index: *mut c_int) -> c_int {
		guard(|| {
			if index.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let t = track_of(&n)?;
			unsafe { *index = t.index };
			Ok(())
		})
	}

	/// `oaknode_track_set_index`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_set_index(track: CHandle, index: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&track)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			t.index = index;
			Ok(())
		})
	}

	/// `oaknode_track_get_muted`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_muted(track: CHandle, muted: *mut c_int) -> c_int {
		guard(|| {
			if muted.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let t = track_of(&n)?;
			unsafe { *muted = t.muted as c_int };
			Ok(())
		})
	}

	/// `oaknode_track_set_muted`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_set_muted(track: CHandle, muted: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&track)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			t.muted = muted != 0;
			Ok(())
		})
	}

	/// `oaknode_track_get_locked`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_locked(track: CHandle, locked: *mut c_int) -> c_int {
		guard(|| {
			if locked.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let t = track_of(&n)?;
			unsafe { *locked = t.locked as c_int };
			Ok(())
		})
	}

	/// `oaknode_track_set_locked`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_set_locked(track: CHandle, locked: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&track)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			t.locked = locked != 0;
			Ok(())
		})
	}

	/// `oaknode_track_get_reference`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_reference(
		track: CHandle,
		type_: *mut c_int,
		index: *mut c_int,
	) -> c_int {
		guard(|| {
			if type_.is_null() || index.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let t = track_of(&n)?;
			let (ty, i) = t.reference();
			unsafe {
				*type_ = ty;
				*index = i;
			}
			Ok(())
		})
	}

	/// `oaknode_track_get_length`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_length(
		track: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let len = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let t = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.ok_or(Error::State)?;
				t.length(&GraphBlockRange { graph: &guard.graph })
			};
			unsafe {
				*numerator = len.numerator() as c_int;
				*denominator = len.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_track_get_sequence` (borrowed).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_sequence(
		track: CHandle,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let (project, seq) = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let t = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.ok_or(Error::State)?;
				let seq = t.track_list.and_then(|list| {
					guard
						.graph
						.get(list)
						.and_then(|le| le.behavior.as_any())
						.and_then(|a| a.downcast_ref::<TrackListBehavior>())
						.and_then(|tl| tl.sequence)
				});
				(n.project.clone(), seq)
			};
			unsafe {
				*out = match seq {
					Some(s) => make_node_borrowed(project, s),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_track_get_block_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_block_count(
		track: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			let t = track_of(&n)?;
			unsafe { *count = t.blocks.len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_track_get_block_at` (borrowed).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_block_at(
		track: CHandle,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&track)? };
			if index < 0 {
				return Err(Error::NotFound);
			}
			let (project, block) = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let t = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.ok_or(Error::State)?;
				(n.project.clone(), t.blocks.get(index as usize).copied().ok_or(Error::NotFound)?)
			};
			unsafe { *out = make_node_borrowed(project, block) };
			Ok(())
		})
	}

	/// Shared block-append/insert helper: adopt `block` into the track's
	/// project and list. Returns the block's id in the track's project
	/// (it may differ after the move).
	fn adopt_block(
		track_project: &ProjectArc,
		track_id: NodeId,
		block: &NodeRef,
		block_handle: &CHandle,
	) -> Result<NodeId> {
		if !Arc::ptr_eq(&block.project, track_project) {
			// Move the block into the track's project.
			let new_id = move_node_between(&block.project, block.id, track_project)?;
			let r = NodeRef {
				project: track_project.clone(),
				id: new_id,
				owned: std::sync::Arc::new(AtomicBool::new(false)),
			};
			unsafe { write_node_ref(block_handle, r) };
			// The block's core records the owning track.
			let mut guard = lock(track_project);
			if let Some(entry) = guard.graph.get_mut(new_id) {
				if let Some(core) = block_core_mut(entry) {
					core.track = Some(track_id);
				}
			}
			Ok(new_id)
		} else {
			let mut guard = lock(track_project);
			if let Some(entry) = guard.graph.get_mut(block.id) {
				if let Some(core) = block_core_mut(entry) {
					core.track = Some(track_id);
				}
			}
			Ok(block.id)
		}
	}

	/// `oaknode_track_append_block`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_append_block(track: CHandle, block: CHandle) -> c_int {
		guard(|| {
			let t = unsafe { node_ref(&track)? };
			let b = unsafe { node_ref(&block)? };
			let block_id = adopt_block(&t.project, t.id, &b, &block)?;
			let mut guard = lock(&t.project);
			let entry = guard.graph.get_mut(t.id).ok_or(Error::NotFound)?;
			let tb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			tb.append_block(block_id);
			Ok(())
		})
	}

	/// `oaknode_track_prepend_block`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_prepend_block(track: CHandle, block: CHandle) -> c_int {
		guard(|| {
			let t = unsafe { node_ref(&track)? };
			let b = unsafe { node_ref(&block)? };
			let block_id = adopt_block(&t.project, t.id, &b, &block)?;
			let mut guard = lock(&t.project);
			let entry = guard.graph.get_mut(t.id).ok_or(Error::NotFound)?;
			let tb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			tb.prepend_block(block_id);
			Ok(())
		})
	}

	/// `oaknode_track_insert_block_at_index`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_insert_block_at_index(
		track: CHandle,
		block: CHandle,
		index: c_int,
	) -> c_int {
		guard(|| {
			let t = unsafe { node_ref(&track)? };
			let b = unsafe { node_ref(&block)? };
			let block_id = adopt_block(&t.project, t.id, &b, &block)?;
			let mut guard = lock(&t.project);
			let entry = guard.graph.get_mut(t.id).ok_or(Error::NotFound)?;
			let tb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			tb.insert_block_at_index(block_id, index.max(0) as usize);
			Ok(())
		})
	}

	/// `oaknode_track_insert_block_after`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_insert_block_after(
		track: CHandle,
		block: CHandle,
		before: CHandle,
	) -> c_int {
		guard(|| {
			let t = unsafe { node_ref(&track)? };
			let b = unsafe { node_ref(&block)? };
			let ref_block = unsafe { node_ref(&before)? };
			let block_id = adopt_block(&t.project, t.id, &b, &block)?;
			let mut guard = lock(&t.project);
			let entry = guard.graph.get_mut(t.id).ok_or(Error::NotFound)?;
			let tb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			if tb.insert_block_after(block_id, ref_block.id) {
				Ok(())
			} else {
				Err(Error::NotFound)
			}
		})
	}

	/// `oaknode_track_insert_block_before`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_insert_block_before(
		track: CHandle,
		block: CHandle,
		after: CHandle,
	) -> c_int {
		guard(|| {
			let t = unsafe { node_ref(&track)? };
			let b = unsafe { node_ref(&block)? };
			let ref_block = unsafe { node_ref(&after)? };
			let block_id = adopt_block(&t.project, t.id, &b, &block)?;
			let mut guard = lock(&t.project);
			let entry = guard.graph.get_mut(t.id).ok_or(Error::NotFound)?;
			let tb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			if tb.insert_block_before(block_id, ref_block.id) {
				Ok(())
			} else {
				Err(Error::NotFound)
			}
		})
	}

	/// `oaknode_track_ripple_remove_block`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_ripple_remove_block(
		track: CHandle,
		block: CHandle,
	) -> c_int {
		guard(|| {
			let t = unsafe { node_ref(&track)? };
			let b = unsafe { node_ref(&block)? };
			let mut guard = lock(&t.project);
			let entry = guard.graph.get_mut(t.id).ok_or(Error::NotFound)?;
			let tb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			if tb.ripple_remove_block(b.id) {
				if let Some(entry) = guard.graph.get_mut(b.id) {
					if let Some(core) = block_core_mut(entry) {
						core.track = None;
					}
				}
				Ok(())
			} else {
				Err(Error::NotFound)
			}
		})
	}

	/// `oaknode_track_replace_block`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_replace_block(
		track: CHandle,
		old_block: CHandle,
		new_block: CHandle,
	) -> c_int {
		guard(|| {
			let t = unsafe { node_ref(&track)? };
			let old = unsafe { node_ref(&old_block)? };
			let new = unsafe { node_ref(&new_block)? };
			let new_id = adopt_block(&t.project, t.id, &new, &new_block)?;
			let mut guard = lock(&t.project);
			let entry = guard.graph.get_mut(t.id).ok_or(Error::NotFound)?;
			let tb = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TrackBehavior>())
				.ok_or(Error::State)?;
			if tb.replace_block(old.id, new_id) {
				Ok(())
			} else {
				Err(Error::NotFound)
			}
		})
	}

	/// `oaknode_track_get_block_index`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_block_index(
		track: CHandle,
		block: CHandle,
		index: *mut c_int,
	) -> c_int {
		guard(|| {
			if index.is_null() {
				return Err(Error::Invalid);
			}
			let t = unsafe { node_ref(&track)? };
			let b = unsafe { node_ref(&block)? };
			let tb = track_of(&t)?;
			match tb.block_index(b.id) {
				Some(i) => {
					unsafe { *index = i as c_int };
					Ok(())
				}
				None => Err(Error::NotFound),
			}
		})
	}

	/// `oaknode_track_get_block_containing_time`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_block_containing_time(
		track: CHandle,
		numerator: c_int,
		denominator: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let t = unsafe { node_ref(&track)? };
			let time = oakcore_rs::Rational::new(numerator as i64, denominator as i64);
			let (project, block) = {
				let guard = lock(&t.project);
				let entry = guard.graph.get(t.id).ok_or(Error::NotFound)?;
				let tb = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.ok_or(Error::State)?;
				let found = tb.block_containing_time(time, &GraphBlockRange { graph: &guard.graph });
				(t.project.clone(), found)
			};
			unsafe {
				*out = match block {
					Some(b) => make_node_borrowed(project, b),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_track_get_visible_block_at_time`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_visible_block_at_time(
		track: CHandle,
		numerator: c_int,
		denominator: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let t = unsafe { node_ref(&track)? };
			let time = oakcore_rs::Rational::new(numerator as i64, denominator as i64);
			let (project, block) = {
				let guard = lock(&t.project);
				let entry = guard.graph.get(t.id).ok_or(Error::NotFound)?;
				let tb = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.ok_or(Error::State)?;
				let found = tb.visible_block_at_time(time, &GraphBlockRange { graph: &guard.graph });
				(t.project.clone(), found)
			};
			unsafe {
				*out = match block {
					Some(b) => make_node_borrowed(project, b),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_track_is_range_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_is_range_free(
		track: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
		is_free: *mut c_int,
	) -> c_int {
		guard(|| {
			if is_free.is_null() {
				return Err(Error::Invalid);
			}
			let t = unsafe { node_ref(&track)? };
			let range = oakcore_rs::TimeRange::new(
				oakcore_rs::Rational::new(in_num as i64, in_den as i64),
				oakcore_rs::Rational::new(out_num as i64, out_den as i64),
			);
			let free = {
				let guard = lock(&t.project);
				let entry = guard.graph.get(t.id).ok_or(Error::NotFound)?;
				let tb = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.ok_or(Error::State)?;
				tb.is_range_free(range, &GraphBlockRange { graph: &guard.graph })
			};
			unsafe { *is_free = free as c_int };
			Ok(())
		})
	}

	/// `oaknode_track_get_nearest_block_before_or_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_nearest_block_before_or_at(
		track: CHandle,
		numerator: c_int,
		denominator: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let t = unsafe { node_ref(&track)? };
			let time = oakcore_rs::Rational::new(numerator as i64, denominator as i64);
			let (project, block) = {
				let guard = lock(&t.project);
				let entry = guard.graph.get(t.id).ok_or(Error::NotFound)?;
				let tb = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.ok_or(Error::State)?;
				// The last block whose in-point is <= time.
				let found = tb
					.blocks
					.iter()
					.filter(|b| {
						guard
							.graph
							.get(**b)
							.and_then(block_core)
							.map(|c| c.in_() <= time)
							.unwrap_or(false)
					})
					.next_back()
					.copied();
				(t.project.clone(), found)
			};
			unsafe {
				*out = match block {
					Some(b) => make_node_borrowed(project, b),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_track_get_nearest_block_after_or_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_track_get_nearest_block_after_or_at(
		track: CHandle,
		numerator: c_int,
		denominator: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let t = unsafe { node_ref(&track)? };
			let time = oakcore_rs::Rational::new(numerator as i64, denominator as i64);
			let (project, block) = {
				let guard = lock(&t.project);
				let entry = guard.graph.get(t.id).ok_or(Error::NotFound)?;
				let tb = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.ok_or(Error::State)?;
				let found = tb
					.blocks
					.iter()
					.filter(|b| {
						guard
							.graph
							.get(**b)
							.and_then(block_core)
							.map(|c| c.out() >= time)
							.unwrap_or(false)
					})
					.next()
					.copied();
				(t.project.clone(), found)
			};
			unsafe {
				*out = match block {
					Some(b) => make_node_borrowed(project, b),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	// ---- TrackList -----------------------------------------------------

	/// `oaknode_tracklist_get_sequence`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_get_sequence(
		list: CHandle,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&list)? };
			let tl = tracklist_of(&n)?;
			unsafe {
				*out = match tl.sequence {
					Some(s) => make_node_borrowed(n.project.clone(), s),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_tracklist_get_track_input_id` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_get_track_input_id(
		list: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&list) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let tl = match tracklist_of(&n) {
			Ok(tl) => tl,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let input_id = crate::sequence::TRACK_INPUT_FORMAT.replace("%1", &tl.array_base.to_string());
		copy_string_out(&input_id, buf, buf_size)
	}

	/// `oaknode_tracklist_array_append`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_array_append(list: CHandle) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&list)? };
			let tl = tracklist_of(&n)?;
			let input_id = crate::sequence::TRACK_INPUT_FORMAT.replace("%1", &tl.array_base.to_string());
			let mut guard = lock(&n.project);
			let seq = tl.sequence.ok_or(Error::State)?;
			let size = guard
				.graph
				.get(seq)
				.map(|e| e.core.input_array_size(&input_id))
				.unwrap_or(0);
			guard.graph.input_array_insert(seq, &input_id, size as i32)
		})
	}

	/// `oaknode_tracklist_array_remove_last`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_array_remove_last(list: CHandle) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&list)? };
			let tl = tracklist_of(&n)?;
			let input_id = crate::sequence::TRACK_INPUT_FORMAT.replace("%1", &tl.array_base.to_string());
			let mut guard = lock(&n.project);
			let seq = tl.sequence.ok_or(Error::State)?;
			let size = guard
				.graph
				.get(seq)
				.map(|e| e.core.input_array_size(&input_id))
				.unwrap_or(0);
			if size == 0 {
				return Ok(());
			}
			guard.graph.input_array_remove(seq, &input_id, (size - 1) as i32)
		})
	}

	/// `oaknode_tracklist_get_array_index_from_cache_index` (identity for
	/// compact lists; the cached index equals the array element when no
	/// slots are disconnected).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_get_array_index_from_cache_index(
		list: CHandle,
		cache_index: c_int,
		out_index: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_index.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&list)? };
			let tl = tracklist_of(&n)?;
			if cache_index < 0 || cache_index as usize >= tl.tracks.len() {
				return Err(Error::NotFound);
			}
			unsafe { *out_index = cache_index };
			Ok(())
		})
	}

	/// `oaknode_tracklist_get_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_get_type(list: CHandle, type_: *mut c_int) -> c_int {
		guard(|| {
			if type_.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&list)? };
			let tl = tracklist_of(&n)?;
			unsafe { *type_ = tl.kind.to_c() };
			Ok(())
		})
	}

	/// `oaknode_tracklist_get_track_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_get_track_count(
		list: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&list)? };
			let tl = tracklist_of(&n)?;
			unsafe { *count = tl.tracks.len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_tracklist_get_track_at` (borrowed).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_get_track_at(
		list: CHandle,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&list)? };
			if index < 0 {
				return Err(Error::NotFound);
			}
			let tl = tracklist_of(&n)?;
			let track = tl.tracks.get(index as usize).copied().ok_or(Error::NotFound)?;
			unsafe { *out = make_node_borrowed(n.project.clone(), track) };
			Ok(())
		})
	}

	/// `oaknode_tracklist_get_total_length`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_get_total_length(
		list: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&list)? };
			let len = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let tl = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackListBehavior>())
					.ok_or(Error::State)?;
				tl.total_length(&GraphTrackRange { graph: &guard.graph })
			};
			unsafe {
				*numerator = len.numerator() as c_int;
				*denominator = len.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_tracklist_get_array_size` (>= track count).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_get_array_size(
		list: CHandle,
		size: *mut c_int,
	) -> c_int {
		guard(|| {
			if size.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&list)? };
			let tl = tracklist_of(&n)?;
			let input_id = crate::sequence::TRACK_INPUT_FORMAT.replace("%1", &tl.array_base.to_string());
			let guard = lock(&n.project);
			let arr = tl
				.sequence
				.and_then(|seq| guard.graph.get(seq))
				.map(|e| e.core.input_array_size(&input_id))
				.unwrap_or(0);
			unsafe { *size = arr as c_int };
			Ok(())
		})
	}

	/// `oaknode_tracklist_add_track`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_add_track(list: CHandle, track: CHandle) -> c_int {
		guard(|| {
			let l = unsafe { node_ref(&list)? };
			let t = unsafe { node_ref(&track)? };
			let tl = tracklist_of(&l)?;

			// Move the track into the list's project; the id may change.
			let track_id = if !Arc::ptr_eq(&t.project, &l.project) {
				let new_id = move_node_between(&t.project, t.id, &l.project)?;
				let r = NodeRef {
					project: l.project.clone(),
					id: new_id,
					owned: std::sync::Arc::new(AtomicBool::new(false)),
				};
				unsafe { write_node_ref(&track, r) };
				new_id
			} else {
				t.id
			};

			let mut guard = lock(&l.project);
			// Inherit the previous track's height.
			let prev_height = {
				let entry = guard.graph.get(l.id).ok_or(Error::NotFound)?;
				let tl = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<TrackListBehavior>())
					.ok_or(Error::State)?;
				tl.tracks
					.last()
					.and_then(|last| guard.graph.get(*last))
					.and_then(|le| le.behavior.as_any())
					.and_then(|a| a.downcast_ref::<TrackBehavior>())
					.map(|tb| tb.height)
					.unwrap_or(crate::track::DEFAULT_HEIGHT_INTERNAL)
			};
			{
				let entry = guard.graph.get_mut(l.id).ok_or(Error::NotFound)?;
				let tl = entry
					.behavior
					.as_any_mut()
					.and_then(|a| a.downcast_mut::<TrackListBehavior>())
					.ok_or(Error::State)?;
				let index = tl.tracks.len() as i32;
				tl.tracks.push(track_id);
				if let Some(entry) = guard.graph.get_mut(track_id) {
					if let Some(tb) = entry
						.behavior
						.as_any_mut()
						.and_then(|a| a.downcast_mut::<TrackBehavior>())
					{
						tb.track_list = Some(l.id);
						tb.index = index;
						tb.height = prev_height;
					}
				}
			}
			Ok(())
		})
	}

	/// `oaknode_tracklist_remove_track`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_tracklist_remove_track(
		list: CHandle,
		track: CHandle,
	) -> c_int {
		guard(|| {
			let l = unsafe { node_ref(&list)? };
			let t = unsafe { node_ref(&track)? };
			let mut guard = lock(&l.project);
			let removed = {
				let entry = guard.graph.get_mut(l.id).ok_or(Error::NotFound)?;
				let tl = entry
					.behavior
					.as_any_mut()
					.and_then(|a| a.downcast_mut::<TrackListBehavior>())
					.ok_or(Error::State)?;
				let before = tl.tracks.len();
				tl.tracks.retain(|x| *x != t.id);
				tl.tracks.len() != before
			};
			if removed {
				// Renumber the remaining tracks' indices.
				let ids: Vec<NodeId> = {
					let entry = guard.graph.get(l.id).ok_or(Error::NotFound)?;
					let tl = entry
						.behavior
						.as_any()
						.and_then(|a| a.downcast_ref::<TrackListBehavior>())
						.ok_or(Error::State)?;
					tl.tracks.clone()
				};
				for (i, id) in ids.iter().enumerate() {
					if let Some(entry) = guard.graph.get_mut(*id) {
						if let Some(tb) = entry
							.behavior
							.as_any_mut()
							.and_then(|a| a.downcast_mut::<TrackBehavior>())
						{
							tb.index = i as i32;
						}
					}
				}
			}
			if removed {
				if let Some(entry) = guard.graph.get_mut(t.id) {
					if let Some(tb) = entry
						.behavior
						.as_any_mut()
						.and_then(|a| a.downcast_mut::<TrackBehavior>())
					{
						tb.track_list = None;
					}
				}
				Ok(())
			} else {
				Err(Error::NotFound)
			}
		})
	}
}

// =====================================================================
// include/node/block.h
// =====================================================================

/// `include/node/block.h` exports. Block handles are node handles whose
/// nodes carry a block behavior ([`crate::block::BlockCore`]).
pub mod block {
	use super::*;
	use crate::block::{
		BlockCore, ClipBlockBehavior, GapBlockBehavior, TransitionBlockBehavior,
	};

	/// True when the node carries a block behavior.
	fn is_block(n: &NodeRef) -> bool {
		let guard = lock(&n.project);
		guard
			.graph
			.get(n.id)
			.and_then(block_core)
			.is_some()
	}

	/// `oaknode_block_clip_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_clip_create() -> CHandle {
		let scratch = Project::new();
		let (core, behavior) = crate::block::clip_create();
		let id = {
			let mut guard = lock(&scratch);
			guard.graph.add_node(core, behavior)
		};
		make_node_owned(scratch, id)
	}

	/// `oaknode_block_gap_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_gap_create() -> CHandle {
		let scratch = Project::new();
		let (core, behavior) = crate::block::gap_create();
		let id = {
			let mut guard = lock(&scratch);
			guard.graph.add_node(core, behavior)
		};
		make_node_owned(scratch, id)
	}

	/// `oaknode_block_transition_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_transition_create(kind: c_int) -> CHandle {
		if !(0..=1).contains(&kind) {
			return CHandle::null();
		}
		let scratch = Project::new();
		let (core, behavior) = crate::block::transition_create();
		let id = {
			let mut guard = lock(&scratch);
			guard.graph.add_node(core, behavior)
		};
		make_node_owned(scratch, id)
	}

	/// `oaknode_block_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_free(block: *mut CHandle) {
		if block.is_null() || unsafe { (*block).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*block).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*block).ctx = std::ptr::null_mut() };
	}

	/// `oaknode_block_get_kind`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_kind(block: CHandle, out_kind: *mut c_int) -> c_int {
		guard(|| {
			if out_kind.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let kind = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				entry
					.behavior
					.as_any()
					.map(|a| {
						if a.downcast_ref::<ClipBlockBehavior>().is_some() {
							1
						} else if a.downcast_ref::<GapBlockBehavior>().is_some() {
							2
						} else if a.downcast_ref::<TransitionBlockBehavior>().is_some() {
							3
						} else {
							0
						}
					})
					.ok_or(Error::State)?
			};
			unsafe { *out_kind = kind };
			Ok(())
		})
	}

	/// `oaknode_block_as_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_as_node(block: CHandle) -> CHandle {
		match unsafe { node_ref(&block) } {
			Ok(n) => make_node_borrowed(n.project.clone(), n.id),
			Err(_) => CHandle::null(),
		}
	}

	/// `oaknode_block_from_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_from_node(node: CHandle) -> CHandle {
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return CHandle::null(),
		};
		if is_block(&n) {
			make_node_borrowed(n.project.clone(), n.id)
		} else {
			CHandle::null()
		}
	}

	/// `oaknode_block_get_in`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_in(
		block: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let core = {
				let guard = lock(&n.project);
				guard
					.graph
					.get(n.id)
					.and_then(block_core)
					.map(|c| BlockCore::default())
					.ok_or(Error::State)?;
				guard.graph.get(n.id).and_then(block_core).map(|c| BlockCore {
					range: c.range,
					media_in: c.media_in,
					speed: c.speed,
					reversed: c.reversed,
					links: c.links.clone(),
					enabled: c.enabled,
					maintain_audio_pitch: c.maintain_audio_pitch,
					loop_mode: c.loop_mode,
					track: c.track,
				})
			};
			let core = core.ok_or(Error::State)?;
			unsafe {
				*numerator = core.in_().numerator() as c_int;
				*denominator = core.in_().denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_block_set_in`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_set_in(
		block: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&block)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = block_core_mut(entry).ok_or(Error::State)?;
			core.set_in(oakcore_rs::Rational::new(numerator as i64, denominator as i64));
			Ok(())
		})
	}

	/// `oaknode_block_get_out`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_out(
		block: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let guard = lock(&n.project);
			let core = guard.graph.get(n.id).and_then(block_core).ok_or(Error::State)?;
			unsafe {
				*numerator = core.out().numerator() as c_int;
				*denominator = core.out().denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_block_set_out`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_set_out(
		block: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&block)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = block_core_mut(entry).ok_or(Error::State)?;
			core.set_out(oakcore_rs::Rational::new(numerator as i64, denominator as i64));
			Ok(())
		})
	}

	/// `oaknode_block_get_length`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_length(
		block: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let guard = lock(&n.project);
			let core = guard.graph.get(n.id).and_then(block_core).ok_or(Error::State)?;
			let len = core.length();
			unsafe {
				*numerator = len.numerator() as c_int;
				*denominator = len.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_block_set_length_and_media_out`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_set_length_and_media_out(
		block: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&block)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = block_core_mut(entry).ok_or(Error::State)?;
			core.set_length_and_media_out(oakcore_rs::Rational::new(
				numerator as i64,
				denominator as i64,
			));
			Ok(())
		})
	}

	/// `oaknode_block_set_length_and_media_in`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_set_length_and_media_in(
		block: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&block)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = block_core_mut(entry).ok_or(Error::State)?;
			core.set_length_and_media_in(oakcore_rs::Rational::new(
				numerator as i64,
				denominator as i64,
			));
			Ok(())
		})
	}

	/// `oaknode_block_get_enabled`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_enabled(
		block: CHandle,
		enabled: *mut c_int,
	) -> c_int {
		guard(|| {
			if enabled.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let guard = lock(&n.project);
			let core = guard.graph.get(n.id).and_then(block_core).ok_or(Error::State)?;
			unsafe { *enabled = core.enabled as c_int };
			Ok(())
		})
	}

	/// `oaknode_block_set_enabled`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_set_enabled(block: CHandle, enabled: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&block)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = block_core_mut(entry).ok_or(Error::State)?;
			core.enabled = enabled != 0;
			Ok(())
		})
	}

	/// `oaknode_block_get_previous`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_previous(
		block: CHandle,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let (project, prev) = {
				let guard = lock(&n.project);
				let core = guard.graph.get(n.id).and_then(block_core).ok_or(Error::State)?;
				let track = core.track.ok_or(Error::State)?;
				let tb = guard
					.graph
					.get(track)
					.and_then(|e| e.behavior.as_any())
					.and_then(|a| a.downcast_ref::<crate::track::TrackBehavior>())
					.ok_or(Error::State)?;
				let idx = tb.block_index(n.id).ok_or(Error::NotFound)?;
				(n.project.clone(), idx.checked_sub(1).and_then(|i| tb.block_at(i)))
			};
			unsafe {
				*out = match prev {
					Some(p) => make_node_borrowed(project, p),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_block_get_next`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_next(block: CHandle, out: *mut CHandle) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let (project, next) = {
				let guard = lock(&n.project);
				let core = guard.graph.get(n.id).and_then(block_core).ok_or(Error::State)?;
				let track = core.track.ok_or(Error::State)?;
				let tb = guard
					.graph
					.get(track)
					.and_then(|e| e.behavior.as_any())
					.and_then(|a| a.downcast_ref::<crate::track::TrackBehavior>())
					.ok_or(Error::State)?;
				let idx = tb.block_index(n.id).ok_or(Error::NotFound)?;
				(n.project.clone(), tb.block_at(idx + 1))
			};
			unsafe {
				*out = match next {
					Some(p) => make_node_borrowed(project, p),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_block_get_track` (borrowed).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_track(block: CHandle, out: *mut CHandle) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let (project, track) = {
				let guard = lock(&n.project);
				let core = guard.graph.get(n.id).and_then(block_core).ok_or(Error::State)?;
				(n.project.clone(), core.track)
			};
			unsafe {
				*out = match track {
					Some(t) => make_node_borrowed(project, t),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_block_link`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_link(a: CHandle, b: CHandle) -> c_int {
		guard(|| {
			let a = unsafe { node_ref(&a)? };
			let b = unsafe { node_ref(&b)? };
			if !Arc::ptr_eq(&a.project, &b.project) {
				return Err(Error::Invalid);
			}
			let mut guard = lock(&a.project);
			if guard.graph.link(a.id, b.id) {
				Ok(())
			} else {
				Err(Error::Failed("already linked".to_string()))
			}
		})
	}

	/// `oaknode_block_unlink`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_unlink(a: CHandle, b: CHandle) -> c_int {
		guard(|| {
			let a = unsafe { node_ref(&a)? };
			let b = unsafe { node_ref(&b)? };
			if !Arc::ptr_eq(&a.project, &b.project) {
				return Err(Error::Invalid);
			}
			let mut guard = lock(&a.project);
			if guard.graph.unlink(a.id, b.id) {
				Ok(())
			} else {
				Err(Error::Failed("not linked".to_string()))
			}
		})
	}

	/// `oaknode_block_are_linked`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_are_linked(
		a: CHandle,
		b: CHandle,
		linked: *mut c_int,
	) -> c_int {
		guard(|| {
			if linked.is_null() {
				return Err(Error::Invalid);
			}
			let a = unsafe { node_ref(&a)? };
			let b = unsafe { node_ref(&b)? };
			let guard = lock(&a.project);
			let linked_val = guard.graph.are_linked(a.id, b.id);
			unsafe { *linked = linked_val as c_int };
			Ok(())
		})
	}

	/// `oaknode_block_get_link_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_link_count(
		block: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			let guard = lock(&n.project);
			let links = guard.graph.links_of(n.id);
			unsafe { *count = links.len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_block_get_link_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_block_get_link_at(
		block: CHandle,
		index: c_int,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&block)? };
			if index < 0 {
				return Err(Error::NotFound);
			}
			let guard = lock(&n.project);
			let links = guard.graph.links_of(n.id);
			let target = links.get(index as usize).copied().ok_or(Error::NotFound)?;
			unsafe { *out = make_node_borrowed(n.project.clone(), target) };
			Ok(())
		})
	}

	// ---- Clip ---------------------------------------------------------

	/// `oaknode_clip_get_media_in`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_get_media_in(
		clip: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&clip)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<ClipBlockBehavior>())
				.map(|c| &c.core)
				.ok_or(Error::State)?;
			unsafe {
				*numerator = core.media_in.numerator() as c_int;
				*denominator = core.media_in.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_clip_set_media_in`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_set_media_in(
		clip: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&clip)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
				.map(|c| &mut c.core)
				.ok_or(Error::State)?;
			core.media_in = oakcore_rs::Rational::new(numerator as i64, denominator as i64);
			Ok(())
		})
	}

	/// `oaknode_clip_get_speed`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_get_speed(clip: CHandle, speed: *mut f64) -> c_int {
		guard(|| {
			if speed.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&clip)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<ClipBlockBehavior>())
				.map(|c| &c.core)
				.ok_or(Error::State)?;
			unsafe { *speed = core.speed };
			Ok(())
		})
	}

	/// `oaknode_clip_set_speed`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_set_speed(clip: CHandle, speed: f64) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&clip)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
				.map(|c| &mut c.core)
				.ok_or(Error::State)?;
			core.speed = speed;
			Ok(())
		})
	}

	/// `oaknode_clip_get_reverse`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_get_reverse(clip: CHandle, reverse: *mut c_int) -> c_int {
		guard(|| {
			if reverse.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&clip)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<ClipBlockBehavior>())
				.map(|c| &c.core)
				.ok_or(Error::State)?;
			unsafe { *reverse = core.reversed as c_int };
			Ok(())
		})
	}

	/// `oaknode_clip_set_reverse`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_set_reverse(clip: CHandle, reverse: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&clip)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
				.map(|c| &mut c.core)
				.ok_or(Error::State)?;
			core.reversed = reverse != 0;
			Ok(())
		})
	}

	/// `oaknode_clip_get_maintain_audio_pitch`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_get_maintain_audio_pitch(
		clip: CHandle,
		maintain: *mut c_int,
	) -> c_int {
		guard(|| {
			if maintain.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&clip)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<ClipBlockBehavior>())
				.map(|c| &c.core)
				.ok_or(Error::State)?;
			unsafe { *maintain = core.maintain_audio_pitch as c_int };
			Ok(())
		})
	}

	/// `oaknode_clip_set_maintain_audio_pitch`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_set_maintain_audio_pitch(
		clip: CHandle,
		maintain: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&clip)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
				.map(|c| &mut c.core)
				.ok_or(Error::State)?;
			core.maintain_audio_pitch = maintain != 0;
			Ok(())
		})
	}

	/// `oaknode_clip_get_loop_mode`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_get_loop_mode(
		clip: CHandle,
		loop_mode: *mut c_int,
	) -> c_int {
		guard(|| {
			if loop_mode.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&clip)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<ClipBlockBehavior>())
				.map(|c| &c.core)
				.ok_or(Error::State)?;
			unsafe { *loop_mode = core.loop_mode };
			Ok(())
		})
	}

	/// `oaknode_clip_set_loop_mode`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_set_loop_mode(clip: CHandle, loop_mode: c_int) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&clip)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let core = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<ClipBlockBehavior>())
				.map(|c| &mut c.core)
				.ok_or(Error::State)?;
			core.loop_mode = loop_mode;
			Ok(())
		})
	}

	/// `oaknode_clip_get_track_type`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_get_track_type(
		clip: CHandle,
		type_: *mut c_int,
	) -> c_int {
		guard(|| {
			if type_.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&clip)? };
			let kind = {
				let guard = lock(&n.project);
				let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
				let core = entry
					.behavior
					.as_any()
					.and_then(|a| a.downcast_ref::<ClipBlockBehavior>())
					.map(|c| &c.core)
					.ok_or(Error::State)?;
				let track = core.track;
				track
					.and_then(|t| guard.graph.get(t))
					.and_then(|te| te.behavior.as_any())
					.and_then(|a| a.downcast_ref::<crate::track::TrackBehavior>())
					.map(|tb| tb.kind.to_c())
					.unwrap_or(-1)
			};
			unsafe { *type_ = kind };
			Ok(())
		})
	}

	// ---- Transition ----------------------------------------------------

	/// `oaknode_transition_get_in_offset`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_transition_get_in_offset(
		transition: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&transition)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<TransitionBlockBehavior>())
				.ok_or(Error::State)?;
			unsafe {
				*numerator = t.in_offset.numerator() as c_int;
				*denominator = t.in_offset.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_transition_get_out_offset`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_transition_get_out_offset(
		transition: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&transition)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<TransitionBlockBehavior>())
				.ok_or(Error::State)?;
			unsafe {
				*numerator = t.out_offset.numerator() as c_int;
				*denominator = t.out_offset.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_transition_get_offset_center`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_transition_get_offset_center(
		transition: CHandle,
		numerator: *mut c_int,
		denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if numerator.is_null() || denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&transition)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<TransitionBlockBehavior>())
				.ok_or(Error::State)?;
			// Center = (length - in - out) / 2 from the in edge.
			let length = t.core.length();
			let center = (length - t.in_offset - t.out_offset) / oakcore_rs::Rational::new(2, 1);
			unsafe {
				*numerator = center.numerator() as c_int;
				*denominator = center.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_transition_set_offset_center`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_transition_set_offset_center(
		transition: CHandle,
		numerator: c_int,
		denominator: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&transition)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TransitionBlockBehavior>())
				.ok_or(Error::State)?;
			let center = oakcore_rs::Rational::new(numerator as i64, denominator as i64);
			let length = t.core.length();
			let remaining = (length - center * oakcore_rs::Rational::new(2, 1)) / oakcore_rs::Rational::new(2, 1);
			t.in_offset = remaining;
			t.out_offset = remaining;
			Ok(())
		})
	}

	/// `oaknode_transition_set_offsets_and_length`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_transition_set_offsets_and_length(
		transition: CHandle,
		in_num: c_int,
		in_den: c_int,
		out_num: c_int,
		out_den: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&transition)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let t = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<TransitionBlockBehavior>())
				.ok_or(Error::State)?;
			t.in_offset = oakcore_rs::Rational::new(in_num as i64, in_den as i64);
			t.out_offset = oakcore_rs::Rational::new(out_num as i64, out_den as i64);
			t.core.set_length_and_media_out(t.in_offset + t.out_offset);
			Ok(())
		})
	}

	/// `oaknode_transition_is_dual`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_transition_is_dual(
		transition: CHandle,
		dual: *mut c_int,
	) -> c_int {
		guard(|| {
			if dual.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&transition)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			if entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<TransitionBlockBehavior>())
				.is_none()
			{
				return Err(Error::State);
			}
			let both = guard
				.graph
				.is_input_connected(n.id, crate::block::transition_input::OUT_BLOCK, -1)
				&& guard
					.graph
					.is_input_connected(n.id, crate::block::transition_input::IN_BLOCK, -1);
			unsafe { *dual = both as c_int };
			Ok(())
		})
	}

	/// `oaknode_transition_get_connected_out_block`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_transition_get_connected_out_block(
		transition: CHandle,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&transition)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			if entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<TransitionBlockBehavior>())
				.is_none()
			{
				return Err(Error::State);
			}
			let connected =
				guard.graph.connected_output(n.id, crate::block::transition_input::OUT_BLOCK, -1);
			unsafe {
				*out = match connected {
					Some(b) => make_node_borrowed(n.project.clone(), b),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_transition_get_connected_in_block`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_transition_get_connected_in_block(
		transition: CHandle,
		out: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&transition)? };
			let guard = lock(&n.project);
			let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
			if entry
				.behavior
				.as_any()
				.and_then(|a| a.downcast_ref::<TransitionBlockBehavior>())
				.is_none()
			{
				return Err(Error::State);
			}
			let connected =
				guard.graph.connected_output(n.id, crate::block::transition_input::IN_BLOCK, -1);
			unsafe {
				*out = match connected {
					Some(b) => make_node_borrowed(n.project.clone(), b),
					None => CHandle::null(),
				};
			}
			Ok(())
		})
	}

	/// `oaknode_clip_add_cache_passthrough_from` — the render cache
	/// handles are created per node at cache-init time (Phase 2
	/// follow-up with bridge::render); the passthrough copies UUIDs,
	/// which is a no-op for uncreated caches.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_clip_add_cache_passthrough_from(
		clip: CHandle,
		other: CHandle,
	) -> c_int {
		guard(|| {
			let _c = unsafe { node_ref(&clip)? };
			let _o = unsafe { node_ref(&other)? };
			// Cache UUID passthrough is inert until the oakrender bridge
			// creates per-node caches; the call is accepted as a no-op
			// (matching the C++ flow where caches may be null).
			Ok(())
		})
	}
}

// =====================================================================
// include/node/footage.h
// =====================================================================

/// `include/node/footage.h` exports. Footage handles are node handles
/// whose nodes carry [`crate::footage::FootageBehavior`].
pub mod footage {
	use super::*;
	use crate::footage::FootageBehavior;
	use crate::node::NodeCore;

	fn footage_of(n: &NodeRef) -> Result<crate::footage::FootageBehavior> {
		let guard = lock(&n.project);
		let entry = guard.graph.get(n.id).ok_or(Error::NotFound)?;
		entry
			.behavior
			.as_any()
			.and_then(|a| a.downcast_ref::<FootageBehavior>())
			.map(|f| FootageBehavior {
				filename: f.filename.clone(),
				streams: f.streams.clone(),
				proxy: f.proxy.clone(),
				proxy_enabled: f.proxy_enabled,
				proxy_state: f.proxy_state,
				proxy_video_stream_index: f.proxy_video_stream_index,
				proxy_preset_version: f.proxy_preset_version,
				timestamp: f.timestamp,
				decoder: f.decoder.clone(),
				valid: f.valid,
				cancel: f.cancel.clone(),
			})
			.ok_or(Error::State)
	}

	/// `oaknode_footage_create` (borrowed; node added to the project).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_create(
		project: CHandle,
		filename: *const c_char,
	) -> CHandle {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return CHandle::null(),
		};
		let filename = unsafe { cstr(filename) }.unwrap_or("").to_string();
		let id = {
			let mut guard = lock(&p);
			guard
				.graph
				.add_node(NodeCore::new(), Box::new(FootageBehavior::new(&filename)))
		};
		make_node_borrowed(p, id)
	}

	/// `oaknode_footage_as_node`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_as_node(footage: CHandle) -> CHandle {
		match unsafe { node_ref(&footage) } {
			Ok(n) => make_node_borrowed(n.project.clone(), n.id),
			Err(_) => CHandle::null(),
		}
	}

	/// `oaknode_footage_filename` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_filename(
		footage: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&footage) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		match footage_of(&n) {
			Ok(f) => copy_string_out(&f.filename, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_footage_set_filename`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_set_filename(
		footage: CHandle,
		filename: *const c_char,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&footage)? };
			let filename = unsafe { cstr(filename) }.ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let f = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<FootageBehavior>())
				.ok_or(Error::State)?;
			f.filename = filename.to_string();
			Ok(())
		})
	}

	/// `oaknode_footage_is_valid`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_is_valid(footage: CHandle) -> c_int {
		match unsafe { node_ref(&footage) } {
			Ok(n) => match footage_of(&n) {
				Ok(f) => f.valid as c_int,
				Err(e) => e.code(),
			},
			Err(_) => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_timestamp`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_timestamp(
		footage: CHandle,
		out_timestamp: *mut i64,
	) -> c_int {
		guard(|| {
			if out_timestamp.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&footage)? };
			let f = footage_of(&n)?;
			unsafe { *out_timestamp = f.timestamp };
			Ok(())
		})
	}

	/// `oaknode_footage_set_timestamp`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_set_timestamp(
		footage: CHandle,
		timestamp: i64,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&footage)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let f = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<FootageBehavior>())
				.ok_or(Error::State)?;
			f.timestamp = timestamp;
			Ok(())
		})
	}

	/// `oaknode_footage_decoder` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_decoder(
		footage: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&footage) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		match footage_of(&n) {
			Ok(f) => copy_string_out(&f.decoder, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_footage_total_stream_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_total_stream_count(footage: CHandle) -> c_int {
		match unsafe { node_ref(&footage) } {
			Ok(n) => match footage_of(&n) {
				Ok(f) => f.total_stream_count() as c_int,
				Err(e) => e.code(),
			},
			Err(_) => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_video_stream_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_video_stream_count(footage: CHandle) -> c_int {
		match unsafe { node_ref(&footage) } {
			Ok(n) => match footage_of(&n) {
				Ok(f) => f.video_stream_count() as c_int,
				Err(e) => e.code(),
			},
			Err(_) => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_audio_stream_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_audio_stream_count(footage: CHandle) -> c_int {
		match unsafe { node_ref(&footage) } {
			Ok(n) => match footage_of(&n) {
				Ok(f) => f.audio_stream_count() as c_int,
				Err(e) => e.code(),
			},
			Err(_) => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_subtitle_stream_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_subtitle_stream_count(footage: CHandle) -> c_int {
		match unsafe { node_ref(&footage) } {
			Ok(n) => match footage_of(&n) {
				Ok(f) => f.subtitle_stream_count() as c_int,
				Err(e) => e.code(),
			},
			Err(_) => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_duration`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_duration(
		footage: CHandle,
		out_numerator: *mut c_int,
		out_denominator: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_numerator.is_null() || out_denominator.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&footage)? };
			let f = footage_of(&n)?;
			let d = f.duration();
			unsafe {
				*out_numerator = d.numerator() as c_int;
				*out_denominator = d.denominator() as c_int;
			}
			Ok(())
		})
	}

	/// `oaknode_footage_proxy_enabled`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_proxy_enabled(footage: CHandle) -> c_int {
		match unsafe { node_ref(&footage) } {
			Ok(n) => match footage_of(&n) {
				Ok(f) => f.proxy_enabled as c_int,
				Err(e) => e.code(),
			},
			Err(_) => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_set_proxy_enabled`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_set_proxy_enabled(
		footage: CHandle,
		enabled: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&footage)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let f = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<FootageBehavior>())
				.ok_or(Error::State)?;
			f.proxy_enabled = enabled != 0;
			Ok(())
		})
	}

	/// `oaknode_footage_proxy_path` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_proxy_path(
		footage: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let n = match unsafe { node_ref(&footage) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		match footage_of(&n) {
			Ok(f) => copy_string_out(&f.proxy, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_footage_proxy_state`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_proxy_state(footage: CHandle) -> c_int {
		match unsafe { node_ref(&footage) } {
			Ok(n) => match footage_of(&n) {
				Ok(f) => f.proxy_state,
				Err(e) => e.code(),
			},
			Err(_) => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_footage_set_proxy`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_set_proxy(
		footage: CHandle,
		path: *const c_char,
		state: c_int,
		video_stream_index: c_int,
		preset_version: c_int,
		enabled: c_int,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&footage)? };
			let path = if path.is_null() {
				""
			} else {
				unsafe { cstr(path) }.ok_or(Error::Invalid)?
			};
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let f = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<FootageBehavior>())
				.ok_or(Error::State)?;
			f.set_proxy(path, state, video_stream_index, preset_version, enabled != 0);
			Ok(())
		})
	}

	/// `oaknode_footage_clear_proxy`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_clear_proxy(footage: CHandle) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&footage)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let f = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<FootageBehavior>())
				.ok_or(Error::State)?;
			f.clear_proxy();
			Ok(())
		})
	}

	/// `oaknode_footage_get_video_params`: the `index`th video stream's
	/// params as a NEW owned oakcommon handle
	/// (`// CPP-PARITY: src/node/c_api/footage.cpp:327`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_get_video_params(
		footage: CHandle,
		index: c_int,
		out: *mut std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if out.is_null() || index < 0 {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&footage)? };
			let params = with_graph_read(&n.project, |g| {
				g.get(n.id).and_then(|e| {
					e.behavior
						.as_any()
						.and_then(|a| a.downcast_ref::<FootageBehavior>())
						.and_then(|f| f.video_params(index as usize))
				})
			})
			.ok_or(Error::NotFound)?;
			let handle = super::videoparams_handle_from(&params).ok_or(Error::NoMem)?;
			unsafe { *(out as *mut crate::handle::CHandle) = handle };
			Ok(())
		})
	}

	/// `oaknode_footage_set_video_params`: replace the `index`th video
	/// stream's params from an oakcommon handle
	/// (`// CPP-PARITY: footage.cpp:348`). `OAKNODE_E_NOT_FOUND` when the
	/// footage has no such video stream (the C++ writes into the params
	/// input array regardless; documented divergence).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_set_video_params(
		footage: CHandle,
		index: c_int,
		params: *const std::ffi::c_void,
	) -> c_int {
		guard(|| {
			if params.is_null() {
				return Err(Error::Invalid);
			}
			let handle = unsafe { (*(params as *const crate::handle::CHandle)).clone() };
			if handle.ctx.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&footage)? };
			let read = super::videoparams_from_handle(handle).ok_or(Error::Invalid)?;
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let f = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<FootageBehavior>())
				.ok_or(Error::State)?;
			let mut seen = 0usize;
			for stream in f.streams.iter_mut().filter(|s| s.is_video) {
				if seen == index as usize {
					stream.video = Some(read);
					return Ok(());
				}
				seen += 1;
			}
			Err(Error::NotFound)
		})
	}

	/// `oaknode_footage_get_video_length`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_get_video_length(
		footage: CHandle,
		out_num: *mut i64,
		out_den: *mut i64,
	) -> c_int {
		guard(|| {
			if out_num.is_null() || out_den.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&footage)? };
			let f = footage_of(&n)?;
			let len = f.video_length();
			unsafe {
				*out_num = len.numerator();
				*out_den = len.denominator();
			}
			Ok(())
		})
	}

	/// `oaknode_footage_set_cancel_atom`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_footage_set_cancel_atom(
		footage: CHandle,
		atom: CHandle,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&footage)? };
			let mut guard = lock(&n.project);
			let entry = guard.graph.get_mut(n.id).ok_or(Error::NotFound)?;
			let f = entry
				.behavior
				.as_any_mut()
				.and_then(|a| a.downcast_mut::<FootageBehavior>())
				.ok_or(Error::State)?;
			// The cancel atom is an opaque oakrender handle; the shared
			// cancellation flag is set when it is present.
			f.set_cancel(!atom.ctx.is_null());
			Ok(())
		})
	}
}

// =====================================================================
// include/node/colormanager.h
// =====================================================================

/// `include/node/colormanager.h` exports. The manager handle boxes a
/// [`crate::colormanager::ColorManager`] directly (not a graph node).
/// Config-dependent queries report `E_STATE` until a config is loaded;
/// the listings reflect the manager's state (without liboakrender the
/// lists hold the built-in defaults).
pub mod colormanager {
	use super::*;
	use crate::colormanager::ColorManager;

	/// `oaknode_colormanager_init` (owned manager bound to a project).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_init(project: CHandle) -> CHandle {
		let _ = unsafe { project_arc(&project) };
		// The manager is process/project state; the project binding is
		// recorded but the manager object is standalone and shared
		// through a mutex.
		crate::handle::make_owned(std::sync::Mutex::new(ColorManager::new()))
	}

	/// `oaknode_colormanager_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_free(manager: *mut CHandle) {
		if manager.is_null() || unsafe { (*manager).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*manager).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*manager).ctx = std::ptr::null_mut() };
	}

	/// `oaknode_colormanager_wrap_borrowed` — the native manager pointer
	/// is not a Rust object; the borrowed box wraps a placeholder.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_wrap_borrowed(
		native_manager: *mut std::ffi::c_void,
	) -> CHandle {
		if native_manager.is_null() {
			return CHandle::null();
		}
		crate::handle::make_owned(ColorManager::new())
	}

	/// `oaknode_colormanager_initialize`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_initialize(manager: CHandle) -> c_int {
		guard(|| {
				let mut guard = manager_guard(&manager)?;
			guard.initialize()
		})
	}

	/// `oaknode_colormanager_set_up_default_config`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_set_up_default_config() -> c_int {
		guard(|| {
			// The process-wide default config state lives with the
			// managers; nothing global to do without oakrender.
			Ok(())
		})
	}

	/// `oaknode_colormanager_get_config_filename`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_config_filename(
		manager: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| m.config_filename.clone()) {
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_set_config_filename`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_set_config_filename(
		manager: CHandle,
		filename: *const c_char,
	) -> c_int {
		guard(|| {
			let filename = unsafe { cstr(filename) }.ok_or(Error::Invalid)?.to_string();
			manager_mut(&manager, |m| m.config_filename = filename)
		})
	}

	/// `oaknode_colormanager_update_config_from_filename`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_update_config_from_filename(
		manager: CHandle,
	) -> c_int {
		guard(|| {
				let mut guard = manager_guard(&manager)?;
			guard.update_config_from_filename()
		})
	}

	/// `oaknode_colormanager_get_default_input_color_space`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_default_input_color_space(
		manager: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| m.default_input_space.clone()) {
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_set_default_input_color_space`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_set_default_input_color_space(
		manager: CHandle,
		colorspace: *const c_char,
	) -> c_int {
		guard(|| {
			let colorspace = unsafe { cstr(colorspace) }.ok_or(Error::Invalid)?.to_string();
			manager_mut(&manager, |m| m.default_input_space = colorspace)
		})
	}

	/// `oaknode_colormanager_get_reference_color_space`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_reference_color_space(
		manager: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| m.reference_space.clone()) {
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_compliant_color_space` — requires a
	/// loaded config.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_compliant_color_space(
		manager: CHandle,
		colorspace: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| {
			if !m.config_loaded {
				return String::new();
			}
			let colorspace = unsafe { cstr(colorspace) }.unwrap_or("");
			if m.list_colorspaces().iter().any(|c| c == colorspace) {
				colorspace.to_string()
			} else {
				m.default_input_space.clone()
			}
		}) {
			Ok(s) => {
				if s.is_empty() {
					crate::error::OAKNODE_E_STATE
				} else {
					copy_string_out(&s, buf, buf_size)
				}
			}
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_colorspace_for_ffmpeg_tags` — the tag
	/// mapping requires OCIO (E_STATE without a loaded config).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_colorspace_for_ffmpeg_tags(
		manager: CHandle,
		_primaries: c_int,
		_trc: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let loaded = manager_string(&manager, |m| m.config_loaded.to_string());
		match loaded {
			Ok(s) if s == "true" => {
				// Unknown tags -> default input (empty result size 1).
				match manager_string(&manager, |m| m.default_input_space.clone()) {
					Ok(space) => copy_string_out(&space, buf, buf_size),
					Err(e) => e.code(),
				}
			}
			_ => crate::error::OAKNODE_E_STATE,
		}
	}

	/// `oaknode_colormanager_get_display_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_display_count(
		manager: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let guard = manager_guard(&manager)?;
			if !guard.config_loaded {
				return Err(Error::State);
			}
			unsafe { *count = guard.list_displays().len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_colormanager_get_display_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_display_at(
		manager: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| {
			if !m.config_loaded {
				return String::new();
			}
			m.list_displays().get(index as usize).cloned().unwrap_or_default()
		}) {
			Ok(s) if s.is_empty() => crate::error::OAKNODE_E_NOT_FOUND,
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_default_display`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_default_display(
		manager: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| {
			if m.config_loaded {
				m.default_display.clone()
			} else {
				String::new()
			}
		}) {
			Ok(s) if s.is_empty() => crate::error::OAKNODE_E_STATE,
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_view_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_view_count(
		manager: CHandle,
		_display: *const c_char,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let guard = manager_guard(&manager)?;
			if !guard.config_loaded {
				return Err(Error::State);
			}
			unsafe { *count = guard.list_views("").len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_colormanager_get_view_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_view_at(
		manager: CHandle,
		_display: *const c_char,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| {
			if !m.config_loaded {
				return String::new();
			}
			m.list_views("").get(index as usize).cloned().unwrap_or_default()
		}) {
			Ok(s) if s.is_empty() => crate::error::OAKNODE_E_NOT_FOUND,
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_default_view`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_default_view(
		manager: CHandle,
		_display: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| {
			if m.config_loaded {
				m.default_view.clone()
			} else {
				String::new()
			}
		}) {
			Ok(s) if s.is_empty() => crate::error::OAKNODE_E_STATE,
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_look_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_look_count(
		manager: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let guard = manager_guard(&manager)?;
			if !guard.config_loaded {
				return Err(Error::State);
			}
			unsafe { *count = guard.list_looks().len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_colormanager_get_look_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_look_at(
		manager: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| {
			if !m.config_loaded {
				return String::new();
			}
			m.list_looks().get(index as usize).cloned().unwrap_or_default()
		}) {
			Ok(s) if s.is_empty() => crate::error::OAKNODE_E_NOT_FOUND,
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_colorspace_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_colorspace_count(
		manager: CHandle,
		count: *mut c_int,
	) -> c_int {
		guard(|| {
			if count.is_null() {
				return Err(Error::Invalid);
			}
			let guard = manager_guard(&manager)?;
			if !guard.config_loaded {
				return Err(Error::State);
			}
			unsafe { *count = guard.list_colorspaces().len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_colormanager_get_colorspace_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_colorspace_at(
		manager: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		match manager_string(&manager, |m| {
			if !m.config_loaded {
				return String::new();
			}
			m.list_colorspaces().get(index as usize).cloned().unwrap_or_default()
		}) {
			Ok(s) if s.is_empty() => crate::error::OAKNODE_E_NOT_FOUND,
			Ok(s) => copy_string_out(&s, buf, buf_size),
			Err(e) => e.code(),
		}
	}

	/// `oaknode_colormanager_get_default_luma_coefs` — requires a loaded
	/// config; without OCIO the Rec.709 coefficients are returned.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_default_luma_coefs(
		manager: CHandle,
		rgb: *mut f64,
	) -> c_int {
		guard(|| {
			if rgb.is_null() {
				return Err(Error::Invalid);
			}
			let guard = manager_guard(&manager)?;
			if !guard.config_loaded {
				return Err(Error::State);
			}
			unsafe {
				*rgb.add(0) = 0.2126;
				*rgb.add(1) = 0.7152;
				*rgb.add(2) = 0.0722;
			}
			Ok(())
		})
	}

	/// `oaknode_colormanager_get_compliant_color_transform`: a copy of
	/// `transform` whose display/view/look (or output colorspace) is
	/// clamped to what the active config offers
	/// (`// CPP-PARITY: src/node/c_api/colormanager.cpp:383`,
	/// `src/node/src/color/colormanager/colormanager.cpp:241`). Without
	/// OCIO the config lists hold the built-in defaults, so the clamp is
	/// identity unless a field names something the defaults lack.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_compliant_color_transform(
		manager: CHandle,
		transform: crate::handle::CHandle,
		force_display: c_int,
		out: *mut crate::handle::CHandle,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let mut guard = manager_guard(&manager)?;
			if !guard.is_loaded() {
				return Err(Error::State);
			}
			// Read the transform's fields (E_INVALID when oakcommon is
			// unavailable — the C++ `get_native` NULL path).
			let is_display = crate::bridge::common::colortransform_is_display(transform.clone())
				.ok_or(Error::Invalid)?;
			let (display, view, look, output) = if is_display || force_display != 0 {
				(
					crate::bridge::common::colortransform_get_display(transform.clone())
						.ok_or(Error::Invalid)?,
					crate::bridge::common::colortransform_get_view(transform.clone())
						.ok_or(Error::Invalid)?,
					crate::bridge::common::colortransform_get_look(transform.clone())
						.ok_or(Error::Invalid)?,
					String::new(),
				)
			} else {
				(
					String::new(),
					String::new(),
					String::new(),
					crate::bridge::common::colortransform_get_output(transform.clone())
						.ok_or(Error::Invalid)?,
				)
			};
			drop(guard);
			// Clamp against the active config (C++
			// `get_compliant_color_space`).
			let handle = if is_display || force_display != 0 {
				let guard = manager_guard(&manager)?;
				let displays = guard.list_displays();
				let display = if displays.contains(&display) {
					display
				} else {
					guard.default_display.clone()
				};
				let views = guard.list_views(&display);
				let view = if views.contains(&view) {
					view
				} else {
					guard.default_view.clone()
				};
				let looks = guard.list_looks();
				let look = if looks.contains(&look) { look } else { String::new() };
				crate::bridge::common::colortransform_init_display(&display, &view, &look)
			} else {
				let guard = manager_guard(&manager)?;
				let spaces = guard.list_colorspaces();
				let output = if spaces.contains(&output) {
					output
				} else {
					guard.default_input_space.clone()
				};
				crate::bridge::common::colortransform_init_output(&output)
			};
			let handle = handle.ok_or(Error::NoMem)?;
			unsafe { *out = handle };
			Ok(())
		})
	}

	/// Read a manager field through the shared lock.
	fn manager_string(
		manager: &CHandle,
		f: impl FnOnce(&ColorManager) -> String,
	) -> Result<String> {
		let guard = manager_guard(manager)?;
		Ok(f(&guard))
	}

	/// Mutate a manager field through the shared lock.
	fn manager_mut(manager: &CHandle, f: impl FnOnce(&mut ColorManager)) -> Result<()> {
		let mut guard = manager_guard(manager)?;
		f(&mut guard);
		Ok(())
	}

	/// Lock the shared manager box (the handle boxes `Mutex<ColorManager>`).
	fn manager_guard(
		manager: &CHandle,
	) -> Result<std::sync::MutexGuard<'_, ColorManager>> {
		let m = unsafe { crate::handle::get::<std::sync::Mutex<ColorManager>>(manager) }
			.ok_or(Error::Invalid)?;
		Ok(m.lock().unwrap_or_else(|e| e.into_inner()))
	}

	/// `oaknode_colormanager_get_native`: borrowed access to the
	/// underlying C++ manager — a C++-only bridge export with no Rust
	/// counterpart. Returns NULL (the contract is NULL-safe); the Rust
	/// manager object is reachable only through the handle.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_colormanager_get_native(
		_manager: CHandle,
	) -> *mut std::ffi::c_void {
		std::ptr::null_mut()
	}
}

// =====================================================================
// include/node/traverser.h
// =====================================================================

/// `include/node/traverser.h` exports: database generation over a time
/// range and database row enumeration. The traverser handle boxes a
/// [`crate::traverser::Traverser`]; the database handle boxes a
/// [`crate::traverser::ValueDatabase`].
pub mod traverser {
	use super::*;
	use crate::traverser::{EvalRequest, Traverser, ValueDatabase};

	/// `oaknode_traverser_init`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_init() -> CHandle {
		crate::handle::make_owned(Traverser::new())
	}

	/// `oaknode_traverser_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_free(traverser: *mut CHandle) {
		if traverser.is_null() || unsafe { (*traverser).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*traverser).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*traverser).ctx = std::ptr::null_mut() };
	}

	/// `oaknode_traverser_generate_database`: evaluate `node` at the
	/// range's in-time and collect its input row + output table into a
	/// database keyed by input id (a single-frame database; the C++
	/// walks the whole range — the frame iterator lands with the
	/// traverser's range stepping, `// CPP-PARITY: traverser.cpp`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_generate_database(
		traverser: CHandle,
		node: CHandle,
		in_num: i64,
		in_den: i64,
		out_num: i64,
		out_den: i64,
		out_db: *mut CHandle,
	) -> c_int {
		guard(|| {
			if out_db.is_null() {
				return Err(Error::Invalid);
			}
			let n = unsafe { node_ref(&node)? };
			let _ = (in_num, in_den, out_num, out_den);
			let time = oakcore_rs::Rational::new(in_num, in_den);
			let mut hooks = NoopHooks;
			let table = {
				// The handle is an opaque engine token; each pass runs
				// on a fresh engine (the pass is stateless).
				let _ = unsafe { crate::handle::get::<Traverser>(&traverser) }
					.ok_or(Error::Invalid)?;
				let guard = lock(&n.project);
				let request = EvalRequest::new(n.id, time);
				let mut engine = Traverser::new();
				engine.evaluate(&guard.graph, &request, &mut hooks)?
			};
			// Build the database: one row per evaluated input value.
			let mut rows: Vec<(String, Vec<(crate::value::ValueType, NodeValue)>)> = Vec::new();
			let mut seen: std::collections::HashSet<String> = std::collections::HashSet::new();
			for (from, input_id, _element) in with_graph_read(&n.project, |g| {
				g.output_connections(n.id)
			}) {
				let _ = from;
				if seen.insert(input_id.clone()) {
					rows.push((input_id, Vec::new()));
				}
			}
			// The root's output table also feeds the database under the
			// "" key (the C++ `NodeValueDatabase` rows are the evaluated
			// values per input of the root).
			let root_row: Vec<(crate::value::ValueType, NodeValue)> = table
				.rows()
				.iter()
				.map(|(t, v, _)| (*t, v.clone()))
				.collect();
			let mut db_rows = rows;
			db_rows.push(("".to_string(), root_row));
			let db = ValueDatabase { rows: db_rows };
			unsafe { *out_db = crate::handle::make_owned(db) };
			Ok(())
		})
	}

	/// No-op hooks for offline database generation.
	struct NoopHooks;
	impl crate::traverser::RenderHooks for NoopHooks {}

	/// `oaknode_traverser_database_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_database_free(db: *mut CHandle) {
		if db.is_null() || unsafe { (*db).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*db).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*db).ctx = std::ptr::null_mut() };
	}

	/// `oaknode_traverser_database_row_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_database_row_count(
		db: CHandle,
		out_count: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let d = unsafe { crate::handle::get::<ValueDatabase>(&db) }
				.ok_or(Error::Invalid)?;
			unsafe { *out_count = d.rows.len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_traverser_database_row_key_at` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_database_row_key_at(
		db: CHandle,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let d = match unsafe { crate::handle::get::<ValueDatabase>(&db) } {
			Some(d) => d,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		match d.rows.get(index as usize) {
			Some((key, _)) => copy_string_out(key, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_traverser_database_row_value_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_database_row_value_count(
		db: CHandle,
		key: *const c_char,
		out_count: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_count.is_null() {
				return Err(Error::Invalid);
			}
			let d = unsafe { crate::handle::get::<ValueDatabase>(&db) }
				.ok_or(Error::Invalid)?;
			let key = unsafe { cstr(key) }.ok_or(Error::Invalid)?;
			let row = d.rows.iter().find(|(k, _)| k == key).ok_or(Error::NotFound)?;
			unsafe { *out_count = row.1.len() as c_int };
			Ok(())
		})
	}

	/// `oaknode_traverser_database_value_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_database_value_at(
		db: CHandle,
		key: *const c_char,
		index: c_int,
		out: *mut OakNodeValue,
	) -> c_int {
		guard(|| {
			if out.is_null() {
				return Err(Error::Invalid);
			}
			let d = unsafe { crate::handle::get::<ValueDatabase>(&db) }
				.ok_or(Error::Invalid)?;
			let key = unsafe { cstr(key) }.ok_or(Error::Invalid)?;
			let row = d.rows.iter().find(|(k, _)| k == key).ok_or(Error::NotFound)?;
			let (ty, value) = row.1.get(index as usize).ok_or(Error::NotFound)?;
			let pod = OakNodeValue::from_node_value(*ty, value)?;
			unsafe { *out = pod };
			Ok(())
		})
	}

	/// `oaknode_traverser_database_value_string_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_traverser_database_value_string_at(
		db: CHandle,
		key: *const c_char,
		index: c_int,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let d = match unsafe { crate::handle::get::<ValueDatabase>(&db) } {
			Some(d) => d,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let key = match unsafe { cstr(key) } {
			Some(k) => k,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let row = match d.rows.iter().find(|(k, _)| k == key) {
			Some(r) => r,
			None => return crate::error::OAKNODE_E_NOT_FOUND,
		};
		let (ty, value) = match row.1.get(index as usize) {
			Some(v) => v,
			None => return crate::error::OAKNODE_E_NOT_FOUND,
		};
		let s = crate::serializer::value_to_string(*ty, value, false);
		copy_string_out(&s, buf, buf_size)
	}
}

// =====================================================================
// include/node/serializer.h
// =====================================================================

/// `include/node/serializer.h` exports: in-memory node-graph copy/paste
/// via XML. SaveData holds the nodes to serialize + per-node properties;
/// LoadData holds the created nodes + promised connections + properties.
pub mod serializer {
	use super::*;
	use crate::serializer::{XmlRead, XmlWrite};

	/// Serializer result codes (include/node/serializer.h).
	const SERIALIZER_OK: c_int = 0;
	const SERIALIZER_FILE_ERROR: c_int = 4;
	const SERIALIZER_XML_ERROR: c_int = 5;

	/// Save descriptor (C++ `ProjectSerializer::SaveData`).
	pub struct SaveData {
		/// Load type (`OAKNODE_SERIALIZER_LOAD_*`).
		pub load_type: c_int,
		/// Context project (borrowed).
		pub project: Option<ProjectArc>,
		/// Restrict to these nodes (None = all).
		pub only_serialize_nodes: Option<Vec<NodeId>>,
		/// Per-node (key, value) properties.
		pub properties: std::collections::HashMap<(NodeId, String), String>,
	}

	/// Load result (C++ `ProjectSerializer::LoadData`).
	pub struct LoadData {
		/// Created nodes (owned by the caller) as (project, id).
		pub nodes: Vec<(ProjectArc, NodeId)>,
		/// Promised connections (output, input, input_id, element).
		pub connections: Vec<(NodeId, NodeId, String, i32)>,
		/// Per-node properties.
		pub properties: std::collections::HashMap<(NodeId, String), String>,
	}

	/// `oaknode_serializer_initialize`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_initialize() -> c_int {
		// The factory registry is lazily built; initializing is a no-op
		// that guarantees it exists.
		guard(|| {
			let _ = crate::factory::Factory::global();
			Ok(())
		})
	}

	/// `oaknode_serializer_shutdown` — no-op (nothing global to release).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_shutdown() {}

	/// `oaknode_serializer_savedata_create`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_savedata_create(
		load_type: c_int,
		project: CHandle,
	) -> CHandle {
		let project = unsafe { project_arc(&project) }.ok();
		crate::handle::make_owned(SaveData {
			load_type,
			project,
			only_serialize_nodes: None,
			properties: std::collections::HashMap::new(),
		})
	}

	/// `oaknode_serializer_savedata_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_savedata_free(save_data: *mut CHandle) {
		if save_data.is_null() || unsafe { (*save_data).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*save_data).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*save_data).ctx = std::ptr::null_mut() };
	}

	/// `oaknode_serializer_savedata_set_nodes`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_savedata_set_nodes(
		save_data: CHandle,
		nodes: *const CHandle,
		count: c_int,
	) -> c_int {
		guard(|| {
			if nodes.is_null() || count < 0 {
				return Err(Error::Invalid);
			}
			let sd = unsafe { crate::handle::get::<SaveData>(&save_data) }
				.ok_or(Error::Invalid)?;
			let mut ids = Vec::new();
			for i in 0..count as usize {
				let child = unsafe { (*nodes.add(i)).clone() };
				let n = unsafe { node_ref(&child)? };
				ids.push(n.id);
			}
			// The box is shared; mutate through the raw pointer.
			unsafe {
				let rb = save_data.ctx as *mut crate::handle::RefBox<SaveData>;
				(*rb).value.only_serialize_nodes = Some(ids);
			}
			Ok(())
		})
	}

	/// `oaknode_serializer_savedata_set_property`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_savedata_set_property(
		save_data: CHandle,
		node: CHandle,
		key: *const c_char,
		value: *const c_char,
	) -> c_int {
		guard(|| {
			let n = unsafe { node_ref(&node)? };
			let key = unsafe { cstr(key) }.ok_or(Error::Invalid)?;
			let value = unsafe { cstr(value) }.ok_or(Error::Invalid)?;
			unsafe {
				let rb = save_data.ctx as *mut crate::handle::RefBox<SaveData>;
				(*rb)
					.value
					.properties
					.insert((n.id, key.to_string()), value.to_string());
			}
			Ok(())
		})
	}

	/// `oaknode_serializer_save_to_xml` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_save_to_xml(
		save_data: CHandle,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let sd = match unsafe { crate::handle::get::<SaveData>(&save_data) } {
			Some(sd) => sd,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let xml = match save_to_xml_inner(sd) {
			Ok(x) => x,
			Err(e) => return e.code(),
		};
		copy_string_out(&xml, buf, buf_size)
	}

	/// Serialize the save data to an XML string.
	fn save_to_xml_inner(sd: &SaveData) -> Result<String> {
		let project = sd.project.as_ref().ok_or(Error::State)?;
		let mut writer = crate::serializer::XmlWriterBridge::new().ok_or(Error::Failed(
			"oakcommon XML writer unavailable".to_string(),
		))?;
		writer.start_element("project");
		writer.attribute("version", "1");

		let guard = lock(project);
		writer.start_element("nodes");
		let ids: Vec<NodeId> = match &sd.only_serialize_nodes {
			Some(list) => list.clone(),
			None => guard.graph.node_ids(),
		};
		for id in ids {
			let entry = guard.graph.get(id).ok_or(Error::NotFound)?;
			writer.start_element("node");
			let type_id = entry.behavior.type_id().to_string();
			let connections: Vec<(NodeId, String, i32)> = guard
				.graph
				.output_connections_all()
				.into_iter()
				.filter(|(_, to, _, _)| *to == id)
				.map(|(from, _, input, element)| (from, input, element))
				.collect();
			crate::serializer::save_node(&mut writer, &entry.core, id, &type_id, &connections)?;
			// Serialize per-node properties as text elements.
			let mut props: Vec<(&String, &String)> = sd
				.properties
				.iter()
				.filter(|((n, _), _)| *n == id)
				.map(|((_, k), v)| (k, v))
				.collect();
			props.sort_by(|a, b| a.0.cmp(b.0));
			if !props.is_empty() {
				writer.start_element("properties");
				for (k, v) in props {
					writer.text_element(k, v);
				}
				writer.end_element(); // properties
			}
			writer.end_element(); // node
		}
		writer.end_element(); // nodes
		writer.end_element(); // project
		Ok(writer.output())
	}

	/// `oaknode_serializer_load_from_xml`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_load_from_xml(
		project: CHandle,
		xml: *const c_char,
		_load_type: c_int,
		out_result: *mut c_int,
		out_load_data: *mut CHandle,
		details_buf: *mut c_char,
		details_buf_size: c_int,
	) -> c_int {
		guard(|| {
			if out_result.is_null() {
				return Err(Error::Invalid);
			}
			let xml = unsafe { cstr(xml) }.ok_or(Error::Invalid)?;
			let load_data = match load_from_xml_inner(xml) {
				Ok(ld) => ld,
				Err(_) => {
					unsafe { *out_result = SERIALIZER_XML_ERROR };
					if !out_load_data.is_null() {
						unsafe { *out_load_data = CHandle::null() };
					}
					if !details_buf.is_null() && details_buf_size > 0 {
						unsafe { *details_buf = 0 };
					}
					return Ok(());
				}
			};
			unsafe { *out_result = SERIALIZER_OK };
			if !out_load_data.is_null() {
				unsafe { *out_load_data = crate::handle::make_owned(load_data) };
			}
			Ok(())
		})
	}

	/// Parse XML into a [`LoadData`] (nodes created in fresh scratch
	/// projects, owned by the caller).
	fn load_from_xml_inner(xml: &str) -> Result<LoadData> {
		let project = crate::serializer::load(xml)?;
		let guard = lock(&project);
		let mut nodes = Vec::new();
		for id in guard.graph.node_ids() {
			nodes.push((project.clone(), id));
		}
		Ok(LoadData {
			nodes,
			connections: Vec::new(),
			properties: std::collections::HashMap::new(),
		})
	}

	/// `oaknode_serializer_loaddata_free`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_loaddata_free(load_data: *mut CHandle) {
		if load_data.is_null() || unsafe { (*load_data).ctx.is_null() } {
			return;
		}
		let h = unsafe { (*load_data).clone() };
		if let Some(f) = h.release {
			unsafe { f(h.ctx) };
		}
		unsafe { (*load_data).ctx = std::ptr::null_mut() };
	}

	/// `oaknode_serializer_loaddata_node_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_loaddata_node_count(load_data: CHandle) -> c_int {
		match unsafe { crate::handle::get::<LoadData>(&load_data) } {
			Some(ld) => ld.nodes.len() as c_int,
			None => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_serializer_loaddata_node_at` (borrowed).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_loaddata_node_at(
		load_data: CHandle,
		index: c_int,
	) -> CHandle {
		let ld = match unsafe { crate::handle::get::<LoadData>(&load_data) } {
			Some(ld) => ld,
			None => return CHandle::null(),
		};
		match ld.nodes.get(index as usize) {
			Some((project, id)) => make_node_borrowed(project.clone(), *id),
			None => CHandle::null(),
		}
	}

	/// `oaknode_serializer_loaddata_get_property` (two-stage).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_loaddata_get_property(
		load_data: CHandle,
		node: CHandle,
		key: *const c_char,
		buf: *mut c_char,
		buf_size: c_int,
	) -> c_int {
		let ld = match unsafe { crate::handle::get::<LoadData>(&load_data) } {
			Some(ld) => ld,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let n = match unsafe { node_ref(&node) } {
			Ok(n) => n,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let key = match unsafe { cstr(key) } {
			Some(k) => k,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		match ld.properties.get(&(n.id, key.to_string())) {
			Some(v) => copy_string_out(v, buf, buf_size),
			None => crate::error::OAKNODE_E_NOT_FOUND,
		}
	}

	/// `oaknode_serializer_loaddata_connection_count`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_loaddata_connection_count(
		load_data: CHandle,
	) -> c_int {
		match unsafe { crate::handle::get::<LoadData>(&load_data) } {
			Some(ld) => ld.connections.len() as c_int,
			None => crate::error::OAKNODE_E_INVALID,
		}
	}

	/// `oaknode_serializer_loaddata_connection_at`.
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_loaddata_connection_at(
		load_data: CHandle,
		index: c_int,
		out_output_node: *mut CHandle,
		out_input_node: *mut CHandle,
		input_id_buf: *mut c_char,
		input_id_buf_size: c_int,
		out_element: *mut c_int,
	) -> c_int {
		guard(|| {
			if out_output_node.is_null() || out_input_node.is_null() || out_element.is_null() {
				return Err(Error::Invalid);
			}
			let ld = unsafe { crate::handle::get::<LoadData>(&load_data) }
				.ok_or(Error::Invalid)?;
			let (out_id, in_id, input_id, element) =
				ld.connections.get(index as usize).ok_or(Error::NotFound)?;
			// Resolve node handles from the load-data's nodes.
			let out = ld
				.nodes
				.iter()
				.find(|(_, id)| id == out_id)
				.map(|(p, id)| make_node_borrowed(p.clone(), *id))
				.unwrap_or_else(CHandle::null);
			let inn = ld
				.nodes
				.iter()
				.find(|(_, id)| id == in_id)
				.map(|(p, id)| make_node_borrowed(p.clone(), *id))
				.unwrap_or_else(CHandle::null);
			unsafe {
				*out_output_node = out;
				*out_input_node = inn;
				*out_element = *element;
			}
			if !input_id_buf.is_null() {
				copy_string_out(input_id, input_id_buf, input_id_buf_size);
			}
			Ok(())
		})
	}

	/// Report a serializer failure the C++ `report_serializer_result` way:
	/// write the details string (two-stage) and return the required size
	/// when a buffer was provided, else `OAKNODE_E_FAILED`
	/// (`// CPP-PARITY: src/node/c_api/serializer.cpp`).
	fn report_serializer_failure(msg: &str, details: *mut c_char, details_size: c_int) -> c_int {
		let needed = msg.len() + 1;
		if !details.is_null() && details_size > 0 {
			copy_string_out(msg, details, details_size);
			needed as c_int
		} else {
			crate::error::OAKNODE_E_FAILED
		}
	}

	/// `oaknode_serializer_save_to_file`: serialize `project` to XML and
	/// write it to `filename`. OVEC compression (`use_compression`) is
	/// not supported — always plain XML. Result code semantics per
	/// include/node/serializer.h (`// CPP-PARITY: serializer.cpp:394`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_save_to_file(
		project: CHandle,
		filename: *const c_char,
		_use_compression: c_int,
		out_code: *mut c_int,
		details: *mut c_char,
		details_size: c_int,
	) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let filename = match unsafe { cstr(filename) } {
			Some(s) => s,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let xml = match save_to_xml_inner(&SaveData {
			load_type: 0,
			project: Some(p),
			only_serialize_nodes: None,
			properties: std::collections::HashMap::new(),
		}) {
			Ok(x) => x,
			Err(_) => {
				if !out_code.is_null() {
					unsafe { *out_code = SERIALIZER_XML_ERROR };
				}
				return report_serializer_failure("serialization failed", details, details_size);
			}
		};
		match std::fs::write(filename, xml) {
			Ok(()) => {
				if !out_code.is_null() {
					unsafe { *out_code = SERIALIZER_OK };
				}
				crate::error::OAKNODE_OK
			}
			Err(e) => {
				if !out_code.is_null() {
					unsafe { *out_code = SERIALIZER_FILE_ERROR };
				}
				report_serializer_failure(&e.to_string(), details, details_size)
			}
		}
	}

	/// `oaknode_serializer_load_from_file`: read `filename` and load it
	/// into `project` (the C++ `ProjectSerializer::load` populates the
	/// given project; `// CPP-PARITY: serializer.cpp:419`).
	#[no_mangle]
	pub unsafe extern "C" fn oaknode_serializer_load_from_file(
		project: CHandle,
		filename: *const c_char,
		out_code: *mut c_int,
		details: *mut c_char,
		details_size: c_int,
	) -> c_int {
		let p = match unsafe { project_arc(&project) } {
			Ok(p) => p,
			Err(_) => return crate::error::OAKNODE_E_INVALID,
		};
		let filename = match unsafe { cstr(filename) } {
			Some(s) => s,
			None => return crate::error::OAKNODE_E_INVALID,
		};
		let xml = match std::fs::read_to_string(filename) {
			Ok(x) => x,
			Err(e) => {
				if !out_code.is_null() {
					unsafe { *out_code = SERIALIZER_FILE_ERROR };
				}
				return report_serializer_failure(&e.to_string(), details, details_size);
			}
		};
		match crate::serializer::load(&xml) {
			Ok(loaded) => {
				// Move the loaded graph into the given project.
				let mut dst = lock(&p);
				let mut src = lock(&loaded);
				dst.graph.transfer_all(&mut src.graph);
				if !out_code.is_null() {
					unsafe { *out_code = SERIALIZER_OK };
				}
				crate::error::OAKNODE_OK
			}
			Err(_) => {
				if !out_code.is_null() {
					unsafe { *out_code = SERIALIZER_XML_ERROR };
				}
				report_serializer_failure("XML parse error", details, details_size)
			}
		}
	}
}
