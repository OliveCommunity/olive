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
#[derive(Debug)]
pub enum Error {
	/// Empty handle or invalid argument.
	Invalid,
	/// Wrong state.
	State,
	/// The underlying operation failed (context string is log-only).
	Failed(String),
	/// Not found.
	NotFound,
	/// Out of memory.
	NoMem,
	/// Cancelled.
	Cancelled,
	/// A module error code that must pass through untranslated.
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
