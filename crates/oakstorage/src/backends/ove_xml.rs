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

//! The `.ove` XML file backend. Graph (de)serialization is delegated
//! to oaknode's serializer C ABI (`bridge::node`); this backend owns
//! the file container: version header probe, optional compression
//! (OAKSTORAGE_SAVE_COMPRESS), and byte-exact round-trip behavior
//! pinned by the golden tests (M10 §4).

/// The ove-xml backend (`file://` + `.ove`).
pub struct OveXmlBackend;

impl OveXmlBackend {
	/// Construct.
	pub fn new() -> Self {
		todo!()
	}
}

impl crate::backend::StorageBackend for OveXmlBackend {
	fn name(&self) -> &'static str {
		todo!()
	}

	fn uri_scheme(&self) -> &'static str {
		todo!()
	}

	fn can_handle(&self, uri: &crate::uri::StorageUri) -> bool {
		todo!()
	}

	fn load(
		&self,
		uri: &crate::uri::StorageUri,
	) -> crate::error::Result<crate::handle::CHandle> {
		todo!()
	}

	fn save(
		&self,
		project: crate::handle::CHandle,
		uri: &crate::uri::StorageUri,
		options: u32,
	) -> crate::error::Result<()> {
		todo!()
	}
}
