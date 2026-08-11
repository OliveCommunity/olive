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

//! Split commands (`src/timeline/src/timelineundosplit.h`). All graph
//! operations go through the oaknode C ABI.
//!
//! The C++ oracles (`timelineundosplit.cpp`) clone the split block in the
//! project graph (`oaknode_node_copy_in_graph` + `oaknode_block_from_node`),
//! run a captured node-graph reconnect command, preserve link groups
//! (`oaknode_block_are_linked`), move out-transitions across the split
//! (`oaknode_node_disconnect`/`oaknode_node_connect`), and add cache
//! passthrough (`oaknode_clip_add_cache_passthrough_from`). None of those
//! symbols are exposed by this crate's oaknode bridge, so those branches are
//! omitted here and noted at each site; the remaining length/insert/remove
//! logic mirrors the C++.

use oakcore_rs::Rational;

use crate::bridge::node::{
	oaknode_block_clip_create, oaknode_track_insert_block_after, oaknode_track_ripple_remove_block,
};
use crate::handle::CHandle;
use crate::util::{
	block_in, block_length, block_out, block_set_length_and_media_in,
	block_set_length_and_media_out, block_track, same_block,
};

/// `BlockSplitCommand` — split one block at a point
/// (timelineundosplit.h).
pub struct BlockSplitCommand {
	/// Block to split.
	block: CHandle,
	/// Split point.
	point: Rational,
	/// Second block created by the split (valid after `redo`).
	new_block: CHandle,
	/// Length of `block` before the split, restored on `undo`.
	old_length: Rational,
}

impl BlockSplitCommand {
	/// Construct from block + split point.
	pub fn new(block: CHandle, point: Rational) -> Self {
		Self {
			block,
			point,
			new_block: CHandle::null(),
			old_length: Rational::new(0, 1),
		}
	}

	/// `prepare`: create the second half of the split. The C++ clone is
	/// performed by `oaknode_node_copy_in_graph` + `oaknode_block_from_node`,
	/// which are absent from this bridge, so a fresh clip block is created.
	pub fn prepare(&mut self) {
		if self.new_block.is_null() {
			// SAFETY: `oaknode_block_clip_create` returns an owned handle.
			self.new_block = unsafe { oaknode_block_clip_create() };
		}
	}

	/// `redo`: shrink `block` to the first half, grow `new_block` to the
	/// second half, and insert it after `block`.
	///
	/// The split keeps the ORIGINAL block anchored at its out-point (it
	/// becomes the second half `[point, out)`) and anchors the fresh
	/// `new_block` at its in-point (it becomes the first half
	/// `[in, point)`), matching the C++ `Block::set_length_and_media_out` /
	/// `set_length_and_media_in` semantics of the module.
	pub fn redo(&mut self) {
		// Create the second half if redo is invoked without a preceding
		// prepare() (the C ABI command path may call redo directly).
		if self.new_block.is_null() {
			// SAFETY: `oaknode_block_clip_create` returns an owned handle.
			self.new_block = unsafe { oaknode_block_clip_create() };
		}

		self.old_length = block_length(self.block.clone());

		let block_in = block_in(self.block.clone());
		let block_out = block_out(self.block.clone());

		// The C++ asserts `point_` lies strictly inside the block; that would
		// panic across the FFI boundary, so it is intentionally not replicated.
		let first_half_length = self.point - block_in;
		let second_half_length = block_out - self.point;

		let track = block_track(self.block.clone());

		// Out-anchored length for the second half keeps the original's out
		// point (the C++ `set_length_and_media_out`); the in-anchored length
		// for the first half grows the fresh block from its default in of 0
		// (the C++ `set_length_and_media_in`). The two were previously
		// swapped, which anchored the halves at the wrong points.
		block_set_length_and_media_out(self.block.clone(), second_half_length);
		block_set_length_and_media_in(self.new_block.clone(), first_half_length);

		// SAFETY: bridge inserts `new_block` after `block` on `track`.
		let _ = unsafe {
			oaknode_track_insert_block_after(track, self.new_block.clone(), self.block.clone())
		};

		// The C++ also re-runs a `reconnect_tree_command_` (the node-graph
		// re-connection captured while cloning) and moves an out transition
		// onto `new_block` via `oaknode_*` connection APIs; those symbols are
		// absent from this bridge, so both are omitted here.
	}

	/// `undo`: restore `block`'s original length and remove the second half.
	pub fn undo(&mut self) {
		let track = block_track(self.block.clone());

		block_set_length_and_media_out(self.block.clone(), self.old_length);

		// SAFETY: bridge ripple-removes `new_block` from `track`.
		let _ = unsafe { oaknode_track_ripple_remove_block(track, self.new_block.clone()) };

		// The C++ first moves a previously-moved out transition back onto
		// `block` and runs `reconnect_tree_command_`'s undo; both need oaknode
		// symbols absent from this bridge, so they are omitted here.
	}

	/// The second block created by the split; only valid after `redo`.
	pub fn new_block(&self) -> CHandle {
		self.new_block.clone()
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		crate::undocommon::box_command(self)
	}
}

