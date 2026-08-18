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

//! Facade scaffolding: engine opaque pointers as thin newtype wrappers
//! around module [`CHandle`] values.
//!
//! Every `OakEngine*` opaque type from `engine/include/oakengine/*.h`
//! is a `#[repr(C)]` struct holding one [`CHandle`] (the module C ABI's
//! `{ctx, addref, release, abi_version}` value handle, see
//! `include/common/handle.h`). The C caller only ever sees an opaque
//! pointer, so the field layout is ours to choose; the wrappers exist so
//! the exported `oakengine_*` signatures match the frozen headers
//! verbatim.
//!
//! A box is created by [`box_handle`] and freed by [`free_box`]: freeing
//! calls the handle's `release` (for a module-borrowed handle that only
//! releases the handle shell, never the graph-owned object) and then
//! deallocates the box. Consuming exports (`oakengine_*_free`,
//! `oakengine_undo_push`, ...) call [`free_box`].
//!
//! String output follows the engine's buf/size convention (see
//! [`write_string`]): the return value is the required length including
//! the terminating NUL; negative values are error codes.

use std::ffi::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::error::{Error, Result};

/// The shared ABI value-handle type (single-lib unification, see
/// `docs/zh/plans/riir/single-lib.md`): one canonical
/// `{ctx, addref, release, abi_version}` type in `oakcore-rs`, re-exported
/// by every module crate, so the facade can pass a handle straight into a
/// module's `pub` Rust functions without an `extern "C"` declaration.
/// `Clone + Copy + Send + Sync` come from the shared type.
pub use oakcore_rs::handle::CHandle;

/// Engine-side boxed payloads holding the oaknode domain (single-lib
/// unification). Every `oakengine_*` node-family handle ultimately wraps
/// one of these behind a [`CHandle`]:
///
/// - projects box [`domain::ProjectArc`] (`Arc<Mutex<Project>>`);
/// - nodes, blocks, tracks, footage, sequences and folders box a
///   [`domain::NodeRef`] (`(Arc<Mutex<Project>>, NodeId)` — the
///   oaknode crate's `project::NodeRef` value type).
///
/// The box is created through `oaknode::handle::make_owned` (refcounted
/// shell + release callback), so the facade's existing
/// [`box_handle`]/[`free_box`] discipline (and the addref copies the
/// engine takes) works unchanged.
pub mod domain {
	use std::sync::{Arc, Mutex};

	use oaknode::id::NodeId;

	use crate::handle::CHandle;

	/// Engine-side boxed payload for project handles: shared ownership of
	/// the oaknode domain project (its graph, settings, filename state).
	pub type ProjectArc = Arc<Mutex<oaknode::project::Project>>;

	/// Engine-side boxed payload for node/block/track/footage/sequence/
	/// folder handles: a reference into a project's graph. Reuses the
	/// oaknode crate's own `NodeRef` value type (project + id + owned
	/// flag); a stale id fails validation instead of aliasing.
	pub type NodeRef = oaknode::project::NodeRef;

	/// Box a project payload behind a refcounted handle.
	pub fn box_project(project: ProjectArc) -> CHandle {
		oaknode::handle::make_owned(project)
	}

	/// Box a node reference behind a refcounted handle. `owned` marks
	/// detached (factory-created) nodes so the engine's debug alive
	/// counter accounts them exactly once.
	pub fn box_node(project: ProjectArc, id: NodeId, owned: bool) -> CHandle {
		oaknode::handle::make_owned(NodeRef::new(project, id, owned))
	}

	/// Borrow the project payload behind a handle.
	///
	/// # Safety
	/// `h` must be a live handle created by [`box_project`] (or empty).
	pub unsafe fn project_of(h: &CHandle) -> Option<&ProjectArc> {
		// SAFETY: forwarded to the oaknode handle contract.
		unsafe { oaknode::handle::get::<ProjectArc>(h) }
	}

	/// Borrow the node-reference payload behind a handle.
	///
	/// # Safety
	/// `h` must be a live handle created by [`box_node`] (or empty).
	pub unsafe fn node_ref_of(h: &CHandle) -> Option<&NodeRef> {
		// SAFETY: forwarded to the oaknode handle contract.
		unsafe { oaknode::handle::get::<NodeRef>(h) }
	}

