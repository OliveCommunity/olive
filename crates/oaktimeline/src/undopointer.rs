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

//! Pointer-edit commands (`src/timeline/src/timelineundopointer.h`): trims
//! that compensate the adjacent block (`BlockTrimCommand`), slides
//! (`TrackSlideCommand`) and destructive placement (`TrackPlaceBlockCommand`).
//!
//! Graph mutation goes through the oaknode C ABI (`bridge::node`); command
//! wrapping through `bridge::undo`.

use oakcore_rs::{Rational, TimeRange};

use crate::bridge::node::{
	oaknode_block_gap_create, oaknode_block_get_kind, oaknode_track_insert_block_after,
	oaknode_track_ripple_remove_block, oaknode_tracklist_get_track_at,
	oaknode_tracklist_get_track_count,
};
use crate::bridge::undo::{oakundo_command_redo_now, oakundo_command_undo_now};
use crate::common::MovementMode;
use crate::handle::CHandle;
use crate::undocommon::{
	block_can_be_removed, box_command, create_and_run_block_remove_command,
	create_block_remove_command, Command, MultiUndoCommand,
};
use crate::util::{
	block_add_to_graph, block_in, block_length, block_next, block_previous,
	block_remove_from_graph, block_set_in, block_set_length_and_media_in,
	block_set_length_and_media_out, block_track, track_append_block, track_insert_block_before,
	track_length,
};

use super::undogeneral::{TimelineAddTrackCommand, TrackReplaceBlockWithGapCommand};
use super::undoripple::TrackRippleRemoveAreaCommand;

/// `BlockTrimCommand` — trim one block, compensating the block directly
/// adjacent so the rest of the track is unaffected (timelineundopointer.h).
///
/// By default only gaps are shortened; when the adjacent block must grow and
/// is not a gap, a gap is inserted. Set [`Self::set_trim_is_a_roll_edit`] to
/// always trim into the adjacent block instead.
pub struct BlockTrimCommand {
	/// Owning track (`track_`).
	track: CHandle,
	/// Block to trim (`block_`).
	block: CHandle,
	/// New length (`new_length_`).
	new_length: Rational,
	/// Trim direction (`mode_`).
	mode: MovementMode,
	/// Whether `prepare` found the length unchanged, so `redo`/`undo` no-op
	/// (`doing_nothing_`).
	doing_nothing_: bool,
	/// Signed difference `old_length - new_length`; positive when trimming
	/// shorter (`trim_diff_`).
	trim_diff_: Rational,
	/// Block length before the trim (`old_length_`).
	old_length_: Rational,
	/// The block to compensate (`adjacent_`).
	adjacent_: CHandle,
	/// Whether the adjacent block is required for this trim (`needs_adjacent_`).
	needs_adjacent_: bool,
	/// Whether `adjacent_` was created by this command (`we_created_adjacent_`).
	we_created_adjacent_: bool,
	/// Whether the adjacent block is removed by this trim (`we_removed_adjacent_`).
	we_removed_adjacent_: bool,
	/// Graph-removal command for a removed adjacent (`deleted_adjacent_command_`).
	///
	/// Owns its handle like the C++ `OakUndoCommand`; `CHandle` is refcounted
	/// and drops without a destructor, so releasing it is implicit — no explicit
	/// `free_command_handle` is needed here.
	deleted_adjacent_command_: CHandle,
	/// Always trim into the adjacent block instead of creating a gap
	/// (`trim_is_a_roll_edit_`).
	trim_is_a_roll_edit_: bool,
	/// Remove a zero-length adjacent block from the whole graph (default true)
	/// rather than only from the track (`remove_block_from_graph_`).
	remove_block_from_graph_: bool,
	/// Whether `adjacent_` is a created gap still detached from the graph
	/// (`adjacent_orphaned_`); a detached handle is owned by this command until
	/// it is added to the graph.
	adjacent_orphaned_: bool,
}

