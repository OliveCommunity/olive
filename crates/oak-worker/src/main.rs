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
//! A thin C-ABI shell over the `liboakengine` facade dylib, mirroring
//! `worker/workermain.cpp`: all runtime logic — render backend selection
//! (dynamic -> OpenGL fallback through the oakrender module), the startup
//! handshake and the NDJSON control loop — lives in the engine
//! (`oakengine_worker_main`, the port of `engine/src/capi/worker.cpp`,
//! contract in `engine/include/oakengine/worker.h`). This crate keeps only
//! the argv forwarding, the `#[link]` declarations ([`engine_ipc`]) and
//! the in-process session mirror ([`session`], [`transport`]) that its
//! unit tests exercise against the engine's real shared-memory transport.
//!
//! See README.md for the full status.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

mod engine_ipc;
mod session;
mod transport;

use std::ffi::{c_char, c_int, CString};
use std::process::exit;

/// Protocol version announced in the startup handshake
/// (`k_protocol_version` in worker.cpp). Mirrors the engine worker
/// module's `PROTOCOL_VERSION`.
pub const PROTOCOL_VERSION: i32 = 1;

/// Log a worker-side message to stderr, mirroring worker.cpp `log_error()`
/// (the `worker: ` prefix).
pub fn log_error(message: &str) {
	eprintln!("worker: {message}");
}

fn main() {
	// Forward argv verbatim: the engine's oakengine_worker_main() scans
	// for `--backend` itself (workermain.cpp does the same).
	let args: Vec<String> = std::env::args_os()
		.map(|a| a.to_string_lossy().into_owned())
		.collect();
	let cstrings: Vec<CString> = args
		.iter()
		.map(|a| CString::new(a.as_str()).unwrap_or_else(|_| CString::new("").unwrap()))
		.collect();
	let mut argv: Vec<*mut c_char> = cstrings
		.iter()
		.map(|c| c.as_ptr() as *mut c_char)
		.collect();
	// SAFETY: `argv` is an array of `argc` NUL-terminated C strings, kept
	// alive for the whole call; the engine only reads them.
	let code = unsafe {
		engine_ipc::oakengine_worker_main(argv.len() as c_int, argv.as_mut_ptr())
	};
	exit(code);
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn protocol_version_is_one() {
		assert_eq!(PROTOCOL_VERSION, 1);
	}
}
