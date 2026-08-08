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

//! Value system and keyframe contract tests.

/// NodeValueTable: last-push-wins per type, tag preserved, `get` of an
/// absent type returns None (C++ NodeValueTable semantics).
#[test]
fn value_table_last_push_wins() {
	todo!()
}

/// Texture values release their handle reference on drop (refcount
/// discipline: no C++ Variant shared_ptr aliasing exists here).
#[test]
fn texture_value_drop_releases() {
	todo!()
}

/// KeyframeTrack: insert keeps order; replace at same time overwrites;
/// remove missing key returns false.
#[test]
fn keyframe_track_ordering() {
	todo!()
}

/// Interpolation parity: linear/bezier/hold values at sampled times
/// match the C++ lerp within 1e-12 (bezier control points included;
/// golden vectors captured from the C++ implementation).
#[test]
fn interpolation_matches_cpp() {
	todo!()
}

/// Empty track value_at returns None; single-key track holds
/// constant before and after the key.
#[test]
fn keyframe_edge_cases() {
	todo!()
}
