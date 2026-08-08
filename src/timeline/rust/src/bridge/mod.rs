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

//! C ABI imports from other oak modules. Signatures mirror the public
//! headers verbatim; they are resolved at link time against the oaknode,
//! oakundo and oakcommon DLLs.
//!
//! Timeline never reimplements C++ internals: every cross-module access
//! goes through these frozen C ABIs (see README.md "no replication").

pub mod common;
pub mod node;
pub mod undo;

/// In-crate C ABI mocks, compiled only for tests (feature `test-stubs`).
/// Each `#[no_mangle]` function here provides a definition for the `extern
/// "C"` symbol declared in the submodules above, so `cargo test
/// --features test-stubs` links without the real oak DLLs.
#[cfg(feature = "test-stubs")]
pub mod teststubs;
