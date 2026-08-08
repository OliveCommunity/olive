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

//! # oaktask — the task execution module (Rust)
//!
//! Reimplements the C++ task module behind its frozen C ABI
//! (`include/task/*.h`). See README.md for the architectural mapping
//! (inheritance → one module per class + trait objects, cancellation via
//! the oakrender cancelatom C ABI, events as mutex-guarded callbacks).
//!
//! ## FFI discipline
//!
//! Identical to the oaknode/oakplugin crates: every export goes through
//! [`handle::guard*`], handles are opaque refcounted boxes, shared state
//! behind `Mutex`.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod bridge;
pub mod codecbridge;
pub mod conform;
pub mod customcache;
pub mod error;
pub mod export;
pub mod ffi;
pub mod handle;
pub mod manager;
pub mod precache;
pub mod project;
pub mod proxy;
pub mod render;
pub mod task;
