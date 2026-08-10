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
#[derive(Debug)]
pub enum Error {
	/// Null handle or invalid argument.
	Invalid,
	/// Wrong state.
	State,
	/// Operation failed (context string is log-only).
	Failed(String),
	/// Index out of range / entry not found.
	NotFound,
	/// Out of memory.
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
