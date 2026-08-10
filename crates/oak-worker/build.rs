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
//! The facade rlib (src/engine/rust) links the module C ABIs into any
//! consumer that pulls its codec surface — oak-worker's use of
//! `oakengine::worker` transitively pulls the facade's codec module, whose
//! oakcodec references carry a few C++-host imports (`oakcore_audioparams_*`
//! from liboakcore, `fb_*` from ffmpeg_bridge). Those live in the host Oak
//! process and are only reachable on media-decode paths this worker never
//! exercises; the CMake worker has the same property through the
//! liboakengine dylib (whose build.rs allows runtime lookups). Mirror that
//! here so the standalone Rust worker binary links.

fn main() {
	if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
		println!("cargo:rustc-link-arg=-Wl,-undefined,dynamic_lookup");
	}
}
