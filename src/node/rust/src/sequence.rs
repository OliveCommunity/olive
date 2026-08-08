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

//! Sequence: the C++ `ViewerOutput`/`Sequence` pair — a node that owns
//! tracks, markers, work area, and playback caches.

use crate::id::NodeId;

/// Sequence behavior (viewer node).
pub struct SequenceBehavior {
	/// Track list node ids (video then audio, C++ order).
	pub track_lists: Vec<NodeId>,
	/// Timeline markers handle (oaktimeline, owned).
	pub markers: crate::handle::CHandle,
	/// Work area handle (oaktimeline, owned).
	pub workarea: crate::handle::CHandle,
	/// Length cache (C++ last_length_).
	pub last_length: oakcore_rs::Rational,
	/// Autocache toggles.
	pub autocache_video: bool,
	/// Audio autocache toggle.
	pub autocache_audio: bool,
}

impl SequenceBehavior {
	/// Default sequence (C++ default parameters: one video + one audio
	/// track, default params). Track creation commands go through the
	/// oaktimeline C ABI (`TimelineAddTrackCommand` path).
	pub fn create_default() -> Self {
		todo!()
	}
}
