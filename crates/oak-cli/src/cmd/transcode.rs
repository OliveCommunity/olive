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

//! `oak-cli transcode <input_media> <out> [width] [--format ppm|mp4]` —
//! "media in, renders out" round trip (port of `cmd_transcode()` in
//! cli/main.cpp), entirely through the C ABI.
//!
//! The source is probed with `oakengine_footage_probe` (geometry / frame
//! rate / duration through the `oakengine_footage_get_*` getters), then a
//! temporary project is assembled the same way the C++ CLI did:
//! `oakengine_project_create` + `oakengine_project_new` +
//! `oakengine_project_import_footage` + `oakengine_sequence_new` +
//! `oakengine_sequence_set_video_params` + `oakengine_sequence_add_track`
//! + `oakengine_sequence_add_footage_clip_ex` (the `_ex` variant: the
//! engine keeps created sequences in their own scratch project — a
//! documented deviation, so the plain variant's same-project check can
//! never pass).
//!
//!   - `--format ppm` (and the image/still path): renders the frame range
//!     through `oakengine_renderer_render_frame` into P6 PPM frames via
//!     [`crate::ppm`], plus the audio range through
//!     `oakengine_renderer_render_audio` into a PCM s16 WAV via
//!     [`crate::wav`] when the source has audio streams.
//!   - `--format mp4` (default): H.264/AAC through
//!     `oakengine_export_render` with the engine's exporter options
//!     (codec-default bit rates). The exporter family is currently NOT
//!     wrapped by the Rust facade, so this path reports
//!     `oakengine_export_last_error` and exits 1 until the dylib grows it
//!     (see `crate::optional`).
//!
//! The engine's footage probe records the decoder id but drops the
//! codec's stream descriptions (module gap), so when the stream info is
//! unavailable the CLI falls back to `[width]` (or 1920), a 16:9 height,
//! 25 fps and a single-frame range — the still-image contract the C++
//! CLI used for duration-less sources. Failures exit 1 (general error);
//! bad arguments exit 64.

use std::ffi::CString;
use std::path::Path;

use crate::cmd::{EXIT_ERROR, EXIT_OK, EXIT_USAGE};
use crate::ffi::{self, OakExportOptions};
use crate::ppm;
use crate::wav;

/// Source description distilled from the probe (through the C ABI).
struct SourceInfo {
	width: i32,
	height: i32,
	fr_num: i32,
	fr_den: i32,
	duration: f64,
	audio_streams: i32,
}

/// Run `transcode`. `width`/`format` are validated exactly like the C++
/// loop over `argv[4..]`.
pub fn run(input_media: String, out: String, width: Option<String>, format: Option<String>) -> i32 {
	if let Some(w) = &width {
		match w.parse::<i64>() {
			Ok(n) if n > 0 => {}
			_ => {
				eprintln!("error: invalid width \"{w}\"");
				return EXIT_USAGE;
			}
		}
	}
	if let Some(f) = &format {
		if f != "ppm" && f != "mp4" {
			eprintln!("error: unknown --format \"{f}\" (ppm|mp4)");
			return EXIT_USAGE;
		}
	}

	let is_ppm = format.as_deref().unwrap_or("mp4") == "ppm";
	let code = run_transcode(&input_media, &out, width.as_deref(), is_ppm);
	unsafe {
		crate::optional::engine_shutdown();
	}
	code
}

