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

/// Errors produced by loading or saving OpenTimelineIO JSON.
#[derive(Debug, thiserror::Error)]
pub enum OtioError {
	/// The document could not be parsed (or a value could not be
	/// serialized) as JSON.
	#[error("OpenTimelineIO JSON error: {0}")]
	Json(#[from] serde_json::Error),
	/// The underlying file could not be read or written.
	#[error("OpenTimelineIO file error: {0}")]
	Io(#[from] std::io::Error),
}

/// Convenience alias used by the binding API.
pub type Result<T> = std::result::Result<T, OtioError>;

#[cfg(test)]
mod tests {
	use super::*;
	use std::error::Error as _;

	/// Every variant renders a non-empty, module-prefixed message.
	#[test]
	fn display_is_non_empty() {
		let json_err = serde_json::from_str::<()>("not json").unwrap_err();
		let io_err = std::io::Error::new(std::io::ErrorKind::NotFound, "no such file");
		let cases = [
			(
				OtioError::Json(json_err),
				"OpenTimelineIO JSON error: ",
			),
			(OtioError::Io(io_err), "OpenTimelineIO file error: "),
		];
		for (err, prefix) in cases {
			let msg = err.to_string();
			assert!(!msg.is_empty(), "Display for {err:?} is empty");
			assert!(msg.starts_with(prefix), "{msg:?} lacks module prefix");
		}
	}

	/// The wrapped error is reachable through `source()` (downstream error
	/// wrapping relationship).
	#[test]
	fn source_forwards() {
		let json_err = serde_json::from_str::<()>("not json").unwrap_err();
		let io_err = std::io::Error::new(std::io::ErrorKind::NotFound, "no such file");
		assert!(OtioError::Json(json_err).source().is_some());
		assert!(OtioError::Io(io_err).source().is_some());
	}

	/// The `From` conversions (used by `?`) still work.
	#[test]
	fn from_conversions() {
		let json_err = serde_json::from_str::<()>("not json").unwrap_err();
		let io_err = std::io::Error::new(std::io::ErrorKind::NotFound, "no such file");
		assert!(matches!(OtioError::from(json_err), OtioError::Json(_)));
		assert!(matches!(OtioError::from(io_err), OtioError::Io(_)));
	}

	/// The error is object-safe: every variant boxes into
	/// `Box<dyn std::error::Error>`.
	#[test]
	fn object_safe() {
		let json_err = serde_json::from_str::<()>("not json").unwrap_err();
		let io_err = std::io::Error::new(std::io::ErrorKind::NotFound, "no such file");
		let errs: Vec<Box<dyn std::error::Error>> =
			vec![Box::new(OtioError::Json(json_err)), Box::new(OtioError::Io(io_err))];
		assert_eq!(errs.len(), 2);
		for err in &errs {
			assert!(!err.to_string().is_empty());
		}
	}
}
