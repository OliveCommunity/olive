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

//! The shared `Timeline` namespace (`src/timeline/src/timelinecommon.h`):
//! movement/thumbnail/waveform modes and the `EditToInfo` struct.
//!
//! Enum discriminants are value-compatible with both `include/timeline/edit.h`
//! (`OAKTIMELINE_MOVEMENT_*`) and `include/timeline/displaymode.h`
//! (`OAK_TIMELINE_THUMBNAIL_*` / `OAK_TIMELINE_WAVEFORMS_*`); `ffi.rs` maps
//! between them mechanically.

use oakcore_rs::Rational;

/// `Timeline::MovementMode` (timelinecommon.h).
///
/// `OAKTIMELINE_MOVEMENT_NONE = 0`, `MOVE = 1`, `TRIM_IN = 2`,
/// `TRIM_OUT = 3` (include/timeline/edit.h).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MovementMode {
	/// `k_none`: no movement.
	None,
	/// `k_move`: slide/position move.
	Move,
	/// `k_trim_in`: trim the in point.
	TrimIn,
	/// `k_trim_out`: trim the out point.
	TrimOut,
}

impl MovementMode {
	/// `Timeline::is_a_trim_mode`: true for `TrimIn`/`TrimOut`.
	pub fn is_a_trim_mode(self) -> bool {
		matches!(self, MovementMode::TrimIn | MovementMode::TrimOut)
	}

	/// Map to the `OAKTIMELINE_MOVEMENT_*` C enum value.
	pub fn to_c_int(self) -> i32 {
		match self {
			MovementMode::None => 0,
			MovementMode::Move => 1,
			MovementMode::TrimIn => 2,
			MovementMode::TrimOut => 3,
		}
	}

	/// Map a `OAKTIMELINE_MOVEMENT_*` C enum value back; `None` when the
	/// integer is out of range.
	pub fn from_c_int(v: i32) -> Option<MovementMode> {
		match v {
			0 => Some(MovementMode::None),
			1 => Some(MovementMode::Move),
			2 => Some(MovementMode::TrimIn),
			3 => Some(MovementMode::TrimOut),
			_ => None,
		}
	}
}

/// `Timeline::ThumbnailMode` (timelinecommon.h). Discriminants match
/// `OAK_TIMELINE_THUMBNAIL_OFF/IN_OUT/ON` (include/timeline/displaymode.h).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThumbnailMode {
	/// `k_thumbnail_off`: no thumbnails.
	Off,
	/// `k_thumbnail_in_out`: thumbnail at in/out.
	InOut,
	/// `k_thumbnail_on`: thumbnails on.
	On,
}

/// `Timeline::WaveformMode` (timelinecommon.h). Discriminants match
/// `OAK_TIMELINE_WAVEFORMS_DISABLED/ENABLED` (include/timeline/displaymode.h).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WaveformMode {
	/// `k_waveforms_disabled`.
	Disabled,
	/// `k_waveforms_enabled`.
	Enabled,
}

/// `Timeline::EditToInfo` (timelinecommon.h): where the nearest block edge
/// falls for a pointer edit. Not `Copy`/`Clone`/`Debug` — it owns
/// [`crate::handle::CHandle`]s, which are opaque refcounted handles.
pub struct EditToInfo {
	/// Owning track of the nearest block.
	pub track: crate::handle::CHandle,
	/// Nearest edit point time.
	pub nearest_time: Rational,
	/// Nearest block.
	pub nearest_block: crate::handle::CHandle,
}
