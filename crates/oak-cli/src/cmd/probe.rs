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

//! `oak-cli probe <mediafile>` — probe a media file and print its decoder,
//! duration and video/audio/subtitle streams (port of `cmd_probe()` in
//! cli/main.cpp).

use crate::cmd::{port_not_wired, require_or, EXIT_ERROR};

/// Run `probe`. `mediafile` is the media path from the command line.
pub fn run(mediafile: String) -> i32 {
	if let Err(code) = require_or(
		"probe",
		&[&crate::deferred::INIT, &crate::deferred::NODE],
		EXIT_ERROR,
	) {
		return code;
	}
	// Facade port (unreachable while the families above are deferred):
	//   oakengine_init(OAKENGINE_INIT_HEADLESS)
	//   footage_probe(mediafile) -> decoder_name/duration/stream infos,
	//     formatted with the fmt::* lines (golden-tested)
	//   footage_free + oakengine_shutdown()
	let _ = &mediafile;
	port_not_wired("probe", EXIT_ERROR)
}
