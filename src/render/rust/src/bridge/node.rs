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

//! oaknode C ABI imports (project copies, node queries).
//!
//! The functions here are declared against the frozen `include/node/*.h`
//! contract and resolved through [`crate::bridge::dlsym`]. When the
//! symbols are missing (cargo test without liboaknode) the wrappers fail
//! with the documented fallbacks; the copier tests that require a live
//! node module are `#[ignore = "needs oaknode C ABI"]`.

use std::ffi::c_int;

use crate::error::{Error, Result};
use crate::handle::CHandle;

/// oaknode project handle.
pub type ProjectHandle = CHandle;
/// oaknode node handle.
pub type NodeHandle = CHandle;

/// Mirror of oaknode's change record (marshalled as plain C structs;
/// layout per include/node/project.h).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ChangeRecord {
	/// Discriminant (see oaknode `ChangeRecord`).
	pub kind: u32,
	/// Payload bytes (per-kind layout documented in project.h).
	pub payload: [u8; 48],
}

/// Change-record discriminants (oaknode project.h).
pub mod change_kind {
	/// Node added.
	pub const NODE_ADD: u32 = 0;
	/// Node removed.
	pub const NODE_REMOVE: u32 = 1;
	/// Edge added.
	pub const EDGE_ADD: u32 = 2;
	/// Edge removed.
	pub const EDGE_REMOVE: u32 = 3;
	/// Value change.
	pub const VALUE_CHANGE: u32 = 4;
	/// Value hint change.
	pub const VALUE_HINT_CHANGE: u32 = 5;
	/// Project setting change.
	pub const PROJECT_SETTING_CHANGE: u32 = 6;
	/// Footage proxy change.
	pub const FOOTAGE_PROXY: u32 = 7;
}

/// `oaknode_project_deep_copy(project)` — new in the Rust-era node C ABI.
/// Returns an owned copied-project handle; empty when the symbol is
/// missing or the copy failed.
pub fn project_deep_copy(project: ProjectHandle) -> CHandle {
	type F = unsafe extern "C" fn(ProjectHandle) -> CHandle;
	crate::bridge::dlsym::call::<F, CHandle>("oaknode_project_deep_copy", |f| unsafe { f(project) })
		.unwrap_or_else(CHandle::null)
}

/// `oaknode_project_sync_copy(source, copy, changes, count)` — pushes a
/// recorded change set into the copy. `OAKNODE_OK` (0) on success; a
/// negative error otherwise (missing symbol → `Error::Failed`).
pub fn project_sync_copy(
	source: ProjectHandle,
	copy: ProjectHandle,
	changes: &[ChangeRecord],
) -> Result<()> {
	type F = unsafe extern "C" fn(
		ProjectHandle,
		ProjectHandle,
		*const ChangeRecord,
		c_int,
	) -> c_int;
	let rc = crate::bridge::dlsym::call::<F, c_int>("oaknode_project_sync_copy", |f| unsafe {
		f(source, copy, changes.as_ptr(), changes.len() as c_int)
	})
	.ok_or_else(|| Error::Failed("oaknode_project_sync_copy missing".into()))?;
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::Failed(format!("oaknode_project_sync_copy rc={rc}")))
	}
}

/// `oaknode_node_get_video_frame_cache(node, out)` — borrowed cache
/// handle of a node. `OAKNODE_OK` (0) on success with `*out` set;
/// otherwise a negative error (missing symbol → `Error::Failed`).
///
/// # Safety
/// `node` must be a valid handle; `out` a valid pointer.
pub unsafe fn node_get_video_frame_cache(node: NodeHandle, out: *mut CHandle) -> Result<()> {
	type F = unsafe extern "C" fn(NodeHandle, *mut CHandle) -> c_int;
	let rc = crate::bridge::dlsym::call::<F, c_int>("oaknode_node_get_video_frame_cache", |f| unsafe {
		f(node, out)
	})
	.ok_or_else(|| Error::Failed("oaknode_node_get_video_frame_cache missing".into()))?;
	if rc == 0 {
		Ok(())
	} else {
		Err(Error::Failed(format!("oaknode_node_get_video_frame_cache rc={rc}")))
	}
}

/// Whether the oaknode C ABI is present in the process (tests use this to
/// gate success-path copier tests).
pub fn node_abi_available() -> bool {
	crate::bridge::dlsym::resolve("oaknode_project_deep_copy").is_some()
}

/// Node identity of a handle (the box pointer; matches the copier's
/// identity tracking).
pub fn node_identity(node: &NodeHandle) -> u64 {
	node.ctx as u64
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn deep_copy_missing_symbol_yields_empty_handle() {
		// Without liboaknode the deep copy cannot exist.
		let h = project_deep_copy(CHandle::null());
		assert!(h.is_null());
	}

	#[test]
	fn sync_copy_missing_symbol_fails_explainably() {
		let changes = [ChangeRecord {
			kind: change_kind::NODE_ADD,
			payload: [0u8; 48],
		}];
		let rc = project_sync_copy(CHandle::null(), CHandle::null(), &changes);
		assert!(rc.is_err(), "missing symbol → explainable failure");
	}

	#[test]
	fn change_record_layout_is_c_stable() {
		// kind first, then 48 payload bytes (include/node/project.h).
		let c = ChangeRecord {
			kind: change_kind::VALUE_CHANGE,
			payload: [7u8; 48],
		};
		assert_eq!(std::mem::size_of::<ChangeRecord>(), 4 + 48);
		let bytes: [u8; 52] = unsafe { std::mem::transmute(c) };
		assert_eq!(u32::from_le_bytes(bytes[0..4].try_into().unwrap()), change_kind::VALUE_CHANGE);
		assert_eq!(bytes[4], 7);
	}

	#[test]
	fn node_identity_is_ctx_value() {
		let h = CHandle {
			ctx: 0x1234 as *mut std::ffi::c_void,
			addref: None,
			release: None,
			abi_version: 1,
		};
		assert_eq!(node_identity(&h), 0x1234);
	}
}
