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

//! C ABI bridges to sibling oak modules.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): every
//! submodule is a compile-time Rust call into the target crate's `ffi`
//! (the `#[no_mangle]` exports stay in the dylib for the external C ABI;
//! internal callers bypass them). Handles cross as the shared
//! `oakcore_rs::handle::CHandle`. The only exception is the copier
//! direction in [`node`], which targets symbols oaknode never implemented
//! and is documented there.

pub mod codec;
pub mod common;
pub mod node;
