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
//!
//! Runs entirely through the C ABI:
//!
//!   1. `oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER)`
//!      — plus `oakengine_render_manager_init()`, the Rust facade's
//!      replacement for the C++ engine core's OAKENGINE_INIT_RENDER boot
//!      (the ticket render path needs the oakrender manager up).
//!   2. `oakengine_project_create` + `oakengine_project_load` (the
//!      process chdirs into the project directory first, like the C++
//!      CLI, so relative footage paths resolve during rendering).
//!   3. Sequence 0's frame rate via `oakengine_sequence_get_frame_rate`
//!      and geometry via `oakengine_sequence_get_video_params`.
//!   4. `oakengine_renderer_create(seq, w, h, f32, fr_num, fr_den, null)`
//!      → for every frame timestamp in `[start, end)`
//!      `oakengine_renderer_render_frame` → the `oakengine_frame_*`
//!      accessors → [`crate::ppm::write_ppm`] (P6, 8-bit RGB).
//!   5. The audio range through `oakengine_renderer_render_audio` → the
//!      `oakengine_audio_*` accessors → [`crate::wav::write_wav`].
//!
//! Exit codes: a renderer-create or per-frame render failure exits 2
//! (rendering unavailable, mirroring the C++ code for a missing render
//! backend); project/sequence/argument failures exit 1; bad seconds exit
//! 64 (usage). Frame progress goes to stderr (`frame N: T s`).

use std::path::Path;

use crate::cmd::{EXIT_ERROR, EXIT_OK, EXIT_RENDER_UNAVAILABLE, EXIT_USAGE};
use crate::ffi;
use crate::ppm;
use crate::wav;

/// Run `render` with the validated (or rejected) seconds arguments.
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

	let code = run_render(&project, start, end, out_dir);
	unsafe {
		crate::optional::engine_shutdown();
	}
	code
}

