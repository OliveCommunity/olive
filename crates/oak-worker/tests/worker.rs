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

//! Binary-level tests for `oak-worker`: the process exit contract and the
//! headless CPU mode's startup behavior. The NDJSON control-loop behavior
//! itself is exercised in-process in `src/worker.rs`; end-to-end runs
//! against the main-process dispatcher live in
//! `tests/procpool_integration.rs`.

use std::process::{Command, Stdio};

fn bin() -> &'static str {
	env!("CARGO_BIN_EXE_oak-worker")
}

#[test]
fn backend_none_exits_one_like_the_cpp_main() {
	// Mirrors oakengine_worker_main(): without a renderer the worker cannot
	// do anything and exits 1.
	let out = Command::new(bin())
		.args(["--backend", "none"])
		.output()
		.expect("spawn oak-worker");
	assert_eq!(out.status.code(), Some(1));
	let stderr = String::from_utf8_lossy(&out.stderr);
	assert!(
		stderr.contains("no renderer initialized"),
		"stderr: {stderr}"
	);
}

#[test]
fn backend_cpu_is_headless_but_fully_operational() {
	// M15 S1: the "cpu" backend skips the renderer like "none" but the
	// session stays up — it writes the startup handshake and exits 0 on
	// EOF. (Also exercises the plugin-runtime install in the binary.)
	let mut child = Command::new(bin())
		.args(["--backend", "cpu"])
		.stdin(Stdio::piped())
		.stdout(Stdio::piped())
		.stderr(Stdio::piped())
		.spawn()
		.expect("spawn oak-worker");
	// Close stdin right away: EOF ends the control loop.
	drop(child.stdin.take());
	let out = child.wait_with_output().expect("wait oak-worker");
	assert_eq!(out.status.code(), Some(0));
	let stdout = String::from_utf8_lossy(&out.stdout);
	let first = stdout.lines().next().unwrap_or("");
	assert!(
		first.contains("\"type\":\"handshake\""),
		"startup handshake on stdout, got: {stdout:?}"
	);
	assert!(
		first.contains("\"protocol_version\":1"),
		"protocol version 1, got: {first:?}"
	);
}
