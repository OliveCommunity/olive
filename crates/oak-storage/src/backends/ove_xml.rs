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

//! The `.ove` XML file backend. Graph (de)serialization is delegated to
//! oaknode's serializer (`bridge::node`); this backend owns the file
//! container: version-header probe (M10 §2.1 info codes) and the plain
//! XML file I/O.
//!
//! The file format is the current-version `<project version="1">`
//! document produced by `oak_node::serializer::save`. Historical files
//! carry an `<olive version="NNNNNN">` root; the backend probes the
//! root element and reports TOO_OLD / TOO_NEW / UNKNOWN_VERSION per
//! M10 §2.1 before delegating to the serializer.
//!
//! `OAKSTORAGE_SAVE_COMPRESS` is accepted but not implemented: the
//! oaknode serializer emits plain XML only (the OVEC compressed
//! container is a C++ follow-up), so the bit is ignored and the file is
//! written uncompressed. Unknown option bits are ignored too (M10 §2.2).

use crate::backend::LoadResult;
use crate::error::{Error, OAKSTORAGE_OK, OAKSTORAGE_TOO_NEW, OAKSTORAGE_TOO_OLD, OAKSTORAGE_UNKNOWN_VERSION};
use crate::nodeutil::ProjectArc;
use crate::uri::StorageUri;
use oak_node::serializer::XmlRead;

/// The ove-xml backend (`file://` + `.ove`).
pub struct OveXmlBackend;

/// Detected version of a project document.
enum Probe {
	/// Current schema (`<project>` root, or `<olive>` at the build
	/// version).
	Current,
	/// Historical `<olive>` root with a known, older version.
	Old,
	/// Newer than this build can load.
	TooNew,
	/// Recognized root without a recognizable version.
	UnknownVersion,
}

/// Probe the root element of an XML document for the version ladder.
/// `Err(())` means the document has no parseable root (corrupt input —
/// the caller reports E_FORMAT, not a version info code).
fn probe_version(xml: &str) -> std::result::Result<Probe, ()> {
	let mut reader = oak_node::serializer::XmlReaderBridge::new(xml).ok_or(())?;
	if !reader.next_start_element() {
		return Err(());
	}
	let root = reader.name().to_string();
	let version = reader
		.attribute("version")
		.and_then(|v| v.parse::<u32>().ok());
	match root.as_str() {
		// Current schema root; the serializer accepts any version attr
		// on `<project>` (schema version 1 today).
		"project" => Ok(Probe::Current),
		"olive" => match version {
			None => Ok(Probe::UnknownVersion),
			Some(v) if v > oak_node::serializer::CURRENT_VERSION.0 => Ok(Probe::TooNew),
			Some(v) if v < oak_node::serializer::CURRENT_VERSION.0 => Ok(Probe::Old),
			Some(_) => Ok(Probe::Current),
		},
		_ => Ok(Probe::UnknownVersion),
	}
}

impl OveXmlBackend {
	/// Construct.
	pub fn new() -> Self {
		OveXmlBackend
	}

	/// Save a project (already read out of its handle) to the URI. The
	/// Rust-typed inner path: the facade-facing trait `save` converts the
	/// project handle and forwards here.
	pub fn save_project(
		&self,
		project: &ProjectArc,
		uri: &StorageUri,
		_options: u32,
	) -> crate::error::Result<()> {
		let path = uri
			.local_path()
			.ok_or(Error::Invalid)?
			.to_string();
		let xml = {
			let guard = project
				.lock()
				.map_err(|_| Error::State)?;
			crate::nodeutil::serializer_save(&guard)?
		};
		std::fs::write(&path, xml).map_err(|e| Error::Io(e.to_string()))?;
		Ok(())
	}
}

impl Default for OveXmlBackend {
	fn default() -> Self {
		Self::new()
	}
}

impl crate::backend::StorageBackend for OveXmlBackend {
	fn name(&self) -> &'static str {
		"ove-xml"
	}

	fn uri_scheme(&self) -> &'static str {
		"file"
	}

	fn can_handle(&self, uri: &StorageUri) -> bool {
		uri.extension().map(|e| e == "ove").unwrap_or(false)
	}

	fn load(&self, uri: &StorageUri) -> crate::error::Result<LoadResult> {
		let path = uri
			.local_path()
			.ok_or(Error::Invalid)?
			.to_string();
		let xml = std::fs::read_to_string(&path).map_err(|e| Error::Io(e.to_string()))?;

		let probe = probe_version(&xml).map_err(|_| {
			Error::Format(format!("'{}' is not a parseable project document", path))
		})?;
		match probe {
			Probe::TooNew => return Ok(LoadResult::info_only(OAKSTORAGE_TOO_NEW)),
			Probe::UnknownVersion => {
				return Ok(LoadResult::info_only(OAKSTORAGE_UNKNOWN_VERSION));
			}
			Probe::Current | Probe::Old => {}
		}

		let project = crate::nodeutil::serializer_load(&xml)?;
		let handle = crate::nodeutil::make_project_owned(project);
		let version_info = if matches!(probe, Probe::Old) {
			OAKSTORAGE_TOO_OLD
		} else {
			OAKSTORAGE_OK
		};
		Ok(LoadResult::with_info(handle, version_info))
	}

	fn save(
		&self,
		project: crate::handle::CHandle,
		uri: &StorageUri,
		options: u32,
	) -> crate::error::Result<()> {
		// Facade boundary: convert the project handle to the boxed
		// project, then run the Rust-typed save.
		let arc = unsafe { crate::nodeutil::project_arc(&project)? };
		self.save_project(&arc, uri, options)
	}
}
