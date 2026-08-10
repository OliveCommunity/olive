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

//! Error type for the oakotio binding.

use std::fmt;

/// Errors produced by loading or saving OpenTimelineIO JSON.
#[derive(Debug)]
pub enum OtioError {
    /// The document could not be parsed (or a value could not be
    /// serialized) as JSON.
    Json(serde_json::Error),
    /// The underlying file could not be read or written.
    Io(std::io::Error),
}

impl fmt::Display for OtioError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            OtioError::Json(e) => write!(f, "OpenTimelineIO JSON error: {e}"),
            OtioError::Io(e) => write!(f, "OpenTimelineIO file error: {e}"),
        }
    }
}

impl std::error::Error for OtioError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            OtioError::Json(e) => Some(e),
            OtioError::Io(e) => Some(e),
        }
    }
}

impl From<serde_json::Error> for OtioError {
    fn from(e: serde_json::Error) -> OtioError {
        OtioError::Json(e)
    }
}

impl From<std::io::Error> for OtioError {
    fn from(e: std::io::Error) -> OtioError {
        OtioError::Io(e)
    }
}

/// Convenience alias used by the binding API.
pub type Result<T> = std::result::Result<T, OtioError>;
