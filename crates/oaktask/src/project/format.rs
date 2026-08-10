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

//! Interchange-format dispatch for the OTIO tasks.
//!
//! `LoadOTIOTask` / `SaveOTIOTask` accept both OpenTimelineIO JSON
//! (`.otio`) and FCPXML (`.fcpxml`) documents. The format is inferred from
//! the filename extension (case-insensitive), so the frozen C ABI
//! (`include/task/project.h`) needs no format parameter: the filename
//! passed to `oaktask_create_project_load_otio` /
//! `oaktask_create_project_save_otio` selects the codec. Format handling
//! ends at the `oakotio` parse/serialize call in each task — the
//! track/clip/footage building is shared between both formats.
//!
//! No OTIO or FCPXML type crosses the oaktask C ABI (README decision #6).

use std::path::Path;

use crate::error::{Error, Result};

/// The interchange formats the OTIO tasks can read and write.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InterchangeFormat {
	/// OpenTimelineIO JSON (`.otio`).
	OtioJson,
	/// Final Cut Pro XML (`.fcpxml`).
	Fcpxml,
}

/// The filename extensions each format dispatches on.
pub const SUPPORTED_EXTENSIONS: &str = ".otio, .fcpxml";

impl InterchangeFormat {
	/// Dispatch on a filename's extension (case-insensitive).
	///
	/// `foo.OTIO` and `foo.FcpXml` select the same format as `foo.otio` /
	/// `foo.fcpxml`; any other extension (or a filename without one) is
	/// `Error::Failed` with a message naming the supported extensions.
	pub fn of(filename: &str) -> Result<InterchangeFormat> {
		let ext = Path::new(filename)
			.extension()
			.map(|e| e.to_string_lossy().into_owned())
			.unwrap_or_default();
		match ext.to_ascii_lowercase().as_str() {
			"otio" => Ok(InterchangeFormat::OtioJson),
			"fcpxml" => Ok(InterchangeFormat::Fcpxml),
			_ => Err(Error::Failed(format!(
				"Unknown project file format for \"{filename}\" (supported: {SUPPORTED_EXTENSIONS})"
			))),
		}
	}
}
