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

//! Binary-level tests for `oak-worker`: argument handling and the process
//! exit contract. The NDJSON control-loop behavior itself is exercised
//! in-process in `src/session.rs` (a real loop test would require a working
//! GPU backend, so it stays out of the unit suite).

use std::process::Command;

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_oak-worker")
}

#[test]
fn help_exits_zero() {
    let out = Command::new(bin()).arg("--help").output().expect("spawn oak-worker");
    assert_eq!(out.status.code(), Some(0));
    let stdout = String::from_utf8_lossy(&out.stdout);
    assert!(stdout.contains("oak-worker"));
    assert!(stdout.contains("--backend"));
}

#[test]
fn unknown_flag_is_a_clap_usage_error() {
    let out = Command::new(bin()).arg("--frobnicate").output().expect("spawn oak-worker");
    // clap's usage-error exit code.
    assert_eq!(out.status.code(), Some(2));
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
    assert!(stderr.contains("no renderer initialized"), "stderr: {stderr}");
}
