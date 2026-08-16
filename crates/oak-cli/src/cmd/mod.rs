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

//! Subcommand implementations.
//!
//! Every subcommand is a REAL implementation over the `oakengine_*` C ABI
//! ([`crate::ffi`] + [`crate::optional`]) — a pure consumer of the built
//! `liboakengine` dylib, exactly like the C++ `cli/main.cpp` host:
//!
//!   - `probe`     → `oakengine_footage_probe` + the footage getters
//!   - `info`      → `oakengine_project_create/load` + project/sequence
//!     getters
//!   - `render`    → `oakengine_render_manager_init`,
//!     `oakengine_renderer_create` / `render_frame` / `render_audio` and
//!     the frame/audio-buffer accessors
//!   - `transcode` → project/sequence/clip assembly +
//!     `oakengine_export_render` for mp4, the renderer frame loop for ppm
//!
//! Exit codes: 0 success, 1 general error, 2 rendering unavailable,
//! 64 usage error.

pub mod info;
pub mod probe;
pub mod render;
pub mod transcode;

/// 0 — success.
pub const EXIT_OK: i32 = 0;
/// 1 — general error (bad project/media file, no sequence, I/O failure).
pub const EXIT_ERROR: i32 = 1;
/// 2 — rendering unavailable or failed (e.g. no render backend).
pub const EXIT_RENDER_UNAVAILABLE: i32 = 2;
/// 64 — usage error.
pub const EXIT_USAGE: i32 = 64;
