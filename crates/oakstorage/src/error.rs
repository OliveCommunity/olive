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

//! Error and info codes (M10 §2.1; -MMCCCC scheme, module 10).

/// Success.
pub const OAKSTORAGE_OK: i32 = 0;
/// Project version too old (info code, positive).
pub const OAKSTORAGE_TOO_OLD: i32 = 1;
/// Project version too new (info code).
pub const OAKSTORAGE_TOO_NEW: i32 = 2;
/// Unknown project version (info code).
pub const OAKSTORAGE_UNKNOWN_VERSION: i32 = 3;
/// Null handle or invalid argument.
pub const OAKSTORAGE_E_INVALID: i32 = -100001;
/// Call not valid in the current state.
pub const OAKSTORAGE_E_STATE: i32 = -100002;
/// Entry not found.
pub const OAKSTORAGE_E_NOT_FOUND: i32 = -100003;
/// The underlying operation failed.
pub const OAKSTORAGE_E_FAILED: i32 = -100004;
/// No backend claimed the URI.
pub const OAKSTORAGE_E_NO_BACKEND: i32 = -100005;
/// Parse/format failure (XML/DB constraint).
pub const OAKSTORAGE_E_FORMAT: i32 = -100006;
/// I/O failure.
pub const OAKSTORAGE_E_IO: i32 = -100007;
/// Allocation failed.
pub const OAKSTORAGE_E_NOMEM: i32 = -100008;

/// Crate-internal result type.
pub type Result<T> = std::result::Result<T, Error>;

/// Crate-internal error.
#[derive(Debug)]
pub enum Error {
	/// Null handle or invalid argument.
	Invalid,
	/// Wrong state.
	State,
	/// Not found.
	NotFound,
	/// Operation failed (context string is log-only).
	Failed(String),
	/// No backend claimed the URI.
	NoBackend,
	/// Format error.
	Format(String),
	/// I/O error.
	Io(String),
	/// Out of memory.
	NoMem,
}

impl Error {
	/// Map to the public error code.
	pub fn code(&self) -> i32 {
		match self {
			Error::Invalid => OAKSTORAGE_E_INVALID,
			Error::State => OAKSTORAGE_E_STATE,
			Error::NotFound => OAKSTORAGE_E_NOT_FOUND,
			Error::Failed(_) => OAKSTORAGE_E_FAILED,
			Error::NoBackend => OAKSTORAGE_E_NO_BACKEND,
			Error::Format(_) => OAKSTORAGE_E_FORMAT,
			Error::Io(_) => OAKSTORAGE_E_IO,
			Error::NoMem => OAKSTORAGE_E_NOMEM,
		}
	}
}
