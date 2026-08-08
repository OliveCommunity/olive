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

//! # oaknode — the node graph engine (Rust)
//!
//! Reimplements the C++ oaknode module behind its frozen C ABI
//! (`include/node/*.h`). See README.md for the architectural mapping
//! (inheritance → arena + trait objects, etc.).
//!
//! ## FFI discipline
//!
//! Identical to the oakplugin crate: every export goes through
//! [`handle::guard*`], handles are opaque refcounted boxes, shared
//! state behind `Mutex`.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod block;
pub mod bridge;
pub mod colormanager;
pub mod error;
pub mod factory;
pub mod ffi;
pub mod folder;
pub mod footage;
pub mod graph;
pub mod handle;
pub mod id;
pub mod input;
pub mod keyframe;
pub mod node;
pub mod nodes;
pub mod ops;
pub mod project;
pub mod sequence;
pub mod serializer;
pub mod track;
pub mod traverser;
pub mod value;
