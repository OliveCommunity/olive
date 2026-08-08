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

//! Pure-Rust OpenTimelineIO JSON binding for Oak Video Editor.
//!
//! `oakotio` is a self-contained serde model of the OpenTimelineIO JSON
//! format, covering exactly the object graph Oak's project load/save tasks
//! use (`src/task/src/project/loadotio/loadotio.cpp` and
//! `src/task/src/project/saveotio/saveotio.cpp`): `RationalTime`,
//! `TimeRange`, `Clip`, `Gap`, `Transition`, `Track`, `Stack`, `Timeline`,
//! `ExternalReference`, `MissingReference` and `SerializableCollection`.
//!
//! The writer reproduces the opentimelineio C++ writer's output byte for
//! byte (4-space indentation, `": "` separators, inline empty objects and
//! arrays, shortest float representation, no trailing newline); the reader
//! tolerates hand-written files, preserving unknown fields verbatim across a
//! round-trip and defaulting missing fields. See `README.md` for the design
//! rationale and the parity notes against the C++ implementation.

use std::path::Path;

pub mod error;
pub mod model;

pub use error::{OtioError, Result};
pub use model::*;

/// Parse an OpenTimelineIO JSON document from a string.
///
/// The root may be a `Timeline`, a `SerializableCollection`, or any other
/// schema; unrecognized roots are kept whole as `model::Serializable::Raw`
/// so they round-trip untouched.
pub fn from_json_string(text: &str) -> Result<Serializable> {
    Ok(serde_json::from_str(text)?)
}

/// Read and parse an OpenTimelineIO JSON document from a file.
pub fn from_json_file(path: impl AsRef<Path>) -> Result<Serializable> {
    from_json_string(&std::fs::read_to_string(path)?)
}
