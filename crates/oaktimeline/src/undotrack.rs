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

//! Track-level block commands (`src/timeline/src/timelineundotrack.h`).
//! All graph operations go through the oaknode C ABI (`bridge::node`).

use crate::bridge::node::{
	oaknode_block_get_previous, oaknode_track_insert_block_after, oaknode_track_prepend_block,
	oaknode_track_replace_block, oaknode_track_ripple_remove_block,
};
use crate::handle::CHandle;
use crate::undocommon::{box_command, Command};

/// `TrackRippleRemoveBlockCommand` — ripple-remove a block from a track
/// (timelineundotrack.h).
pub struct TrackRippleRemoveBlockCommand {
	/// Owning track.
	track: CHandle,
	/// Block to remove.
	block: CHandle,
	/// Predecessor of the removed block, captured at `redo` time so `undo`
	/// can restore the block at its original position.
	before: CHandle,
}

impl TrackRippleRemoveBlockCommand {
	/// Construct from track + block.
	pub fn new(track: CHandle, block: CHandle) -> Self {
		Self {
			track,
			block,
			before: CHandle::null(),
		}
	}

	/// `redo`: capture the predecessor, then `oaknode_track_ripple_remove_block`.
	pub fn redo(&mut self) {
		unsafe {
			oaknode_block_get_previous(self.block.clone(), &mut self.before);
			oaknode_track_ripple_remove_block(self.track.clone(), self.block.clone());
		}
	}

	/// `undo`: re-insert via `oaknode_track_insert_block_after`.
	pub fn undo(&mut self) {
		unsafe {
			oaknode_track_insert_block_after(
				self.track.clone(),
				self.block.clone(),
				self.before.clone(),
			);
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackRippleRemoveBlockCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `TrackPrependBlockCommand` — prepend a block to a track
/// (timelineundotrack.h).
pub struct TrackPrependBlockCommand {
	/// Owning track.
	track: CHandle,
	/// Block to prepend.
	block: CHandle,
}

impl TrackPrependBlockCommand {
	/// Construct from track + block.
	pub fn new(track: CHandle, block: CHandle) -> Self {
		Self { track, block }
	}

	/// `redo`: `oaknode_track_prepend_block`.
	pub fn redo(&mut self) {
		unsafe {
			oaknode_track_prepend_block(self.track.clone(), self.block.clone());
		}
	}

	/// `undo`: `oaknode_track_ripple_remove_block`.
	pub fn undo(&mut self) {
		unsafe {
			oaknode_track_ripple_remove_block(self.track.clone(), self.block.clone());
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackPrependBlockCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `TrackInsertBlockAfterCommand` — insert a block after a predecessor
/// (timelineundotrack.h).
pub struct TrackInsertBlockAfterCommand {
	/// Owning track.
	track: CHandle,
	/// Block to insert.
	block: CHandle,
	/// Predecessor block.
	before: CHandle,
}

impl TrackInsertBlockAfterCommand {
	/// Construct from track + block + predecessor.
	pub fn new(track: CHandle, block: CHandle, before: CHandle) -> Self {
		Self {
			track,
			block,
			before,
		}
	}

	/// `redo`: `oaknode_track_insert_block_after`.
	pub fn redo(&mut self) {
		unsafe {
			oaknode_track_insert_block_after(
				self.track.clone(),
				self.block.clone(),
				self.before.clone(),
			);
		}
	}

	/// `undo`: `oaknode_track_ripple_remove_block`.
	pub fn undo(&mut self) {
		unsafe {
			oaknode_track_ripple_remove_block(self.track.clone(), self.block.clone());
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackInsertBlockAfterCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `TrackReplaceBlockCommand` — replace block `old` with block `replace`
/// (equal lengths required) (timelineundotrack.h).
pub struct TrackReplaceBlockCommand {
	/// Owning track.
	track: CHandle,
	/// Block to replace.
	old: CHandle,
	/// Replacement block.
	replace: CHandle,
}

impl TrackReplaceBlockCommand {
	/// Construct from track + old + replace.
	pub fn new(track: CHandle, old: CHandle, replace: CHandle) -> Self {
		Self {
			track,
			old,
			replace,
		}
	}

	/// `redo`: `oaknode_track_replace_block(track, old, replace)`.
	pub fn redo(&mut self) {
		unsafe {
			oaknode_track_replace_block(self.track.clone(), self.old.clone(), self.replace.clone());
		}
	}

	/// `undo`: `oaknode_track_replace_block(track, replace, old)`.
	pub fn undo(&mut self) {
		unsafe {
			oaknode_track_replace_block(self.track.clone(), self.replace.clone(), self.old.clone());
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackReplaceBlockCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}
