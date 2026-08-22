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
//! cli/main.cpp), entirely through the module crates (M14 R2).
//!
//! The source is probed with an [`oak_node::footage::FootageBehavior`]
//! (geometry / frame rate / duration), then a temporary sequence is
//! assembled the same way the facade did: a scratch project holds the
//! sequence (the facade's documented `oakengine_sequence_new` deviation),
//! [`crate::engine::set_sequence_video_params`] sets the output geometry
//! and frame rate, tracks are added with the module's
//! `TimelineAddTrackCommand`, and clips are placed with the
//! `TrackPlaceBlockCommand` (scratch footage connected to each clip —
//! the facade's `oakengine_sequence_add_footage_clip_ex` semantics).
//!
//!   - `--format ppm` (and the image/still path): renders the frame range
//!     through [`crate::engine::render_frame`] into P6 PPM frames via
//!     [`crate::ppm`], plus the audio range through
//!     [`crate::engine::render_audio`] into a PCM s16 WAV via
//!     [`crate::wav`] when the source has audio streams.
//!   - `--format mp4` (default): H.264/AAC through
//!     [`crate::engine::export_sequence`] (the module export task, the
//!     facade's `oakengine_export_render` equivalent).
//!
//! The module probe records the decoder id but drops the codec's stream
//! descriptions (module gap), so when the stream info is unavailable the
//! CLI falls back to `[width]` (or 1920), a 16:9 height, 25 fps and a
//! single-frame range — the still-image contract the C++ CLI used for
//! duration-less sources. Failures exit 1 (general error); bad arguments
//! exit 64.

use oak_node::footage::FootageBehavior;
use oak_node::track::TrackType;

use crate::cmd::{EXIT_ERROR, EXIT_OK, EXIT_USAGE};
use crate::engine;
use crate::ppm;
use crate::wav;

/// Source description distilled from the probe (through the module).
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
	run_transcode(&input_media, &out, width.as_deref(), is_ppm)
}

/// The transcode body.
fn run_transcode(input: &str, out: &str, width: Option<&str>, is_ppm: bool) -> i32 {
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

	// The temporary sequence both output paths share. The facade kept
	// created sequences in their own scratch project (documented
	// deviation); the CLI does the same, so only the scratch project holds
	// the sequence and its clips.
	let assembly = match assemble_sequence(input, out_w, out_h, fr_num, fr_den, frames, src.audio_streams) {
		Ok(a) => a,
		Err(msg) => {
			eprintln!("error: transcode: {msg}");
			return EXIT_ERROR;
		}
	};

	let code = if is_ppm {
		transcode_ppm(&assembly, out, out_w, out_h, fr_num, fr_den, frames, src.audio_streams > 0)
	} else {
		transcode_mp4(&assembly, out, fr_num, fr_den, frames)
	};
	engine::render_manager_shutdown();
	code
}

/// The assembled temporary sequence: the scratch project + the sequence
/// node (with one video track/clip and, when the source has audio
/// streams, one audio track/clip).
struct Assembly {
	project: engine::ProjectRef,
	sequence: oak_node::id::NodeId,
}

/// Build the temporary sequence for the render/export stage.
fn assemble_sequence(
	input: &str,
	out_w: i32,
	out_h: i32,
	fr_num: i32,
	fr_den: i32,
	frames: i64,
	audio_streams: i32,
) -> Result<Assembly, String> {
	if let Err(e) = engine::render_manager_init() {
		return Err(format!("cannot initialize the render manager: {e}"));
	}
	let project = oak_node::project::Project::new();
	let sequence = engine::create_sequence(&project, "transcode");
	engine::set_sequence_video_params(&project, sequence, out_w, out_h, fr_num, fr_den);

	let video_track = engine::add_track(&project, sequence, TrackType::Video)
		.map_err(|e| format!("cannot add video track: {e}"))?;
	engine::place_footage_clip(
		&project,
		sequence,
		input,
		TrackType::Video,
		video_track,
		0,
		frames,
		0,
		fr_num,
		fr_den,
	)
	.map_err(|e| format!("cannot place video clip: {e}"))?;

	if audio_streams > 0 {
		let audio_track = engine::add_track(&project, sequence, TrackType::Audio)
			.map_err(|e| format!("cannot add audio track: {e}"))?;
		engine::place_footage_clip(
			&project,
			sequence,
			input,
			TrackType::Audio,
			audio_track,
			0,
			frames,
			0,
			fr_num,
			fr_den,
		)
		.map_err(|e| format!("cannot place audio clip: {e}"))?;
	}

	Ok(Assembly { project, sequence })
}

/// `--format ppm`: render the frame range through the module ticket arena
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

	for i in 0..frames {
		let time = oak_core::Rational::new(i * i64::from(fr_den), i64::from(fr_num));
		let montage = engine::video_montage(&assembly.project, assembly.sequence, time);
		let frame = match engine::render_frame(assembly.sequence, time, montage, out_w, out_h) {
			Ok(f) => f,
			Err(e) => {
				let msg = if e.is_empty() {
					format!("frame {i} failed to render")
				} else {
					format!("frame {i} failed to render: {e}")
				};
				eprintln!("error: transcode: {msg}");
				return EXIT_ERROR;
			}
		};
		if let Err(msg) = write_frame_ppm(&frame, out, i) {
			eprintln!("error: transcode: {msg}");
			return EXIT_ERROR;
		}
	}
	eprintln!("transcoded {frames} frames to \"{out}\"");

	// Audio range (the assembly only adds audio clips when the source has
	// audio streams).
	if audio {
		return write_audio_wav(assembly, out, frames, fr_num, fr_den);
	}
	EXIT_OK
}

