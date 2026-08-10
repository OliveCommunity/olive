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

//! Build-time link configuration for the `liboakengine` cdylib.
//!
//! The dylib now carries the module C ABIs itself (oakundo_*, oakcommon_*,
//! ... — see Cargo.toml), so the only remaining undefined imports are the
//! C++ host-provided symbols the modules call directly: `oakcore_*`
//! (liboakcore's `oakcore_audioparams_*` / `oakcore_rational_*`, called by
//! oakcodec) and `fb_find_best_pix_fmt_of_list` (ffmpeg_bridge, called by
//! oakcommon's pixel-format helper). Those live in the host Oak process,
//! which loads this dylib, so macOS `ld` must accept them as runtime
//! lookups instead of link-time errors. Only the cdylib gets this flag —
//! the rlib/staticlib (and the worker/cli consumers) are unaffected.

fn main() {
	if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
		println!("cargo:rustc-cdylib-link-arg=-Wl,-undefined,dynamic_lookup");
	}
}