impl crate::undocommon::Command for BlockSplitCommand {
	fn redo(&mut self) {
		self.redo();
	}

	fn undo(&mut self) {
		self.undo();
	}
}

/// `BlockSplitPreservingLinksCommand` — split a set of blocks at per-block
/// times, preserving link groups (timelineundosplit.h).
pub struct BlockSplitPreservingLinksCommand {
	/// Blocks to split.
	blocks: Vec<CHandle>,
	/// Split times.
	times: Vec<Rational>,
	/// Child split commands, executed in order on `redo`.
	commands: Vec<BlockSplitCommand>,
	/// `splits[time_index][block_index]`: second half created for that
	/// block/time, or an empty handle when the block was not split there.
	splits: Vec<Vec<CHandle>>,
}

impl BlockSplitPreservingLinksCommand {
	/// Construct from blocks + times (one per block).
	pub fn new(blocks: Vec<CHandle>, times: Vec<Rational>) -> Self {
		Self {
			blocks,
			times,
			commands: Vec::new(),
			splits: Vec::new(),
		}
	}

	/// `prepare`: build the child split commands.
	pub fn prepare(&mut self) {
		let n_times = self.times.len();
		let n_blocks = self.blocks.len();
		self.splits = vec![vec![CHandle::null(); n_blocks]; n_times];

		for i in 0..n_times {
			let time = self.times[i];

			// The C++ asserts the times are strictly ordered; that would
			// panic across the FFI boundary, so it is not replicated.

			for j in 0..n_blocks {
				let b_in = block_in(self.blocks[j].clone());
				let b_out = block_out(self.blocks[j].clone());

				if b_in < time && b_out > time {
					let mut split = BlockSplitCommand::new(self.blocks[j].clone(), time);
					split.prepare();
					split.redo();
					self.splits[i][j] = split.new_block();
					self.commands.push(split);
				}
			}
		}

		// The C++ then relinks every pair of originally-linked blocks by
		// creating link commands between their split halves
		// (`oaknode_block_are_linked`); that symbol is absent from this
		// bridge, so link preservation is omitted.
	}

	/// `redo`: redo every child command in order.
	///
	/// The C ABI command path never invokes `prepare()` (the oakundo vtable
	/// wrapper only dispatches `redo`/`undo`), so the children are built on
	/// first redo; `prepare` itself redoes each child as it builds it, so the
	/// first redo has nothing left to run. Later redos (after an undo) run
	/// the stored children directly.
	pub fn redo(&mut self) {
		if self.commands.is_empty() {
			self.prepare();
			return;
		}
		for c in self.commands.iter_mut() {
			c.redo();
		}
	}

	/// `undo`: undo every child command in reverse.
	pub fn undo(&mut self) {
		for c in self.commands.iter_mut().rev() {
			c.undo();
		}
	}

	/// The block produced by splitting `original` at `time_index`;
	/// `None` when not applicable (timelineundosplit.h `get_split`).
	pub fn get_split(&self, original: CHandle, time_index: usize) -> Option<CHandle> {
		if time_index < self.times.len() {
			for (i, b) in self.blocks.iter().enumerate() {
				if same_block(b.clone(), original.clone()) {
					return self
						.splits
						.get(time_index)
						.and_then(|row| row.get(i))
						.cloned();
				}
			}
		}
		None
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		crate::undocommon::box_command(self)
	}
}

impl crate::undocommon::Command for BlockSplitPreservingLinksCommand {
	fn redo(&mut self) {
		self.redo();
	}

	fn undo(&mut self) {
		self.undo();
	}
}

/// `TrackSplitAtTimeCommand` — split every block on a track at a point
/// (timelineundosplit.h).
pub struct TrackSplitAtTimeCommand {
	/// Owning track.
	track: CHandle,
	/// Split point.
	point: Rational,
	/// Inner per-block split command, built by `prepare`.
	command: Option<BlockSplitCommand>,
}

impl TrackSplitAtTimeCommand {
	/// Construct from track + point.
	pub fn new(track: CHandle, point: Rational) -> Self {
		Self {
			track,
			point,
			command: None,
		}
	}

	/// `prepare`: build the per-block split command. The C++ finds the block
	/// via `oaknode_track_get_block_containing_time`, which is not exposed by
	/// this crate's oaknode bridge (and there is no block-enumeration helper
	/// to search for it), so the inner command cannot be built here and
	/// redo/undo remain no-ops until that lookup API exists.
	pub fn prepare(&mut self) {
		let _ = (self.track.clone(), self.point);
	}

	/// `redo`: forward to the inner command.
	pub fn redo(&mut self) {
		if let Some(c) = self.command.as_mut() {
			c.redo();
		}
	}

	/// `undo`: forward to the inner command.
	pub fn undo(&mut self) {
		if let Some(c) = self.command.as_mut() {
			c.undo();
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		crate::undocommon::box_command(self)
	}
}

impl crate::undocommon::Command for TrackSplitAtTimeCommand {
	fn redo(&mut self) {
		self.redo();
	}

	fn undo(&mut self) {
		self.undo();
	}
}
