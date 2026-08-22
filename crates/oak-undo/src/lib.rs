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
//! Reimplements the C++ oakundo module. See README.md for the
//! architectural mapping — most notably the **trait-object command
//! pattern**: C++ subclassing of `olive::UndoCommand` becomes a boxed
//! [`undocommand::Command`] (closure-backed or a
//! [`undocommand::MultiUndoCommand`] composite) behind a plain owned
//! [`undocommand::UndoCommand`] value.
//!
//! ## Consumers
//!
//! Every consumer is in-process Rust: the app's edit layer
//! (`oakui::graphops` / `oakui::real`) drives the process-wide stack
//! through [`global`], `oaktimeline`/`oaktask`/`oaknode`/`oakplugin`
//! build commands as [`undocommand::UndoCommand`] values, and
//! `oakstorage` subscribes to the command-success observers in
//! [`global`]. The former frozen C ABI (`include/undo/*.h`), the
//! refcounted [`CHandle`] layer and the callback-table vtable commands
//! were deleted with the engine facade — the crate is pure owned-value
//! Rust with no `unsafe`.

#![warn(missing_docs)]

pub mod error;
pub mod global;
pub mod undocommand;
pub mod undostack;
