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

//! Ripple commands (`src/timeline/src/timelineundoripple.h`): clearing a time
//! area on one or all tracks, the ripple tool, and gap deletion at regions.
//!
//! Graph mutation goes through the oaknode C ABI (`bridge::node`); command
//! wrapping through `bridge::undo`. The C++ `std::map<..., TrackHandleLess>`
//! containers are modelled as order-preserving `Vec<(CHandle, _)>` pairs;
//! order follows the track handle, matching `TrackHandleLess`.
//!
//! The oaknode queries a `prepare()` needs — locating blocks at a time,
//! detecting gaps, enumerating track lists / sequence tracks and reading the
//! locked flag — go through the matching `bridge::node` entry points, wrapped
//! by the thin private helpers below (each annotated with its `// CPP-PARITY`
//! source line).

use oakcore_rs::{Rational, TimeRange};

use crate::common::MovementMode;
use crate::bridge::node::{
	oaknode_block_gap_create, oaknode_block_get_kind, oaknode_sequence_get_all_track_at,
	oaknode_sequence_get_all_track_count, oaknode_sequence_get_track_list, oaknode_track_get_locked,
	oaknode_track_get_nearest_block_after_or_at, oaknode_track_get_nearest_block_before_or_at,
	oaknode_track_insert_block_after, oaknode_track_prepend_block, oaknode_track_ripple_remove_block,
	oaknode_tracklist_get_track_at, oaknode_tracklist_get_track_count,
};
use crate::bridge::undo::{oakundo_command_redo_now, oakundo_command_undo_now};
use crate::handle::CHandle;
use crate::undocommon::{block_can_be_removed, box_command, create_block_remove_command, free_command_handle, Command};
use crate::util::{
	block_add_to_graph, block_in, block_length, block_next, block_out, block_previous,
	block_remove_from_graph, block_set_length_and_media_in, block_set_length_and_media_out, block_track,
	free_detached_handle, rat_nd,
};

use super::undogeneral::BlockResizeCommand;
use super::undosplit::BlockSplitCommand;
use super::undotrack::TrackRippleRemoveBlockCommand;

/// Copy a handle's token so it can be re-passed by value to a bridge
/// function while the original stays stored. `CHandle` is a `#[repr(C)]` POD
/// without a `Drop`, so the copy is just a struct copy; refcounts are not
/// touched (the bridge only ever re-boxes/re-releases the object it backs).
fn hdup(h: &CHandle) -> CHandle {
	CHandle {
		ctx: h.ctx,
		addref: h.addref,
		release: h.release,
		abi_version: h.abi_version,
	}
}

/// C++ unary `Rational::operator-` (`Rational(num_, -den_)`); the crate's
/// `Rational` has no `Neg`, so negate by subtracting from zero (same idiom as
/// `marker.rs`).
fn rat_neg(r: Rational) -> Rational {
	Rational::new(0, 1) - r
}

// The helpers below mirror the oaknode C ABI queries the C++ `prepare()`
// bodies use. Each wraps a `bridge::node` extern (see the `// CPP-PARITY`
// marker on each).

/// C++ `oaknode_track_get_nearest_block_before_or_at`.
// CPP-PARITY timelineundoripple.cpp:64
fn nearest_block_before_or_at(track: CHandle, t: Rational) -> CHandle {
	let (mut n, mut d) = (0, 0);
	rat_nd(t, &mut n, &mut d);
	let mut out = CHandle::null();
	// SAFETY: `out` is a valid out pointer.
	let _ = unsafe { oaknode_track_get_nearest_block_before_or_at(track, n, d, &mut out) };
	out
}

/// C++ `oaknode_track_get_nearest_block_after_or_at`.
// CPP-PARITY timelineundoripple.cpp:551
fn nearest_block_after_or_at(track: CHandle, t: Rational) -> CHandle {
	let (mut n, mut d) = (0, 0);
	rat_nd(t, &mut n, &mut d);
	let mut out = CHandle::null();
	// SAFETY: `out` is a valid out pointer.
	let _ = unsafe { oaknode_track_get_nearest_block_after_or_at(track, n, d, &mut out) };
	out
}

/// C++ anonymous-namespace `is_gap` (`oaknode_block_get_kind`).
// CPP-PARITY timelineundoripple.cpp:448
fn is_gap(b: CHandle) -> bool {
	let mut kind = 0;
	// SAFETY: `kind` is a valid out pointer.
	if unsafe { oaknode_block_get_kind(b, &mut kind) } != 0 {
		return false;
	}
	kind == 2 // OAKNODE_BLOCK_GAP
}

