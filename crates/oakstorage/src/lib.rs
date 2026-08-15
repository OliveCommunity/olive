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

//! # oakstorage — project persistence (Rust)
//!
//! Single module owning project load/save. Backends are pluggable
//! (ove-xml today, oakdb tomorrow) behind URI dispatch. Manual:
//! docs/zh/plans/riir/M10-oakstorage.md.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod backend;
pub mod backends;
pub mod error;
pub mod handle;
pub mod nodeutil;
pub mod registry;
pub mod session;
pub mod uri;
