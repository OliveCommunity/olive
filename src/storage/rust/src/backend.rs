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

/// A storage backend (M10 §2.3 vtable semantics).
pub trait StorageBackend: Send + Sync {
	/// Backend name ("ove-xml" / "otio" / "oakdb").
	fn name(&self) -> &'static str;

	/// URI scheme this backend serves ("file" / "oakdb").
	fn uri_scheme(&self) -> &'static str;

	/// Whether this backend claims the URI (suffix, magic bytes,
	/// reachability — backend's own judgement).
	fn can_handle(&self, uri: &StorageUri) -> bool;

	/// Load a project; returns an owned oaknode project handle
	/// (CHandle with refcount 1) or an error code/context.
	fn load(&self, uri: &StorageUri) -> crate::error::Result<crate::handle::CHandle>;

	/// Save a project to the URI. `options` is the M10 bitmask
	/// (OAKSTORAGE_SAVE_COMPRESS etc.); unknown bits are ignored.
	fn save(
		&self,
		project: crate::handle::CHandle,
		uri: &StorageUri,
		options: u32,
	) -> crate::error::Result<()>;
}
