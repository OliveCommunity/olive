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

//! Subcommand implementations.
//!
//! Each subcommand is a faithful port of its `cli/main.cpp` counterpart:
//! the argument validation is real (same messages, same usage-error code),
//! and the facade work gates on [`crate::deferred::require`] — while the
//! families a subcommand needs are deferred, it prints the "not yet
//! available" error with the reasons and exits with the C++-compatible code
//! (1 for info/probe, 2 for render/transcode), never crashing.

pub mod info;
pub mod probe;
pub mod render;
pub mod transcode;

use crate::deferred::DeferredFamily;

/// 0 — success.
pub const EXIT_OK: i32 = 0;
/// 1 — general error (bad project/media file, no sequence, I/O failure).
pub const EXIT_ERROR: i32 = 1;
/// 2 — rendering unavailable or failed (e.g. no GL render backend).
pub const EXIT_RENDER_UNAVAILABLE: i32 = 2;
/// 64 — usage error.
pub const EXIT_USAGE: i32 = 64;

/// Gate a subcommand on its facade families.
///
/// When every family is wrapped this returns `Ok(())` and the subcommand's
/// port runs; when any is deferred it prints the composed "not yet
/// available" message to stderr and returns `Err(unavailable_code)` — the
/// code the C++ binary would exit with when that family's work is
/// impossible (1 for info/probe, 2 for render/transcode).
pub fn require_or(
	cmd: &str,
	families: &[&DeferredFamily],
	unavailable_code: i32,
) -> Result<(), i32> {
	match crate::deferred::require(families) {
		Ok(()) => Ok(()),
		Err(msg) => {
			eprintln!("error: {cmd}: {msg}");
			Err(unavailable_code)
		}
	}
}

/// Fallback for the (today unreachable) success arm of `require_or`: the
/// gate reported the families available, but the call-through port is not
/// wired yet. Never panics; reports an internal error and returns `code`.
pub fn port_not_wired(cmd: &str, code: i32) -> i32 {
	eprintln!(
		"error: {cmd}: internal error: facade families reported available but no port is wired yet"
	);
	code
}
