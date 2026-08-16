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
//! The dylib carries the module C ABIs itself (oakundo_*, oakcommon_*, ...
//! — see Cargo.toml). The `oakcore_audioparams_*` accessors the audio
//! paths read through used to be host-provided C++ liboakcore symbols,
//! left as runtime lookups via `-undefined,dynamic_lookup`; M12 P5
//! implemented them inside the dylib (src/stubs.rs, module `audio`), so
//! no undefined imports remain except system frameworks/libc++, and the
//! cdylib links on every platform (Windows DLLs reject undefined symbols,
//! which was the blocker).

fn main() {
	let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
	if os == "macos" {
		// The static FFmpeg's transitive system deps (libz etc.) are
		// recorded as `@rpath/libz.1.dylib`; the dylib itself carries the
		// rpath so standalone binaries (and the packaged app) resolve
		// them without extra host rpaths.
		println!("cargo:rustc-cdylib-link-arg=-Wl,-rpath,/usr/lib");
	}
	if os == "macos" || os == "linux" {
		// The dlsym codec bridge (M12 P0) resolves `oakcodec_*` from the
		// process-global scope; the engine's unit-test binary (the former
		// integration tests live in src/test_support/) statically links the
		// module crates, so their symbols must be exported from the test
		// executable. `cargo:rustc-link-arg-tests` is NOT usable: the facade
		// is cdylib-only, so cargo reports "does not have a test target" for
		// that directive — use the generic `rustc-link-arg` (a no-op for the
		// cdylib link itself, and not emitted on Windows where the flag is
		// meaningless and would break the DLL link).
		println!("cargo:rustc-link-arg=-Wl,-export_dynamic");
	}
	if os == "macos" {
		// The bundled OpenColorIO's macOS system monitor references
		// IOKit / ColorSync / CoreGraphics display APIs; link them for
		// the cdylib link and the unit-test binary (which statically
		// pulls the same OCIO rlib).
		for fw in ["IOKit", "ColorSync", "CoreGraphics"] {
			println!("cargo:rustc-link-arg=-framework");
			println!("cargo:rustc-link-arg={fw}");
		}
	}
}