/// C++ `oaknode_tracklist_get_track_count`.
// CPP-PARITY timelineundoripple.cpp:211
fn track_list_count(list: CHandle) -> usize {
	let mut count = 0;
	// SAFETY: `count` is a valid out pointer.
	let _ = unsafe { oaknode_tracklist_get_track_count(list, &mut count) };
	count as usize
}

/// C++ `oaknode_tracklist_get_track_at`.
// CPP-PARITY timelineundoripple.cpp:215
fn track_list_at(list: CHandle, index: usize) -> CHandle {
	let mut out = CHandle::null();
	// SAFETY: `out` is a valid out pointer.
	let _ = unsafe { oaknode_tracklist_get_track_at(list, index as i32, &mut out) };
	out
}

/// C++ `oaknode_track_get_locked`.
// CPP-PARITY timelineundoripple.cpp:221
fn track_locked(track: CHandle) -> bool {
	let mut locked = 0;
	// SAFETY: `locked` is a valid out pointer.
	let _ = unsafe { oaknode_track_get_locked(track, &mut locked) };
	locked != 0
}

/// C++ `TimelineRippleRemoveAreaCommand` ctor loop over
/// `oaknode_sequence_get_track_list` (one list per track type).
// CPP-PARITY timelineundoripple.cpp:254
fn sequence_track_lists(timeline: CHandle) -> Vec<CHandle> {
	// OAKNODE_TRACK_TYPE_VIDEO / AUDIO / SUBTITLE.
	let mut lists = Vec::new();
	for t in [0, 1, 2] {
		let mut out = CHandle::null();
		// SAFETY: `out` is a valid out pointer.
		let _ = unsafe { oaknode_sequence_get_track_list(hdup(&timeline), t, &mut out) };
		if !out.is_null() {
			lists.push(out);
		}
	}
	lists
}

/// C++ `oaknode_sequence_get_all_track_count` / `get_all_track_at`.
// CPP-PARITY timelineundoripple.cpp:523
fn sequence_all_tracks(timeline: CHandle) -> Vec<CHandle> {
	let mut count = 0;
	// SAFETY: `count` is a valid out pointer.
	let _ = unsafe { oaknode_sequence_get_all_track_count(hdup(&timeline), &mut count) };
	let mut tracks = Vec::new();
	for i in 0..count {
		let mut out = CHandle::null();
		// SAFETY: `out` is a valid out pointer.
		let _ = unsafe { oaknode_sequence_get_all_track_at(hdup(&timeline), i, &mut out) };
		if !out.is_null() {
			tracks.push(out);
		}
	}
	tracks
}

/// `TrackRippleRemoveAreaCommand` — clear the area between `range.in` and
/// `range.out` on a single track (timelineundoripple.h). Blocks are trimmed and
/// removed; subsequent blocks push backward unless a block is inserted at the
/// in point (see `get_insertion_index`).
///
/// `track_`/`range_` are the fixed inputs; `prepare()` derives the per-block
/// trim/remove/splice operations. `redo()`/`undo()` are self-contained against
/// that derived state.
pub struct TrackRippleRemoveAreaCommand {
	/// Owning track.
	track: CHandle,
	/// Area to clear.
	range: TimeRange,
	/// Out-point trim on the first block (`timelineundoripple.h` `trim_out_`).
	trim_out_: Option<TrimOperation>,
	/// Blocks fully inside the range to remove (`removals_`).
	removals_: Vec<RemoveOperation>,
	/// In-point trim on the trailing block (`trim_in_`).
	trim_in_: Option<TrimOperation>,
	/// Block any replacement should be inserted after (`insert_previous_`).
	insert_previous_: CHandle,
	/// Whether a gap spanning the range may be split (`allow_splitting_gaps_`).
	allow_splitting_gaps_: bool,
	/// Split command used when the range cuts through a block (`splice_split_command_`).
	splice_split_command_: Option<BlockSplitCommand>,
	/// Node-remove sub-commands for removed blocks (`remove_block_commands_`).
	remove_block_commands_: Vec<CHandle>,
}

/// One block trimmed at its out or in edge (C++ `TrimOperation`).
struct TrimOperation {
	/// The trimmed block.
	block: CHandle,
	/// Length before the trim.
	old_length: Rational,
	/// Length after the trim.
	new_length: Rational,
}

/// One block removed and the predecessor it should be re-inserted after
/// (C++ `RemoveOperation`).
struct RemoveOperation {
	/// The removed block.
	block: CHandle,
	/// Predecessor to re-insert after on undo.
	before: CHandle,
}