	/// Mutable view of the node-reference payload (used by the graph
	/// transfer paths, which rewrite the shared box in place — the
	/// "write_node_ref" semantics).
	///
	/// # Safety
	/// `h` must be a live handle created by [`box_node`]; the caller must
	/// hold exclusive access to the boxed value.
	pub unsafe fn node_ref_mut(h: &CHandle) -> Option<&mut NodeRef> {
		// SAFETY: forwarded to the shared-box contract.
		unsafe { boxed_mut::<NodeRef>(h) }
	}

	/// Mutable typed view into an oaknode-style `RefBox` payload (the
	/// oaknode crate exposes only a read-only `get`; this mirrors its
	/// box layout — `refs`/`value` are `pub` fields).
	///
	/// # Safety
	/// `h` must be a live handle boxing `T`; the caller must hold
	/// exclusive access to the boxed value.
	pub unsafe fn boxed_mut<T: 'static>(h: &CHandle) -> Option<&mut T> {
		if h.ctx.is_null() {
			return None;
		}
		// SAFETY: contract above; the box is an
		// `oaknode::handle::RefBox<T>`.
		unsafe { Some(&mut (*(h.ctx as *mut oaknode::handle::RefBox<T>)).value) }
	}
}

/// Engine opaque handle types, one per `typedef struct OakEngine*` in
/// `engine/include/oakengine/*.h`. All are thin newtype wrappers around a
/// [`CHandle`] value with a uniform extraction surface ([`EngineBox`]).
macro_rules! engine_handle {
	($($name:ident),* $(,)?) => {
		$(
			/// Opaque engine handle: thin newtype wrapper around a module
			/// [`CHandle`] value.
			#[repr(C)]
			#[derive(Clone, Copy)]
			pub struct $name {
				/// The wrapped module handle.
				pub handle: CHandle,
			}

			impl EngineBox for $name {
				fn boxed_new(handle: CHandle) -> Self {
					$name { handle }
				}
				fn handle(&self) -> CHandle {
					self.handle
				}
			}
		)*
	};
}

engine_handle! {
	OakEngineAudioBuffer,
	OakEngineAudioProcessor,
	OakEngineBlock,
	OakEngineClip,
	OakEngineClipboard,
	OakEngineColorConfig,
	OakEngineColorManager,
	OakEngineColorProcessor,
	OakEngineEncodingParams,
	OakEngineFootage,
	OakEngineFrame,
	OakEngineFrameCache,
	OakEngineKeyframe,
	OakEngineMarker,
	OakEngineMarkerList,
	OakEngineNode,
	OakEngineNodeDragger,
	OakEnginePlayback,
	OakEnginePlaybackCache,
	OakEnginePreviewRequest,
	OakEngineProject,
	OakEngineRenderer,
	OakEngineSequence,
	OakEngineTask,
	OakEngineThumbnailCache,
	OakEngineTrack,
	OakEngineTrackList,
	OakEngineTraverseDb,
	OakEngineWaveformCache,
	OakEngineWorkarea,
}

/// Uniform construction/extraction surface of the engine opaque types.
pub trait EngineBox: Sized {
	/// Build the wrapper from a module handle.
	fn boxed_new(handle: CHandle) -> Self;
	/// Extract the wrapped module handle (copy).
	fn handle(&self) -> CHandle;
}

/// Allocate a heap box for a module handle and return its raw pointer.
/// The box must later be released with [`free_box`].
pub fn box_handle<T: EngineBox>(handle: CHandle) -> *mut T {
	Box::into_raw(Box::new(T::boxed_new(handle)))
}

/// Dereference an engine opaque pointer and copy out its module handle.
/// Returns [`Error::Invalid`] for a NULL pointer or an empty handle.
///
/// # Safety
/// `ptr` must point to a live box created by [`box_handle`] (or be
/// NULL).
pub unsafe fn unbox<T: EngineBox>(ptr: *const T) -> Result<CHandle> {
	unsafe {
		if ptr.is_null() {
			return Err(Error::Invalid);
		}
		let h = (*ptr).handle();
		if h.is_null() {
			return Err(Error::Invalid);
		}
		Ok(h)
	}
}

