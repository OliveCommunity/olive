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

//! Error codes, mirroring `include/codec/error.h` verbatim; project-wide
//! -MMCCCC scheme (module registry in include/common/error.h), pass-through untranslated.

/// Success.
pub const OAKCODEC_OK: i32 = 0;
/// Null handle or invalid argument.
pub const OAKCODEC_E_INVALID: i32 = -50001;
/// Call not valid in the current state.
pub const OAKCODEC_E_STATE: i32 = -50002;
/// The underlying operation failed.
pub const OAKCODEC_E_FAILED: i32 = -50003;
/// Index out of range / entry not found.
pub const OAKCODEC_E_NOT_FOUND: i32 = -50004;
/// Allocation failed.
pub const OAKCODEC_E_NOMEM: i32 = -50005;
/// The operation was cancelled.
pub const OAKCODEC_E_CANCELLED: i32 = -50006;

/// The frozen C-ABI `abi_version` tag (`include/codec/decoder.h`); kept for
/// parity with the handle structs, no longer stamped at runtime.
pub const OAKCODEC_ABI_VERSION: u32 = 1;

/// Crate-internal result type; the FFI layer maps it to the codes.
pub type Result<T> = std::result::Result<T, Error>;

/// Crate-internal error.
#[derive(Debug, thiserror::Error)]
pub enum Error {
	/// Null handle or invalid argument.
	#[error("codec: invalid argument")]
	Invalid,
	/// Wrong state.
	#[error("codec: invalid state")]
	State,
	/// Operation failed (context string is log-only).
	#[error("codec: operation failed: {0}")]
	Failed(String),
	/// Not found.
	#[error("codec: not found")]
	NotFound,
	/// Out of memory.
	#[error("codec: out of memory")]
	NoMem,
	/// The operation was cancelled.
	#[error("codec: operation cancelled")]
	Cancelled,
}

impl Error {
	/// Map to the public error code.
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKCODEC_E_INVALID,
			Error::State => OAKCODEC_E_STATE,
			Error::Failed(_) => OAKCODEC_E_FAILED,
			Error::NotFound => OAKCODEC_E_NOT_FOUND,
			Error::NoMem => OAKCODEC_E_NOMEM,
			Error::Cancelled => OAKCODEC_E_CANCELLED,
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	/// Every variant must produce a non-empty `Display` message; the
	/// `Failed` variant must surface its context string.
	#[test]
	fn display_is_non_empty() {
		for msg in [
			Error::Invalid.to_string(),
			Error::State.to_string(),
			Error::Failed("context".into()).to_string(),
			Error::NotFound.to_string(),
			Error::NoMem.to_string(),
			Error::Cancelled.to_string(),
		] {
			assert!(!msg.trim().is_empty(), "empty Display message");
		}
		assert!(
			Error::Failed("context".into())
				.to_string()
				.contains("context")
		);
	}

	/// `Error` must be usable behind a trait object.
	#[test]
	fn error_is_object_safe() {
		let errs: Vec<Box<dyn std::error::Error>> = vec![
			Box::new(Error::Invalid),
			Box::new(Error::State),
			Box::new(Error::Failed("context".into())),
			Box::new(Error::NotFound),
			Box::new(Error::NoMem),
			Box::new(Error::Cancelled),
		];
		assert_eq!(errs.len(), 6);
	}

	/// No variant wraps a downstream error, so `source()` stays `None`.
	#[test]
	fn source_is_none() {
		assert!(std::error::Error::source(&Error::Failed("context".into())).is_none());
	}

	/// The `code()` mapping must be unchanged by the `Error` trait impl.
	#[test]
	fn code_mapping_unchanged() {
		assert_eq!(Error::Invalid.code(), OAKCODEC_E_INVALID);
		assert_eq!(Error::State.code(), OAKCODEC_E_STATE);
		assert_eq!(Error::Failed("context".into()).code(), OAKCODEC_E_FAILED);
		assert_eq!(Error::NotFound.code(), OAKCODEC_E_NOT_FOUND);
		assert_eq!(Error::NoMem.code(), OAKCODEC_E_NOMEM);
		assert_eq!(Error::Cancelled.code(), OAKCODEC_E_CANCELLED);
	}
}
