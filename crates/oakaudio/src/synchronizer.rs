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

//! Timeline placement helpers (`olive::AudioSynchronizer`). All static in
//! C++; becomes a plain function module. Times are `core::Rational` (from
//! oakcore-rs).

use oakcore_rs::Rational;

use crate::params::rational_from_double;

/// One clip's source-time metadata.
pub struct SourceClip {
	/// Source start time in seconds.
	pub source_start_time: Rational,
	/// Media in point in seconds.
	pub media_in: Rational,
	/// Whether `source_start_time` is set.
	pub has_source_start_time: bool,
}

/// A timeline placement result.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Placement {
	/// Candidate's timeline in point in seconds.
	pub timeline_in: Rational,
	/// Whether placement succeeded.
	pub valid: bool,
}

/// Place the candidate on the timeline so its source time aligns with the
/// reference clip.
///
/// `// CPP-PARITY: src/audio/src/audiosynchronizer.cpp:27`
/// (`AudioSynchronizer::place_by_source_time`): invalid when either clip
/// lacks a source start time or carries a NaN rational.
pub fn place_by_source_time(
	reference: &SourceClip,
	candidate: &SourceClip,
	reference_timeline_in: Rational,
) -> Placement {
	let mut placement = Placement {
		timeline_in: Rational::NULL,
		valid: false,
	};
	if !reference.has_source_start_time
		|| !candidate.has_source_start_time
		|| reference.source_start_time.is_nan()
		|| candidate.source_start_time.is_nan()
	{
		return placement;
	}

	let reference_head_source = reference.source_start_time + reference.media_in;
	let candidate_head_source = candidate.source_start_time + candidate.media_in;

	placement.timeline_in =
		reference_timeline_in + candidate_head_source - reference_head_source;
	placement.valid = !placement.timeline_in.is_nan();
	placement
}

/// Timeline placement from a measured waveform offset.
///
/// `// CPP-PARITY: src/audio/src/audiosynchronizer.cpp:50`
/// (`AudioSynchronizer::place_by_waveform_offset`): invalid for
/// `sample_rate <= 0`.
pub fn place_by_waveform_offset(
	reference_timeline_in: Rational,
	candidate_offset_samples: i64,
	sample_rate: i32,
) -> Placement {
	let mut placement = Placement {
		timeline_in: Rational::NULL,
		valid: false,
	};
	if sample_rate <= 0 {
		return placement;
	}

	placement.timeline_in = reference_timeline_in
		+ rational_from_double(candidate_offset_samples as f64 / f64::from(sample_rate));
	placement.valid = !placement.timeline_in.is_nan();
	placement
}