/// `--format mp4`: H.264/AAC through the module export task (codec-default
/// bit rates, 48 kHz stereo — the facade's `oakengine_export_render`
/// defaults).
fn transcode_mp4(assembly: &Assembly, out: &str, fr_num: i32, fr_den: i32, frames: i64) -> i32 {
	match engine::export_sequence(&assembly.project, assembly.sequence, out, fr_num, fr_den, frames) {
		Ok(()) => EXIT_OK,
		Err(msg) => {
			let err = if msg.is_empty() {
				"export failed".to_string()
			} else {
				msg
			};
			eprintln!("error: transcode: {err}");
			EXIT_ERROR
		}
	}
}

/// Write a rendered frame as `frame_%05d.ppm` in `out` (the rendered
/// frames are always in the internal RGBA layout, 4 channels).
fn write_frame_ppm(frame: &engine::RenderedFrame, out: &str, index: i64) -> Result<(), String> {
	if frame.data.is_empty() || frame.width <= 0 || frame.height <= 0 || frame.linesize <= 0 {
		return Err(format!(
			"frame {index} has no pixel data ({}x{}, linesize {})",
			frame.width, frame.height, frame.linesize
		));
	}
	let path = std::path::Path::new(out).join(format!("frame_{index:05}.ppm"));
	ppm::write_ppm(
		&path,
		frame.width,
		frame.height,
		frame.format,
		4,
		frame.linesize,
		&frame.data,
	)
	.map_err(|e| format!("cannot write \"{}\": {e}", path.display()))
}

/// Render the audio range and write it as `audio.wav` in `out`. The
/// assembled clips span `[0, frames)` sequence timestamps, so the range
/// length is `frames` time-base ticks.
fn write_audio_wav(
	assembly: &Assembly,
	out: &str,
	frames: i64,
	fr_num: i32,
	fr_den: i32,
) -> i32 {
	let range = oak_core::TimeRange::new(
		oak_core::Rational::new(0, 1),
		oak_core::Rational::new(frames * i64::from(fr_den), i64::from(fr_num)),
	);
	let montage = engine::audio_montage(&assembly.project, assembly.sequence, range);
	let audio = match engine::render_audio(assembly.sequence, range, montage) {
		Ok(a) => a,
		Err(e) => {
			eprintln!("error: transcode: audio render failed: {e}");
			return EXIT_ERROR;
		}
	};
	if audio.data.is_empty() || audio.sample_rate <= 0 || audio.channel_count <= 0 {
		eprintln!("error: transcode: audio buffer is empty");
		return EXIT_ERROR;
	}
	let samples = (audio.data.len() / audio.channel_count as usize) as i64;
	let path = std::path::Path::new(out).join("audio.wav");
	if let Err(e) = wav::write_wav(&path, audio.sample_rate, audio.channel_count, samples, &audio.data) {
		eprintln!("error: transcode: cannot write \"{}\": {e}", path.display());
		return EXIT_ERROR;
	}
	EXIT_OK
}

/// Probe the source media into a [`SourceInfo`].
///
/// The module probe records the decoder but (currently) drops the codec's
/// stream descriptions, so a successful probe still reports zero streams /
/// unavailable stream info. The CLI then falls back to the documented
/// defaults: `[width]` or 1920, 16:9 height, 25 fps, duration 0 (a single
/// frame — the still-image contract). A missing file is a hard error.
fn probe_source(path: &str) -> Result<SourceInfo, String> {
	if !std::path::Path::new(path).exists() {
		return Err(format!("file does not exist: {path}"));
	}
	let mut footage = FootageBehavior::new(path);
	if let Err(e) = footage.probe() {
		// The module probe failure keeps the node usable (like the facade's
		// footage create); the fallback below applies.
		let _ = e;
	}

	let duration = footage.duration();
	let duration = if duration.denominator() != 0 {
		duration.numerator() as f64 / duration.denominator() as f64
	} else {
		0.0
	};
	let audio_streams = footage.audio_stream_count() as i32;
	let video_streams = footage.video_stream_count() as i32;

	let info = if video_streams > 0 {
		footage.video_params(0)
	} else {
		None
	};

	let src = if let Some(v) = info {
		if v.width > 0 && v.height > 0 {
			SourceInfo {
				width: v.width,
				height: v.height,
				fr_num: v.frame_rate.numerator() as i32,
				fr_den: v.frame_rate.denominator() as i32,
				duration,
				audio_streams,
			}
		} else {
			// Module gap fallback (see the docs above).
			SourceInfo {
				width: 1920,
				height: 1080,
				fr_num: 25,
				fr_den: 1,
				duration,
				audio_streams,
			}
		}
	} else {
		// Module gap fallback (see the docs above).
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
