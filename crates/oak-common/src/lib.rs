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

//! # oakcommon — shared utilities (Rust). Leaf module.
//!
//! Implements `include/common/*.h` verbatim. See README.md.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod cancelatom;
pub mod colormath;
pub mod colortransform;
pub mod commandlineparser;
pub mod configstore;
pub mod debug;
pub mod displayicc;
pub mod error;
pub mod ffmpegutils;
pub mod filefunctions;
pub mod miscutils;
pub mod ocioutils;
pub mod oiioutils;
pub mod qtutils;
pub mod subtitleparams;
pub mod videoparams;
pub mod xmlutils;

/// Test-only helpers shared across unit-test modules.
///
/// Several domain test modules (e.g. `configstore`, `filefunctions`) mutate
/// process-global state — notably the `OAK_CONFIG_DIR` environment variable
/// and shared temp paths — while exercising configuration-location logic.
/// Rust runs tests in parallel, so all such tests must serialize on a single
/// process-wide lock to avoid racing each other across module boundaries.
#[cfg(test)]
#[doc(hidden)]
pub mod test_support {
	use std::sync::Mutex;

	static ENV_LOCK: Mutex<()> = Mutex::new(());

	/// Process-wide lock guarding tests that mutate global config/env state.
	///
	/// Hold this for the duration of any test (or helper) that sets/removes
	/// `OAK_CONFIG_DIR` or touches the shared configuration temp path.
	pub fn env_lock() -> &'static Mutex<()> {
		&ENV_LOCK
	}
}