impl TrackRippleRemoveAreaCommand {
	/// Construct from track + range.
	pub fn new(track: CHandle, range: TimeRange) -> Self {
		Self {
			track,
			range,
			trim_out_: None,
			removals_: Vec::new(),
			trim_in_: None,
			insert_previous_: CHandle::null(),
			allow_splitting_gaps_: false,
			splice_split_command_: None,
			remove_block_commands_: Vec::new(),
		}
	}

	/// The block that will follow the cleared area, for inserting a replacement
	/// (`get_insertion_index`); the empty handle when not applicable.
	pub fn get_insertion_index(&self) -> CHandle {
		hdup(&self.insert_previous_)
	}

	/// The block produced by the splice split, if the range split a block
	/// (`get_spliced_block`); the empty handle when nothing was split.
	pub fn get_spliced_block(&self) -> CHandle {
		match &self.splice_split_command_ {
			Some(cmd) => cmd.new_block(),
			None => CHandle::null(),
		}
	}

	/// Whether gaps inside the range may be split instead of removed outright
	/// (`set_allow_splitting_gaps`).
	pub fn set_allow_splitting_gaps(&mut self, e: bool) {
		self.allow_splitting_gaps_ = e;
	}

	/// `prepare`: compute the trim/remove operations for the range.
	///
	/// Mirrors the C++ algorithm; the leading block lookup requires
	/// `oaknode_track_get_nearest_block_before_or_at`, which the bridge does
	/// not yet expose, so this currently finds no block and no-ops (see the
	/// module note).
	pub fn prepare(&mut self) {
		let track = hdup(&self.track);
		let in_ = self.range.in_();
		let out = self.range.out();

		// CPP-PARITY timelineundoripple.cpp:57-64
		let first_block = nearest_block_before_or_at(track, in_);
		if first_block.is_null() {
			return;
		}
		let fb = hdup(&first_block);

		// Determine if this first block is getting trimmed or removed
		let first_block_is_out_trimmed = block_in(hdup(&fb)) < in_;
		let first_block_is_in_trimmed = block_out(hdup(&fb)) > out;

		// Set's the block that any insert command should insert AFTER.
		self.insert_previous_ = if first_block_is_out_trimmed {
			hdup(&fb)
		} else {
			block_previous(hdup(&fb))
		};

		// If it's getting trimmed, determine if it's actually getting spliced
		if first_block_is_out_trimmed && first_block_is_in_trimmed {
			if !self.allow_splitting_gaps_ && is_gap(hdup(&fb)) {
				// As a rule, we don't split gaps, so we just treat it as a
				// trim of the range requested
				self.trim_out_ = Some(TrimOperation {
					block: hdup(&fb),
					old_length: block_length(hdup(&fb)),
					new_length: block_length(hdup(&fb)) - self.range.length(),
				});
			} else {
				// This block is getting spliced, so we'll handle that later
				self.splice_split_command_ =
					Some(BlockSplitCommand::new(hdup(&fb), self.range.in_()));
			}
		} else {
			// It's just getting trimmed or removed, so we'll append that operation
			if first_block_is_out_trimmed {
				self.trim_out_ = Some(TrimOperation {
					block: hdup(&fb),
					old_length: block_length(hdup(&fb)),
					new_length: block_length(hdup(&fb)) - (block_out(hdup(&fb)) - in_),
				});
			} else if first_block_is_in_trimmed {
				self.trim_in_ = Some(TrimOperation {
					block: hdup(&fb),
					old_length: block_length(hdup(&fb)),
					new_length: block_length(hdup(&fb)) - (out - block_in(hdup(&fb))),
				});
			} else {
				// We know for sure this block is within the range so it will be removed
				self.removals_.push(RemoveOperation {
					block: hdup(&fb),
					before: block_previous(hdup(&fb)),
				});
			}

			// If the first block is getting in trimmed, we're already at the
			// end of our range
			if !first_block_is_in_trimmed {
				let mut next = block_next(hdup(&fb));
				while !next.is_null() {
					let nx = hdup(&next);
					let trimming = block_out(hdup(&nx)) > out;

					if trimming {
						self.trim_in_ = Some(TrimOperation {
							block: hdup(&nx),
							old_length: block_length(hdup(&nx)),
							new_length: block_length(hdup(&nx)) - (out - block_in(hdup(&nx))),
						});
						break;
					} else {
						self.removals_.push(RemoveOperation {
							block: hdup(&nx),
							before: block_previous(hdup(&nx)),
						});

						if block_out(hdup(&nx)) == out {
							break;
						}
					}

					next = block_next(hdup(&nx));
				}
			}
		}
	}