/// The render body; the caller owns the engine shutdown.
fn run_render(project: &str, start: f64, end: f64, out_dir: &str) -> i32 {
	let rc = unsafe {
		crate::optional::engine_init(
			crate::ffi::OAKENGINE_INIT_HEADLESS | crate::ffi::OAKENGINE_INIT_RENDER,
		)
	};
	if rc != crate::ffi::OAKENGINE_OK {
		eprintln!("error: render: engine init failed ({rc})");
		return EXIT_ERROR;
	}

	// The Rust facade's render boot: the ticket render path requires the
	// oakrender manager (the C++ OAKENGINE_INIT_RENDER equivalent).
	if unsafe { crate::ffi::oakengine_render_manager_init() } != crate::ffi::OAKENGINE_OK {
		eprintln!("error: render: cannot initialize the render manager");
		return EXIT_RENDER_UNAVAILABLE;
	}

	// Absolute project path first — the C++ CLI chdirs into the project
	// directory so relative footage paths resolve during rendering.
	let abs = match std::fs::canonicalize(project) {
		Ok(p) => p,
		Err(e) => {
			eprintln!("error: render: cannot open project \"{project}\": {e}");
			unsafe { crate::ffi::oakengine_render_manager_shutdown() };
			return EXIT_ERROR;
		}
	};
	if let Some(dir) = abs.parent() {
		let _ = std::env::set_current_dir(dir);
	}

	let handle = unsafe { crate::ffi::oakengine_project_create() };
	if handle.is_null() {
		eprintln!("error: render: cannot create project");
		unsafe { crate::ffi::oakengine_render_manager_shutdown() };
		return EXIT_ERROR;
	}
	let path = match std::ffi::CString::new(abs.as_os_str().as_encoded_bytes()) {
		Ok(p) => p,
		Err(_) => {
			eprintln!("error: render: invalid path (NUL byte)");
			unsafe {
				crate::ffi::oakengine_project_free(handle);
				crate::ffi::oakengine_render_manager_shutdown();
			}
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
			eprintln!("error: render: cannot load project \"{project}\"");
		} else {
			eprintln!("error: render: {detail}");
		}
		unsafe {
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_ERROR;
	}

	if unsafe { crate::ffi::oakengine_project_sequence_count(handle) } < 1 {
		eprintln!("error: render: project has no sequences");
		unsafe {
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_ERROR;
	}
	// Borrowed sequence box (no free export); lives for the project.
	let seq = unsafe { crate::ffi::oakengine_project_sequence_at(handle, 0) };
	if seq.is_null() {
		eprintln!("error: render: sequence 0 unavailable");
		unsafe {
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_ERROR;
	}

	let mut fr_num: i32 = 0;
	let mut fr_den: i32 = 0;
	unsafe {
		crate::ffi::oakengine_sequence_get_frame_rate(seq, &mut fr_num, &mut fr_den);
	}
	if fr_num <= 0 || fr_den <= 0 {
		eprintln!("error: render: invalid sequence frame rate {fr_num}/{fr_den}");
		unsafe {
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_ERROR;
	}

	let mut width: i32 = 0;
	let mut height: i32 = 0;
	unsafe {
		crate::ffi::oakengine_sequence_get_video_params(
			seq,
			&mut width,
			&mut height,
			&mut 0,
			&mut 0,
		);
	}
	if width <= 0 || height <= 0 {
		eprintln!("error: render: sequence has no video geometry");
		unsafe {
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_ERROR;
	}

	if let Err(e) = std::fs::create_dir_all(out_dir) {
		eprintln!("error: render: cannot create output directory \"{out_dir}\": {e}");
		unsafe {
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_ERROR;
	}

	let renderer = unsafe {
		crate::ffi::oakengine_renderer_create(
			seq,
			width,
			height,
			crate::ffi::PIXEL_FORMAT_F32,
			fr_num,
			fr_den,
			std::ptr::null(),
		)
	};
	if renderer.is_null() {
		// The engine's create path returns NULL without setting the
		// renderer's last error (invalid geometry/format or no module
		// backing); report the contract message.
		eprintln!("error: render: cannot create renderer");
		unsafe {
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_RENDER_UNAVAILABLE;
	}

	// Frame loop: timestamps in the sequence time base (1/fr_num s).
	let start_frames = (start * fr_num as f64 / fr_den as f64).round() as i64;
	let mut index = start_frames;
	let mut written: u64 = 0;
	loop {
		let time = index as f64 * fr_den as f64 / fr_num as f64;
		if time >= end {
			break;
		}
		let frame = unsafe { crate::ffi::oakengine_renderer_render_frame(renderer, index) };
		if frame.is_null() {
			let err = crate::ffi::string_get(|buf, size| unsafe {
				crate::ffi::oakengine_renderer_last_error(renderer, buf, size)
			});
			let msg = if err.is_empty() {
				format!("frame at {time:.6} s failed to render")
			} else {
				format!("frame at {time:.6} s failed: {err}")
			};
			eprintln!("error: render: {msg}");
			unsafe {
				crate::ffi::oakengine_renderer_free(renderer);
				crate::ffi::oakengine_project_free(handle);
				crate::ffi::oakengine_render_manager_shutdown();
			}
			return EXIT_RENDER_UNAVAILABLE;
		}
		if let Err(msg) = unsafe { write_frame_ppm(frame, out_dir, written) } {
			eprintln!("error: render: {msg}");
			unsafe {
				crate::ffi::oakengine_frame_free(frame);
				crate::ffi::oakengine_renderer_free(renderer);
				crate::ffi::oakengine_project_free(handle);
				crate::ffi::oakengine_render_manager_shutdown();
			}
			return EXIT_ERROR;
		}
		unsafe {
			crate::ffi::oakengine_frame_free(frame);
		}
		eprintln!("frame {written}: {time:.6} s");
		written += 1;
		index += 1;
	}

	if written == 0 {
		eprintln!("error: render: empty frame range");
		unsafe {
			crate::ffi::oakengine_renderer_free(renderer);
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_ERROR;
	}

	// Audio range in the sequence time base.
	let start_ts = (start * fr_num as f64 / fr_den as f64).round() as i64;
	let length_ts = ((end - start) * fr_num as f64 / fr_den as f64).round() as i64;
	let audio = unsafe { crate::ffi::oakengine_renderer_render_audio(renderer, start_ts, length_ts) };
	if audio.is_null() {
		let err = crate::ffi::string_get(|buf, size| unsafe {
			crate::ffi::oakengine_renderer_last_error(renderer, buf, size)
		});
		let msg = if err.is_empty() {
			"audio render failed".to_string()
		} else {
			format!("audio render failed: {err}")
		};
		eprintln!("error: render: {msg}");
		unsafe {
			crate::ffi::oakengine_renderer_free(renderer);
			crate::ffi::oakengine_project_free(handle);
			crate::ffi::oakengine_render_manager_shutdown();
		}
		return EXIT_RENDER_UNAVAILABLE;
	}
	let code = unsafe { write_audio_wav(audio, out_dir) };
	unsafe {
		crate::ffi::oakengine_audio_free(audio);
		crate::ffi::oakengine_renderer_free(renderer);
		crate::ffi::oakengine_project_free(handle);
		crate::ffi::oakengine_render_manager_shutdown();
	}
	code
}

/// Write a rendered frame as `frame_%05d.ppm` in `out_dir` (the
/// `oakengine_frame_*` accessors feed the [`crate::ppm`] writer).
///
/// `oakengine_frame_channel_count` is not backed by the current engine
/// (returns 0); the render module's frames are always in the internal
/// RGBA layout (`VideoParams::k_internal_channel_count == 4`), so a
/// zero/negative channel report falls back to 4 channels.
unsafe fn write_frame_ppm(frame: *mut ffi::OakEngineFrame, out_dir: &str, index: u64) -> Result<(), String> {
	let width = unsafe { crate::ffi::oakengine_frame_width(frame) };
	let height = unsafe { crate::ffi::oakengine_frame_height(frame) };
	let format = unsafe { crate::ffi::oakengine_frame_format(frame) };
	let channels = unsafe { crate::ffi::oakengine_frame_channel_count(frame) };
	let channels = if channels > 0 { channels } else { 4 };
	let linesize = unsafe { crate::ffi::oakengine_frame_linesize_bytes(frame) };
	let data = unsafe { crate::ffi::oakengine_frame_data(frame) };
	if data.is_null() || width <= 0 || height <= 0 || linesize <= 0 {
		return Err(format!(
			"frame {index} has no pixel data ({}x{}, linesize {linesize})",
			width, height
		));
	}
	let len = (linesize as usize)
		.checked_mul(height as usize)
		.ok_or_else(|| "frame buffer size overflow".to_string())?;
	// SAFETY: the engine's frame buffer is valid for linesize * height
	// bytes for the duration of this call.
	let bytes = unsafe { std::slice::from_raw_parts(data as *const u8, len) };
	let path = Path::new(out_dir).join(format!("frame_{index:05}.ppm"));
	ppm::write_ppm(&path, width, height, format, channels, linesize, bytes)
		.map_err(|e| format!("cannot write \"{}\": {e}", path.display()))
}

/// Write the rendered audio buffer as `audio.wav` in `out_dir` (the
/// `oakengine_audio_*` accessors feed the [`crate::wav`] writer). The
/// buffer is interleaved f32; the engine returns the whole buffer base
/// for every channel, so channel 0 covers all frames.
unsafe fn write_audio_wav(audio: *mut ffi::OakEngineAudioBuffer, out_dir: &str) -> i32 {
	let rate = unsafe { crate::ffi::oakengine_audio_sample_rate(audio) };
	let channels = unsafe { crate::ffi::oakengine_audio_channel_count(audio) };
	let samples = unsafe { crate::ffi::oakengine_audio_sample_count(audio) };
	let data = unsafe { crate::ffi::oakengine_audio_data(audio, 0) };
	if data.is_null() || rate <= 0 || channels <= 0 || samples <= 0 {
		eprintln!("error: render: audio buffer is empty");
		return EXIT_RENDER_UNAVAILABLE;
	}
	let len = match (samples as usize).checked_mul(channels as usize) {
		Some(l) => l,
		None => {
			eprintln!("error: render: audio buffer size overflow");
			return EXIT_RENDER_UNAVAILABLE;
		}
	};
	// SAFETY: the engine's audio buffer is valid for samples * channels
	// floats for the duration of this call.
	let floats = unsafe { std::slice::from_raw_parts(data, len) };
	let path = Path::new(out_dir).join("audio.wav");
	if let Err(e) = wav::write_wav(&path, rate, channels, samples, floats) {
		eprintln!("error: render: cannot write \"{}\": {e}", path.display());
		return EXIT_ERROR;
	}
	EXIT_OK
}