impl BlockTrimCommand {
	/// Construct from track + block + new length + movement mode.
	pub fn new(track: CHandle, block: CHandle, new_length: Rational, mode: MovementMode) -> Self {
		Self {
			track,
			block,
			new_length,
			mode,
			doing_nothing_: false,
			trim_diff_: Rational::new(0, 1),
			old_length_: Rational::new(0, 1),
			adjacent_: CHandle::null(),
			needs_adjacent_: false,
			we_created_adjacent_: false,
			we_removed_adjacent_: false,
			deleted_adjacent_command_: CHandle::null(),
			trim_is_a_roll_edit_: false,
			remove_block_from_graph_: true,
			adjacent_orphaned_: false,
		}
	}

	/// Always trim the adjacent block instead of creating a gap
	/// (`set_trim_is_a_roll_edit`).
	pub fn set_trim_is_a_roll_edit(&mut self, e: bool) {
		self.trim_is_a_roll_edit_ = e;
	}

	/// Whether an adjacent block shortened to zero length is removed from the
	/// whole graph (default true) or only from the track
	/// (`set_remove_zero_length_from_graph`).
	pub fn set_remove_zero_length_from_graph(&mut self, e: bool) {
		self.remove_block_from_graph_ = e;
	}

	/// `prepare`: compute the trim delta and whether an adjacent block is needed.
	pub fn prepare(&mut self) {
		// Store old length
		self.old_length_ = block_length(self.block.clone());

		// If the length isn't changing, set a flag to do nothing
		if self.old_length_ == self.new_length {
			self.doing_nothing_ = true;
			return;
		}
		self.doing_nothing_ = false;

		// Positive when trimming shorter, negative when trimming longer
		self.trim_diff_ = self.old_length_ - self.new_length;

		// Retrieve our adjacent block (or an empty handle if none)
		self.adjacent_ = if self.mode == MovementMode::TrimIn {
			block_previous(self.block.clone())
		} else {
			block_next(self.block.clone())
		};

		// Ignore when trimming the out with no adjacent, because the user must
		// have trimmed the end of the last block in the track
		self.needs_adjacent_ = self.mode == MovementMode::TrimIn || !self.adjacent_.is_null();

		if self.needs_adjacent_ {
			// If we're trimming shorter, we need an adjacent, so check if we have a
			// viable one
			let mut adjacent_kind = 0; // OAKNODE_BLOCK_OTHER
			if !self.adjacent_.is_null() {
				// SAFETY: `adjacent_kind` is a valid out pointer.
				let _ =
					unsafe { oaknode_block_get_kind(self.adjacent_.clone(), &mut adjacent_kind) };
			}
			self.we_created_adjacent_ = self.trim_diff_ > Rational::new(0, 1)
				&& (self.adjacent_.is_null() || (adjacent_kind != 2 && !self.trim_is_a_roll_edit_));

			if self.we_created_adjacent_ {
				// We shortened but don't have a viable adjacent to lengthen, so create
				// one
				// SAFETY: `oaknode_block_gap_create` returns a fresh owned handle.
				self.adjacent_ = unsafe { oaknode_block_gap_create() };
				block_set_length_and_media_out(self.adjacent_.clone(), self.trim_diff_);
				self.adjacent_orphaned_ = true;
			} else {
				// Determine if we're removing the adjacent
				self.we_removed_adjacent_ =
					(block_length(self.adjacent_.clone()) + self.trim_diff_).is_null();
			}
		}
	}

