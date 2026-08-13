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

//! Error codes, mirroring `include/undo/error.h`; project-wide
//! -MMCCCC scheme (module 02), pass-through untranslated.

use thiserror::Error;

/// Success.
pub const OAKUNDO_OK: i32 = 0;
/// Empty handle or invalid argument.
pub const OAKUNDO_E_INVALID: i32 = -20001;
/// Call not valid in the current state.
pub const OAKUNDO_E_STATE: i32 = -20002;
/// The underlying operation failed.
pub const OAKUNDO_E_FAILED: i32 = -20003;
/// Index out of range / entry not found.
pub const OAKUNDO_E_NOT_FOUND: i32 = -20004;
/// Allocation failed.
pub const OAKUNDO_E_NOMEM: i32 = -20005;

/// Crate-internal result type.
pub type Result<T> = std::result::Result<T, Error>;

/// Crate-internal error.
#[derive(Debug, Error)]
pub enum Error {
	/// Empty handle or invalid argument.
	#[error("undo: empty handle or invalid argument")]
	Invalid,
	/// Wrong state.
	#[error("undo: call not valid in current state")]
	State,
	/// Operation failed (context string is log-only).
	#[error("undo: operation failed: {0}")]
	Failed(String),
	/// Not found.
	#[error("undo: entry not found")]
	NotFound,
	/// Out of memory.
	#[error("undo: allocation failed")]
	NoMem,
}

impl Error {
	/// Map to the public error code.
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKUNDO_E_INVALID,
			Error::State => OAKUNDO_E_STATE,
			Error::Failed(_) => OAKUNDO_E_FAILED,
			Error::NotFound => OAKUNDO_E_NOT_FOUND,
			Error::NoMem => OAKUNDO_E_NOMEM,
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn public_codes_match_header_values() {
		// Load-bearing values from include/undo/error.h (module 02).
		assert_eq!(OAKUNDO_OK, 0);
		assert_eq!(OAKUNDO_E_INVALID, -20001);
		assert_eq!(OAKUNDO_E_STATE, -20002);
		assert_eq!(OAKUNDO_E_FAILED, -20003);
		assert_eq!(OAKUNDO_E_NOT_FOUND, -20004);
		assert_eq!(OAKUNDO_E_NOMEM, -20005);
	}

	#[test]
	fn code_maps_each_variant() {
		assert_eq!(Error::Invalid.code(), OAKUNDO_E_INVALID);
		assert_eq!(Error::State.code(), OAKUNDO_E_STATE);
		assert_eq!(Error::Failed("boom".to_string()).code(), OAKUNDO_E_FAILED);
		assert_eq!(Error::NotFound.code(), OAKUNDO_E_NOT_FOUND);
		assert_eq!(Error::NoMem.code(), OAKUNDO_E_NOMEM);
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