/// The transcode body; the caller owns the engine shutdown.
fn run_transcode(input: &str, out: &str, width: Option<&str>, is_ppm: bool) -> i32 {
	let rc = unsafe {
		crate::optional::engine_init(
			crate::ffi::OAKENGINE_INIT_HEADLESS | crate::ffi::OAKENGINE_INIT_RENDER,
		)
	};
	if rc != crate::ffi::OAKENGINE_OK {
		eprintln!("error: transcode: engine init failed ({rc})");
		return EXIT_ERROR;
	}

	let src = match probe_source(input) {
		Ok(s) => s,
		Err(msg) => {
			eprintln!("error: transcode: {msg}");
			return EXIT_ERROR;
		}
	};

	let out_w = width
		.and_then(|w| w.parse::<i32>().ok())
		.unwrap_or(src.width);
	let out_h = if src.width > 0 && src.height > 0 {
		((out_w as f64 * src.height as f64 / src.width as f64).round() as i32).max(1)
	} else {
		(out_w as f64 * 9.0 / 16.0).round() as i32
	};
	let fr_num = if src.fr_num > 0 { src.fr_num } else { 25 };
	let fr_den = if src.fr_den > 0 { src.fr_den } else { 1 };
	// A still/duration-less source counts as a single frame.
	let frames = (src.duration * fr_num as f64 / fr_den as f64).round() as i64;
	let frames = frames.max(1);

	// The temporary project + sequence + clips both output paths share.
	let assembly = match assemble_project(input, out_w, out_h, fr_num, fr_den, frames, src.audio_streams) {
		Ok(a) => a,
		Err(msg) => {
			eprintln!("error: transcode: {msg}");
			return EXIT_ERROR;
		}
	};

	let code = if is_ppm {
		transcode_ppm(
			&assembly,
			out,
			out_w,
			out_h,
			fr_num,
			fr_den,
			frames,
			src.audio_streams > 0,
		)
	} else {
		transcode_mp4(&assembly, out, out_w, out_h, frames)
	};
	unsafe {
		crate::ffi::oakengine_render_manager_shutdown();
		crate::ffi::oakengine_project_free(assembly.project);
	}
	code
}

/// The assembled temporary project: project + footage + sequence (with
/// one video track/clip and, when the source has audio streams, one
/// audio track/clip).
struct Assembly {
	project: *mut ffi::OakEngineProject,
	sequence: *mut ffi::OakEngineSequence,
}