	/// `redo`: apply the ripple removal.
	pub fn redo(&mut self) {
		if self.splice_split_command_.is_some() {
			// We're just splicing (C++ `redo_now` = prepare + redo)
			let cmd = self.splice_split_command_.as_mut().unwrap();
			cmd.prepare();
			cmd.redo();

			// Trim the in of the split
			let split = cmd.new_block();
			if !split.is_null() {
				let new_len = block_length(split.clone()) - (self.range.out() - block_in(split.clone()));
				block_set_length_and_media_in(split, new_len);
			}
		} else {
			if let Some(t) = &self.trim_out_ {
				block_set_length_and_media_out(hdup(&t.block), t.new_length);
			}

			if let Some(t) = &self.trim_in_ {
				block_set_length_and_media_in(hdup(&t.block), t.new_length);
			}

			// Perform removals
			if !self.removals_.is_empty() {
				let track = hdup(&self.track);
				for op in &self.removals_ {
					// Ripple remove them all first
					let _ = unsafe { oaknode_track_ripple_remove_block(track.clone(), hdup(&op.block)) };
				}

				// Create undo commands for node removals where possible
				if self.remove_block_commands_.is_empty() {
					for op in &self.removals_ {
						if block_can_be_removed(hdup(&op.block)) {
							self.remove_block_commands_
								.push(create_block_remove_command(hdup(&op.block)));
						}
					}
				}

				for i in 0..self.remove_block_commands_.len() {
					let c = hdup(&self.remove_block_commands_[i]);
					let _ = unsafe { oakundo_command_redo_now(c) };
				}
			}
		}
	}

