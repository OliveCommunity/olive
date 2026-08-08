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

//! Keyframes and interpolation (C++ `NodeKeyframe` + track logic).

use oakcore_rs::Rational;

use crate::value::NodeValue;

/// Interpolation mode (values match the C++ `NodeKeyframe::Type`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Interpolation {
	/// Constant hold.
	Hold,
	/// Linear.
	Linear,
	/// Cubic bezier (control points on the keyframe).
	Bezier,
}

/// A single keyframe.
#[derive(Clone, Debug)]
pub struct Keyframe {
	/// Time.
	pub time: Rational,
	/// Value.
	pub value: NodeValue,
	/// Interpolation to the next keyframe.
	pub interpolation: Interpolation,
	/// Bezier control points (used when `interpolation == Bezier`).
	pub bezier_in: (f64, f64),
	/// Bezier out control point.
	pub bezier_out: (f64, f64),
}

/// Sorted keyframe track for one (input, element).
#[derive(Default, Debug)]
pub struct KeyframeTrack {
	keys: Vec<Keyframe>,
}

impl KeyframeTrack {
	/// Insert or replace the keyframe at `time` (keeps sort order).
	pub fn set_key(&mut self, key: Keyframe) {
		todo!()
	}

	/// Remove the keyframe at `time`; false when absent.
	pub fn remove_key(&mut self, time: Rational) -> bool {
		todo!()
	}

	/// Interpolated value at `time`; `None` on an empty track.
	/// Interpolation math must match the C++ lerp/bezier exactly
	/// (`// CPP-PARITY: nodekeyframe.cpp`).
	pub fn value_at(&self, time: Rational) -> Option<NodeValue> {
		todo!()
	}

	/// Sorted keyframes view.
	pub fn keys(&self) -> &[Keyframe] {
		todo!()
	}
}
