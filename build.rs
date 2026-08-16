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

//! Build-time link configuration for the `oakapp` crate.
//!
//! M14 R3: the app links the oak* module crates as plain rlibs — there is
//! no `liboakengine` dylib to locate anymore. The only remaining link
//! concern is gpui's macOS backend: gpui_macos reaches the IOSurface API
//! through the `core-video` crate, which depends on `io-surface` with
//! `default-features = false` — that disables io-surface's `link` feature,
//! so nothing adds the IOSurface.framework to the final link and the
//! binary fails with undefined `_IOSurface*` symbols. The app's build
//! script is the single place that configures the macOS link, so link the
//! framework here.

fn main() {
	let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
	if os == "macos" {
		println!("cargo:rustc-link-lib=framework=IOSurface");
	}
}