	/// `redo`: apply the trim, creating/removing the adjacent block as needed.
	pub fn redo(&mut self) {
		if self.doing_nothing_ {
			return;
		}

		if self.mode == MovementMode::TrimIn {
			block_set_length_and_media_in(self.block.clone(), self.new_length);
		} else {
			block_set_length_and_media_out(self.block.clone(), self.new_length);
		}

		if self.needs_adjacent_ {
			if self.we_created_adjacent_ {
				// Add the adjacent and insert it
				block_add_to_graph(self.adjacent_.clone(), self.track.clone());
				if self.mode == MovementMode::TrimIn {
					track_insert_block_before(
						self.track.clone(),
						self.adjacent_.clone(),
						self.block.clone(),
					);
				} else {
					// SAFETY: `track`/`adjacent_`/`block` are valid handles.
					let _ = unsafe {
						oaknode_track_insert_block_after(
							self.track.clone(),
							self.adjacent_.clone(),
							self.block.clone(),
						)
					};
				}
				self.adjacent_orphaned_ = false;
			} else if self.we_removed_adjacent_ {
				// SAFETY: valid handles.
				let _ = unsafe {
					oaknode_track_ripple_remove_block(self.track.clone(), self.adjacent_.clone())
				};

				// It no longer inputs/outputs anything, remove it
				if self.remove_block_from_graph_ && block_can_be_removed(self.adjacent_.clone()) {
					if self.deleted_adjacent_command_.is_null() {
						self.deleted_adjacent_command_ =
							create_and_run_block_remove_command(self.adjacent_.clone());
					} else {
						// SAFETY: `deleted_adjacent_command_` is a valid command handle.
						let _ = unsafe {
							oakundo_command_redo_now(self.deleted_adjacent_command_.clone())
						};
					}
				}
			} else {
				let adjacent_length = block_length(self.adjacent_.clone()) + self.trim_diff_;

				if self.mode == MovementMode::TrimIn {
					block_set_length_and_media_out(self.adjacent_.clone(), adjacent_length);
				} else {
					block_set_length_and_media_in(self.adjacent_.clone(), adjacent_length);
				}
			}
		}
	}

