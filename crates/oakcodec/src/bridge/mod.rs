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

//! C ABI imports from the other oak modules.
//!
//! The codec module links against oakcommon and oakrender at the C ABI.
//! Every signature below mirrors the corresponding public header
//! verbatim and is resolved at link time. The by-value handle structs
//! (`OakVideoParams`, `OakRenderTexture`, …) are `#[repr(C)]` mirrors of
//! the `{ctx, addref, release, abi_version}` layout so the codec crate
//! can hold and hand them across the FFI boundary without translation.

pub mod common;
pub mod render;

// In-memory mocks for the oakcommon/oakrender C ABI so the crate links
// and is testable under `cargo test` (where those dylibs are absent).
// The `test-stubs` feature additionally compiles the oakcore_*/oakrender_*
// host-mocks for consumer test binaries (e.g. oaknode's) that link this
// crate directly — those symbols are not provided by any Rust crate.
#[cfg(any(test, feature = "test-stubs"))]
pub mod test_stubs;
