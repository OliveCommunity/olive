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

//! URI parsing and classification. The core never sees bare file
//! paths — every entry point takes a URI (M10 §2.2).

/// A parsed storage URI.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StorageUri {
	/// Scheme ("file", "oakdb", …).
	pub scheme: String,
	/// Scheme-specific body (path for file://, connection string for
	/// oakdb://).
	pub body: String,
}

impl StorageUri {
	/// Parse a URI string; bare paths are normalized to file:// URIs
	/// (C++ callers pass plain paths — M10 §2.2 note).
	pub fn parse(s: &str) -> crate::error::Result<StorageUri> {
		todo!()
	}

	/// File extension (lowercased, without dot) for file:// URIs.
	pub fn extension(&self) -> Option<String> {
		todo!()
	}

	/// Back to string form.
	pub fn to_uri_string(&self) -> String {
		todo!()
	}
}
