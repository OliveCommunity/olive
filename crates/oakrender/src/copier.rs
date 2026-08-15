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

//! Render-side project copy client (the C++ ProjectCopier, inverted):
//! all copying happens inside oaknode. oaknode never implemented the
//! deep-copy direction (single-lib plan §4.1 — dead direction), so the
//! copy operations fail explainably and the success-path tests are
//! `#[ignore]`d.

use crate::error::{Error, Result};

/// A project handle (oaknode-owned; the shared canonical handle type).
pub type ProjectHandle = crate::handle::CHandle;

/// One change record (see oaknode `ChangeRecord`).
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

/// `oaknode_project_deep_copy(project)` — would return an owned
/// copied-project handle.
///
/// Never implemented: oaknode has no such Rust function (single-lib plan
/// §4.1 — dead direction), so this always yields the empty handle, exactly
/// as the previous runtime-symbol lookup did when the symbol was absent.
pub fn project_deep_copy(_project: ProjectHandle) -> ProjectHandle {
	ProjectHandle::null()
}

/// `oaknode_project_sync_copy` — never implemented (dead direction); the
/// sync always fails explainably.
pub fn project_sync_copy(
	_source: ProjectHandle,
	_copy: ProjectHandle,
	_changes: &[ChangeRecord],
) -> Result<()> {
	Err(Error::Failed(
		"oaknode_project_sync_copy missing (not implemented in oaknode)".into(),
	))
}

/// A handle to a render-side project copy.
pub struct ProjectCopy {
	/// Identity of the source project.
	pub source: u64,
	/// Identity of the copied project (oaknode-owned).
	pub copy: u64,
	/// Owned oaknode handle to the copy (kept alive for the copier's
	/// lifetime; released on drop).
	copy_handle: Option<ProjectHandle>,
	/// Change-generation counter of the last successful sync.
	pub last_sync_generation: u64,
	/// True while recorded changes await `sync`.
	pub has_pending_updates: bool,
}

impl ProjectCopy {
	/// A copier with no project attached yet (C++ `ProjectCopier()`).
	pub fn new() -> Self {
		Self {
			source: 0,
			copy: 0,
			copy_handle: None,
			last_sync_generation: 0,
			has_pending_updates: false,
		}
	}

	/// Create a deep copy of `source` through the oaknode C ABI
	/// (C++ `ProjectCopier::set_project`).
	pub fn set_project(&mut self, source: ProjectHandle) -> Result<()> {
		if source.is_null() {
			return Err(Error::Invalid);
		}
		// Release any previous copy.
		self.release_copy();
		let copy = crate::copier::project_deep_copy(source);
		if copy.is_null() {
			return Err(Error::Failed(
				"oaknode_project_deep_copy failed (symbol missing or copy error)".into(),
			));
		}
		self.source = source.ctx as u64;
		self.copy = copy.ctx as u64;
		self.copy_handle = Some(copy);
		self.last_sync_generation = 0;
		self.has_pending_updates = false;
		Ok(())
	}

	/// Push a recorded change set into the copy (C++
	/// ProjectCopier::process_update_queue).
	pub fn sync(&mut self, changes: &[ChangeRecord]) -> Result<()> {
		let source = ProjectHandle {
			ctx: self.source as *mut std::ffi::c_void,
			addref: None,
			release: None,
			abi_version: crate::handle::OAKRENDER_ABI_VERSION,
		};
		let copy = self.copy_handle.unwrap_or_else(ProjectHandle::null);
		if copy.is_null() {
			return Err(Error::State);
		}
		crate::copier::project_sync_copy(source, copy, changes)?;
		self.last_sync_generation += 1;
		self.has_pending_updates = false;
		Ok(())
	}

	/// The copied project handle (owned by this copier; borrowed for the
	/// caller).
	pub fn copied_project(&self) -> Option<ProjectHandle> {
		self.copy_handle
	}

	/// The copied counterpart of an original node — requires the oaknode
	/// node-map query (`oaknode_project_copy_of_node`), which is part of
	/// the pending node bridge; returns `None` until then.
	pub fn copy_of_node(&self, _original: u64) -> Option<u64> {
		None
	}

	/// Drop the copy (releases the oaknode handle).
	pub fn destroy(&mut self) {
		self.release_copy();
	}

	fn release_copy(&mut self) {
		if let Some(handle) = self.copy_handle.take() {
			if let Some(release) = handle.release {
				// SAFETY: the handle came from oaknode_project_deep_copy;
				// releasing the last reference destroys the copy.
				unsafe { release(handle.ctx) };
			}
		}
		self.copy = 0;
	}
}

impl Default for ProjectCopy {
	fn default() -> Self {
		Self::new()
	}
}

impl Drop for ProjectCopy {
	fn drop(&mut self) {
		self.release_copy();
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn fresh_copier_has_no_copy() {
		let pc = ProjectCopy::new();
		assert_eq!(pc.source, 0);
		assert_eq!(pc.copy, 0);
		assert!(pc.copied_project().is_none());
		assert!(!pc.has_pending_updates);
	}

	#[test]
	fn set_project_rejects_empty_handle() {
		let mut pc = ProjectCopy::new();
		assert_eq!(
			pc.set_project(ProjectHandle::null()).unwrap_err().code(),
			Error::Invalid.code()
		);
	}

	#[test]
	fn sync_without_project_is_state_error() {
		let mut pc = ProjectCopy::new();
		let changes = [ChangeRecord {
			kind: crate::copier::change_kind::NODE_ADD,
			payload: [0u8; 48],
		}];
		assert_eq!(pc.sync(&changes).unwrap_err().code(), Error::State.code());
	}

	#[test]
	fn destroy_releases_cleanly() {
		let mut pc = ProjectCopy::new();
		pc.destroy();
		assert_eq!(pc.copy, 0);
	}

	#[test]
	#[ignore = "needs oaknode deep-copy (not implemented)"]
	fn deep_copy_roundtrip_with_real_node() {
		// oaknode never implemented the deep-copy direction; the copy
		// always fails explainably, which is what the live path checks.
		let mut pc = ProjectCopy::new();
		let src = ProjectHandle {
			ctx: 1 as *mut std::ffi::c_void,
			addref: None,
			release: None,
			abi_version: crate::handle::OAKRENDER_ABI_VERSION,
		};
		pc.set_project(src).unwrap();
		assert_ne!(pc.copy, 0);
		assert!(pc.copied_project().is_some());
	}
}
