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
//! Runs entirely through the module crates (M14 R2): an
//! [`oaknode::footage::FootageBehavior`] probes the file through the
//! oakcodec decoder registry and the CLI prints what the module records.
//! The module currently records the decoder id but drops the codec's
//! stream descriptions, so `duration` reports 0 and the stream counts
//! report 0 for media the module has not loaded stream metadata for — the
//! CLI prints exactly what the module answers, unchanged from the facade
//! contract. A missing file prints `error: probe: file does not exist:
//! <path>` on stderr and exits 1.

use oaknode::footage::FootageBehavior;

use crate::cmd::{EXIT_ERROR, EXIT_OK};
use crate::fmt;

/// Run `probe`. `mediafile` is the media path from the command line.
pub fn run(mediafile: String) -> i32 {
	run_probe(&mediafile)
}

/// The probe body.
fn run_probe(mediafile: &str) -> i32 {
	if !std::path::Path::new(mediafile).exists() {
		eprintln!("error: probe: file does not exist: {mediafile}");
		return EXIT_ERROR;
	}

	// The module probe records the decoder id (best effort; a failed probe
	// leaves the node usable with empty streams, exactly like the facade's
	// footage create).
	let mut footage = FootageBehavior::new(mediafile);
	let _ = footage.probe();

	println!("{}", fmt::decoder_line(&footage.decoder));

	let duration = footage.duration();
	let duration_secs = if duration.denominator() != 0 {
		duration.numerator() as f64 / duration.denominator() as f64
	} else {
		0.0
	};
	println!("{}", fmt::duration_line(duration_secs));

	let video = footage.video_stream_count();
	println!("{}", fmt::video_streams_line(video as i64));
	for index in 0..video {
		if let Some(params) = footage.video_params(index) {
			let fr = params.frame_rate;
			let secs = if fr.denominator() != 0 {
				fr.numerator() as f64 / fr.denominator() as f64
			} else {
				0.0
			};
			println!(
				"{}",
				fmt::video_stream(
					index as i64,
					index as i64,
					params.width as i64,
					params.height as i64,
					fr.numerator(),
					fr.denominator(),
					0,
					fr.denominator(),
					secs,
					0,
					0,
					false,
				)
			);
		}
	}

	let audio = footage.audio_stream_count();
	println!("{}", fmt::audio_streams_line(audio as i64));
	for index in 0..audio {
		// The module's audio stream descriptions are not reachable yet
		// (the stream entries are dropped by the probe); streams the module
		// cannot describe are counted only.
		if let Some(params) = footage.audio_params(index) {
			println!(
				"{}",
				fmt::audio_stream(
					index as i64,
					index as i64,
					params.sample_rate as i64,
					params.channel_layout.count_ones() as i64,
					0,
					1,
					0.0,
				)
			);
		}
	}

	let subtitle = footage.subtitle_stream_count();
	println!("{}", fmt::subtitle_streams_line(subtitle as i64));

	EXIT_OK
}
