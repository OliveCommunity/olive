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

//! Facade error codes, mirroring `engine/include/oakengine/init.h`.
//!
//! The facade is module 00 of the project-wide -MMCCCC scheme
//! (see `include/common/error.h`): its own codes are `-(0*10000 + CCCC)`,
//! i.e. -1..-6. Codes returned by a wrapped module call pass through
//! **untranslated** — the numeric module prefix preserves provenance
//! (e.g. -20004 is oakundo's NOT_FOUND, -30001 oaknode's INVALID) and the
//! facade never rewrites them.

use thiserror::Error;

/// Success.
pub const OAKENGINE_OK: i32 = 0;
/// Empty handle or invalid argument.
pub const OAKENGINE_E_INVALID: i32 = -1;
/// Call not valid in the current state.
pub const OAKENGINE_E_STATE: i32 = -2;
/// The underlying operation failed.
pub const OAKENGINE_E_FAILED: i32 = -3;
/// Index out of range / entry not found.
pub const OAKENGINE_E_NOT_FOUND: i32 = -4;
/// Allocation failed (reserved; mirrors the -MMCCCC reserved list).
pub const OAKENGINE_E_NOMEM: i32 = -5;
/// The operation was cancelled (reserved; mirrors the -MMCCCC reserved
/// list).
pub const OAKENGINE_E_CANCELLED: i32 = -6;

/// Crate-internal result type.
pub type Result<T> = std::result::Result<T, Error>;

/// Crate-internal error.
#[derive(Debug, Error)]
pub enum Error {
	/// Empty handle or invalid argument.
	#[error("engine: invalid argument")]
	Invalid,
	/// Wrong state.
	#[error("engine: call not valid in the current state")]
	State,
	/// The underlying operation failed (context string is log-only).
	#[error("engine: operation failed: {0}")]
	Failed(String),
	/// Not found.
	#[error("engine: not found")]
	NotFound,
	/// Out of memory.
	#[error("engine: out of memory")]
	NoMem,
	/// Cancelled.
	#[error("engine: cancelled")]
	Cancelled,
	/// A module error code that must pass through untranslated.
	#[error("engine: module error code {0}")]
	Module(i32),
}

impl Error {
	/// Map to the public error code. Module codes pass through verbatim.
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKENGINE_E_INVALID,
			Error::State => OAKENGINE_E_STATE,
			Error::Failed(_) => OAKENGINE_E_FAILED,
			Error::NotFound => OAKENGINE_E_NOT_FOUND,
			Error::NoMem => OAKENGINE_E_NOMEM,
			Error::Cancelled => OAKENGINE_E_CANCELLED,
			Error::Module(code) => *code,
		}
	}

	/// Wrap a module return code. `0` (OK) never becomes an error; any
	/// negative code is kept as a pass-through [`Error::Module`].
	pub fn from_module(code: i32) -> Result<()> {
		if code == 0 {
			Ok(())
		} else {
			Err(Error::Module(code))
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	/// One instance of every variant (data-carrying ones get a sample
	/// payload).
	fn all_errors() -> Vec<Error> {
		vec![
			Error::Invalid,
			Error::State,
			Error::Failed("boom".to_string()),
			Error::NotFound,
			Error::NoMem,
			Error::Cancelled,
			Error::Module(-20004),
		]
	}

	#[test]
	fn display_is_non_empty_for_every_variant() {
		for e in all_errors() {
			let s = e.to_string();
			assert!(!s.is_empty(), "Display produced an empty message for {e:?}");
		}
	}

	#[test]
	fn error_is_object_safe() {
		// `Box<dyn std::error::Error>` must be constructible for every
		// variant; `source()` stays None (no wrapped downstream error).
		let errors: Vec<Box<dyn std::error::Error>> = all_errors()
			.into_iter()
			.map(|e| Box::new(e) as Box<dyn std::error::Error>)
			.collect();
		for e in &errors {
			assert!(!e.to_string().is_empty());
			assert!(e.source().is_none());
		}
	}

	#[test]
	fn code_is_unaffected_by_trait_impl() {
		assert_eq!(Error::Invalid.code(), OAKENGINE_E_INVALID);
		assert_eq!(Error::State.code(), OAKENGINE_E_STATE);
		assert_eq!(Error::Failed("boom".to_string()).code(), OAKENGINE_E_FAILED);
		assert_eq!(Error::NotFound.code(), OAKENGINE_E_NOT_FOUND);
		assert_eq!(Error::NoMem.code(), OAKENGINE_E_NOMEM);
		assert_eq!(Error::Cancelled.code(), OAKENGINE_E_CANCELLED);
		// Module codes pass through verbatim, untranslated.
		assert_eq!(Error::Module(-20004).code(), -20004);
	}

	#[test]
	fn from_module_wraps_negative_and_accepts_ok() {
		assert!(Error::from_module(0).is_ok());
		assert_eq!(Error::from_module(-20004).unwrap_err().code(), -20004);
	}
}
