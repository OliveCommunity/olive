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
//! oakcodec decoder registry and the CLI prints what the module records:
//! the decoder id, the footage duration and the probed stream inventory
//! (per-stream duration in rational seconds). A missing file prints
//! `error: probe: file does not exist: <path>` on stderr and exits 1.

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

	// The module probe records the decoder id and the stream inventory
	// (best effort; a failed probe leaves the footage unprobed with empty
	// streams, exactly like the facade's footage create).
	let mut footage = FootageBehavior::new(mediafile);
	let _ = footage.probe();

	println!("{}", fmt::decoder_line(&footage.decoder));

	let duration = footage.duration();
	println!("{}", fmt::duration_line(rational_secs(duration)));

	let total = footage.total_stream_count();
	let mut video_index = 0i64;
	let mut audio_index = 0i64;
	println!("{}", fmt::video_streams_line(footage.video_stream_count() as i64));
	for i in 0..total {
		let Some(stream) = footage.stream_at(i) else { continue };
		if !stream.is_video {
			continue;
		}
		if let Some(params) = stream.video {
			let fr = params.frame_rate;
			println!(
				"{}",
				fmt::video_stream(
					video_index,
					stream.index as i64,
					params.width as i64,
					params.height as i64,
					fr.numerator(),
					fr.denominator(),
					stream.duration.numerator(),
					stream.duration.denominator(),
					rational_secs(stream.duration),
					0,
					0,
					false,
				)
			);
			video_index += 1;
		}
	}

	println!("{}", fmt::audio_streams_line(footage.audio_stream_count() as i64));
	for i in 0..total {
		let Some(stream) = footage.stream_at(i) else { continue };
		if stream.is_video {
			continue;
		}
		if let Some(params) = stream.audio {
			println!(
				"{}",
				fmt::audio_stream(
					audio_index,
					stream.index as i64,
					params.sample_rate as i64,
					params.channel_layout.count_ones() as i64,
					stream.duration.numerator(),
					stream.duration.denominator(),
					rational_secs(stream.duration),
				)
			);
			audio_index += 1;
		}
	}

	println!("{}", fmt::subtitle_streams_line(footage.subtitle_stream_count() as i64));

	EXIT_OK
}

/// A rational as floating-point seconds (0 on a zero denominator).
fn rational_secs(r: oakcore_rs::Rational) -> f64 {
	if r.denominator() != 0 {
		r.numerator() as f64 / r.denominator() as f64
	} else {
		0.0
	}
}
