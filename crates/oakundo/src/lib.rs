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

//! # oakundo — the undo/redo history module (Rust)
//!
//! Reimplements the C++ oakundo module behind its frozen C ABI
//! (`include/undo/*.h`). See README.md for the architectural mapping —
//! most notably the **vtable-command pattern**: C++ subclassing of
//! `olive::UndoCommand` becomes a callback table + `userdata` pointer.
//!
//! ## FFI discipline
//!
//! Identical to the oaknode/oakcodec crates: every export goes through
//! [`handle::guard*`], handles are opaque refcounted boxes, shared
//! state behind `Mutex`.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod error;
pub mod handle;
pub mod undocommand;
pub mod undostack;