/// Build the temporary project for the render/export stage.
fn assemble_project(
	input: &str,
	out_w: i32,
	out_h: i32,
	fr_num: i32,
	fr_den: i32,
	frames: i64,
	audio_streams: i32,
) -> Result<Assembly, String> {
	if unsafe { crate::ffi::oakengine_render_manager_init() } != crate::ffi::OAKENGINE_OK {
		return Err("cannot initialize the render manager".to_string());
	}
	let project = unsafe { crate::ffi::oakengine_project_create() };
	if project.is_null() {
		return Err("cannot create project".to_string());
	}
	if unsafe { crate::ffi::oakengine_project_new(project) } != crate::ffi::OAKENGINE_OK {
		unsafe { crate::ffi::oakengine_project_free(project) };
		return Err("cannot initialize project".to_string());
	}

	let input_c = CString::new(input).map_err(|_| "invalid path (NUL byte)".to_string())?;
	let footage = unsafe { crate::ffi::oakengine_project_import_footage(project, input_c.as_ptr()) };
	if footage.is_null() {
		let err = crate::ffi::string_get(|buf, size| unsafe {
			crate::ffi::oakengine_footage_last_error(buf, size)
		});
		unsafe { crate::ffi::oakengine_project_free(project) };
		return Err(err);
	}

	let seq_name = CString::new("transcode").unwrap();
	let sequence = unsafe { crate::ffi::oakengine_sequence_new(project, seq_name.as_ptr()) };
	if sequence.is_null() {
		unsafe {
			crate::ffi::oakengine_footage_free(footage);
			crate::ffi::oakengine_project_free(project);
		}
		return Err(seq_error("cannot create sequence"));
	}
	if unsafe {
		crate::ffi::oakengine_sequence_set_video_params(
			sequence,
			out_w,
			out_h,
			fr_num,
			fr_den,
			1,
			1,
			0,
			crate::ffi::PIXEL_FORMAT_F32,
			0,
		)
	} != crate::ffi::OAKENGINE_OK
	{
		unsafe {
			crate::ffi::oakengine_footage_free(footage);
			crate::ffi::oakengine_project_free(project);
		}
		return Err(seq_error("cannot set sequence video params"));
	}

	let video_track =
		unsafe { crate::ffi::oakengine_sequence_add_track(sequence, crate::ffi::OAKENGINE_TRACK_TYPE_VIDEO) };
	if video_track < 0 {
		unsafe {
			crate::ffi::oakengine_footage_free(footage);
			crate::ffi::oakengine_project_free(project);
		}
		return Err(seq_error("cannot add video track"));
	}
	let clip = unsafe {
		crate::ffi::oakengine_sequence_add_footage_clip_ex(
			sequence,
			footage,
			crate::ffi::OAKENGINE_TRACK_TYPE_VIDEO,
			video_track,
			0,
			frames,
			0,
		)
	};
	if clip.is_null() {
		unsafe {
			crate::ffi::oakengine_footage_free(footage);
			crate::ffi::oakengine_project_free(project);
		}
		return Err(seq_error("cannot place video clip"));
	}

	if audio_streams > 0 {
		let audio_track =
			unsafe { crate::ffi::oakengine_sequence_add_track(sequence, crate::ffi::OAKENGINE_TRACK_TYPE_AUDIO) };
		if audio_track < 0 {
			unsafe {
				crate::ffi::oakengine_footage_free(footage);
				crate::ffi::oakengine_project_free(project);
			}
			return Err(seq_error("cannot add audio track"));
		}
		let clip = unsafe {
			crate::ffi::oakengine_sequence_add_footage_clip_ex(
				sequence,
				footage,
				crate::ffi::OAKENGINE_TRACK_TYPE_AUDIO,
				audio_track,
				0,
				frames,
				0,
			)
		};
		if clip.is_null() {
			unsafe {
				crate::ffi::oakengine_footage_free(footage);
				crate::ffi::oakengine_project_free(project);
			}
			return Err(seq_error("cannot place audio clip"));
		}
	}

	unsafe {
		crate::ffi::oakengine_footage_free(footage);
	}
	Ok(Assembly { project, sequence })
}

/// `oakengine_sequence_last_error` (or the fallback when empty).
fn seq_error(fallback: &str) -> String {
	let err = crate::ffi::string_get(|buf, size| unsafe {
		crate::ffi::oakengine_sequence_last_error(buf, size)
	});
	if err.is_empty() {
		fallback.to_string()
	} else {
		err
	}
}

/// `--format ppm`: render the frame range through the engine renderer
/// into PPM frames (+ the audio range into a WAV when the source has
/// audio).
fn transcode_ppm(
	assembly: &Assembly,
	out: &str,
	out_w: i32,
	out_h: i32,
	fr_num: i32,
	fr_den: i32,
	frames: i64,
	audio: bool,
) -> i32 {
	if let Err(e) = std::fs::create_dir_all(out) {
		eprintln!("error: transcode: cannot create output directory \"{out}\": {e}");
		return EXIT_ERROR;
	}
	let renderer = unsafe {
		crate::ffi::oakengine_renderer_create(
			assembly.sequence,
			out_w,
			out_h,
			crate::ffi::PIXEL_FORMAT_F32,
			fr_num,
			fr_den,
			std::ptr::null(),
		)
	};
	if renderer.is_null() {
		// The engine's create path returns NULL without setting the
		// renderer's last error; report the contract message.
		eprintln!("error: transcode: cannot create renderer");
		return EXIT_ERROR;
	}

	for i in 0..frames {
		let frame = unsafe { crate::ffi::oakengine_renderer_render_frame(renderer, i) };
		if frame.is_null() {
			let err = crate::ffi::string_get(|buf, size| unsafe {
				crate::ffi::oakengine_renderer_last_error(renderer, buf, size)
			});
			let msg = if err.is_empty() {
				format!("frame {i} failed to render")
			} else {
				format!("frame {i} failed to render: {err}")
			};
			eprintln!("error: transcode: {msg}");
			unsafe { crate::ffi::oakengine_renderer_free(renderer) };
			return EXIT_ERROR;
		}
		if let Err(msg) = unsafe { write_frame_ppm(frame, out, i) } {
			eprintln!("error: transcode: {msg}");
			unsafe {
				crate::ffi::oakengine_frame_free(frame);
				crate::ffi::oakengine_renderer_free(renderer);
			}
			return EXIT_ERROR;
		}
		unsafe { crate::ffi::oakengine_frame_free(frame) };
	}
	eprintln!("transcoded {frames} frames to \"{out}\"");

	// Audio range (the assembly only adds audio clips when the source
	// has audio streams).
	let mut code = EXIT_OK;
	if audio {
		code = write_audio_wav(renderer, out, frames);
	}
	unsafe { crate::ffi::oakengine_renderer_free(renderer) };
	code
}

