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

//! `oak-cli transcode <input_media> <out> [width] [--format ppm|mp4]` —
//! "media in, renders out" round trip (port of `cmd_transcode()` in
//! cli/main.cpp).

use crate::cmd::{port_not_wired, require_or, EXIT_RENDER_UNAVAILABLE, EXIT_USAGE};

/// Run `transcode`. `width`/`format` are validated exactly like the C++ loop
/// over `argv[4..]`; the facade work is gated on the deferred families below.
pub fn run(input_media: String, out: String, width: Option<String>, format: Option<String>) -> i32 {
	if let Some(w) = &width {
		match w.parse::<i64>() {
			Ok(n) if n > 0 => {}
			_ => {
				eprintln!("error: invalid width \"{w}\"");
				return EXIT_USAGE;
			}
		}
	}
	if let Some(f) = &format {
		if f != "ppm" && f != "mp4" {
			eprintln!("error: unknown --format \"{f}\" (ppm|mp4)");
			return EXIT_USAGE;
		}
	}

	if let Err(code) = require_or(
		"transcode",
		&[
			&crate::deferred::INIT,
			&crate::deferred::NODE,
			&crate::deferred::TIMELINE,
			&crate::deferred::RENDER,
			&crate::deferred::EXPORT,
		],
		EXIT_RENDER_UNAVAILABLE,
	) {
		return code;
	}
	// Facade port (unreachable while the families above are deferred):
	//   probe the source for geometry/fps/duration, build a temporary
	//   project (new + import_footage + sequence_new + add_track x2 +
	//   add_footage_clip x2), then either the ppm path (render_frame /
	//   render_audio -> ppm::write_ppm / wav::write_wav) or the mp4 path
	//   (oakengine_export_render with H.264/AAC options + progress
	//   callback). The C++ exits 2 when the render/export backend is
	//   unavailable, which is also the code used here.
	let _ = (&input_media, &out, &width, &format);
	port_not_wired("transcode", EXIT_RENDER_UNAVAILABLE)
}