	/// `undo`: restore the removed/trimmed blocks.
	pub fn undo(&mut self) {
		if self.splice_split_command_.is_some() {
			let cmd = self.splice_split_command_.as_mut().unwrap();
			cmd.undo();
		} else {
			if let Some(t) = &self.trim_out_ {
				block_set_length_and_media_out(hdup(&t.block), t.old_length);
			}

			if let Some(t) = &self.trim_in_ {
				block_set_length_and_media_in(hdup(&t.block), t.old_length);
			}

			// Un-remove any blocks
			for i in (0..self.remove_block_commands_.len()).rev() {
				let c = hdup(&self.remove_block_commands_[i]);
				let _ = unsafe { oakundo_command_undo_now(c) };
			}

			let track = hdup(&self.track);
			for op in &self.removals_ {
				let _ = unsafe {
					oaknode_track_insert_block_after(track.clone(), hdup(&op.block), hdup(&op.before))
				};
			}
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackRippleRemoveAreaCommand {
	fn redo(&mut self) {
		self.redo()
	}

	fn undo(&mut self) {
		self.undo()
	}
}

impl Drop for TrackRippleRemoveAreaCommand {
	fn drop(&mut self) {
		// C++ dtor frees every remove-block command handle.
		for i in 0..self.remove_block_commands_.len() {
			free_command_handle(&mut self.remove_block_commands_[i]);
		}
		// `splice_split_command_` (a plain `BlockSplitCommand`) is dropped by
		// the field's own `Drop`.
	}
}

/// `TrackListRippleRemoveAreaCommand` — clear a time area across every track
/// in a list (timelineundoripple.h). Composes a `TrackRippleRemoveAreaCommand`
/// per track.
pub struct TrackListRippleRemoveAreaCommand {
	/// Track list.
	list: CHandle,
	/// Area to clear.
	range: TimeRange,
	/// The tracks actually worked on (`working_tracks_`).
	working_tracks_: Vec<CHandle>,
	/// Per-track child commands (`commands_`).
	commands_: Vec<TrackRippleRemoveAreaCommand>,
}

impl TrackListRippleRemoveAreaCommand {
	/// Construct from track list + in + out points.
	pub fn new(list: CHandle, in_: Rational, out: Rational) -> Self {
		Self {
			list,
			range: TimeRange::new(in_, out),
			working_tracks_: Vec::new(),
			commands_: Vec::new(),
		}
	}

	/// `prepare`: build the per-track commands.
	///
	/// Requires the track-list enumeration and locked-flag queries the bridge
	/// does not yet expose, so this currently produces no child commands (see
	/// the module note).
	pub fn prepare(&mut self) {
		self.working_tracks_.clear();
		self.commands_.clear();

		let list = hdup(&self.list);
		let count = track_list_count(list.clone());
		for i in 0..count {
			let track = track_list_at(list.clone(), i);
			if track.is_null() {
				continue;
			}

			if track_locked(hdup(&track)) {
				continue;
			}

			self.commands_
				.push(TrackRippleRemoveAreaCommand::new(hdup(&track), self.range));
			self.working_tracks_.push(hdup(&track));
		}
	}

	/// `redo`: apply every per-track command.
	pub fn redo(&mut self) {
		for cmd in &mut self.commands_ {
			// C++ `redo_now` = prepare + redo.
			cmd.prepare();
			cmd.redo();
		}
	}

	/// `undo`: revert every per-track command.
	pub fn undo(&mut self) {
		for cmd in &mut self.commands_ {
			cmd.undo();
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TrackListRippleRemoveAreaCommand {
	fn redo(&mut self) {
		self.redo()
	}

	fn undo(&mut self) {
		self.undo()
	}
}

/// `TimelineRippleRemoveAreaCommand` — clear a time area across the whole
/// timeline (timelineundoripple.h). A multi-command that drives a
/// `TrackListRippleRemoveAreaCommand` per track list.
pub struct TimelineRippleRemoveAreaCommand {
	/// Owning sequence.
	timeline: CHandle,
	/// Area to clear.
	range: TimeRange,
	/// Per-track-list children (C++ `MultiUndoCommand` children).
	commands_: Vec<TrackListRippleRemoveAreaCommand>,
}

impl TimelineRippleRemoveAreaCommand {
	/// Construct from sequence + in + out points.
	///
	/// The C++ ctor walks the sequence's track lists via
	/// `oaknode_sequence_get_track_list` (one per track type) and creates a
	/// `TrackListRippleRemoveAreaCommand` for each; that query is not yet in
	/// `bridge::node`, so no children are created until it is (see the module
	/// note).
	pub fn new(timeline: CHandle, in_: Rational, out: Rational) -> Self {
		let range = TimeRange::new(in_, out);

		let mut commands_ = Vec::new();
		for list in sequence_track_lists(hdup(&timeline)) {
			if !list.is_null() {
				commands_
					.push(TrackListRippleRemoveAreaCommand::new(hdup(&list), in_, out));
			}
		}

		Self {
			timeline,
			range,
			commands_,
		}
	}

	/// `redo`: apply the ripple removal.
	pub fn redo(&mut self) {
		for cmd in &mut self.commands_ {
			cmd.prepare();
			cmd.redo();
		}
	}

	/// `undo`: revert the ripple removal.
	pub fn undo(&mut self) {
		for cmd in &mut self.commands_ {
			cmd.undo();
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TimelineRippleRemoveAreaCommand {
	fn redo(&mut self) {
		self.redo()
	}

	fn undo(&mut self) {
		self.undo()
	}
}

/// `TrackListRippleToolCommand::RippleInfo` — which block to ripple and
/// whether a trailing gap should be appended (timelineundoripple.h).
pub struct RippleInfo {
  /// Block to ripple.
  block: CHandle,
  /// Whether to append a gap after the ripple.
  append_gap: bool,
}

/// `TrackListRippleToolCommand` — ripple-tool edit: shift blocks on the listed
/// tracks by `ripple_movement` (timelineundoripple.h).
pub struct TrackListRippleToolCommand {
  /// Track list.
  track_list: CHandle,
  /// Per-track ripple info, ordered by track handle (TrackHandleLess parity).
  info: Vec<(CHandle, RippleInfo)>,
  /// Signed ripple movement.
  ripple_movement: Rational,
  /// Movement mode.
  movement_mode: MovementMode,
  /// Per-track working data persisting across redo/undo (`working_data_`).
  working_data_: Vec<(CHandle, WorkingData)>,
}

impl TrackListRippleToolCommand {
  /// Construct from track list + per-track info + movement + mode.
  pub fn new(
    track_list: CHandle,
    info: Vec<(CHandle, RippleInfo)>,
    ripple_movement: Rational,
    movement_mode: MovementMode,
  ) -> Self {
    Self {
      track_list,
      info,
      ripple_movement,
      movement_mode,
      working_data_: Vec::new(),
    }
  }

  /// `redo`: ripple forward.
  pub fn redo(&mut self) {
    self.ripple(true);
  }

  /// `undo`: ripple backward.
  pub fn undo(&mut self) {
    self.ripple(false);
  }

  /// Apply the movement in the given direction (`TrackListRippleToolCommand::ripple`,
  /// timelineundoripple.cpp:292).
  fn ripple(&mut self, redo: bool) {
    if self.info.is_empty() {
      return;
    }

    // C++ accumulates `pre_latest_out`/`post_latest_out` (per block) into
    // `pre_latest_out`/`post_latest_out` for cache invalidation, but never
    // reads them afterwards; they are omitted here as dead code.

    for (track, info) in &self.info {
      let track_copy = hdup(track);
      let b = hdup(&info.block);

      // Generate block length
      let mut operation_movement = self.ripple_movement;

      if self.movement_mode == MovementMode::TrimIn {
        operation_movement = rat_neg(operation_movement);
      }

      if !redo {
        operation_movement = rat_neg(operation_movement);
      }

      let mut new_block_length = Rational::NULL;
      if !b.is_null() {
        new_block_length = block_length(hdup(&b)) + operation_movement;
      }

      // Fetch or default-construct this track's working data (C++ map
      // `working_data_[track]`, persisted across redo/undo).
      let wd_index = match self
        .working_data_
        .iter()
        .position(|(t, _)| t.ctx == track_copy.ctx)
      {
        Some(i) => i,
        None => {
          self.working_data_.push((hdup(&track_copy), WorkingData::default()));
          self.working_data_.len() - 1
        }
      };
      let wd = &mut self.working_data_[wd_index].1;

      if info.append_gap {
        // Rather than rippling the referenced block, we'll insert a gap and
        // ripple with that
        let mut gap = hdup(&wd.created_gap);

        if redo {
          if gap.is_null() {
            // SAFETY: `oaknode_block_gap_create` returns a fresh owned handle.
            gap = unsafe { oaknode_block_gap_create() };
            block_set_length_and_media_out(
              hdup(&gap),
              if self.ripple_movement < Rational::new(0, 1) {
                rat_neg(self.ripple_movement)
              } else {
                self.ripple_movement
              },
            );
            wd.created_gap = hdup(&gap);
            wd.created_gap_orphaned = true;
          }

          block_add_to_graph(hdup(&gap), hdup(&track_copy));

          // C++ `oaknode_track_insert_block_before(track, gap, b)`; the bridge
          // has no such entry point, so insert after `b`'s predecessor instead
          // (prepend when `b` is the first block).
          let before = block_previous(hdup(&b));
          if before.is_null() {
            // SAFETY: valid handles.
            let _ = unsafe { oaknode_track_prepend_block(hdup(&track_copy), hdup(&gap)) };
          } else {
            // SAFETY: valid handles.
            let _ = unsafe { oaknode_track_insert_block_after(hdup(&track_copy), hdup(&gap), before) };
          }
          wd.created_gap_orphaned = false;

          // As an insertion, the earliest change is at the gap's in point
          wd.earliest_point_of_change = block_in(hdup(&gap));
        } else {
          // SAFETY: valid handles.
          let _ = unsafe { oaknode_track_ripple_remove_block(hdup(&track_copy), hdup(&gap)) };
          block_remove_from_graph(hdup(&gap), hdup(&track_copy));
          wd.created_gap_orphaned = true;
        }
      } else if (redo && new_block_length.is_null())
        || (!redo && block_track(hdup(&b)).is_null())
      {
        // The ripple is the length of this block. We assume that for this to
        // happen, it must have been a gap that we will now remove.
        if redo {
          // The earliest point changes will happen is at the start of this block
          wd.earliest_point_of_change = block_in(hdup(&b));

          // Remove gap from track and from graph
          wd.removed_gap = hdup(&b);
          wd.removed_gap_after = block_previous(hdup(&b));
          // SAFETY: valid handles.
          let _ = unsafe { oaknode_track_ripple_remove_block(hdup(&track_copy), hdup(&b)) };
          block_remove_from_graph(hdup(&b), hdup(&track_copy));
          wd.removed_gap_orphaned = true;
        } else {
          // Restore gap to graph and track
          block_add_to_graph(hdup(&b), hdup(&track_copy));
          // SAFETY: valid handles; `removed_gap_after` may be empty when the
          // gap was first on the track, which the ABI tolerates like C++.
          let _ = unsafe {
            oaknode_track_insert_block_after(hdup(&track_copy), hdup(&b), hdup(&wd.removed_gap_after))
          };
          wd.removed_gap_orphaned = false;

          // The earliest point changes will happen is at the start of this block
          wd.earliest_point_of_change = block_in(hdup(&b));
        }
      } else {
        // Store old length
        wd.old_length = block_length(hdup(&b));

        if self.movement_mode == MovementMode::TrimIn {
          // The earliest point changes will occur is in point of this block
          wd.earliest_point_of_change = block_in(hdup(&b));

          // Update length
          block_set_length_and_media_in(hdup(&b), new_block_length);
        } else {
          // The earliest point changes will occur is the out point if trimming
          // out or the in point if trimming in
          wd.earliest_point_of_change = block_out(hdup(&b));

          // Update length
          block_set_length_and_media_out(hdup(&b), new_block_length);
        }
      }
    }
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Command for TrackListRippleToolCommand {
  fn redo(&mut self) {
    self.redo()
  }

  fn undo(&mut self) {
    self.undo()
  }
}

impl Drop for TrackListRippleToolCommand {
  fn drop(&mut self) {
    // C++ dtor: free any gaps still detached from the graph
    // (timelineundoripple.cpp:278).
    for (_, wd) in &mut self.working_data_ {
      if wd.created_gap_orphaned {
        free_detached_handle(&mut wd.created_gap);
      }
      if wd.removed_gap_orphaned {
        free_detached_handle(&mut wd.removed_gap);
      }
    }
  }
}

/// `TrackListRippleToolCommand::WorkingData` — per-track state computed during
/// a ripple edit (timelineundoripple.h).
pub struct WorkingData {
  /// Gap created by the ripple.
  created_gap: CHandle,
  /// Whether `created_gap` is detached from the graph (owned by this command).
  created_gap_orphaned: bool,
  /// Gap removed by the ripple.
  removed_gap: CHandle,
  /// Whether `removed_gap` is detached from the graph (owned by this command).
  removed_gap_orphaned: bool,
  /// Block that follows the removed gap.
  removed_gap_after: CHandle,
  /// Track length before the edit.
  old_length: Rational,
  /// Earliest point affected on the track.
  earliest_point_of_change: Rational,
}

impl Default for WorkingData {
  /// C++ default ctor: null block handles, false flags, NULL rationals.
  fn default() -> Self {
    Self {
      created_gap: CHandle::null(),
      created_gap_orphaned: false,
      removed_gap: CHandle::null(),
      removed_gap_orphaned: false,
      removed_gap_after: CHandle::null(),
      old_length: Rational::NULL,
      earliest_point_of_change: Rational::NULL,
    }
  }
}

/// `TimelineRippleDeleteGapsAtRegionsCommand` — delete gaps located at the
/// given (track, range) regions (timelineundoripple.h).
pub struct TimelineRippleDeleteGapsAtRegionsCommand {
	/// Owning sequence.
	timeline: CHandle,
	/// Regions to clear, ordered by track handle.
	regions: Vec<(CHandle, TimeRange)>,
	/// Gap-removal/resize sub-command handles (`commands_`).
	commands_: Vec<CHandle>,
}

/// One requested (track, range) region resolved to a gap block (C++
/// `RemovalRequest`).
struct RemovalRequest {
	/// The gap block to remove or resize.
	gap: CHandle,
	/// The region it must clear.
	range: TimeRange,
}

impl TimelineRippleDeleteGapsAtRegionsCommand {
	/// Construct from sequence + regions.
	pub fn new(timeline: CHandle, regions: Vec<(CHandle, TimeRange)>) -> Self {
		Self {
			timeline,
			regions,
			commands_: Vec::new(),
		}
	}

	/// Whether `prepare` produced any work (`has_commands`).
	pub fn has_commands(&self) -> bool {
		!self.commands_.is_empty()
	}

	/// `prepare`: build the per-region gap-removal commands.
	///
	/// Requires the gap-kind, nearest-block, sequence-track and locked-flag
	/// queries the bridge does not yet expose, so it currently produces no
	/// commands (see the module note). The algorithm below mirrors the C++.
	pub fn prepare(&mut self) {
		self.commands_.clear();

		let mut max_gaps = 0usize;
		let mut requested_gaps: Vec<(CHandle, Vec<RemovalRequest>)> = Vec::new();

		// Convert regions to gaps
		for (track, range) in &self.regions {
			let track_copy = hdup(track);
			let block = nearest_block_before_or_at(track_copy.clone(), range.in_());

			if is_gap(hdup(&block)) {
				let index = requested_gaps
					.iter()
					.position(|(t, _)| t.ctx == track_copy.ctx)
					.unwrap_or_else(|| {
						requested_gaps.push((hdup(&track_copy), Vec::new()));
						requested_gaps.len() - 1
					});

				let gaps_on_track = &mut requested_gaps[index].1;
				let this_req = RemovalRequest {
					gap: hdup(&block),
					range: *range,
				};

				// Insertion sort
				let mut insert_at = None;
				for i in 0..gaps_on_track.len() {
					if gaps_on_track[i].range.in_() < range.in_() {
						insert_at = Some(i);
						break;
					}
				}
				match insert_at {
					Some(i) => gaps_on_track.insert(i, this_req),
					None => gaps_on_track.push(this_req),
				}

				max_gaps = max_gaps.max(gaps_on_track.len());
			}
			// C++ prints "Failed to find corresponding gap to region" to
			// stderr here; we simply skip the region.
		}

		// For each gap on each track, find a corresponding gap on every other
		// track (which may include a requested gap) to ripple in order to keep
		// everything synchronized
		let mut gap_lengths: Vec<(CHandle, Rational)> = Vec::new();
		for gap_index in 0..max_gaps {
			let mut earliest_point = Rational::new(2147483647, 1); // RATIONAL_MAX
			let mut ripple_length = Rational::new(2147483647, 1); // RATIONAL_MAX
			let mut latest_point = Rational::new(-2147483647, 1); // RATIONAL_MIN

			for (_track, gaps_on_track) in &requested_gaps {
				if gap_index < gaps_on_track.len() {
					let gap = &gaps_on_track[gap_index];
					earliest_point = std::cmp::min(earliest_point, gap.range.in_());
					ripple_length = std::cmp::min(ripple_length, gap.range.length());
					latest_point = std::cmp::max(latest_point, gap.range.out());
				}
			}

			// Determine which gaps will be involved in this operation
			let mut gaps: Vec<CHandle> = Vec::new();

			for track in sequence_all_tracks(hdup(&self.timeline)) {
				if track.is_null() {
					continue;
				}
				if track_locked(hdup(&track)) {
					continue;
				}

				let mut gap = CHandle::null();
				let requested_here = requested_gaps.iter().find(|(t, _)| t.ctx == track.ctx);
				if let Some((_, requested_on_track)) = requested_here {
					if gap_index < requested_on_track.len() {
						gap = hdup(&requested_on_track[gap_index].gap);
					}
				}

				if gap.is_null() {
					// No requested gap was at this index, find one
					let block = nearest_block_after_or_at(hdup(&track), earliest_point);
					if !block.is_null() {
						// Found a block, test if it's a gap
						if is_gap(hdup(&block)) {
							gap = hdup(&block);
						} else {
							if block_in(hdup(&block)) == earliest_point {
								let next = block_next(hdup(&block));
								if is_gap(hdup(&next)) {
									gap = hdup(&next);
								} else {
									ripple_length = Rational::new(0, 1);
								}
							} else {
								let prev = block_previous(hdup(&block));
								if is_gap(hdup(&prev)) {
									gap = hdup(&prev);
								} else {
									ripple_length = Rational::new(0, 1);
								}
							}
						}
					} else {
						// Assume track finishes here and track won't be
						// affected by this operation
					}
				}

				if !gap.is_null() {
					let g = hdup(&gap);
					gaps.push(hdup(&g));

					if !gap_lengths.iter().any(|(b, _)| b.ctx == g.ctx) {
						gap_lengths.push((hdup(&g), block_length(hdup(&g))));
					}

					if let Some((_, len)) = gap_lengths.iter().find(|(b, _)| b.ctx == g.ctx) {
						ripple_length = std::cmp::min(ripple_length, *len);
					}
				}

				if ripple_length == Rational::new(0, 1) {
					break;
				}
			}

			if ripple_length > Rational::new(0, 1) {
				for g in gaps {
					let g_len = gap_lengths
						.iter()
						.find(|(b, _)| b.ctx == g.ctx)
						.map(|(_, l)| *l)
						.unwrap_or(Rational::NULL);

					if g_len == ripple_length {
						self.commands_
							.push(TrackRippleRemoveBlockCommand::new(
								block_track(hdup(&g)),
								hdup(&g),
							)
							.to_command());
					} else {
						let mut new_len = g_len;
						for (b, l) in gap_lengths.iter_mut() {
							if b.ctx == g.ctx {
								*l = *l - ripple_length;
								new_len = *l;
							}
						}
						self.commands_
							.push(BlockResizeCommand::new(hdup(&g), new_len).to_command());
					}
				}
			}
		}
	}

	/// `redo`: apply the gap deletions.
	pub fn redo(&mut self) {
		for i in 0..self.commands_.len() {
			let c = hdup(&self.commands_[i]);
			let _ = unsafe { oakundo_command_redo_now(c) };
		}
	}

	/// `undo`: restore the deleted gaps.
	pub fn undo(&mut self) {
		for i in (0..self.commands_.len()).rev() {
			let c = hdup(&self.commands_[i]);
			let _ = unsafe { oakundo_command_undo_now(c) };
		}
	}

	/// Wrap as an oakundo vtable command handle.
	pub fn to_command(self) -> CHandle {
		box_command(self)
	}
}

impl Command for TimelineRippleDeleteGapsAtRegionsCommand {
	fn redo(&mut self) {
		self.redo()
	}

	fn undo(&mut self) {
		self.undo()
	}
}

impl Drop for TimelineRippleDeleteGapsAtRegionsCommand {
	fn drop(&mut self) {
		// C++ dtor frees every sub-command (`delete c`).
		for i in 0..self.commands_.len() {
			free_command_handle(&mut self.commands_[i]);
		}
	}
}