/// Free a box created by [`box_handle`]: release the module handle (via
/// its `release` function pointer) and deallocate the box. NULL and
/// empty handles are no-ops. After the call `ptr` is dangling; the
/// caller must not use it again.
///
/// # Safety
/// `ptr` must be a pointer previously returned by [`box_handle`] (or
/// NULL) and must not be freed twice.
pub unsafe fn free_box<T: EngineBox>(ptr: *mut T) {
	unsafe {
		if ptr.is_null() {
			return;
		}
		let handle = (*ptr).handle();
		if let Some(release) = handle.release {
			release(handle.ctx);
		}
		drop(Box::from_raw(ptr));
	}
}

/// Panic-catching FFI wrapper for `i32`-returning exports.
pub fn guard<F: FnOnce() -> Result<()>>(f: F) -> c_int {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(())) => crate::error::OAKENGINE_OK,
		Ok(Err(e)) => e.code(),
		Err(_) => crate::error::OAKENGINE_E_FAILED,
	}
}

/// Panic-catching FFI wrapper for pointer-returning exports.
pub fn guard_ptr<T, F: FnOnce() -> Result<*mut T>>(f: F) -> *mut T {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(p)) => p,
		_ => std::ptr::null_mut(),
	}
}

/// Panic-catching FFI wrapper for `int64_t`-returning exports
/// (`OAKENGINE_E_INVALID` sentinel on error, matching the engine's
/// "no application core exists" convention).
pub fn guard_i64<F: FnOnce() -> Result<i64>>(f: F) -> i64 {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(v)) => v,
		Ok(Err(_)) => crate::error::OAKENGINE_E_INVALID as i64,
		Err(_) => crate::error::OAKENGINE_E_FAILED as i64,
	}
}

/// Panic-catching FFI wrapper for void exports.
pub fn guard_void<F: FnOnce()>(f: F) {
	let _ = catch_unwind(AssertUnwindSafe(f));
}

/// Panic-catching FFI wrapper for exports whose return value IS the
/// result (a count, a 1/0 flag, a required string length): the closure
/// returns the positive payload, errors are returned as negative codes.
pub fn guard_int<F: FnOnce() -> Result<c_int>>(f: F) -> c_int {
	match catch_unwind(AssertUnwindSafe(f)) {
		Ok(Ok(v)) => v,
		Ok(Err(e)) => e.code(),
		Err(_) => crate::error::OAKENGINE_E_FAILED,
	}
}

/// Write `s` into `buf` following the engine buf/size convention and
/// return the string length **excluding** the terminating NUL (the engine
/// headers' "would-be length"; module getters report len+1 and are
/// converted with [`string_result`]). A NULL `buf` or `buf_size <= 0`
/// only reports the length. `s` is truncated to `buf_size - 1` bytes when
/// it does not fit.
///
/// # Safety
/// `buf` must point to `buf_size` writable bytes when non-NULL and
/// `buf_size > 0`.
pub unsafe fn write_string(s: &str, buf: *mut c_char, buf_size: c_int) -> c_int {
	unsafe {
		if !buf.is_null() && buf_size > 0 {
			let copy_len = s.len().min((buf_size as usize).saturating_sub(1));
			std::ptr::copy_nonoverlapping(s.as_ptr(), buf as *mut u8, copy_len);
			*buf.add(copy_len) = 0;
		}
	}
	s.len() as c_int
}

/// Read a NUL-terminated C string; NULL yields an empty string.
///
/// # Safety
/// `s` must be a valid NUL-terminated string, or NULL.
pub unsafe fn read_cstr(s: *const c_char) -> String {
	unsafe {
		if s.is_null() {
			String::new()
		} else {
			std::ffi::CStr::from_ptr(s).to_string_lossy().into_owned()
		}
	}
}

/// Convert a module two-stage getter result to the engine convention.
/// Module getters report the required buffer size **including** the
/// terminating NUL; the engine headers' buf/size convention reports the
/// string **length** (excluding the NUL, mirroring the C++ capi
/// `write_string`). Negative codes pass through untranslated.
pub fn string_result(module_ret: c_int) -> c_int {
	if module_ret > 0 {
		module_ret - 1
	} else {
		module_ret
	}
}
