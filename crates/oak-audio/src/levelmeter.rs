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

//! Loudness analysis (`olive::AudioLevelMeter`). A static helper producing
//! per-channel peak/RMS/VU statistics plus an overall LUFS-integrated
//! summary. Pure function module in Rust; feeds the level-meter UI widget.

/// dB floor for all decibel readings.
///
/// `// CPP-PARITY: src/audio/src/audiolevelmeter.cpp:30`
/// (`k_decibel_minimum`, inlined from engine/common/decibel.h).
const DECIBEL_MINIMUM: f64 = -200.0;

/// `// CPP-PARITY: src/audio/src/audiolevelmeter.cpp:32`
/// (`decibel_from_linear`): -inf clamps to the floor.
fn decibel_from_linear(linear: f64) -> f64 {
	let v = 20.0 * linear.log10();
	if v.is_infinite() {
		return DECIBEL_MINIMUM;
	}
	v
}

/// `// CPP-PARITY: src/audio/src/audiolevelmeter.cpp:99`
/// (`AudioLevelMeter::linear_to_db`).
fn linear_to_db(linear: f64) -> f64 {
	if linear <= 0.0 {
		return DECIBEL_MINIMUM;
	}
	decibel_from_linear(linear)
}

/// `// CPP-PARITY: src/audio/src/audiolevelmeter.cpp:107`
/// (`AudioLevelMeter::power_to_lufs`): BS.1770-compatible unit, no
/// K-weighting.
fn power_to_lufs(mean_square: f64) -> f64 {
	if mean_square <= 0.0 {
		return DECIBEL_MINIMUM;
	}
	-0.691 + 10.0 * mean_square.log10()
}

/// Per-channel statistics for a single analysis pass.
///
/// `// CPP-PARITY: src/audio/src/audiolevelmeter.h`
/// (`AudioLevelMeter::ChannelStats`).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ChannelStats {
	/// Peak amplitude, linear scale. `0.0` when silent.
	pub peak_linear: f64,
	/// Peak amplitude, decibel scale. `-200.0` when silent.
	pub peak_db: f64,
	/// Root-mean-square level, linear scale.
	pub rms_linear: f64,
	/// Root-mean-square level, decibel scale. `-200.0` when silent.
	pub rms_db: f64,
	/// VU-meter ballistics reading, decibel scale.
	pub vu_db: f64,
}

/// Aggregate statistics over all analyzed channels.
///
/// `// CPP-PARITY: src/audio/src/audiolevelmeter.h`
/// (`AudioLevelMeter::Stats`).
#[derive(Debug, Clone, PartialEq)]
pub struct Stats {
	/// Per-channel statistics, indexed by channel.
	pub channels: Vec<ChannelStats>,
	/// Maximum peak across all channels, linear scale.
	pub max_peak_linear: f64,
	/// Integrated loudness (EBU R128 LUFS). `-200.0` for silence.
	pub integrated_lufs: f64,
	/// Whether every channel was silent below the noise gate.
	pub silence: bool,
}

/// Compute per-channel and summary statistics for a planar sample buffer.
///
/// `planar` holds one f32 slice per channel; every channel slice is assumed to
/// have the same length. Mirrors `AudioLevelMeter::analyze_sample_buffer`.
///
/// `// CPP-PARITY: src/audio/src/audiolevelmeter.cpp:42`
/// (`AudioLevelMeter::analyze_sample_buffer`): VU == RMS dB (no separate
/// ballistics); the silence gate is `qFuzzyIsNull` (|x| < 1e-12) on the
/// max peak; LUFS uses the mean square over ALL channels' samples.
pub fn analyze_sample_buffer(planar: &[&[f32]]) -> Stats {
	let mut stats = Stats {
		channels: vec![
			ChannelStats {
				peak_linear: 0.0,
				peak_db: DECIBEL_MINIMUM,
				rms_linear: 0.0,
				rms_db: DECIBEL_MINIMUM,
				vu_db: DECIBEL_MINIMUM,
			};
			planar.len()
		],
		max_peak_linear: 0.0,
		integrated_lufs: DECIBEL_MINIMUM,
		silence: true,
	};

	if planar.is_empty() || planar[0].is_empty() {
		return stats;
	}

	let sample_count = planar[0].len();
	let mut total_square = 0.0f64;
	let mut total_samples = 0usize;

	for (channel, data) in planar.iter().enumerate() {
		let mut peak = 0.0f64;
		let mut square_sum = 0.0f64;

		for &s in data.iter() {
			let value = f64::from(s);
			peak = peak.max(value.abs());
			square_sum += value * value;
		}

		let mean_square = square_sum / sample_count as f64;
		let rms = mean_square.sqrt();

		let rms_db = linear_to_db(rms);
		stats.channels[channel] = ChannelStats {
			peak_linear: peak,
			peak_db: linear_to_db(peak),
			rms_linear: rms,
			rms_db,
			vu_db: rms_db,
		};

		stats.max_peak_linear = stats.max_peak_linear.max(peak);
		total_square += square_sum;
		total_samples += sample_count;
	}

	// qFuzzyIsNull(double): |x| < 1e-12
	stats.silence = stats.max_peak_linear.abs() < 1e-12;
	stats.integrated_lufs = power_to_lufs(total_square / total_samples as f64);

	stats
}
