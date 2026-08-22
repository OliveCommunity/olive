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

//! Test-binary link flags.
//!
//! `-Wl,-export_dynamic` keeps the test binary's symbols in its dynamic
//! symbol table — originally so the M12 P0 decode bridge could resolve
//! the oakcodec C ABI with `dlsym(RTLD_DEFAULT)` (the decode bridge is a
//! direct Rust call now). It is spelled the macOS way and is therefore
//! emitted on macOS only: on ELF platforms lld parses `-export_dynamic`
//! as `-e xport_dynamic` and links a binary with NO entry point, which
//! then dies with SIGSEGV inside ld.so at startup (Rust >= 1.90 links
//! x86_64-unknown-linux-gnu with rust-lld by default). The macOS
//! framework flags below are the load-bearing part: the bundled
//! OpenColorIO's system monitor references IOKit / ColorSync /
//! CoreGraphics display APIs.

fn main() {
	if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
		// Only the test binaries need this; the library itself links no
		// dynamic symbols. (macOS-only — see the module comment.)
		println!("cargo:rustc-link-arg-tests=-Wl,-export_dynamic");
		// The bundled OpenColorIO's macos system monitor references
		// IOKit / ColorSync / CoreGraphics display APIs; the engine
		// dylib links with `-undefined,dynamic_lookup`, so test binaries
		// must resolve them.
		for fw in ["IOKit", "ColorSync", "CoreGraphics"] {
			println!("cargo:rustc-link-arg-tests=-framework");
			println!("cargo:rustc-link-arg-tests={fw}");
		}
	}
}
