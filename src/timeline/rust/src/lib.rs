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

//! # oaktimeline — the timeline edit module (Rust)
//!
//! Reimplements the C++ oaktimeline module (`src/timeline/src`) behind
//! its frozen C ABI (`include/timeline/*.h`). See README.md for the
//! architectural mapping (per-header domain modules, no UndoCommand
//! inheritance → vtable commands).
//!
//! ## FFI discipline
//!
//! Identical to the oaknode crate: every export goes through
//! [`handle::guard*`], handles are opaque refcounted boxes, and all
//! cross-module access goes through the oaknode/oakundo/oakcommon C ABIs
//! (`bridge::*`).

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod bridge;
pub mod common;
pub mod error;
pub mod ffi;
pub mod handle;
pub mod marker;
pub mod undocommon;
pub mod undogeneral;
pub mod undopointer;
pub mod undoripple;
pub mod undosplit;
pub mod undotrack;
pub mod util;
pub mod workarea;
