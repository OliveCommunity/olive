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
//! The app does NOT depend on the `oakengine` crate as an rlib: the real
//! engine binding ([`RealEngine`](crate::oakui::real)) calls only the
//! frozen `oakengine_*` C ABI, which lives in the built
//! `liboakengine.dylib` (crates/oakengine, crate-type `cdylib`). This
//! script points the linker at that dylib and arranges for `cargo run` to
//! find it at runtime without any environment variables.
//!
//! The dylib is built by cargo before this script runs (the `oakengine`
//! entry in `[build-dependencies]` below guarantees the build order). Cargo
//! puts it at:
//!
//! * `target/<profile>/deps/liboakengine.dylib` — when built as a
//!   dependency of the app (the normal case),
//! * `target/<profile>/liboakengine.dylib` — when built as a workspace
//!   member (`cargo build -p oakengine`).
//!
//! macOS: the dylib carries a Mach-O install name pointing back into
//! `target/<profile>/deps/`, so dyld finds it by that absolute path at
//! load time; the `-rpath` flag covers `@rpath`-relative configurations.
//!
//! Linux: undefined symbols in a `.so` need no link-time flag; the app's
//! own link only needs the search path plus `-Wl,--export-dynamic` (the
//! ELF equivalent of `-export_dynamic`) so the host symbols resolve from
//! the binary at runtime. An `$ORIGIN`-relative rpath lets a packaged
//! binary find a sibling `liboakengine.so`.
//!
//! Windows: NOT SUPPORTED YET — a DLL cannot carry the undefined
//! `oakcore_*` imports (they need a stub import library or delay-load
//! plumbing that does not exist yet), so the app does not link there.

use std::path::PathBuf;

fn main() {
	let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
	if os != "macos" && os != "linux" {
		return;
	}

	let target_dir = std::env::var("CARGO_TARGET_DIR")
		.map(PathBuf::from)
		.unwrap_or_else(|_| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("target"));
	let profile = std::env::var("PROFILE").unwrap_or_else(|_| "debug".to_string());
	let profile_dir = target_dir.join(&profile);
	let deps_dir = profile_dir.join("deps");
	let dylib = if os == "macos" {
		"liboakengine.dylib"
	} else {
		"liboakengine.so"
	};

	// The un-hashed dependency artifact is the normal case; the
	// workspace-member copy is the fallback. If only the hashed artifact
	// exists (liboakengine-<hash>.so), link it by full path.
	if deps_dir.join(dylib).exists() {
		link_search(&deps_dir, os == "macos");
	} else if profile_dir.join(dylib).exists() {
		link_search(&profile_dir, os == "macos");
	} else if let Some(hashed) = find_hashed_dylib(&deps_dir, os == "macos") {
		println!("cargo:rustc-link-arg={}", hashed.display());
		rpath_and_export(&deps_dir, os == "macos");
	} else {
		panic!(
			"{dylib} not found under {}: build the workspace from the repo root \
			 (cargo build -p oakengine) so the liboakengine cdylib is produced before the app links",
			profile_dir.display()
		);
	}
}

/// Emits the link-search path plus `-loakengine`, the runtime rpath and
/// the host-symbol export flag (see the module docs).
fn link_search(dir: &std::path::Path, macos: bool) {
	println!("cargo:rustc-link-search=native={}", dir.display());
	println!("cargo:rustc-link-lib=dylib=oakengine");
	rpath_and_export(dir, macos);
	if macos {
		// gpui_macos reaches the IOSurface API through the `core-video`
		// crate, which depends on `io-surface` with `default-features =
		// false` — that disables io-surface's `link` feature, so nothing
		// adds the IOSurface.framework to the final link and the binary
		// fails with undefined `_IOSurface*` symbols. The app's build
		// script is the single place that configures the macOS link, so
		// link the framework here.
		println!("cargo:rustc-link-lib=framework=IOSurface");
	}
}

/// The rpath (absolute deps dir + `$ORIGIN` on Linux) and the flag that
/// exports the binary's own symbols for the dylib's runtime lookups.
fn rpath_and_export(dir: &std::path::Path, macos: bool) {
	if macos {
		println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
		println!("cargo:rustc-link-arg=-Wl,-export_dynamic");
	} else {
		println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
		println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
		println!("cargo:rustc-link-arg=-Wl,--export-dynamic");
	}
}

/// Finds `liboakengine-<hash>.{dylib,so}` in `deps/` (some cargo
/// configurations name dependency cdylibs with a hash suffix).
fn find_hashed_dylib(deps_dir: &std::path::Path, macos: bool) -> Option<PathBuf> {
	let suffix = if macos { ".dylib" } else { ".so" };
	let entries = std::fs::read_dir(deps_dir).ok()?;
	for entry in entries.flatten() {
		let name = entry.file_name();
		let name = name.to_string_lossy();
		if name.starts_with("liboakengine-") && name.ends_with(suffix) {
			return Some(entry.path());
		}
	}
	None
}
