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

//! Build-time link configuration for the `oakffmpeg-link` crate.
//!
//! ffmpeg-sys-next only emits the link directives for FFmpeg's own
//! libraries. When FFmpeg is linked STATICALLY (the project FFmpeg built
//! by tooling/ffmpeg/build-ffmpeg.sh, pointed at via `FFMPEG_DIR`), its
//! archives reference every external codec library they were configured
//! with (x264, x265, dav1d, ..., plus system frameworks on macOS), and
//! the final link fails with undefined symbols unless those transitive
//! dependencies are linked too. FFmpeg's own `.pc` files carry the exact
//! list in `Libs.private`, so this script asks pkg-config for the static
//! link line and forwards it to cargo.
//!
//! No-op when `FFMPEG_DIR` is unset (shared system FFmpeg carries its
//! own transitive dependencies).

use std::path::PathBuf;
use std::process::Command;

fn main() {
	let Ok(dir) = std::env::var("FFMPEG_DIR") else {
		return;
	};
	let pc_dir = PathBuf::from(&dir).join("lib").join("pkgconfig");
	if !pc_dir.exists() {
		return;
	}
	println!("cargo:rerun-if-env-changed=FFMPEG_DIR");

	let pkg_path = std::env::var("PKG_CONFIG_PATH").unwrap_or_default();
	let output = Command::new("pkg-config")
		.arg("--static")
		.arg("--libs")
		.args([
			"libavformat",
			"libavcodec",
			"libavfilter",
			"libavdevice",
			"libavutil",
			"libswscale",
			"libswresample",
		])
		.env(
			"PKG_CONFIG_PATH",
			format!("{}{}{}", pc_dir.display(), if pkg_path.is_empty() { "" } else { ":" }, pkg_path),
		)
		.output()
		.expect("pkg-config is required when FFMPEG_DIR is set");
	if !output.status.success() {
		panic!(
			"pkg-config --static --libs failed for the FFMPEG_DIR install: {}",
			String::from_utf8_lossy(&output.stderr)
		);
	}

	for token in String::from_utf8_lossy(&output.stdout).split_whitespace() {
		if let Some(path) = token.strip_prefix("-L") {
			println!("cargo:rustc-link-search=native={path}");
		} else if let Some(lib) = token.strip_prefix("-l") {
			// System libs (m, z, bz2, iconv, ...) and externals alike; the
			// linker picks .a or .dylib per -L search order.
			println!("cargo:rustc-link-lib={lib}");
		} else if token == "-framework" {
			// Handled on the next token (see below).
		} else if let Some(framework) = token.strip_prefix("-framework=") {
			println!("cargo:rustc-link-lib=framework={framework}");
		}
		// Bare tokens after "-framework" are handled by the stateful pass
		// below; pkg-config prints "-framework Foo" as two tokens.
	}
	// Second pass for the two-token "-framework Foo" form.
	let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
	let tokens: Vec<&str> = stdout.split_whitespace().collect();
	for pair in tokens.windows(2) {
		if pair[0] == "-framework" {
			println!("cargo:rustc-link-lib=framework={}", pair[1]);
		}
	}

	// Some externals (Homebrew lame, snappy) reach the FFmpeg archives via
	// the configure-time --extra-ldflags instead of Requires.private, so
	// their -L never shows up above; add the Homebrew lib dir as a
	// fallback search path.
	#[cfg(target_os = "macos")]
	for prefix in ["/opt/homebrew/lib", "/usr/local/lib"] {
		if std::path::Path::new(prefix).exists() {
			println!("cargo:rustc-link-search=native={prefix}");
		}
	}
}
