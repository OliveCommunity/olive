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

//! Error codes, mirroring `include/timeline/error.h` verbatim;
//! project-wide -MMCCCC scheme (module registry in include/common/error.h),
//! pass-through untranslated. Timeline is module 04 (no CANCELLED code;
//! only codec=05 and task=08 carry one).

/// ABI version stamped into every handle.
pub const OAKTIMELINE_ABI_VERSION: u32 = 1;
/// Success.
pub const OAKTIMELINE_OK: i32 = 0;
/// Null handle or invalid argument.
pub const OAKTIMELINE_E_INVALID: i32 = -40001;
/// Call not valid in the current state.
pub const OAKTIMELINE_E_STATE: i32 = -40002;
/// The underlying operation failed.
pub const OAKTIMELINE_E_FAILED: i32 = -40003;
/// Index out of range / entry not found.
pub const OAKTIMELINE_E_NOT_FOUND: i32 = -40004;
/// Allocation failed.
pub const OAKTIMELINE_E_NOMEM: i32 = -40005;

/// Crate-internal result type; the FFI layer maps it to the codes.
pub type Result<T> = std::result::Result<T, Error>;

/// Crate-internal error.
#[derive(Debug, thiserror::Error)]
pub enum Error {
	/// Null handle or invalid argument.
	#[error("timeline: invalid argument")]
	Invalid,
	/// Wrong state.
	#[error("timeline: wrong state")]
	State,
	/// Operation failed (context string is log-only).
	#[error("timeline: operation failed: {0}")]
	Failed(String),
	/// Index out of range / entry not found.
	#[error("timeline: not found")]
	NotFound,
	/// Out of memory.
	#[error("timeline: out of memory")]
	NoMem,
}

impl Error {
	/// Map to the public error code.
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKTIMELINE_E_INVALID,
			Error::State => OAKTIMELINE_E_STATE,
			Error::Failed(_) => OAKTIMELINE_E_FAILED,
			Error::NotFound => OAKTIMELINE_E_NOT_FOUND,
			Error::NoMem => OAKTIMELINE_E_NOMEM,
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	/// Every variant renders a non-empty, module-prefixed message.
	#[test]
	fn display_is_non_empty() {
		let cases = [
			(Error::Invalid, "timeline: invalid argument"),
			(Error::State, "timeline: wrong state"),
			(Error::Failed("boom".into()), "timeline: operation failed: boom"),
			(Error::NotFound, "timeline: not found"),
			(Error::NoMem, "timeline: out of memory"),
		];
		for (err, expect) in cases {
			let msg = err.to_string();
			assert!(!msg.is_empty(), "Display for {err:?} is empty");
			assert_eq!(msg, expect, "Display for {err:?}");
		}
	}

	/// `Failed` carries the context string in its message.
	#[test]
	fn failed_includes_context() {
		let msg = Error::Failed("disk full".into()).to_string();
		assert!(msg.contains("disk full"));
	}

	/// The error is object-safe: every variant boxes into
	/// `Box<dyn std::error::Error>`.
	#[test]
	fn object_safe() {
		let errs: Vec<Box<dyn std::error::Error>> = vec![
			Box::new(Error::Invalid),
			Box::new(Error::State),
			Box::new(Error::Failed("boom".into())),
			Box::new(Error::NotFound),
			Box::new(Error::NoMem),
		];
		assert_eq!(errs.len(), 5);
		for err in &errs {
			assert!(!err.to_string().is_empty());
		}
	}

	/// Implementing `std::error::Error` must not change the FFI code mapping.
	#[test]
	fn codes_unchanged() {
		assert_eq!(Error::Invalid.code(), OAKTIMELINE_E_INVALID);
		assert_eq!(Error::State.code(), OAKTIMELINE_E_STATE);
		assert_eq!(Error::Failed("x".into()).code(), OAKTIMELINE_E_FAILED);
		assert_eq!(Error::NotFound.code(), OAKTIMELINE_E_NOT_FOUND);
		assert_eq!(Error::NoMem.code(), OAKTIMELINE_E_NOMEM);
	}
}
