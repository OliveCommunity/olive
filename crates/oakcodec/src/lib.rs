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

//! # oakcodec — the media codec module (Rust)
//!
//! Reimplements the C++ oakcodec module behind its frozen C ABI
//! (`include/codec/*.h`). See README.md for the architectural mapping
//! (inheritance → traits, shared_ptr → refcounted handles, etc.).
//!
//! ## FFI discipline
//!
//! Identical to the oaknode/oakplugin crates: every export goes through
//! [`handle::guard*`], handles are opaque refcounted boxes, shared
//! state behind `Mutex`.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod bridge;
pub mod conformmanager;
pub mod decoder;
pub mod encoder;
pub mod encodingparams;
pub mod error;
pub mod exportcodec;
pub mod exportformat;
pub mod ffmpeg;
pub mod ffi;
pub mod footagedescription;
pub mod frame;
pub mod framemanager;
pub mod handle;
pub mod oiio;
pub mod oiioframebridge;
pub mod planarfiledevice;
pub mod proxymanager;
pub mod task;
pub mod timecodemetadata;

#[cfg(test)]
mod realmedia_tests;
