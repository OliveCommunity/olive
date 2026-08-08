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

//! Generational identifiers for arena-stored graph objects.
//!
//! The public C ABI hands out opaque refcounted handle boxes; inside
//! the crate, a handle boxes `(Arc<Mutex<Project>>, NodeId)`. The
//! generation counter makes a stale `NodeId` (whose slot was reused)
//! fail loudly instead of aliasing a different node — this replaces
//! the C++ design's dangling-pointer failure mode with a checked one.

/// Id of a node inside a [`crate::graph::Graph`] arena.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct NodeId {
	index: u32,
	generation: u32,
}

impl NodeId {
	/// Construct from raw parts (arena-internal use).
	pub(crate) fn new(index: u32, generation: u32) -> NodeId {
		NodeId { index, generation }
	}

	/// Slot index.
	pub fn index(self) -> u32 {
		self.index
	}

	/// Generation counter.
	pub fn generation(self) -> u32 {
		self.generation
	}

	/// Stable identity integer for registry keys and XML cross-
	/// references (replaces the C++ raw-pointer `uintptr_t` identity;
	/// NOT an address, safe to persist within a session).
	pub fn identity(self) -> u64 {
		((self.generation as u64) << 32) | self.index as u64
	}
}
