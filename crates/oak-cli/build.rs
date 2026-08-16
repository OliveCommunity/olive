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

//! Link configuration for the `oak-cli` binary.
//!
//! oak-cli is a pure C-ABI consumer of the built `liboakengine` cdylib
//! (crates/oakengine): the `#[link(name = "oakengine", kind = "dylib")]`
//! block in `src/ffi.rs` puts `-loakengine` into the binary link, and this
//! script points the linker (and dyld, via the rpath) at the directory
//! that holds the dylib.
//!
//! The dylib is produced by the engine's own build (`cargo build -p
//! oakengine`); as a workspace member it lands in `target/<profile>/`
//! (un-hashed, unlike dependency artifacts). The profile dir is derived
//! from `OUT_DIR` — `target/<profile>/build/oak-cli-<hash>/out` — by
//! walking three ancestors up, so custom `CARGO_TARGET_DIR` layouts work
//! without duplication.
//!
//! `-Wl,-export_dynamic` exports the binary's own symbols: the CLI is the
//! *host* process for the engine dylib (exactly like the C++ cli/main.cpp
//! host), so the `oakcore_audioparams_*` shims in `src/host.rs` must be
//! visible to the dylib's runtime lookups (its `-undefined
//! dynamic_lookup` imports).
//!
//! Build order: `cargo build -p oakengine` must have run before the
//! binary link (`cargo build -p oak-cli`, `cargo test -p oak-cli`).
//! `cargo check` never links, so it stays green without the dylib.

fn main() {
	let out_dir = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap_or_default());
	// out -> oak-cli-<hash> -> build -> <profile> (debug/release)
	let profile_dir = out_dir
		.ancestors()
		.nth(3)
		.expect("OUT_DIR has a profile ancestor");
	println!("cargo:rustc-link-search=native={}", profile_dir.display());
	if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
		println!("cargo:rustc-link-arg=-Wl,-rpath,{}", profile_dir.display());
		println!("cargo:rustc-link-arg=-Wl,-export_dynamic");
	} else {
		println!("cargo:rustc-link-arg=-Wl,-rpath,{}", profile_dir.display());
		println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
		println!("cargo:rustc-link-arg=-Wl,--export-dynamic");
	}
}
