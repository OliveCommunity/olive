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
//!
//! Runs entirely through the C ABI: `oakengine_init(OAKENGINE_INIT_HEADLESS)`
//! → `oakengine_project_create` + `oakengine_project_load(path)` →
//! `oakengine_project_name`/`filename`/`is_modified`/`sequence_count`/
//! `sequence_at` (+ the `oakengine_sequence_*` getters) /`footage_count`/
//! `footage_filename` → `oakengine_project_free` + `oakengine_shutdown()`.
//! The output is formatted by `crate::fmt` exactly like the C++ binary.
//!
//! Footage filenames stored relative to the `.ove` file are resolved
//! against the project directory for display (the C++ CLI's project-dir
//! convention); the online flag reports whether the resolved file exists.
//! A load failure prints the engine's error and exits 1.

use std::ffi::CString;
use std::path::Path;

use crate::cmd::{EXIT_ERROR, EXIT_OK};
use crate::ffi;
use crate::fmt;

/// Run `info`. `project` is the .ove path from the command line.
pub fn run(project: String) -> i32 {
	let code = run_info(&project);
	unsafe {
		crate::optional::engine_shutdown();
	}
	code
}

/// The info body; the caller owns the engine shutdown.
fn run_info(project: &str) -> i32 {
	let rc = unsafe { crate::optional::engine_init(crate::ffi::OAKENGINE_INIT_HEADLESS) };
	if rc != crate::ffi::OAKENGINE_OK {
		eprintln!("error: info: engine init failed ({rc})");
		return EXIT_ERROR;
	}

	let handle = unsafe { crate::ffi::oakengine_project_create() };
	if handle.is_null() {
		eprintln!("error: info: cannot create project");
		return EXIT_ERROR;
	}

	let path = match CString::new(project) {
		Ok(p) => p,
		Err(_) => {
			eprintln!("error: info: invalid path (NUL byte)");
			unsafe { crate::ffi::oakengine_project_free(handle) };
			return EXIT_ERROR;
		}
	};
	let mut err = [0 as std::ffi::c_char; 4096];
	let rc = unsafe {
		crate::ffi::oakengine_project_load(handle, path.as_ptr(), err.as_mut_ptr(), err.len() as i32)
	};
	if rc != crate::ffi::OAKENGINE_OK {
		// SAFETY: the engine NUL-terminates `err` on failure.
		let detail = unsafe { std::ffi::CStr::from_ptr(err.as_ptr()) }
			.to_string_lossy()
			.into_owned();
		if detail.is_empty() {
			eprintln!("error: info: cannot load project \"{project}\"");
		} else {
			eprintln!("error: info: {detail}");
		}
		unsafe { crate::ffi::oakengine_project_free(handle) };
		return EXIT_ERROR;
	}

	let name = crate::ffi::string_get(|buf, size| unsafe {
		crate::ffi::oakengine_project_name(handle, buf, size)
	});
	let filename = crate::ffi::string_get(|buf, size| unsafe {
		crate::ffi::oakengine_project_filename(handle, buf, size)
	});
	let modified = unsafe { crate::ffi::oakengine_project_is_modified(handle) } != 0;

	// The engine's serializer swaps a fresh project payload in on load,
	// wiping the pre-load filename (documented engine behavior); when the
	// engine reports an empty filename the CLI falls back to the path it
	// loaded — the C++ CLI's own project filename convention.
	let abs = std::fs::canonicalize(project).unwrap_or_else(|_| Path::new(project).to_path_buf());
	let name = if name.is_empty() || name == "(untitled)" {
		abs.file_name()
			.map(|f| f.to_string_lossy().into_owned())
			.and_then(|f| f.split('.').next().map(|s| s.to_string()))
			.unwrap_or_else(|| "(untitled)".to_string())
	} else {
		name
	};
	let filename = if filename.is_empty() {
		abs.to_string_lossy().into_owned()
	} else {
		filename
	};

	println!("{}", fmt::project_line(&name));
	println!("{}", fmt::file_line(&filename));
	println!("{}", fmt::modified_line(modified));

	// Footage paths in .ove files can be relative to the project file;
	// resolve them for the online check (the C++ project-dir convention).
	let project_dir = Path::new(project).parent().map(|p| p.to_path_buf());

	let sequences = unsafe { crate::ffi::oakengine_project_sequence_count(handle) }.max(0);
	println!("{}", fmt::sequences_line(sequences as i64));
	for index in 0..sequences {
		// `sequence_at` returns an owned box with no matching free
		// export (borrowed contract); it stays alive for the project.
		let seq = unsafe { crate::ffi::oakengine_project_sequence_at(handle, index) };
		if seq.is_null() {
			continue;
		}
		print_sequence(seq, index as i64);
	}

	let footage = unsafe { crate::ffi::oakengine_project_footage_count(handle) }.max(0);
	println!("{}", fmt::footage_line(footage as i64));
	for index in 0..footage {
		let stored = crate::ffi::string_get(|buf, size| unsafe {
			crate::ffi::oakengine_project_footage_filename(handle, index, buf, size)
		});
		let resolved = resolve_footage(&stored, project_dir.as_deref());
		let online = resolved.is_file();
		println!(
			"{}",
			fmt::footage_entry(index as i64, &resolved.to_string_lossy(), online)
		);
	}

	unsafe {
		crate::ffi::oakengine_project_free(handle);
	}
	EXIT_OK
}

/// Print one sequence block (`print_sequence` in cli/main.cpp) through
/// the `oakengine_sequence_*` getters.
fn print_sequence(seq: *mut ffi::OakEngineSequence, index: i64) {
	let name = crate::ffi::string_get(|buf, size| unsafe {
		crate::ffi::oakengine_sequence_name(seq, buf, size)
	});

	let mut length = 0.0f64;
	let mut len_num: i32 = 0;
	let mut len_den: i32 = 0;
	unsafe {
		let _ = crate::ffi::oakengine_sequence_get_length(seq, &mut length);
		let _ = crate::ffi::oakengine_sequence_get_length_rational(seq, &mut len_num, &mut len_den);
	}

	let mut fr_num: i32 = 0;
	let mut fr_den: i32 = 0;
	unsafe {
		let _ = crate::ffi::oakengine_sequence_get_frame_rate(seq, &mut fr_num, &mut fr_den);
	}

	let mut video: i32 = 0;
	let mut audio: i32 = 0;
	let mut subtitle: i32 = 0;
	unsafe {
		let _ = crate::ffi::oakengine_sequence_track_count(
			seq,
			&mut video,
			&mut audio,
			&mut subtitle,
		);
	}

	let mut playhead: i64 = 0;
	let mut playhead_seconds = 0.0f64;
	unsafe {
		let _ = crate::ffi::oakengine_sequence_get_playhead(seq, &mut playhead);
		let _ = crate::ffi::oakengine_sequence_get_playhead_seconds(seq, &mut playhead_seconds);
	}

	println!(
		"{}",
		fmt::sequence(
			index,
			&name,
			length,
			len_num as i64,
			len_den as i64,
			fr_num as i64,
			fr_den as i64,
			video as i64,
			audio as i64,
			subtitle as i64,
			playhead,
			playhead_seconds,
		)
	);
}

/// Resolve a stored footage filename against the project directory (the
/// C++ project-dir convention); absolute paths pass through.
fn resolve_footage(stored: &str, project_dir: Option<&Path>) -> std::path::PathBuf {
	let p = Path::new(stored);
	if p.is_absolute() || project_dir.is_none() {
		p.to_path_buf()
	} else {
		project_dir.unwrap().join(p)
	}
}
