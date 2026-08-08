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

//! Blocks (C++ `Block`, `ClipBlock`, `GapBlock`, `TransitionBlock`).

use oakcore_rs::{Rational, TimeRange};

use crate::id::NodeId;

/// Block core data (C++ `Block` members): timeline span + media range.
pub struct BlockCore {
	/// Position and length on the timeline.
	pub range: TimeRange,
	/// Media in-point.
	pub media_in: Rational,
	/// Speed (1.0 = normal).
	pub speed: f64,
	/// Reversed flag.
	pub reversed: bool,
	/// Linked blocks (C++ block_links_).
	pub links: Vec<NodeId>,
}

/// Clip block behavior (media-bearing block; C++ `ClipBlock`).
pub struct ClipBlockBehavior {
	/// Block core.
	pub core: BlockCore,
	/// Connected footage (via the footage input edge).
	pub footage: Option<NodeId>,
}

/// Gap block behavior (empty span; C++ `GapBlock`).
pub struct GapBlockBehavior {
	/// Block core.
	pub core: BlockCore,
}

/// Transition block behavior (C++ `TransitionBlock`).
pub struct TransitionBlockBehavior {
	/// Block core.
	pub core: BlockCore,
	/// In offset (C++ in_offset).
	pub in_offset: Rational,
	/// Out offset.
	pub out_offset: Rational,
}
