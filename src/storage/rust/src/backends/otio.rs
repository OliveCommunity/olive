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

//! The `.otio` backend (import/export semantics via the native
//! `oakotio` crate). OTIO is a one-way interchange format: `load`
//! imports into a fresh project; `save` exports the current timeline.

/// The otio backend (`file://` + `.otio`).
pub struct OtioBackend;

impl OtioBackend {
	/// Construct.
	pub fn new() -> Self {
		todo!()
	}
}

impl crate::backend::StorageBackend for OtioBackend {
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
