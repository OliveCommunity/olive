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

//! The database backend (PostgreSQL + SQLite via SeaORM).
//!
//! URI forms:
//! - `oakdb+sqlite:///absolute/path.db` — local file database
//! - `oakdb+pg://user:pass@host:5432/dbname` — PostgreSQL
//!
//! The async SeaORM API is driven by a private current-thread tokio
//! runtime so the public surface stays synchronous (M10: no callbacks,
//! sync commands only). The graph payload is the same serialized form
//! the ove-xml backend produces — one serialization truth.

/// The database backend (schemes `oakdb+sqlite` / `oakdb+pg`).
pub struct DatabaseBackend {
	// Private current-thread runtime + SeaORM connection cache.
}

impl DatabaseBackend {
	/// Construct (no connection is opened until first use).
	pub fn new() -> Self {
		todo!()
	}

	/// Parse the URI into a SeaORM connection URL + project key
	/// (query param `?project=` or row id; default: singleton "default"
	/// project row).
	pub(crate) fn parse_target(
		uri: &crate::uri::StorageUri,
	) -> crate::error::Result<(String, String)> {
		todo!()
	}
}

impl crate::backend::StorageBackend for DatabaseBackend {
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
