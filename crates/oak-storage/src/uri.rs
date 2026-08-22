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
///
/// Two shapes:
/// - `scheme://body` — explicit scheme URIs (`file:///abs/path.ove`,
///   `oakdb://user:pass@host/db`). The body is the scheme-specific part
///   (for `file` it is the path, for `oakdb` the connection string).
/// - a bare path (`/abs/path.ove`, `rel/path.ove`) — normalized to a
///   `file://` URI so C++ callers that pass plain filenames keep working
///   (M10 §2.2 note).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StorageUri {
	/// Scheme ("file", "oakdb", …).
	pub scheme: String,
	/// Scheme-specific body (path for file://, connection string for
	/// oakdb://).
	pub body: String,
}

impl StorageUri {
	/// Parse a URI string; bare paths are normalized to file:// URIs.
	///
	/// `Err(Error::Invalid)` for an empty string. A scheme is recognized
	/// only in the `scheme://` form (`[a-zA-Z][a-zA-Z0-9+.-]*` per RFC
	/// 3986 §3.1); anything else is a bare path.
	pub fn parse(s: &str) -> crate::error::Result<StorageUri> {
		if s.is_empty() {
			return Err(crate::error::Error::Invalid);
		}
		match s.find("://") {
			Some(pos) if is_scheme(&s[..pos]) => Ok(StorageUri {
				scheme: s[..pos].to_string(),
				body: s[pos + 3..].to_string(),
			}),
			_ => Ok(StorageUri {
				scheme: "file".to_string(),
				body: s.to_string(),
			}),
		}
	}

	/// File extension (lowercased, without dot) for file:// URIs;
	/// `None` for other schemes or a path without an extension.
	pub fn extension(&self) -> Option<String> {
		if self.scheme != "file" {
			return None;
		}
		let name = self.body.rsplit('/').next().unwrap_or(&self.body);
		let base = match name.rsplit_once('.') {
			// A stem-less name (dotfile) has no extension.
			Some((stem, ext)) if !stem.is_empty() && !ext.is_empty() => ext,
			_ => "",
		};
		if base.is_empty() {
			None
		} else {
			Some(base.to_lowercase())
		}
	}

	/// The local filesystem path for file:// URIs (`None` otherwise).
	pub fn local_path(&self) -> Option<&str> {
		if self.scheme == "file" {
			Some(&self.body)
		} else {
			None
		}
	}

	/// Back to string form (`scheme://body`).
	pub fn to_uri_string(&self) -> String {
		format!("{}://{}", self.scheme, self.body)
	}
}

/// Whether `s` is a valid URI scheme (RFC 3986 §3.1).
fn is_scheme(s: &str) -> bool {
	let mut chars = s.chars();
	match chars.next() {
		Some(c) if c.is_ascii_alphabetic() => {}
		_ => return false,
	}
	chars.all(|c| c.is_ascii_alphanumeric() || matches!(c, '+' | '-' | '.'))
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn bare_paths_normalize_to_file() {
		let uri = StorageUri::parse("/tmp/proj.ove").unwrap();
		assert_eq!(uri.scheme, "file");
		assert_eq!(uri.body, "/tmp/proj.ove");
		assert_eq!(uri.to_uri_string(), "file:///tmp/proj.ove");

		let rel = StorageUri::parse("proj.ove").unwrap();
		assert_eq!(rel.to_uri_string(), "file://proj.ove");
	}

	#[test]
	fn explicit_schemes_are_preserved() {
		let uri = StorageUri::parse("file:///a/b.ove").unwrap();
		assert_eq!(uri.scheme, "file");
		assert_eq!(uri.body, "/a/b.ove");

		let db = StorageUri::parse("oakdb://user:pass@host:5432/db").unwrap();
		assert_eq!(db.scheme, "oakdb");
		assert_eq!(db.body, "user:pass@host:5432/db");

		// Scheme names follow RFC 3986: alphanumeric plus + - .
		let with_dots = StorageUri::parse("oakdb+sqlite:///abs/path.db").unwrap();
		assert_eq!(with_dots.scheme, "oakdb+sqlite");
		assert_eq!(with_dots.body, "/abs/path.db");
	}

	#[test]
	fn empty_input_is_invalid() {
		assert!(matches!(
			StorageUri::parse(""),
			Err(crate::error::Error::Invalid)
		));
	}

	#[test]
	fn extension_is_lowercased_and_dotless() {
		assert_eq!(
			StorageUri::parse("/a/b.ove").unwrap().extension(),
			Some("ove".to_string())
		);
		assert_eq!(
			StorageUri::parse("/a/b.OTIO").unwrap().extension(),
			Some("otio".to_string())
		);
		assert_eq!(
			StorageUri::parse("/a/b.fcpxml").unwrap().extension(),
			Some("fcpxml".to_string())
		);
		assert_eq!(StorageUri::parse("/a/b").unwrap().extension(), None);
		assert_eq!(StorageUri::parse("/a/b.").unwrap().extension(), None);
		// Dotfiles do not count as an extension.
		assert_eq!(StorageUri::parse("/a/.ove").unwrap().extension(), None);
		// Non-file schemes carry no extension.
		assert_eq!(
			StorageUri::parse("oakdb://conn").unwrap().extension(),
			None
		);
	}

	#[test]
	fn local_path_is_file_body_only() {
		assert_eq!(
			StorageUri::parse("file:///x/y.ove").unwrap().local_path(),
			Some("/x/y.ove")
		);
		assert_eq!(StorageUri::parse("/x/y.ove").unwrap().local_path(), Some("/x/y.ove"));
		assert_eq!(StorageUri::parse("oakdb://c").unwrap().local_path(), None);
	}

	#[test]
	fn uri_string_round_trips() {
		for s in [
			"file:///a/b.ove",
			"oakdb://user@host/db",
			"oakdb+sqlite:///abs/path.db",
		] {
			let uri = StorageUri::parse(s).unwrap();
			assert_eq!(uri.to_uri_string(), s);
		}
	}
}
