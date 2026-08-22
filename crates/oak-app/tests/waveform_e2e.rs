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

//! M12 P4 acceptance: the waveform cache extracts real peaks for a media
//! file with an audio track, and hits the cache on re-query.
//!
//! Runs in its own test binary: the FFmpeg teardown state after a video
//! decode + an audio decode in one process crashes at exit, so the
//! waveform test stays isolated from the in-lib media tests.
//!
//! M14 R3: the extraction is a direct `oak_audio::waveform::extract` call
//! (no facade).

use oakapp::oakui::waveform::{MinMax, WaveformCache};

#[test]
fn waveform_extract_and_cache_hit() {
	let media = std::env::temp_dir().join(format!("oakapp_waveform_{}.mp4", std::process::id()));
	oak_codec::testmedia::write_test_clip(&media, 64, 64, 10, 10).expect("generate test media");
	let filename = media.to_string_lossy().into_owned();
	let cache = WaveformCache::new(25.0);
	cache.refresh(7, &filename, 250);
	let wf = cache.get(7).expect("waveform extracted");
	assert!(wf.channel_count >= 1);
	assert!(!wf.peaks.is_empty(), "the sine tone yields peaks");
	let peak = wf
		.peaks
		.iter()
		.fold(0.0f32, |a, p| a.max(p.max.abs().max(p.min.abs())));
	assert!(peak > 0.1, "the sine tone is audible in the peaks: {peak}");
	// Cache hit: a second refresh does not re-extract.
	cache.refresh(7, &filename, 250);
	let again = cache.get(7).unwrap();
	assert_eq!(again.peaks.len(), wf.peaks.len());
	let _ = std::fs::remove_file(&media);
}

/// The MinMax mirror matches the oakaudio waveform point layout (two
/// f32s).
#[test]
fn minmax_layout_is_two_f32s() {
	assert_eq!(std::mem::size_of::<MinMax>(), 8);
	assert_eq!(
		std::mem::size_of::<oak_audio::waveform::SamplePerChannel>(),
		8
	);
}
