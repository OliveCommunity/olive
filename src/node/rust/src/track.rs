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

//! Tracks and track lists (C++ `Track`, `TrackList`).

use crate::id::NodeId;

/// Track media type (values match C++ `Track::Type`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TrackType {
	/// Video.
	Video,
	/// Audio.
	Audio,
	/// Subtitle.
	Subtitle,
}

/// Track behavior: an ordered block list (C++ `Track`).
pub struct TrackBehavior {
	/// Media type.
	pub kind: TrackType,
	/// Block node ids in timeline order.
	pub blocks: Vec<NodeId>,
	/// Muted flag.
	pub muted: bool,
	/// Locked flag.
	pub locked: bool,
}

/// Track list behavior (C++ `TrackList`): the per-type collection of
/// tracks inside a sequence.
pub struct TrackListBehavior {
	/// Media type of this list.
	pub kind: TrackType,
	/// Track node ids in stack order.
	pub tracks: Vec<NodeId>,
}
