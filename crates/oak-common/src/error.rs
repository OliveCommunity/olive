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

//! Error codes, mirroring `include/common/error.h`; project-wide
//! -MMCCCC scheme (module 01), pass-through untranslated.

use thiserror::Error;

/// Success.
pub const OAKCOMMON_OK: i32 = 0;
/// Empty handle or invalid argument.
pub const OAKCOMMON_E_INVALID: i32 = -10001;
/// Call not valid in the current state.
pub const OAKCOMMON_E_STATE: i32 = -10002;
/// The underlying operation failed.
pub const OAKCOMMON_E_FAILED: i32 = -10003;
/// Index out of range / entry not found.
pub const OAKCOMMON_E_NOT_FOUND: i32 = -10004;
/// Allocation failed.
pub const OAKCOMMON_E_NOMEM: i32 = -10005;

/// Crate-internal result type.
pub type Result<T> = std::result::Result<T, Error>;

/// Crate-internal error.
#[derive(Debug, Error)]
pub enum Error {
	/// Empty handle or invalid argument.
	#[error("common: empty handle or invalid argument")]
	Invalid,
	/// Wrong state.
	#[error("common: call not valid in current state")]
	State,
	/// Operation failed (context string is log-only).
	#[error("common: operation failed: {0}")]
	Failed(String),
	/// Not found.
	#[error("common: entry not found")]
	NotFound,
	/// Out of memory.
	#[error("common: allocation failed")]
	NoMem,
}

impl Error {
	/// Map to the public error code.
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKCOMMON_E_INVALID,
			Error::State => OAKCOMMON_E_STATE,
			Error::Failed(_) => OAKCOMMON_E_FAILED,
			Error::NotFound => OAKCOMMON_E_NOT_FOUND,
			Error::NoMem => OAKCOMMON_E_NOMEM,
		}
	}

	/// Create a [`Error::Failed`] carrying a context message (log-only).
	///
	/// Convenience constructor used by the OCIO/`image` wrappers in
	/// `ocioutils.rs` / `oiioutils.rs` when an underlying library call fails.
	pub fn new(message: impl Into<String>) -> Self {
		Error::Failed(message.into())
	}
}

impl From<ocio_rs::OcioError> for Error {
	fn from(e: ocio_rs::OcioError) -> Self {
		// The Display impl of `OcioError` always produces a non-empty message
		// (every variant carries text or a fixed phrase); it becomes the
		// log-only context of `Error::Failed`.
		Error::Failed(e.to_string())
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn public_codes_match_header_values() {
		// Load-bearing values from include/common/error.h (module 01).
		assert_eq!(OAKCOMMON_OK, 0);
		assert_eq!(OAKCOMMON_E_INVALID, -10001);
		assert_eq!(OAKCOMMON_E_STATE, -10002);
		assert_eq!(OAKCOMMON_E_FAILED, -10003);
		assert_eq!(OAKCOMMON_E_NOT_FOUND, -10004);
		assert_eq!(OAKCOMMON_E_NOMEM, -10005);
	}

	#[test]
	fn error_code_maps_each_variant() {
		assert_eq!(Error::Invalid.code(), OAKCOMMON_E_INVALID);
		assert_eq!(Error::State.code(), OAKCOMMON_E_STATE);
		assert_eq!(Error::Failed("boom".to_string()).code(), OAKCOMMON_E_FAILED);
		assert_eq!(Error::NotFound.code(), OAKCOMMON_E_NOT_FOUND);
		assert_eq!(Error::NoMem.code(), OAKCOMMON_E_NOMEM);
	}

	#[test]
	fn error_codes_are_all_distinct() {
		let codes = [
			Error::Invalid.code(),
			Error::State.code(),
			Error::Failed(String::new()).code(),
			Error::NotFound.code(),
			Error::NoMem.code(),
		];
		for (i, a) in codes.iter().enumerate() {
			for b in &codes[i + 1..] {
				assert_ne!(a, b);
			}
			// Errors are strictly negative; OK stays zero.
			assert!(*a < 0);
		}
	}

	#[test]
	fn failed_message_is_preserved_in_debug() {
		// The context string is log-only but must survive to the log.
		let e = Error::Failed("context info".to_string());
		let dbg = format!("{e:?}");
		assert!(dbg.contains("context info"));
	}

	#[test]
	fn result_alias_round_trips_ok_and_err() {
		let ok: Result<i32> = Ok(7);
		assert_eq!(ok.unwrap(), 7);
		let err: Result<i32> = Err(Error::NotFound);
		assert_eq!(err.unwrap_err().code(), OAKCOMMON_E_NOT_FOUND);
	}

	#[test]
	fn new_creates_failed_with_message() {
		let e = Error::new("context info");
		assert!(matches!(e, Error::Failed(_)));
		assert_eq!(e.code(), OAKCOMMON_E_FAILED);
		assert!(format!("{e:?}").contains("context info"));
	}

	#[test]
	fn ocio_error_converts_to_failed() {
		let e = Error::from(ocio_rs::OcioError::InvalidInput(
			"bad colorspace".to_string(),
		));
		assert!(matches!(e, Error::Failed(_)));
		assert_eq!(e.code(), OAKCOMMON_E_FAILED);
		assert!(format!("{e:?}").contains("bad colorspace"));
	}

	#[test]
	fn display_is_non_empty_for_each_variant() {
		let variants = [
			Error::Invalid,
			Error::State,
			Error::Failed("context".to_string()),
			Error::NotFound,
			Error::NoMem,
		];
		for e in &variants {
			assert!(!e.to_string().is_empty());
		}
	}

	#[test]
	fn failed_display_includes_context() {
		let e = Error::Failed("context info".to_string());
		assert!(e.to_string().contains("context info"));
	}

	#[test]
	fn error_is_object_safe() {
		let e: Box<dyn std::error::Error> = Box::new(Error::NoMem);
		assert!(!e.to_string().is_empty());
	}
}
