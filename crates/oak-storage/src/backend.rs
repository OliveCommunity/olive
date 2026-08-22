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

//! The backend interface. `StorageBackend` is the Rust trait shape of
//! M10 §2.3's manual C vtable; `ffi.rs` marshals foreign (C-side)
//! vtable registrations onto the same registry.

use crate::uri::StorageUri;

/// Outcome of a backend load: the loaded project plus the version
/// information code the caller surfaces through `oakstorage_open`'s
/// `result_code` (M10 §2.2). Version probing (TOO_OLD / TOO_NEW /
/// UNKNOWN_VERSION) is the backend's own judgement; a "not loadable"
/// outcome carries an empty `project` handle and a positive info code,
/// so the caller can report it instead of a hard error.
#[derive(Clone, Debug)]
pub struct LoadResult {
	/// The loaded project handle (owned, refcount 1; empty when version
	/// probing declined to load).
	pub project: crate::handle::CHandle,
	/// Version info code: `OAKSTORAGE_OK` or a positive info code
	/// (TOO_OLD / TOO_NEW / UNKNOWN_VERSION).
	pub version_info: i32,
}

impl LoadResult {
	/// A normal successful load.
	pub fn success(project: crate::handle::CHandle) -> Self {
		LoadResult {
			project,
			version_info: crate::error::OAKSTORAGE_OK,
		}
	}

	/// A loaded project plus an info code (e.g. TOO_OLD: the project was
	/// written by an older build and has been upgraded on the fly).
	pub fn with_info(project: crate::handle::CHandle, version_info: i32) -> Self {
		LoadResult {
			project,
			version_info,
		}
	}

	/// No project — only an info code (TOO_NEW / UNKNOWN_VERSION).
	pub fn info_only(version_info: i32) -> Self {
		LoadResult {
			project: crate::handle::CHandle::null(),
			version_info,
		}
	}
}

/// A storage backend (M10 §2.3 vtable semantics).
pub trait StorageBackend: Send + Sync {
	/// Backend name ("ove-xml" / "otio" / "oakdb").
	fn name(&self) -> &str;

	/// URI scheme this backend serves ("file" / "oakdb").
	fn uri_scheme(&self) -> &str;

	/// Whether this backend claims the URI (suffix, magic bytes,
	/// reachability — backend's own judgement).
	fn can_handle(&self, uri: &StorageUri) -> bool;

	/// Load a project; returns an owned oaknode project handle
	/// (CHandle with refcount 1) wrapped in a [`LoadResult`], or an
	/// error code/context.
	fn load(&self, uri: &StorageUri) -> crate::error::Result<LoadResult>;

	/// Save a project to the URI. `options` is the M10 bitmask
	/// (OAKSTORAGE_SAVE_COMPRESS etc.); unknown bits are ignored.
	fn save(
		&self,
		project: crate::handle::CHandle,
		uri: &StorageUri,
		options: u32,
	) -> crate::error::Result<()>;
}
