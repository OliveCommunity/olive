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

//! oak-worker: headless render worker process (Rust).
//!
//! A thin shell over the facade, mirroring `worker/workermain.cpp`: all
//! runtime logic — render backend selection (dynamic -> OpenGL fallback
//! through the oakrender module C ABI), the startup handshake and the
//! NDJSON control loop — lives in `oakengine::worker` (the Rust port of
//! `engine/src/capi/worker.cpp`, contract in
//! `engine/include/oakengine/worker.h`). This crate keeps only the CLI
//! surface (arg parsing) and the in-process session mirror
//! ([`session`], [`transport`]) that its unit tests exercise against the
//! facade's real shared-memory transport.
//!
//! See README.md for the full status.

mod ipc;
mod session;
mod transport;

use std::process::exit;

use clap::Parser;

// Force-link the oakrender module crate: the facade's worker module
// (oakengine::worker) initializes the render backend through the oakrender
// C ABI, but its bridge imports are `extern "C"` declarations — nothing in
// the worker source names the crate, so without this the oakrender rlib
// would not be added to the link and those imports would stay undefined.
#[allow(unused_imports)]
use oakrender as _;

/// Protocol version announced in the startup handshake
/// (`k_protocol_version` in worker.cpp). Mirrors
/// `oakengine::worker::PROTOCOL_VERSION`.
pub const PROTOCOL_VERSION: i32 = 1;

/// CLI surface (the C++ worker scans argv for `--backend`; clap formalizes
/// that single option).
#[derive(Parser, Debug)]
#[command(
    name = "oak-worker",
    about = "Oak render worker: headless render process for the editor's worker pool",
    disable_version_flag = true
)]
struct Args {
    /// Render backend to initialize: "opengl", "vulkan", "metal", "auto",
    /// or "none" (no renderer; the process exits 1 like the C++ worker).
    #[arg(long, default_value = "opengl")]
    backend: String,
}

fn main() {
    let args = Args::parse();
    // The facade's worker_main is the C++ oakengine_worker_main() — the
    // whole worker flow. Like workermain.cpp, this main only forwards.
    exit(oakengine::worker::worker_main(&args.backend.to_ascii_lowercase()));
}

/// Log a worker-side message to stderr, mirroring worker.cpp `log_error()`
/// (the `worker: ` prefix).
pub fn log_error(message: &str) {
    eprintln!("worker: {message}");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clap_parses_backend_default() {
        use clap::Parser;
        let args = Args::try_parse_from(["oak-worker"]).unwrap();
        assert_eq!(args.backend, "opengl");
    }

    #[test]
    fn clap_parses_backend_flag() {
        use clap::Parser;
        let args = Args::try_parse_from(["oak-worker", "--backend", "none"]).unwrap();
        assert_eq!(args.backend, "none");
    }

    #[test]
    fn clap_rejects_unknown_flags() {
        use clap::Parser;
        assert!(Args::try_parse_from(["oak-worker", "--frobnicate"]).is_err());
    }
}
