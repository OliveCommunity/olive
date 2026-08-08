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
#[derive(Debug)]
pub enum Error {
	/// Null handle or invalid argument.
	Invalid,
	/// Wrong state.
	State,
	/// Operation failed (context string is log-only).
	Failed(String),
	/// Not found.
	NotFound,
	/// Out of memory.
	NoMem,
	/// The operation was cancelled.
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