/// `--format mp4`: H.264/AAC through `oakengine_export_render` (the
/// exporter options mirror the C++ `cmd_transcode` defaults: codec-
/// default bit rates, source audio rate or 48 kHz, stereo).
fn transcode_mp4(assembly: &Assembly, out: &str, out_w: i32, out_h: i32, frames: i64) -> i32 {
	let out_c = match CString::new(out) {
		Ok(p) => p,
		Err(_) => {
			eprintln!("error: transcode: invalid output path (NUL byte)");
			return EXIT_ERROR;
		}
	};
	let opts = OakExportOptions {
		video_codec: crate::ffi::OAKENGINE_EXPORT_VIDEO_H264,
		audio_codec: crate::ffi::OAKENGINE_EXPORT_AUDIO_AAC,
		video_bit_rate: 0,
		audio_sample_rate: 48000,
		audio_channel_count: 2,
	};
	match unsafe {
		crate::optional::export_render(
			assembly.sequence,
			out_c.as_ptr(),
			0,
			frames,
			out_w,
			out_h,
			&opts,
		)
	} {
		Some(rc) if rc == crate::ffi::OAKENGINE_OK => EXIT_OK,
		Some(rc) => {
			let err = unsafe { crate::optional::export_last_error() };
			let msg = if err.is_empty() {
				format!("export failed ({rc})")
			} else {
				err
			};
			eprintln!("error: transcode: {msg}");
			EXIT_ERROR
		}
		None => {
			let err = unsafe { crate::optional::export_last_error() };
			eprintln!("error: transcode: {err}");
			EXIT_ERROR
		}
	}
}

/// Write a rendered frame as `frame_%05d.ppm` in `out` (the
/// `oakengine_frame_*` accessors feed the [`crate::ppm`] writer).
///
/// `oakengine_frame_channel_count` is not backed by the current engine
/// (returns 0); the render module's frames are always in the internal
/// RGBA layout (`VideoParams::k_internal_channel_count == 4`), so a
/// zero/negative channel report falls back to 4 channels.
unsafe fn write_frame_ppm(frame: *mut ffi::OakEngineFrame, out: &str, index: i64) -> Result<(), String> {
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
	let path = Path::new(out).join(format!("frame_{index:05}.ppm"));
	ppm::write_ppm(&path, width, height, format, channels, linesize, bytes)
		.map_err(|e| format!("cannot write \"{}\": {e}", path.display()))
}

