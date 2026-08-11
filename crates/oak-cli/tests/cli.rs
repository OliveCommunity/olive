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

//! End-to-end tests for the built `oak-cli` binary (the C++ ctest suite
//! `oak_cli_info`/`oak_cli_render`/`oak_cli_probe`/`oak_cli_transcode`
//! equivalents, as far as the deferred facade allows).
//!
//! All four subcommands depend on facade families that are still deferred in
//! oakfacade (see `src/deferred.rs`), so the data-producing paths assert the
//! documented "not yet available" behavior with the C++-compatible exit
//! codes; the argument-validation paths assert the exact C++ messages and
//! exit code 64.

use std::process::Command;

fn bin() -> &'static str {
	env!("CARGO_BIN_EXE_oak-cli")
}

fn run(args: &[&str]) -> (i32, String, String) {
	let out = Command::new(bin())
		.args(args)
		.output()
		.expect("spawn oak-cli");
	(
		out.status.code().expect("exit code"),
		String::from_utf8_lossy(&out.stdout).into_owned(),
		String::from_utf8_lossy(&out.stderr).into_owned(),
	)
}

#[test]
fn help_prints_the_cpp_usage_text_and_exits_zero() {
	let (code, stdout, stderr) = run(&["--help"]);
	assert_eq!(code, 0);
	assert!(stderr.is_empty());
	assert!(stdout.starts_with("oak-cli - headless consumer of the liboakengine C ABI\n"));
	assert!(stdout.contains("oak-cli transcode <input_media> <out> [width] [--format ppm|mp4]"));
	assert!(stdout.contains("Exit codes:"));
	assert!(stdout.contains("64  usage error"));
}

#[test]
fn no_arguments_is_a_usage_error() {
	let (code, _stdout, stderr) = run(&[]);
	assert_eq!(code, 64);
	assert!(stderr.contains("Usage:"));
}

#[test]
fn unknown_command_is_a_usage_error() {
	let (code, _stdout, stderr) = run(&["frobnicate"]);
	assert_eq!(code, 64);
	assert!(stderr.contains("error: unknown command \"frobnicate\""));
}

#[test]
fn info_on_a_fixture_reports_not_yet_available() {
	// The fixture mirrors the ctest invocation; the deferred gate fires
	// before any file access.
	let (code, _stdout, stderr) = run(&["info", "tests/project_with_footage.ove"]);
	assert_eq!(code, 1);
	assert!(
		stderr.contains("error: info: not yet available"),
		"stderr: {stderr}"
	);
	// The crate was renamed oakfacade -> oakengine; the deferral reason
	// names the current crate.
	assert!(stderr.contains("oakengine"));
}

#[test]
fn info_with_missing_argument_is_a_usage_error() {
	let (code, _stdout, stderr) = run(&["info"]);
	assert_eq!(code, 64);
	assert!(stderr.contains("Usage:"));
}

#[test]
fn probe_reports_not_yet_available() {
	let (code, _stdout, stderr) = run(&["probe", "tests/demo.mp4"]);
	assert_eq!(code, 1);
	assert!(
		stderr.contains("error: probe: not yet available"),
		"stderr: {stderr}"
	);
}

#[test]
fn render_reports_render_unavailable() {
	let (code, _stdout, stderr) = run(&["render", "p.ove", "0", "1", "out"]);
	assert_eq!(code, 2);
	assert!(
		stderr.contains("error: render: not yet available"),
		"stderr: {stderr}"
	);
}

#[test]
fn render_bad_seconds_is_a_usage_error() {
	let (code, _stdout, stderr) = run(&["render", "p.ove", "abc", "1", "out"]);
	assert_eq!(code, 64);
	assert!(
		stderr.contains("error: invalid start seconds \"abc\""),
		"stderr: {stderr}"
	);
}

#[test]
fn render_end_not_after_start_is_a_usage_error() {
	let (code, _stdout, stderr) = run(&["render", "p.ove", "2", "1", "out"]);
	assert_eq!(code, 64);
	assert!(
		stderr.contains("error: invalid end seconds \"1\""),
		"stderr: {stderr}"
	);
}

#[test]
fn transcode_reports_render_unavailable() {
	let (code, _stdout, stderr) = run(&["transcode", "in.mp4", "out.mp4", "960"]);
	assert_eq!(code, 2);
	assert!(
		stderr.contains("error: transcode: not yet available"),
		"stderr: {stderr}"
	);
}

#[test]
fn transcode_bad_width_is_a_usage_error() {
	let (code, _stdout, stderr) = run(&["transcode", "in.mp4", "out.mp4", "banana"]);
	assert_eq!(code, 64);
	assert!(
		stderr.contains("error: invalid width \"banana\""),
		"stderr: {stderr}"
	);
}

#[test]
fn transcode_nonpositive_width_is_a_usage_error() {
	let (code, _stdout, stderr) = run(&["transcode", "in.mp4", "out.mp4", "0"]);
	assert_eq!(code, 64);
	assert!(
		stderr.contains("error: invalid width \"0\""),
		"stderr: {stderr}"
	);
}

#[test]
fn transcode_unknown_format_is_a_usage_error() {
	let (code, _stdout, stderr) = run(&["transcode", "in.mp4", "out.mp4", "--format", "webm"]);
	assert_eq!(code, 64);
	assert!(
		stderr.contains("error: unknown --format \"webm\" (ppm|mp4)"),
		"stderr: {stderr}"
	);
}

#[test]
fn transcode_ppm_format_is_accepted_then_reports_not_available() {
	let (code, _stdout, stderr) = run(&["transcode", "in.mp4", "outdir", "960", "--format", "ppm"]);
	assert_eq!(code, 2);
	assert!(
		stderr.contains("error: transcode: not yet available"),
		"stderr: {stderr}"
	);
}