	/// `undo`: revert the trim and any adjacent-block changes.
	pub fn undo(&mut self) {
		if self.doing_nothing_ {
			return;
		}

		// `trim_diff_` is positive when trimming shorter, negative when trimming
		// longer
		if self.needs_adjacent_ {
			if self.we_created_adjacent_ {
				// The adjacent is ours, just delete it
				// SAFETY: valid handles.
				let _ = unsafe {
					oaknode_track_ripple_remove_block(self.track.clone(), self.adjacent_.clone())
				};
				block_remove_from_graph(self.adjacent_.clone(), self.track.clone());
				self.adjacent_orphaned_ = true;
			} else {
				if self.we_removed_adjacent_ {
					if !self.deleted_adjacent_command_.is_null() {
						// We deleted the adjacent, restore it now
						// SAFETY: `deleted_adjacent_command_` is a valid command handle.
						let _ = unsafe {
							oakundo_command_undo_now(self.deleted_adjacent_command_.clone())
						};
					}

					if self.mode == MovementMode::TrimIn {
						track_insert_block_before(
							self.track.clone(),
							self.adjacent_.clone(),
							self.block.clone(),
						);
					} else {
						// SAFETY: valid handles.
						let _ = unsafe {
							oaknode_track_insert_block_after(
								self.track.clone(),
								self.adjacent_.clone(),
								self.block.clone(),
							)
						};
					}
				} else {
					let adjacent_length = block_length(self.adjacent_.clone()) - self.trim_diff_;

					if self.mode == MovementMode::TrimIn {
						block_set_length_and_media_out(self.adjacent_.clone(), adjacent_length);
					} else {
						block_set_length_and_media_in(self.adjacent_.clone(), adjacent_length);
					}
				}
			}
		}

		if self.mode == MovementMode::TrimIn {
			block_set_length_and_media_in(self.block.clone(), self.old_length_);
		} else {
			block_set_length_and_media_out(self.block.clone(), self.old_length_);
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for BlockTrimCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `TrackSlideCommand` — slide a set of blocks along a track by `movement`,
/// trimming or inserting the in/out adjacent blocks to compensate
/// (timelineundopointer.h).
pub struct TrackSlideCommand {
	/// Owning track (`track_`).
	track: CHandle,
	/// Blocks to slide (`blocks_`; non-empty for a valid command).
	blocks: Vec<CHandle>,
	/// Signed movement (`movement_`).
	movement: Rational,
	/// Whether `prepare` created the in adjacent (`we_created_in_adjacent_`).
	we_created_in_adjacent_: bool,
	/// Whether the slide removes the in adjacent (`we_removed_in_adjacent_`).
	we_removed_in_adjacent_: bool,
	/// Block adjacent on the in side (`in_adjacent_`).
	in_adjacent: CHandle,
	/// Graph-removal command for a removed in adjacent (`in_adjacent_remove_command_`).
	///
	/// Owns its handle like the C++ `OakUndoCommand`; `CHandle` is refcounted
	/// and drops without a destructor, so releasing it is implicit.
	in_adjacent_remove_command_: CHandle,
	/// Whether `in_adjacent_` is a created gap still detached from the graph
	/// (`in_adjacent_orphaned_`); a detached handle is owned by this command.
	in_adjacent_orphaned_: bool,
	/// Whether `prepare` created the out adjacent (`we_created_out_adjacent_`).
	we_created_out_adjacent_: bool,
	/// Whether the slide removes the out adjacent (`we_removed_out_adjacent_`).
	we_removed_out_adjacent_: bool,
	/// Block adjacent on the out side (`out_adjacent_`).
	out_adjacent: CHandle,
	/// Graph-removal command for a removed out adjacent (`out_adjacent_remove_command_`).
	out_adjacent_remove_command_: CHandle,
	/// Whether `out_adjacent_` is a created gap still detached from the graph
	/// (`out_adjacent_orphaned_`); a detached handle is owned by this command.
	out_adjacent_orphaned_: bool,
}

impl TrackSlideCommand {
	/// Construct from track + moving blocks + in/out adjacent blocks + movement.
	pub fn new(
		track: CHandle,
		blocks: Vec<CHandle>,
		in_adjacent: CHandle,
		out_adjacent: CHandle,
		movement: Rational,
	) -> Self {
		Self {
			track,
			blocks,
			movement,
			we_created_in_adjacent_: false,
			we_removed_in_adjacent_: false,
			in_adjacent,
			in_adjacent_remove_command_: CHandle::null(),
			in_adjacent_orphaned_: false,
			we_created_out_adjacent_: false,
			we_removed_out_adjacent_: false,
			out_adjacent,
			out_adjacent_remove_command_: CHandle::null(),
			out_adjacent_orphaned_: false,
		}
	}

	/// `prepare`: capture adjacent state.
	pub fn prepare(&mut self) {
		if self.in_adjacent.is_null() {
			// SAFETY: `oaknode_block_gap_create` returns a fresh owned handle.
			self.in_adjacent = unsafe { oaknode_block_gap_create() };
			block_set_length_and_media_out(self.in_adjacent.clone(), self.movement);
			self.in_adjacent_orphaned_ = true;
			self.we_created_in_adjacent_ = true;
		} else {
			self.we_created_in_adjacent_ = false;
		}

		if self.out_adjacent.is_null()
			&& !block_next(self.blocks[self.blocks.len() - 1].clone()).is_null()
		{
			// SAFETY: `oaknode_block_gap_create` returns a fresh owned handle.
			self.out_adjacent = unsafe { oaknode_block_gap_create() };
			block_set_length_and_media_out(
				self.out_adjacent.clone(),
				Rational::new(0, 1) - self.movement,
			);
			self.out_adjacent_orphaned_ = true;
			self.we_created_out_adjacent_ = true;
		} else {
			self.we_created_out_adjacent_ = false;
		}
	}

	/// `redo`: apply the slide.
	pub fn redo(&mut self) {
		// We will always have an in adjacent if there was a valid slide
		if self.we_created_in_adjacent_ {
			// We created the in adjacent, so all we have to do is insert it
			block_add_to_graph(self.in_adjacent.clone(), self.track.clone());
			// `blocks` is non-empty when the command was constructed validly
			track_insert_block_before(
				self.track.clone(),
				self.in_adjacent.clone(),
				self.blocks[0].clone(),
			);
			self.in_adjacent_orphaned_ = false;
		} else if Rational::new(0, 1) - self.movement == block_length(self.in_adjacent.clone()) {
			// Movement will remove the in adjacent
			// SAFETY: valid handles.
			let _ = unsafe {
				oaknode_track_ripple_remove_block(self.track.clone(), self.in_adjacent.clone())
			};

			if block_can_be_removed(self.in_adjacent.clone()) {
				if self.in_adjacent_remove_command_.is_null() {
					self.in_adjacent_remove_command_ =
						create_block_remove_command(self.in_adjacent.clone());
				}

				// SAFETY: `in_adjacent_remove_command_` is a valid command handle.
				let _ =
					unsafe { oakundo_command_redo_now(self.in_adjacent_remove_command_.clone()) };
			}

			self.we_removed_in_adjacent_ = true;
		} else {
			// Simply resize the adjacent
			block_set_length_and_media_out(
				self.in_adjacent.clone(),
				block_length(self.in_adjacent.clone()) + self.movement,
			);
		}

		// We may not have an out adjacent if the slide was at the end of the track
		if !self.out_adjacent.is_null() {
			if self.we_created_out_adjacent_ {
				// We created the out adjacent, so we just have to insert it
				block_add_to_graph(self.out_adjacent.clone(), self.track.clone());
				// SAFETY: valid handles.
				let _ = unsafe {
					oaknode_track_insert_block_after(
						self.track.clone(),
						self.out_adjacent.clone(),
						self.blocks[self.blocks.len() - 1].clone(),
					)
				};
				self.out_adjacent_orphaned_ = false;
			} else if self.movement == block_length(self.out_adjacent.clone()) {
				// Movement will remove the out adjacent
				// SAFETY: valid handles.
				let _ = unsafe {
					oaknode_track_ripple_remove_block(self.track.clone(), self.out_adjacent.clone())
				};

				if block_can_be_removed(self.out_adjacent.clone()) {
					if self.out_adjacent_remove_command_.is_null() {
						self.out_adjacent_remove_command_ =
							create_block_remove_command(self.out_adjacent.clone());
					}

					// SAFETY: `out_adjacent_remove_command_` is a valid command handle.
					let _ = unsafe {
						oakundo_command_redo_now(self.out_adjacent_remove_command_.clone())
					};
				}

				self.we_removed_out_adjacent_ = true;
			} else {
				// Simply resize the adjacent
				block_set_length_and_media_in(
					self.out_adjacent.clone(),
					block_length(self.out_adjacent.clone()) - self.movement,
				);
			}
		}
	}

	/// `undo`: revert the slide.
	pub fn undo(&mut self) {
		if self.we_created_in_adjacent_ {
			// We created this, so we can remove it now
			// SAFETY: valid handles.
			let _ = unsafe {
				oaknode_track_ripple_remove_block(self.track.clone(), self.in_adjacent.clone())
			};
			block_remove_from_graph(self.in_adjacent.clone(), self.track.clone());
			self.in_adjacent_orphaned_ = true;
		} else if self.we_removed_in_adjacent_ {
			if !self.in_adjacent_remove_command_.is_null() {
				// We removed this, so we can restore it now
				// SAFETY: `in_adjacent_remove_command_` is a valid command handle.
				let _ =
					unsafe { oakundo_command_undo_now(self.in_adjacent_remove_command_.clone()) };
			}

			// `blocks` is non-empty when the command was constructed validly
			track_insert_block_before(
				self.track.clone(),
				self.in_adjacent.clone(),
				self.blocks[0].clone(),
			);
		} else {
			// Simply resize the adjacent
			block_set_length_and_media_out(
				self.in_adjacent.clone(),
				block_length(self.in_adjacent.clone()) - self.movement,
			);
		}

		if !self.out_adjacent.is_null() {
			if self.we_created_out_adjacent_ {
				// We created this, so we can remove it now
				// SAFETY: valid handles.
				let _ = unsafe {
					oaknode_track_ripple_remove_block(self.track.clone(), self.out_adjacent.clone())
				};
				block_remove_from_graph(self.out_adjacent.clone(), self.track.clone());
				self.out_adjacent_orphaned_ = true;
			} else if self.we_removed_out_adjacent_ {
				if !self.out_adjacent_remove_command_.is_null() {
					// We removed this, so we can restore it now
					// SAFETY: `out_adjacent_remove_command_` is a valid command handle.
					let _ = unsafe {
						oakundo_command_undo_now(self.out_adjacent_remove_command_.clone())
					};
				}

				// SAFETY: valid handles.
				let _ = unsafe {
					oaknode_track_insert_block_after(
						self.track.clone(),
						self.out_adjacent.clone(),
						self.blocks[self.blocks.len() - 1].clone(),
					)
				};
			} else {
				// Simply resize the adjacent
				block_set_length_and_media_in(
					self.out_adjacent.clone(),
					block_length(self.out_adjacent.clone()) + self.movement,
				);
			}
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackSlideCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `TrackPlaceBlockCommand` — destructively place `block` at `in`, trimming or
/// removing blocks that occupy that area and inserting a gap if the block
/// extends past the end of the sequence (timelineundopointer.h).
pub struct TrackPlaceBlockCommand {
	/// Track list (`timeline_`).
	timeline: CHandle,
	/// Target track index (`track_index_`).
	track_index: i32,
	/// Placement in point (`in_`).
	in_: Rational,
	/// Gap inserted when the block extends past the end of the sequence (`gap_`).
	gap_: CHandle,
	/// Whether `gap_` is detached from the graph (`gap_orphaned_`); a detached
	/// handle is owned by this command.
	gap_orphaned_: bool,
	/// Block to place (C++ `insert_`).
	block: CHandle,
	/// Track-add commands created when the track index is out of range
	/// (`add_track_commands_`).
	add_track_commands_: Vec<TimelineAddTrackCommand>,
	/// Ripple-removal of the area the block occupies (`ripple_remove_command_`).
	ripple_remove_command_: Option<TrackRippleRemoveAreaCommand>,
}

impl TrackPlaceBlockCommand {
	/// Construct from track list + track index + block + in point.
	pub fn new(timeline: CHandle, track_index: i32, block: CHandle, in_: Rational) -> Self {
		Self {
			timeline,
			track_index,
			in_,
			gap_: CHandle::null(),
			gap_orphaned_: false,
			block,
			add_track_commands_: Vec::new(),
			ripple_remove_command_: None,
		}
	}

	/// `redo`: place the block destructively.
	pub fn redo(&mut self) {
		// Determine if we need to add tracks
		let mut track_count = 0;
		// SAFETY: `track_count` is a valid out pointer.
		let _ =
			unsafe { oaknode_tracklist_get_track_count(self.timeline.clone(), &mut track_count) };

		if self.track_index >= track_count {
			if self.add_track_commands_.is_empty() {
				// First redo, create the missing tracks now
				for _ in 0..(self.track_index - track_count + 1) {
					self.add_track_commands_
						.push(TimelineAddTrackCommand::new(self.timeline.clone()));
				}
			}

			for cmd in &mut self.add_track_commands_ {
				cmd.redo();
			}
		}

		let mut track = CHandle::null();
		// SAFETY: `track` is a valid out pointer.
		let _ = unsafe {
			oaknode_tracklist_get_track_at(self.timeline.clone(), self.track_index, &mut track)
		};

		let in_ = self.in_;
		let append = in_ >= track_length(track.clone());

		// Check if the placement location is past the end of the timeline
		if append {
			if in_ > track_length(track.clone()) {
				// If so, insert a gap here. In the module world the gap's in point
				// is stored on the gap (Olive derives positions from the track
				// order), so it must be length-set IN-anchored — an out-anchored
				// `set_length_and_media_out` on the fresh (in = 0) gap would push
				// the in point negative.
				if self.gap_.is_null() {
					// SAFETY: `oaknode_block_gap_create` returns a fresh owned handle.
					self.gap_ = unsafe { oaknode_block_gap_create() };
					block_set_length_and_media_in(
						self.gap_.clone(),
						in_ - track_length(track.clone()),
					);
				}
				block_add_to_graph(self.gap_.clone(), track.clone());
				track_append_block(track.clone(), self.gap_.clone());
				self.gap_orphaned_ = false;
			}

			track_append_block(track, self.block.clone());
		} else {
			// Place the block at this point
			if self.ripple_remove_command_.is_none() {
				let insert_length = block_length(self.block.clone());
				let mut cmd = TrackRippleRemoveAreaCommand::new(
					track.clone(),
					TimeRange::new(in_, in_ + insert_length),
				);
				cmd.set_allow_splitting_gaps(true);
				self.ripple_remove_command_ = Some(cmd);
			}

			let cmd = self.ripple_remove_command_.as_mut().unwrap();
			cmd.prepare();
			cmd.redo();

			// Insert after the block that follows the cleared area (`blocks` is
			// non-empty when the command was constructed validly)
			let index = self
				.ripple_remove_command_
				.as_ref()
				.unwrap()
				.get_insertion_index();
			// SAFETY: valid handles; `index` may be empty when the insertion is at
			// the front of the track, which the ABI tolerates like C++.
			let _ = unsafe { oaknode_track_insert_block_after(track, self.block.clone(), index) };
		}
	}

	/// `undo`: remove the placed block and restore displaced blocks.
	pub fn undo(&mut self) {
		let mut t = CHandle::null();
		// SAFETY: `t` is a valid out pointer.
		let _ = unsafe {
			oaknode_tracklist_get_track_at(self.timeline.clone(), self.track_index, &mut t)
		};

		// Firstly, remove our insert
		// SAFETY: valid handles.
		let _ = unsafe { oaknode_track_ripple_remove_block(t.clone(), self.block.clone()) };

		if self.ripple_remove_command_.is_some() {
			// If we ripple-removed, just undo that
			self.ripple_remove_command_.as_mut().unwrap().undo();
		} else if !self.gap_.is_null() {
			// SAFETY: valid handles.
			let _ = unsafe { oaknode_track_ripple_remove_block(t.clone(), self.gap_.clone()) };
			block_remove_from_graph(self.gap_.clone(), t.clone());
			self.gap_orphaned_ = true;
		}

		// Remove tracks if we added them
		for cmd in self.add_track_commands_.iter_mut().rev() {
			cmd.undo();
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackPlaceBlockCommand {
	/// `Command::redo` — the inherent method takes precedence.
	fn redo(&mut self) {
		self.redo();
	}

	/// `Command::undo` — the inherent method takes precedence.
	fn undo(&mut self) {
		self.undo();
	}
}

/// `TrackMoveBlockCommand` — move `block` within the track list so its in
/// point becomes `in_` (the capi's `oakengine_sequence_move_clip` assembly:
/// the clip's old spot is replaced with a gap, then the clip is placed at the
/// destination; both halves run as ONE undoable entry). The destination is
/// `track_index` in the list, which the caller normally resolves from the
/// block itself — passing a different track index yields a cross-track move.
///
/// Unlike Olive, the module's block in/out points are stored on the block
/// rather than derived from the track order, so the move also rewrites the
/// block's in point (keeping the length; the out follows) on redo and
/// restores it on undo.
pub struct TrackMoveBlockCommand {
	/// Composite of the gap + place halves (`children_`).
	children: MultiUndoCommand,
	/// The block being moved (`block_`).
	block: CHandle,
	/// In point captured at construction (`old_in_`).
	old_in: Rational,
	/// Destination in point (`in_`).
	in_: Rational,
}

impl TrackMoveBlockCommand {
	/// Construct from track list + destination track index + block + in point.
	pub fn new(timeline: CHandle, track_index: i32, block: CHandle, in_: Rational) -> Self {
		let mut children = MultiUndoCommand::new();
		children.add_child(Box::new(TrackReplaceBlockWithGapCommand::new(
			block_track(block.clone()),
			block.clone(),
			true,
		)));
		children.add_child(Box::new(TrackPlaceBlockCommand::new(
			timeline,
			track_index,
			block.clone(),
			in_,
		)));
		Self {
			children,
			block,
			old_in: block_in(block.clone()),
			in_,
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackMoveBlockCommand {
	/// `Command::redo` — the composite runs in child order.
	fn redo(&mut self) {
		// The module stores the block's position on the block itself (no
		// track-order derivation), so re-home it before placing.
		block_set_in(self.block.clone(), self.in_);
		self.children.redo();
	}

	/// `Command::undo` — the composite runs children in reverse.
	fn undo(&mut self) {
		self.children.undo();
		block_set_in(self.block.clone(), self.old_in);
	}
}
