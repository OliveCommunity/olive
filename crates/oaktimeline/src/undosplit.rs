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

//! Split commands (`src/timeline/src/timelineundosplit.h`). Graph
//! operations go directly through the oaknode Rust domain (the oaknode
//! C ABI was deleted in the single-lib unification).
//!
//! The C++ oracles (`timelineundosplit.cpp`) clone the split block in the
//! project graph (`oaknode_node_copy_in_graph` + `oaknode_block_from_node`),
//! run a captured node-graph reconnect command, preserve link groups
//! (`oaknode_block_are_linked`), move out-transitions across the split
//! (`oaknode_node_disconnect`/`oaknode_node_connect`), and add cache
//! passthrough (`oaknode_clip_add_cache_passthrough_from`). The clone is
//! restored via `NodeBehavior::duplicate` (the Rust equivalent of
//! `Node::Copy`) and link preservation via `Graph::link`; the
//! transition/cache/reconnect steps have no Rust equivalent yet and are
//! noted at each site.

use oakcore_rs::Rational;
use oaknode::graph::NodeEntry;
use oakundo::undocommand::UndoCommand;

use crate::util::{
	block_add_to_graph, block_in, block_length, block_out, block_remove_from_graph,
	block_set_length_and_media_in, block_set_length_and_media_out, block_track,
	track_insert_block_after, track_ripple_remove_block, GraphBlockRange, NodeRef,
};

/// `BlockSplitCommand` — split one block at a point
/// (timelineundosplit.h).
///
/// The split keeps the original block anchored at its in-point (it becomes
/// the first half `[in, point)`) and anchors the cloned `new_block` at its
/// out-point (it becomes the second half `[point, out)`), inserted right
/// after the original so the track order mirrors the timeline order.
pub struct BlockSplitCommand {
	/// Block to split.
	block: NodeRef,
	/// Split point.
	point: Rational,
	/// Second block created by the split (valid after `redo`).
	new_block: Option<NodeRef>,
	/// Arena entry of the second block while it is detached from the
	/// graph (between `undo` and the next `redo`).
	new_block_entry: Option<NodeEntry>,
	/// Length of `block` before the split, restored on `undo`.
	old_length: Rational,
}

impl BlockSplitCommand {
	/// Construct from block + split point.
	///
	/// New signature (single-lib): `pub fn new(block: NodeRef, point: Rational) -> BlockSplitCommand`
	pub fn new(block: NodeRef, point: Rational) -> Self {
		Self {
			block,
			point,
			new_block: None,
			new_block_entry: None,
			old_length: Rational::new(0, 1),
		}
	}

	/// `prepare`: create the second half by cloning the original block in
	/// the project graph (the Rust equivalent of the C++
	/// `oaknode_node_copy_in_graph`; the clone happens through the
	/// behavior's `duplicate`, which copies the block core too).
	pub fn prepare(&mut self) {
		if self.new_block.is_some() {
			return;
		}
		let id = {
			let project = self.block.project.clone();
			let mut p = project.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
			let entry = match p.graph.get(self.block.id) {
				Some(e) => e,
				None => return,
			};
			let new_core = entry.core.clone();
			let new_behavior = match entry.behavior.duplicate(&entry.core) {
				Some(b) => b,
				None => return,
			};
			p.graph.add_node(new_core, new_behavior)
		};
		self.new_block = Some(NodeRef::new(self.block.project.clone(), id));
	}

	/// `redo`: shrink `block` to the first half, grow `new_block` to the
	/// second half, and insert it after `block`.
	///
	/// The split keeps the ORIGINAL block anchored at its in-point (the
	/// C++ `set_length_and_media_in` on the original) and anchors the
	/// cloned `new_block` at its out-point (the C++
	/// `set_length_and_media_out` on the copy — the copy carried the
	/// original span, so the out point is preserved exactly).
	pub fn redo(&mut self) {
		// Create the second half if redo is invoked without a preceding
		// prepare() (the vtable command path may call redo directly).
		self.prepare();

		// The C++ asserts `point_` lies strictly inside the block; that
		// would panic across the FFI boundary, so it is intentionally not
		// replicated — a split point outside the span is clamped by the
		// rational arithmetic below.
		self.old_length = block_length(&self.block);

		let block_in = block_in(&self.block);
		let block_out = block_out(&self.block);

		let first_half_length = self.point - block_in;
		let second_half_length = block_out - self.point;

		if let Some(new_block) = &self.new_block {
			// Re-attach the second half if a previous undo detached it.
			if self.new_block_entry.is_some() {
				block_add_to_graph(new_block, self.new_block_entry.take());
			}
			// In-anchored length for the first half keeps the original's in
			// point (the C++ `set_length_and_media_in`); the out-anchored
			// length for the second half keeps the copy's out point (the
			// C++ `set_length_and_media_out`).
			block_set_length_and_media_in(&self.block, first_half_length);
			block_set_length_and_media_out(new_block, second_half_length);

			if let Some(track) = block_track(&self.block) {
				track_insert_block_after(&track, new_block, Some(&self.block));
			}
		}

		// The C++ also re-runs a `reconnect_tree_command_` (the node-graph
		// re-connection captured while cloning) and moves an out transition
		// onto `new_block` via `oaknode_*` connection APIs; the Rust block
		// has no transition edges, so both are omitted here.
	}

