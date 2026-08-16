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

//! Link configuration for the `oak-worker` binary.
//!
//! The worker is a pure C-ABI consumer of the built `liboakengine` dylib
//! (crates/oakengine, crate-type cdylib): src/engine_ipc.rs declares the
//! `oakengine_*` symbols with `#[link(name = "oakengine", kind = "dylib")]`.
//! This build script points the linker at the target profile directory
//! that holds the dylib (OUT_DIR is
//! `<target>/<profile>/build/oak-worker-<hash>/out`, so the profile dir is
//! the third ancestor — where cargo places `liboakengine.dylib` /
//! `liboakengine.so`) and embeds an rpath so the binary finds the dylib at
//! runtime without environment variables.

use std::path::Path;

fn main() {
	let out_dir = std::env::var("OUT_DIR").expect("OUT_DIR is set by cargo");
	let profile_dir = Path::new(&out_dir)
		.ancestors()
		.nth(3)
		.expect("OUT_DIR is nested at least 3 levels under the profile dir");

	println!("cargo:rustc-link-search=native={}", profile_dir.display());
	println!("cargo:rustc-link-arg=-Wl,-rpath,{}", profile_dir.display());
}
