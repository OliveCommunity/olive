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

//! `oak-cli render <project.ove> <start_seconds> <end_seconds> <out_dir>` —
//! render the first sequence to PPM frames plus a PCM s16 WAV (port of
//! `cmd_render()` in cli/main.cpp).

use crate::cmd::{port_not_wired, require_or, EXIT_RENDER_UNAVAILABLE, EXIT_USAGE};

/// Run `render` with the validated (or rejected) seconds arguments.
///
/// The seconds are validated exactly like the C++ `strtod` checks before any
/// facade work; the facade work itself (init + project + sequence + renderer,
/// then [`crate::ppm::write_ppm`] / [`crate::wav::write_wav`] per frame) is
/// gated on the deferred families below.
pub fn run(project: String, start_seconds: &str, end_seconds: &str, out_dir: &str) -> i32 {
	let start: f64 = match start_seconds.parse() {
		Ok(v) => v,
		Err(_) => {
			eprintln!("error: invalid start seconds \"{start_seconds}\"");
			return EXIT_USAGE;
		}
	};
	let end: f64 = match end_seconds.parse() {
		Ok(v) => v,
		Err(_) => {
			eprintln!("error: invalid end seconds \"{end_seconds}\"");
			return EXIT_USAGE;
		}
	};
	if end <= start {
		eprintln!("error: invalid end seconds \"{end_seconds}\"");
		return EXIT_USAGE;
	}

	if let Err(code) = require_or(
		"render",
		&[
			&crate::deferred::INIT,
			&crate::deferred::NODE,
			&crate::deferred::TIMELINE,
			&crate::deferred::RENDER,
		],
		EXIT_RENDER_UNAVAILABLE,
	) {
		return code;
	}
	// Facade port (unreachable while the families above are deferred):
	//   oakengine_init(HEADLESS | RENDER), chdir to the project dir,
	//   project_load, sequence 0 frame rate -> start_ts/end_ts,
	//   renderer_create(f32, fr_num, fr_den), then for each timestamp
	//   render_frame -> ppm::write_ppm (progress on stderr), then
	//   render_audio -> wav::write_wav. Both writers are golden-tested.
	let _ = (&project, &start, &end, &out_dir);
	port_not_wired("render", EXIT_RENDER_UNAVAILABLE)
}
