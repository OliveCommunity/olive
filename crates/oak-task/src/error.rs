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

//! Error codes, mirroring `include/task/error.h` verbatim; project-wide
//! -MMCCCC scheme (module registry in include/common/error.h), pass-through
//! untranslated. Module number for task is **08**; task additionally defines
//! `OAKTASK_E_CANCELLED` (see include/task/error.h).

/// Success.
pub const OAKTASK_OK: i32 = 0;
/// Null handle or invalid argument.
pub const OAKTASK_E_INVALID: i32 = -80001;
/// Call not valid in the current state.
pub const OAKTASK_E_STATE: i32 = -80002;
/// The underlying operation failed.
pub const OAKTASK_E_FAILED: i32 = -80003;
/// Index out of range / entry not found.
pub const OAKTASK_E_NOT_FOUND: i32 = -80004;
/// Allocation failed.
pub const OAKTASK_E_NOMEM: i32 = -80005;
/// The task was cancelled.
pub const OAKTASK_E_CANCELLED: i32 = -80006;

/// Crate-internal result type; the FFI layer maps it to the codes.
pub type Result<T> = std::result::Result<T, Error>;

/// Crate-internal error.
#[derive(Debug, thiserror::Error)]
pub enum Error {
	/// Null handle or invalid argument.
	#[error("task: invalid argument")]
	Invalid,
	/// Wrong state.
	#[error("task: wrong state")]
	State,
	/// Operation failed (context string is log-only).
	#[error("task: operation failed: {0}")]
	Failed(String),
	/// Not found.
	#[error("task: not found")]
	NotFound,
	/// Out of memory.
	#[error("task: out of memory")]
	NoMem,
	/// The operation was cancelled.
	#[error("task: cancelled")]
	Cancelled,
}

impl Error {
	/// Map to the public error code.
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKTASK_E_INVALID,
			Error::State => OAKTASK_E_STATE,
			Error::Failed(_) => OAKTASK_E_FAILED,
			Error::NotFound => OAKTASK_E_NOT_FOUND,
			Error::NoMem => OAKTASK_E_NOMEM,
			Error::Cancelled => OAKTASK_E_CANCELLED,
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
			(Error::Invalid, "task: invalid argument"),
			(Error::State, "task: wrong state"),
			(Error::Failed("boom".into()), "task: operation failed: boom"),
			(Error::NotFound, "task: not found"),
			(Error::NoMem, "task: out of memory"),
			(Error::Cancelled, "task: cancelled"),
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
			Box::new(Error::Cancelled),
		];
		assert_eq!(errs.len(), 6);
		for err in &errs {
			assert!(!err.to_string().is_empty());
		}
	}

	/// Implementing `std::error::Error` must not change the FFI code mapping.
	#[test]
	fn codes_unchanged() {
		assert_eq!(Error::Invalid.code(), OAKTASK_E_INVALID);
		assert_eq!(Error::State.code(), OAKTASK_E_STATE);
		assert_eq!(Error::Failed("x".into()).code(), OAKTASK_E_FAILED);
		assert_eq!(Error::NotFound.code(), OAKTASK_E_NOT_FOUND);
		assert_eq!(Error::NoMem.code(), OAKTASK_E_NOMEM);
		assert_eq!(Error::Cancelled.code(), OAKTASK_E_CANCELLED);
	}
}
