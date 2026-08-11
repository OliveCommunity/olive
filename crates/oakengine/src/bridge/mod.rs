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

//! C ABI imports from the oak modules, one submodule per module crate.
//!
//! The facade consumes the module C ABIs (`include/<mod>/*.h`) purely as
//! `extern "C"` imports — it never links the module crates at build time.
//! At the final app link the symbols resolve against the module shared
//! libraries; `cargo test` resolves them against the module crates' rlibs
//! (dev-dependencies, see Cargo.toml).
//!
//! **Signatures are declared from the module crates' actual `#[no_mangle]`
//! exports** (their `src/*/ffi*` modules), not from memory of the include
//! headers — module bridges in sibling crates have drifted from the real
//! ABI before. Every handle crosses the boundary as [`crate::handle::CHandle`]
//! (structurally identical to every `Oak<Mod><Type>` value handle).
//!
//! Only functions the module crates actually implement are declared here;
//! engine functions whose backing is still C++-only are facade stubs (see
//! the area modules) and never reach this module.

pub mod audio;
pub mod codec;
pub mod common;
pub mod node;
pub mod plugin;
pub mod render;
pub mod task;
pub mod timeline;
pub mod undo;