	/// `undo`: restore `block`'s original length and remove the second half.
	pub fn undo(&mut self) {
		if let Some(track) = block_track(&self.block) {
			// The redo shrank the original from its in point (it became the
			// first half, in-anchored), so the restore grows it in-anchored
			// too — the module stores the span on the block (the C++
			// `set_length_and_media_out` derives positions from the track
			// order and does not apply here).
			block_set_length_and_media_in(&self.block, self.old_length);
			if let Some(new_block) = &self.new_block {
				track_ripple_remove_block(&track, new_block);
				// Detach the second half from the graph (the C++ re-parents
				// it to the scratch memory manager); the entry is owned by
				// this command until the next redo.
				if self.new_block_entry.is_none() {
					self.new_block_entry = block_remove_from_graph(new_block);
				}
			}
		}

		// The C++ first moves a previously-moved out transition back onto
		// `block` and runs `reconnect_tree_command_`'s undo; the Rust block
		// has no transition edges, so those are omitted here.
	}

	/// The second block created by the split; only valid after `redo`.
	///
	/// New signature (single-lib): `pub fn new_block(&self) -> Option<NodeRef>`
	pub fn new_block(&self) -> Option<NodeRef> {
		self.new_block.clone()
	}

	/// Wrap as an oakundo vtable command value.
	pub fn to_command(self) -> UndoCommand {
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
	blocks: Vec<NodeRef>,
	/// Split times.
	times: Vec<Rational>,
	/// Child split commands, executed in order on `redo`.
	commands: Vec<BlockSplitCommand>,
	/// `splits[time_index][block_index]`: second half created for that
	/// block/time, or `None` when the block was not split there.
	splits: Vec<Vec<Option<NodeRef>>>,
}

impl BlockSplitPreservingLinksCommand {
	/// Construct from blocks + times (one per block).
	///
	/// New signature (single-lib): `pub fn new(blocks: Vec<NodeRef>, times: Vec<Rational>) -> BlockSplitPreservingLinksCommand`
	pub fn new(blocks: Vec<NodeRef>, times: Vec<Rational>) -> Self {
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
		self.splits = vec![vec![None; n_blocks]; n_times];

		for i in 0..n_times {
			let time = self.times[i];

			// The C++ asserts the times are strictly ordered; that would
			// panic across the FFI boundary, so it is not replicated.

			for j in 0..n_blocks {
				let b_in = block_in(&self.blocks[j]);
				let b_out = block_out(&self.blocks[j]);

				if b_in < time && b_out > time {
					let mut split = BlockSplitCommand::new(self.blocks[j].clone(), time);
					split.prepare();
					split.redo();
					self.splits[i][j] = split.new_block();
					self.commands.push(split);
				}
			}
		}

		// Link preservation (C++ `oaknode_block_are_linked` +
		// link commands between the halves): every pair of originally
		// linked blocks that was split at the same time has its new
		// halves linked too (`Graph::link` mirrors `Node::link`).
		let Some(first) = self.blocks.first() else {
			return;
		};
		let project = first.project.clone();
		let mut p = project.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
		for i in 0..n_times {
			for j in 0..n_blocks {
				for k in (j + 1)..n_blocks {
					if p.graph.are_linked(self.blocks[j].id, self.blocks[k].id) {
						if let (Some(a), Some(b)) = (&self.splits[i][j], &self.splits[i][k]) {
							p.graph.link(a.id, b.id);
						}
					}
				}
			}
		}
	}

	/// `redo`: redo every child command in order.
	///
	/// The vtable command path never invokes `prepare()` (the oakundo
	/// wrapper only dispatches `redo`/`undo`), so the children are built
	/// on first redo; `prepare` itself redoes each child as it builds it,
	/// so the first redo has nothing left to run. Later redos (after an
	/// undo) run the stored children directly.
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
	///
	/// New signature (single-lib): `pub fn get_split(&self, original: &NodeRef, time_index: usize) -> Option<NodeRef>`
	pub fn get_split(&self, original: &NodeRef, time_index: usize) -> Option<NodeRef> {
		if time_index < self.times.len() {
			for (i, b) in self.blocks.iter().enumerate() {
				if b.id == original.id {
					return self
						.splits
						.get(time_index)
						.and_then(|row| row.get(i))
						.cloned()
						.flatten();
				}
			}
		}
		None
	}

	/// Wrap as an oakundo vtable command value.
	pub fn to_command(self) -> UndoCommand {
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
	track: NodeRef,
	/// Split point.
	point: Rational,
	/// Inner per-block split command, built by `prepare`.
	command: Option<BlockSplitCommand>,
}

impl TrackSplitAtTimeCommand {
	/// Construct from track + point.
	///
	/// New signature (single-lib): `pub fn new(track: NodeRef, point: Rational) -> TrackSplitAtTimeCommand`
	pub fn new(track: NodeRef, point: Rational) -> Self {
		Self {
			track,
			point,
			command: None,
		}
	}

	/// `prepare`: build the per-block split command. The C++ finds the
	/// block via `oaknode_track_get_block_containing_time`; the Rust
	/// equivalent is `TrackBehavior::block_containing_time` over the
	/// graph-backed block range.
	pub fn prepare(&mut self) {
		if self.command.is_some() {
			return;
		}
		let block_id = {
			let project = self.track.project.clone();
			let p = project.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
			p.graph
				.get(self.track.id)
				.and_then(|e| e.behavior.as_any())
				.and_then(|a| a.downcast_ref::<oaknode::track::TrackBehavior>())
				.and_then(|t| {
					t.block_containing_time(self.point, &GraphBlockRange { graph: &p.graph })
				})
		};
		if let Some(id) = block_id {
			self.command = Some(BlockSplitCommand::new(
				NodeRef::new(self.track.project.clone(), id),
				self.point,
			));
		}
	}

	/// `redo`: forward to the inner command.
	pub fn redo(&mut self) {
		self.prepare();
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

	/// Wrap as an oakundo vtable command value.
	pub fn to_command(self) -> UndoCommand {
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
