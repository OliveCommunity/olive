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

/// In-crate mock implementations of the bridge surface, compiled for tests
/// (`cfg(test)` or the `test-stubs` feature); the bridge fns route to them
/// via their `#[cfg(any(test, feature = "test-stubs"))]` variants. Plain
/// Rust (no `#[no_mangle]`), so they coexist with the real
/// oaknode/oakundo/oakcommon rlibs linked in the same test binary.
#[cfg(any(test, feature = "test-stubs"))]
pub mod teststubs;
