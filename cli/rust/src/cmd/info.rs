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

//! `oak-cli info <project.ove>` — print the project name, its sequences and
//! its footage (port of `cmd_info()` in cli/main.cpp).

use crate::cmd::{port_not_wired, require_or, EXIT_ERROR};

/// Run `info`. `project` is the .ove path from the command line.
pub fn run(project: String) -> i32 {
    if let Err(code) = require_or(
        "info",
        &[
            &crate::deferred::INIT,
            &crate::deferred::NODE,
            &crate::deferred::TIMELINE,
        ],
        EXIT_ERROR,
    ) {
        return code;
    }
    // Facade port (unreachable while the families above are deferred):
    //   oakengine_init(OAKENGINE_INIT_HEADLESS)
    //   project_create + project_load(project, ...)
    //   name/filename/is_modified/sequence_count/sequence_at(...) +
    //     fmt::sequence() / fmt::footage_entry() for each
    //   project_free + oakengine_shutdown()
    // The formatters already exist in crate::fmt and are golden-tested.
    let _ = &project;
    port_not_wired("info", EXIT_ERROR)
}
