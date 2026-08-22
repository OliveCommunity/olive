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

//! `olive::AudioParams` — the audio stream parameter value type.
//!
//! Single-lib unification (see `docs/zh/plans/riir/single-lib.md`): the
//! codec crate used to create audio parameter sets through the oakcore C
//! ABI (`oakcore_audioparams_*`, host-provided symbols) and store them
//! behind refcounted handles. The parameters are plain values now — the
//! FFmpeg probe fills them in directly and the footage description stores
//! them by value.

/// `olive::AudioParams` — the audio stream description recorded at probe
/// time.
///
/// Mirrors `core/include/olive/core/oakcore/audioparams.h`: sample rate,
/// ffmpeg channel-layout mask, sample format, stream index, duration and
/// time base.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AudioParams {
	/// Sample rate in Hz.
	pub sample_rate: i32,
	/// ffmpeg-style channel layout mask (e.g. 0x3 = stereo).
	pub channel_layout: u64,
	/// `olive::core::SampleFormat::Format` value.
	pub format: i32,
	/// Stream index within the source container.
	pub stream_index: i32,
	/// Stream length in time-base units.
	pub duration: i64,
	/// Time base (num/den seconds per tick).
	pub time_base: (i32, i32),
}

impl AudioParams {
	/// Channel count derived from the layout mask, mirroring the C++
	/// `AudioParams::channel_count()`.
	pub fn channel_count(&self) -> i32 {
		self.channel_layout.count_ones() as i32
	}

	/// Whether the parameter set describes a usable stream, mirroring the
	/// C++ `AudioParams::is_valid()` (positive rate and a non-zero layout).
	pub fn is_valid(&self) -> bool {
		self.sample_rate > 0 && self.channel_layout != 0
	}
}