/// Render the audio range and write it as `audio.wav` in `out`. The
/// assembled clips span `[0, frames)` sequence timestamps, so the range
/// length is `frames` time-base ticks.
fn write_audio_wav(renderer: *mut ffi::OakEngineRenderer, out: &str, frames: i64) -> i32 {
	let audio = unsafe { crate::ffi::oakengine_renderer_render_audio(renderer, 0, frames) };
	if audio.is_null() {
		eprintln!("error: transcode: audio render failed");
		return EXIT_ERROR;
	}
	let rate = unsafe { crate::ffi::oakengine_audio_sample_rate(audio) };
	let channels = unsafe { crate::ffi::oakengine_audio_channel_count(audio) };
	let samples = unsafe { crate::ffi::oakengine_audio_sample_count(audio) };
	let data = unsafe { crate::ffi::oakengine_audio_data(audio, 0) };
	let code = if data.is_null() || rate <= 0 || channels <= 0 || samples <= 0 {
		eprintln!("error: transcode: audio buffer is empty");
		EXIT_ERROR
	} else {
		match (samples as usize).checked_mul(channels as usize) {
			None => {
				eprintln!("error: transcode: audio buffer size overflow");
				EXIT_ERROR
			}
			Some(len) => {
				// SAFETY: the engine's audio buffer is valid for
				// samples * channels floats for this call's duration.
				let floats = unsafe { std::slice::from_raw_parts(data, len) };
				let path = Path::new(out).join("audio.wav");
				if let Err(e) = wav::write_wav(&path, rate, channels, samples, floats) {
					eprintln!("error: transcode: cannot write \"{}\": {e}", path.display());
					EXIT_ERROR
				} else {
					EXIT_OK
				}
			}
		}
	};
	unsafe { crate::ffi::oakengine_audio_free(audio) };
	code
}

/// Probe the source media through the C ABI into a [`SourceInfo`].
///
/// The engine's footage probe records the decoder but (currently) drops
/// the codec's stream descriptions, so a successful probe still reports
/// zero streams / unavailable stream info. The CLI then falls back to
/// the documented defaults: `[width]` or 1920, 16:9 height, 25 fps,
/// duration 0 (a single frame — the still-image contract). A failed
/// probe is a hard error.
fn probe_source(path: &str) -> Result<SourceInfo, String> {
	let path_c = CString::new(path).map_err(|_| "invalid path (NUL byte)".to_string())?;
	let footage = unsafe { crate::ffi::oakengine_footage_probe(path_c.as_ptr()) };
	if footage.is_null() {
		let err = crate::ffi::string_get(|buf, size| unsafe {
			crate::ffi::oakengine_footage_last_error(buf, size)
		});
		return Err(err);
	}

	let mut duration = 0.0f64;
	let _ = unsafe { crate::ffi::oakengine_footage_get_duration(footage, &mut duration) };
	let audio_streams = unsafe { crate::ffi::oakengine_footage_get_audio_stream_count(footage) }.max(0);

	let video_streams =
		unsafe { crate::ffi::oakengine_footage_get_video_stream_count(footage) }.max(0);

	let mut info = ffi::OakFootageVideoInfo {
		stream_index: 0,
		width: 0,
		height: 0,
		frame_rate_num: 0,
		frame_rate_den: 0,
		duration_ts: 0,
		time_base_num: 0,
		time_base_den: 0,
		color_primaries: 0,
		color_trc: 0,
		interlaced: 0,
	};
	let rc = if video_streams > 0 {
		unsafe { crate::ffi::oakengine_footage_get_video_stream_info(footage, 0, &mut info) }
	} else {
		crate::ffi::OAKENGINE_E_NOT_FOUND
	};
	unsafe { crate::ffi::oakengine_footage_free(footage) };

	let src = if rc == crate::ffi::OAKENGINE_OK && info.width > 0 && info.height > 0 {
		SourceInfo {
			width: info.width,
			height: info.height,
			fr_num: info.frame_rate_num,
			fr_den: info.frame_rate_den,
			duration,
			audio_streams,
		}
	} else {
		// Engine gap fallback (see the docs above).
		SourceInfo {
			width: 1920,
			height: 1080,
			fr_num: 25,
			fr_den: 1,
			duration,
			audio_streams,
		}
	};
	Ok(src)
}
