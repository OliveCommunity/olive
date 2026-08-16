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

//! `oak-cli probe <mediafile>` — probe a media file and print its decoder,
//! duration and video/audio/subtitle streams (port of `cmd_probe()` in
//! cli/main.cpp).
//!
//! Runs entirely through the C ABI: `oakengine_init(OAKENGINE_INIT_HEADLESS)`
//! → `oakengine_footage_probe(path)` → the `oakengine_footage_get_*`
//! getters → `oakengine_footage_free` + `oakengine_shutdown()`. The output
//! is formatted by `crate::fmt` exactly like the C++ `printf` calls.
//!
//! The engine's probe records what the oaknode footage module probes: the
//! decoder id, the stream counts and (per stream) the module-visible
//! params. The module currently drops the codec's probe description, so
//! `get_duration` reports 0 and the stream counts report 0 for media the
//! module has not loaded stream metadata for — the CLI prints exactly
//! what the engine answers. A missing file / failed probe prints
//! `oakengine_footage_last_error` on stderr and exits 1.

use std::ffi::{CString, c_int};

use crate::cmd::{EXIT_ERROR, EXIT_OK};
use crate::ffi::{self, OakFootageAudioInfo, OakFootageVideoInfo};
use crate::fmt;

/// Run `probe`. `mediafile` is the media path from the command line.
pub fn run(mediafile: String) -> i32 {
	let code = run_probe(&mediafile);
	unsafe {
		crate::optional::engine_shutdown();
	}
	code
}

/// The probe body; the caller owns the engine shutdown.
fn run_probe(mediafile: &str) -> i32 {
	let rc = unsafe { crate::optional::engine_init(crate::ffi::OAKENGINE_INIT_HEADLESS) };
	if rc != crate::ffi::OAKENGINE_OK {
		eprintln!("error: probe: engine init failed ({rc})");
		return EXIT_ERROR;
	}

	let path = match CString::new(mediafile) {
		Ok(p) => p,
		Err(_) => {
			eprintln!("error: probe: invalid path (NUL byte)");
			return EXIT_ERROR;
		}
	};
	let footage = unsafe { crate::ffi::oakengine_footage_probe(path.as_ptr()) };
	if footage.is_null() {
		let err = crate::ffi::string_get(|buf, size| unsafe {
			crate::ffi::oakengine_footage_last_error(buf, size)
		});
		eprintln!("error: probe: {err}");
		return EXIT_ERROR;
	}

	println!(
		"{}",
		fmt::decoder_line(&crate::ffi::string_get(|buf, size| unsafe {
			crate::ffi::oakengine_footage_get_decoder_name(footage, buf, size)
		}))
	);

	let mut duration = 0.0f64;
	let rc = unsafe { crate::ffi::oakengine_footage_get_duration(footage, &mut duration) };
	if rc == crate::ffi::OAKENGINE_OK {
		println!("{}", fmt::duration_line(duration));
	} else {
		println!("{}", fmt::duration_line(0.0));
	}

	let video = unsafe { crate::ffi::oakengine_footage_get_video_stream_count(footage) }.max(0);
	println!("{}", fmt::video_streams_line(video as i64));
	for index in 0..video {
		if let Some(info) = unsafe { video_stream_info(footage, index) } {
			let secs = stream_seconds(
				info.duration_ts,
				(info.time_base_num, info.time_base_den),
			);
			println!(
				"{}",
				fmt::video_stream(
					index as i64,
					info.stream_index as i64,
					info.width as i64,
					info.height as i64,
					info.frame_rate_num as i64,
					info.frame_rate_den as i64,
					info.duration_ts,
					info.time_base_den as i64,
					secs,
					info.color_primaries as i64,
					info.color_trc as i64,
					info.interlaced != 0,
				)
			);
		}
	}

	let audio = unsafe { crate::ffi::oakengine_footage_get_audio_stream_count(footage) }.max(0);
	println!("{}", fmt::audio_streams_line(audio as i64));
	for index in 0..audio {
		// The stream-info getter reports what the engine can describe
		// (the module's audio stream descriptions are not reachable
		// yet); streams the engine cannot describe are counted only.
		if let Some(info) = unsafe { audio_stream_info(footage, index) } {
			let secs = stream_seconds(
				info.duration_ts,
				(info.time_base_num, info.time_base_den),
			);
			println!(
				"{}",
				fmt::audio_stream(
					index as i64,
					info.stream_index as i64,
					info.sample_rate as i64,
					info.channel_count as i64,
					info.duration_ts,
					info.time_base_den as i64,
					secs,
				)
			);
		}
	}

	let subtitle =
		unsafe { crate::ffi::oakengine_footage_get_subtitle_stream_count(footage) }.max(0);
	println!("{}", fmt::subtitle_streams_line(subtitle as i64));

	unsafe {
		crate::ffi::oakengine_footage_free(footage);
	}
	EXIT_OK
}

/// `oakengine_footage_get_video_stream_info` into an owned POD
/// (`None` when the engine reports the stream as unavailable).
unsafe fn video_stream_info(footage: *mut ffi::OakEngineFootage, index: c_int) -> Option<OakFootageVideoInfo> {
	let mut info = OakFootageVideoInfo {
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
	let rc = unsafe { crate::ffi::oakengine_footage_get_video_stream_info(footage, index, &mut info) };
	(rc == crate::ffi::OAKENGINE_OK).then_some(info)
}

/// `oakengine_footage_get_audio_stream_info` into an owned POD
/// (`None` when the engine reports the stream as unavailable).
unsafe fn audio_stream_info(footage: *mut ffi::OakEngineFootage, index: c_int) -> Option<OakFootageAudioInfo> {
	let mut info = OakFootageAudioInfo {
		stream_index: 0,
		sample_rate: 0,
		channel_layout: 0,
		channel_count: 0,
		duration_ts: 0,
		time_base_num: 0,
		time_base_den: 0,
	};
	let rc = unsafe { crate::ffi::oakengine_footage_get_audio_stream_info(footage, index, &mut info) };
	(rc == crate::ffi::OAKENGINE_OK).then_some(info)
}

/// Seconds a stream spans: `duration_ts` ticks of the stream time base
/// (`num/den` seconds per tick).
fn stream_seconds(duration_ts: i64, time_base: (c_int, c_int)) -> f64 {
	let (num, den) = time_base;
	if den == 0 {
		return 0.0;
	}
	duration_ts as f64 * num as f64 / den as f64
}
