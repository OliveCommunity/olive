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

//! Error codes, mirroring `include/node/error.h` verbatim; project-wide
//! -MMCCCC scheme (module registry in include/common/error.h), pass-through untranslated.

use thiserror::Error;

/// Success.
pub const OAKNODE_OK: i32 = 0;
/// Null handle or invalid argument.
pub const OAKNODE_E_INVALID: i32 = -30001;
/// Call not valid in the current state.
pub const OAKNODE_E_STATE: i32 = -30002;
/// The underlying operation failed.
pub const OAKNODE_E_FAILED: i32 = -30003;
/// Index out of range / entry not found.
pub const OAKNODE_E_NOT_FOUND: i32 = -30004;
/// Allocation failed.
pub const OAKNODE_E_NOMEM: i32 = -30005;

/// Crate-internal result type; the FFI layer maps it to the codes.
pub type Result<T> = std::result::Result<T, Error>;

/// Crate-internal error.
#[derive(Debug, PartialEq, Error)]
pub enum Error {
	/// Null handle or invalid argument.
	#[error("node: empty handle or invalid argument")]
	Invalid,
	/// Wrong state.
	#[error("node: call not valid in current state")]
	State,
	/// Operation failed (context string is log-only).
	#[error("node: operation failed: {0}")]
	Failed(String),
	/// Not found.
	#[error("node: entry not found")]
	NotFound,
	/// Out of memory.
	#[error("node: allocation failed")]
	NoMem,
}

impl Error {
	/// Map to the public error code.
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKNODE_E_INVALID,
			Error::State => OAKNODE_E_STATE,
			Error::Failed(_) => OAKNODE_E_FAILED,
			Error::NotFound => OAKNODE_E_NOT_FOUND,
			Error::NoMem => OAKNODE_E_NOMEM,
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn public_codes_match_header_values() {
		// Load-bearing values from include/node/error.h (module 03).
		assert_eq!(OAKNODE_OK, 0);
		assert_eq!(OAKNODE_E_INVALID, -30001);
		assert_eq!(OAKNODE_E_STATE, -30002);
		assert_eq!(OAKNODE_E_FAILED, -30003);
		assert_eq!(OAKNODE_E_NOT_FOUND, -30004);
		assert_eq!(OAKNODE_E_NOMEM, -30005);
	}

	#[test]
	fn code_maps_each_variant() {
		assert_eq!(Error::Invalid.code(), OAKNODE_E_INVALID);
		assert_eq!(Error::State.code(), OAKNODE_E_STATE);
		assert_eq!(Error::Failed("boom".to_string()).code(), OAKNODE_E_FAILED);
		assert_eq!(Error::NotFound.code(), OAKNODE_E_NOT_FOUND);
		assert_eq!(Error::NoMem.code(), OAKNODE_E_NOMEM);
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

