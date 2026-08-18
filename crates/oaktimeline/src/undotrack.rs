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
//! Graph operations go directly through the oaknode Rust domain
//! ([`crate::util::NodeRef`] + the project graph) since the single-lib
//! unification removed the oaknode C ABI.

use oakundo::undocommand::UndoCommand;

use crate::undocommon::{box_command, Command};
use crate::util::{
	block_previous, track_insert_block_after, track_prepend_block, track_replace_block,
	track_ripple_remove_block, NodeRef,
};

/// `TrackRippleRemoveBlockCommand` — ripple-remove a block from a track
/// (timelineundotrack.h).
pub struct TrackRippleRemoveBlockCommand {
	/// Owning track.
	track: NodeRef,
	/// Block to remove.
	block: NodeRef,
	/// Predecessor of the removed block, captured at `redo` time so `undo`
	/// can restore the block at its original position.
	before: Option<NodeRef>,
}

impl TrackRippleRemoveBlockCommand {
	/// Construct from track + block.
	///
	/// New signature (single-lib): `pub fn new(track: NodeRef, block: NodeRef) -> TrackRippleRemoveBlockCommand`
	pub fn new(track: NodeRef, block: NodeRef) -> Self {
		Self {
			track,
			block,
			before: None,
		}
	}

	/// `redo`: capture the predecessor, then ripple-remove.
	pub fn redo(&mut self) {
		self.before = block_previous(&self.block);
		track_ripple_remove_block(&self.track, &self.block);
	}

	/// `undo`: re-insert after the captured predecessor.
	pub fn undo(&mut self) {
		track_insert_block_after(&self.track, &self.block, self.before.as_ref());
	}

	/// Wrap as an oakundo command value.
	pub fn to_command(self) -> UndoCommand {
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
	track: NodeRef,
	/// Block to prepend.
	block: NodeRef,
}

impl TrackPrependBlockCommand {
	/// Construct from track + block.
	///
	/// New signature (single-lib): `pub fn new(track: NodeRef, block: NodeRef) -> TrackPrependBlockCommand`
	pub fn new(track: NodeRef, block: NodeRef) -> Self {
		Self { track, block }
	}

	/// `redo`: prepend the block.
	pub fn redo(&mut self) {
		track_prepend_block(&self.track, &self.block);
	}

	/// `undo`: ripple-remove the block.
	pub fn undo(&mut self) {
		track_ripple_remove_block(&self.track, &self.block);
	}

	/// Wrap as an oakundo command value.
	pub fn to_command(self) -> UndoCommand {
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
/// (timelineundotrack.h). A `None` predecessor inserts at the front of
/// the track.
pub struct TrackInsertBlockAfterCommand {
	/// Owning track.
	track: NodeRef,
	/// Block to insert.
	block: NodeRef,
	/// Predecessor block (`None` = front).
	before: Option<NodeRef>,
}

impl TrackInsertBlockAfterCommand {
	/// Construct from track + block + predecessor.
	///
	/// New signature (single-lib): `pub fn new(track: NodeRef, block: NodeRef, before: Option<NodeRef>) -> TrackInsertBlockAfterCommand`
	pub fn new(track: NodeRef, block: NodeRef, before: Option<NodeRef>) -> Self {
		Self {
			track,
			block,
			before,
		}
	}

	/// `redo`: insert after the predecessor.
	pub fn redo(&mut self) {
		track_insert_block_after(&self.track, &self.block, self.before.as_ref());
	}

	/// `undo`: ripple-remove the block.
	pub fn undo(&mut self) {
		track_ripple_remove_block(&self.track, &self.block);
	}

	/// Wrap as an oakundo command value.
	pub fn to_command(self) -> UndoCommand {
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
	track: NodeRef,
	/// Block to replace.
	old: NodeRef,
	/// Replacement block.
	replace: NodeRef,
}

impl TrackReplaceBlockCommand {
	/// Construct from track + old + replace.
	///
	/// New signature (single-lib): `pub fn new(track: NodeRef, old: NodeRef, replace: NodeRef) -> TrackReplaceBlockCommand`
	pub fn new(track: NodeRef, old: NodeRef, replace: NodeRef) -> Self {
		Self {
			track,
			old,
			replace,
		}
	}

	/// `redo`: replace `old` with `replace`.
	pub fn redo(&mut self) {
		track_replace_block(&self.track, &self.old, &self.replace);
	}

	/// `undo`: swap back.
	pub fn undo(&mut self) {
		track_replace_block(&self.track, &self.replace, &self.old);
	}

	/// Wrap as an oakundo command value.
	pub fn to_command(self) -> UndoCommand {
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
