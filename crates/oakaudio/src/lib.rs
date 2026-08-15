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

//! # oakaudio — the audio I/O, processing and synchronization engine (Rust)
//!
//! Reimplements the C++ oakaudio module behind its frozen C ABI
//! (`include/audio/*.h`). See README.md for the architectural mapping
//! (singleton manager, stateless sync helpers, local value types).
//!
//! ## FFI discipline
//!
//! Identical to the oaknode crate: every export goes through
//! [`handle::guard*`], handles are opaque refcounted boxes (or, for the
//! singleton `AudioManager`, borrow-only no-ops), shared state behind
//! `Mutex`.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

pub mod config;
pub mod error;
pub mod levelmeter;
pub mod manager;
pub mod params;
pub mod outputdevice;
pub mod previewdevice;
pub mod processor;
pub mod synchronizer;
pub mod waveform;
pub mod waveformsync;

// Keep the oakffmpeg-link rlib referenced so its build script's native
// link flags (the static FFmpeg's transitive dependencies) reach the
// final link — rustc prunes the flags of an unreferenced rlib.
#[used]
static FORCE_FFMPEG_LINK: fn() = oakffmpeg_link::force_link;
