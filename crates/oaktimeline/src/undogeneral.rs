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

//! General-purpose timeline commands (`src/timeline/src/timelineundogeneral.h`):
//! block resizing, media-in, track add/remove, transition removal, gap
//! replacement, enable/disable, gap insertion and default transitions.
//!
//! Graph mutation goes through the oaknode C ABI (`bridge::node`); command
//! wrapping through `bridge::undo`. Commands that compose child commands own
//! them as plain structs rather than pointers (see `undocommon`).

use oakcore_rs::Rational;

use crate::bridge::node::{
  oaknode_block_gap_create, oaknode_block_get_enabled, oaknode_block_get_kind,
  oaknode_block_set_enabled, oaknode_clip_get_media_in, oaknode_clip_set_media_in,
  oaknode_track_create, oaknode_track_get_block_at, oaknode_track_get_block_count,
  oaknode_track_get_locked, oaknode_track_insert_block_after, oaknode_track_replace_block,
  oaknode_track_ripple_remove_block, oaknode_tracklist_array_append,
  oaknode_tracklist_array_remove_last, oaknode_tracklist_get_track_at,
  oaknode_tracklist_get_track_count, oaknode_tracklist_get_type,
};
use crate::bridge::undo::{oakundo_command_redo_now, oakundo_command_undo_now};
use crate::handle::CHandle;
use crate::undocommon::{box_command, create_block_remove_command, free_command_handle, Command};
use crate::undosplit::BlockSplitPreservingLinksCommand;
use crate::util::{
  block_add_to_graph, block_in, block_length, block_next, block_out, block_previous,
  block_remove_from_graph, block_set_length_and_media_in, block_set_length_and_media_out,
  block_track, free_detached_handle, rat_nd, same_block,
};

// `oaknode/block.h` block kinds.
const OAKNODE_BLOCK_OTHER: i32 = 0;
const OAKNODE_BLOCK_CLIP: i32 = 1;
const OAKNODE_BLOCK_GAP: i32 = 2;
const OAKNODE_BLOCK_TRANSITION: i32 = 3;

// `oaknode/track.h` track types.
const OAKNODE_TRACK_TYPE_NONE: i32 = -1;
const OAKNODE_TRACK_TYPE_VIDEO: i32 = 0;
const OAKNODE_TRACK_TYPE_AUDIO: i32 = 1;

// `oaknode/sequence.h` element input ids.
const OAKNODE_SEQUENCE_TEXTURE_INPUT: &str = "tex_in";
const OAKNODE_SEQUENCE_SAMPLES_INPUT: &str = "samples_in";

/// `BlockResizeCommand` — change a block's length without touching its
/// media-in point (timelineundogeneral.h). The old length is captured at
/// `redo` time.
pub struct BlockResizeCommand {
  /// Block to resize.
  block: CHandle,
  /// New length.
  new_length: Rational,
  /// Length captured at `redo` time, restored by `undo`.
  old_length: Rational,
}

impl BlockResizeCommand {
  /// Construct from block + new length.
  pub fn new(block: CHandle, new_length: Rational) -> Self {
    Self {
      block,
      new_length,
      old_length: Rational::new(0, 1),
    }
  }

  /// `redo`: capture `old_length`, then set the block's length.
  pub fn redo(&mut self) {
    self.old_length = block_length(self.block.clone());
    block_set_length_and_media_out(self.block.clone(), self.new_length);
  }

  /// `undo`: restore `old_length`.
  pub fn undo(&mut self) {
    block_set_length_and_media_out(self.block.clone(), self.old_length);
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Command for BlockResizeCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `BlockResizeWithMediaInCommand` — change a block's length while keeping
/// its out point fixed (media-in moves with the length)
/// (timelineundogeneral.h). The old length is captured at `redo` time.
pub struct BlockResizeWithMediaInCommand {
  /// Block to resize.
  block: CHandle,
  /// New length.
  new_length: Rational,
  /// Length captured at `redo` time, restored by `undo`.
  old_length: Rational,
}

impl BlockResizeWithMediaInCommand {
  /// Construct from block + new length.
  pub fn new(block: CHandle, new_length: Rational) -> Self {
    Self {
      block,
      new_length,
      old_length: Rational::new(0, 1),
    }
  }

  /// `redo`: capture `old_length`, then resize keeping the out point fixed.
  pub fn redo(&mut self) {
    self.old_length = block_length(self.block.clone());
    block_set_length_and_media_in(self.block.clone(), self.new_length);
  }

  /// `undo`: restore `old_length`.
  pub fn undo(&mut self) {
    block_set_length_and_media_in(self.block.clone(), self.old_length);
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Command for BlockResizeWithMediaInCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `BlockSetMediaInCommand` — change a block's media-in point
/// (timelineundogeneral.h). The old media-in is captured at `redo` time.
pub struct BlockSetMediaInCommand {
  /// Block whose media-in changes.
  block: CHandle,
  /// New media-in.
  new_media_in: Rational,
  /// Media-in captured at `redo` time, restored by `undo`.
  old_media_in: Rational,
}

impl BlockSetMediaInCommand {
  /// Construct from block + new media-in.
  pub fn new(block: CHandle, new_media_in: Rational) -> Self {
    Self {
      block,
      new_media_in,
      old_media_in: Rational::new(0, 1),
    }
  }

  /// `redo`: capture `old_media_in`, then set the media-in.
  pub fn redo(&mut self) {
    let mut n = 0;
    let mut d = 0;
    // SAFETY: `n`/`d` are valid out pointers.
    let _ = unsafe { oaknode_clip_get_media_in(self.block.clone(), &mut n, &mut d) };
    self.old_media_in = Rational::new(n as i64, d as i64);
    rat_nd(self.new_media_in, &mut n, &mut d);
    // SAFETY: `self.block` (copied) is a valid clip handle.
    let _ = unsafe { oaknode_clip_set_media_in(self.block.clone(), n, d) };
  }

  /// `undo`: restore `old_media_in`.
  pub fn undo(&mut self) {
    let mut n = 0;
    let mut d = 0;
    rat_nd(self.old_media_in, &mut n, &mut d);
    // SAFETY: `self.block` (copied) is a valid clip handle.
    let _ = unsafe { oaknode_clip_set_media_in(self.block.clone(), n, d) };
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Command for BlockSetMediaInCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `TimelineAddTrackCommand` — append a track (and optionally merge an
/// existing one) to a track list (timelineundogeneral.h). When `automerge` is
/// set, a matching neighbouring track is merged into the new one.
pub struct TimelineAddTrackCommand {
  /// Target track list.
  timeline: CHandle,
  /// Track created by this command.
  track: CHandle,
  /// Merge node used when `automerge_tracks` is set (unused: see `new`).
  merge: CHandle,
  /// Input id the track connects to (`tex_in`/`samples_in`).
  direct_input: String,
  /// Merge base input id (unused: see `new`).
  base_input: String,
  /// Merge blend input id (unused: see `new`).
  blend_input: String,
  /// Undoable position command (unused: see `redo`).
  position_command: CHandle,
  /// Whether to automerge a matching adjacent track.
  automerge_tracks: bool,
  /// Whether `track` is detached and owned by this command.
  track_orphaned: bool,
  /// Whether `merge` is detached and owned by this command.
  merge_orphaned: bool,
}

impl TimelineAddTrackCommand {
  /// Construct with the default automerge behaviour.
  pub fn new(timeline: CHandle) -> Self {
    // NOTE: the C++ reads the `AutoMergeTracks` config via
    // `oakcommon_config_get_bool`; the bridge exposes no config getter, so the
    // default is hardcoded to `false`.
    Self::with_automerge(timeline, false)
  }

  /// Construct with an explicit automerge flag.
  pub fn with_automerge(timeline: CHandle, automerge: bool) -> Self {
    let mut kind = OAKNODE_TRACK_TYPE_NONE;
    // SAFETY: `kind` is a valid out pointer.
    let _ = unsafe { oaknode_tracklist_get_type(timeline.clone(), &mut kind) };
    // SAFETY: `kind` is a valid track type.
    let track = unsafe { oaknode_track_create(kind) };
    let direct_input = if kind == OAKNODE_TRACK_TYPE_VIDEO {
      OAKNODE_SEQUENCE_TEXTURE_INPUT.to_string()
    } else if kind == OAKNODE_TRACK_TYPE_AUDIO {
      OAKNODE_SEQUENCE_SAMPLES_INPUT.to_string()
    } else {
      String::new()
    };
    // NOTE: the C++ merge branch creates a merge/math node via
    // `oaknode_factory_create_from_id` when the sequence input is already
    // connected; that needs `oaknode_tracklist_get_sequence`,
    // `oaknode_node_input_is_connected` and `oaknode_factory_create_from_id`,
    // which are absent from this bridge, so `merge`/`base_input`/`blend_input`
    // stay empty and the automerge flag is retained but not acted on.
    Self {
      timeline,
      track,
      merge: CHandle::null(),
      direct_input,
      base_input: String::new(),
      blend_input: String::new(),
      position_command: CHandle::null(),
      automerge_tracks: automerge,
      track_orphaned: true,
      merge_orphaned: false,
    }
  }

  /// `redo`: create (and merge) the track; `track()` then reports it.
  pub fn redo(&mut self) {
    // NOTE: the C++ redo adds the track node to the project graph (via
    // `oaknode_track_as_node` + `oaknode_node_get_project`), copies the last
    // track's height, connects the track to the sequence element and builds an
    // undoable position command; those symbols are absent from this bridge, so
    // only the array append is performed. The ownership flag is still flipped,
    // as the track is handed to the track list.
    // SAFETY: `self.timeline` (copied) is a valid track list handle.
    let _ = unsafe { oaknode_tracklist_array_append(self.timeline.clone()) };
    self.track_orphaned = false;
  }

  /// `undo`: remove the created track and restore the merged track.
  pub fn undo(&mut self) {
    // NOTE: the C++ undo disconnects the merge/direct input and removes the
    // track node from the project graph; those paths need the absent
    // `oaknode_node_*` symbols above, so only the array removal is performed.
    // SAFETY: `self.timeline` (copied) is a valid track list handle.
    let _ = unsafe { oaknode_tracklist_array_remove_last(self.timeline.clone()) };
    self.track_orphaned = true;
  }

  /// The track created by this command; only meaningful after `redo`.
  pub fn track(&self) -> CHandle {
    self.track.clone()
  }

  /// `TimelineAddTrackCommand::run_immediately` — construct, `redo` and
  /// return the created track in one step (static-semantics factory).
  pub fn run_immediately(timeline: CHandle) -> CHandle {
    let mut c = Self::new(timeline);
    c.redo();
    c.track()
  }

  /// Overload of `run_immediately` with an explicit automerge flag.
  pub fn run_immediately_with_automerge(timeline: CHandle, automerge: bool) -> CHandle {
    let mut c = Self::with_automerge(timeline, automerge);
    c.redo();
    c.track()
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Drop for TimelineAddTrackCommand {
  /// Free the position command, and the created track/merge if still detached.
  fn drop(&mut self) {
    if !self.position_command.is_null() {
      free_command_handle(&mut self.position_command);
    }
    if self.track_orphaned {
      free_detached_handle(&mut self.track);
    }
    if self.merge_orphaned {
      free_detached_handle(&mut self.merge);
    }
  }
}

impl Command for TimelineAddTrackCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `TimelineRemoveTrackCommand` — remove a track from its list and restore it
/// on `undo` (timelineundogeneral.h).
pub struct TimelineRemoveTrackCommand {
  /// Track to remove.
  track: CHandle,
  /// Owning track list, located at `prepare` time (unused: see `prepare`).
  list: CHandle,
  /// Index of the track in its list (unused: see `prepare`).
  index: i32,
  /// Graph-removal command (unused: see `prepare`).
  remove_command: CHandle,
}

impl TimelineRemoveTrackCommand {
  /// Construct from the track to remove.
  pub fn new(track: CHandle) -> Self {
    Self {
      track,
      list: CHandle::null(),
      index: 0,
      remove_command: CHandle::null(),
    }
  }

  /// `prepare`: locate the owning list and index.
  pub fn prepare(&mut self) {
    let _ = (self.track.clone(), self.list.clone(), self.index);
    // NOTE: the C++ prepare locates the owning list and index via
    // `oaknode_track_get_sequence`, `oaknode_track_get_type`,
    // `oaknode_track_get_index` and
    // `oaknode_tracklist_get_array_index_from_cache_index`, and builds a
    // `NodeRemoveCommand` from `oaknode_track_as_node`; those symbols are
    // absent from this bridge, so the command retains `track` only.
  }

  /// `redo`: remove the track from the graph.
  pub fn redo(&mut self) {
    // NOTE: the C++ redo runs the removal command and detaches the track's
    // sequence element; that needs `remove_command` plus
    // `oaknode_tracklist_get_sequence`, `oaknode_tracklist_get_track_input_id`
    // and `oaknode_node_input_array_remove`, absent from this bridge, so
    // nothing is executed.
  }

  /// `undo`: re-insert the track at its former index.
  pub fn undo(&mut self) {
    // NOTE: the C++ undo re-inserts the track's sequence element before
    // running the removal command's undo; that needs the same absent symbols,
    // so nothing is executed.
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Drop for TimelineRemoveTrackCommand {
  /// Free the removal command if still owned.
  fn drop(&mut self) {
    if !self.remove_command.is_null() {
      free_command_handle(&mut self.remove_command);
    }
  }
}

impl Command for TimelineRemoveTrackCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `TransitionRemoveCommand` — detach a transition block from its neighbours,
/// optionally removing it from the whole graph (timelineundogeneral.h).
pub struct TransitionRemoveCommand {
  /// Transition block to remove.
  block: CHandle,
  /// Whether to also remove the block from the graph.
  remove_from_graph: bool,
  /// Owning track, captured at `redo` time.
  track: CHandle,
  /// Block after the transition (unused: see `redo`).
  out_block: CHandle,
  /// Block before the transition (unused: see `redo`).
  in_block: CHandle,
  /// Graph-removal command built at `redo` time.
  remove_command: CHandle,
}

impl TransitionRemoveCommand {
  /// Construct from the transition block + remove-from-graph flag.
  pub fn new(block: CHandle, remove_from_graph: bool) -> Self {
    Self {
      block,
      remove_from_graph,
      track: CHandle::null(),
      out_block: CHandle::null(),
      in_block: CHandle::null(),
      remove_command: CHandle::null(),
    }
  }

  /// `redo`: relink neighbours around the transition and remove it.
  pub fn redo(&mut self) {
    self.track = block_track(self.block.clone());
    // NOTE: the C++ redo extends the neighbouring blocks by the transition
    // offsets and disconnects the transition's inputs; that needs the
    // `oaknode_transition_get_*` accessors and `oaknode_block_as_node` +
    // `oaknode_node_disconnect`, absent from this bridge, so only the
    // ripple-remove is performed.
    // SAFETY: `self.track`/`self.block` (copied) are valid track/block handles.
    let _ = unsafe { oaknode_track_ripple_remove_block(self.track.clone(), self.block.clone()) };
    if self.remove_from_graph {
      if self.remove_command.is_null() {
        self.remove_command = create_block_remove_command(self.block.clone());
      }
      // SAFETY: `self.remove_command` (copied) is a valid command handle.
      let _ = unsafe { oakundo_command_redo_now(self.remove_command.clone()) };
    }
  }

  /// `undo`: restore the transition between its neighbours.
  pub fn undo(&mut self) {
    if self.remove_from_graph && !self.remove_command.is_null() {
      // SAFETY: `self.remove_command` (copied) is a valid command handle.
      let _ = unsafe { oakundo_command_undo_now(self.remove_command.clone()) };
    }
    // NOTE: the C++ undo re-inserts the transition between its former
    // neighbours, reconnects them and restores the offsets; that needs
    // `oaknode_track_insert_block_before`, the `oaknode_transition_get_*`
    // accessors and `oaknode_block_as_node` + `oaknode_node_connect`, absent
    // from this bridge, so the block stays detached.
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Drop for TransitionRemoveCommand {
  /// Free the removal command if still owned.
  fn drop(&mut self) {
    if !self.remove_command.is_null() {
      free_command_handle(&mut self.remove_command);
    }
  }
}

impl Command for TransitionRemoveCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `TrackReplaceBlockWithGapCommand` — replace a block on a track with a gap
/// of equal length (timelineundogeneral.h). When `handle_transitions` is set,
/// attached transitions are also removed.
pub struct TrackReplaceBlockWithGapCommand {
  /// Owning track.
  track: CHandle,
  /// Block to replace.
  block: CHandle,
  /// Whether to also remove attached transitions.
  handle_transitions: bool,
  /// Pre-existing gap extended in place (found at `redo` time).
  existing_gap: CHandle,
  /// Second gap merged into `existing_gap` (found at `redo` time).
  existing_merged_gap: CHandle,
  /// Whether `existing_gap` preceded the block (decides re-insert order).
  existing_gap_precedes: bool,
  /// Gap created by this command (owned until placed in the graph).
  our_gap: CHandle,
  /// Whether `our_gap` is detached and owned by this command.
  our_gap_orphaned: bool,
  /// Whether `existing_merged_gap` is detached and owned by this command.
  merged_gap_orphaned: bool,
  /// Child transition-removal commands (empty: see `create_remove_...`).
  transition_remove_commands: Vec<TransitionRemoveCommand>,
}

impl TrackReplaceBlockWithGapCommand {
  /// Construct from track + block + transition handling flag.
  pub fn new(track: CHandle, block: CHandle, handle_transitions: bool) -> Self {
    Self {
      track,
      block,
      handle_transitions,
      existing_gap: CHandle::null(),
      existing_merged_gap: CHandle::null(),
      existing_gap_precedes: false,
      our_gap: CHandle::null(),
      our_gap_orphaned: false,
      merged_gap_orphaned: false,
      transition_remove_commands: Vec::new(),
    }
  }

  /// `redo`: merge/replace with a gap, removing transitions as configured.
  pub fn redo(&mut self) {
    if self.handle_transitions && self.transition_remove_commands.is_empty() {
      self.create_remove_transition_command_if_necessary(false);
      self.create_remove_transition_command_if_necessary(true);
    }
    for c in self.transition_remove_commands.iter_mut() {
      c.redo();
    }

    if !block_next(self.block.clone()).is_null() {
      // The block is not at the end of the track, so a gap must fill its space.
      let mut new_gap_length = block_length(self.block.clone());
      let previous = block_previous(self.block.clone());
      let next = block_next(self.block.clone());

      let mut prev_kind = OAKNODE_BLOCK_OTHER;
      let mut next_kind = OAKNODE_BLOCK_OTHER;
      if !previous.is_null() {
        // SAFETY: `prev_kind` is a valid out pointer.
        let _ = unsafe { oaknode_block_get_kind(previous.clone(), &mut prev_kind) };
      }
      if !next.is_null() {
        // SAFETY: `next_kind` is a valid out pointer.
        let _ = unsafe { oaknode_block_get_kind(next.clone(), &mut next_kind) };
      }
      let previous_is_a_gap = prev_kind == OAKNODE_BLOCK_GAP;
      let next_is_a_gap = next_kind == OAKNODE_BLOCK_GAP;

      if previous_is_a_gap && next_is_a_gap {
        // The clip is preceded and followed by a gap, so merge the two.
        self.existing_gap = previous.clone();
        self.existing_merged_gap = next;
        new_gap_length = new_gap_length + block_length(self.existing_merged_gap.clone());
        // SAFETY: `self.track`/`self.existing_merged_gap` (copied) are valid handles.
        let _ = unsafe {
          oaknode_track_ripple_remove_block(self.track.clone(), self.existing_merged_gap.clone())
        };
        block_remove_from_graph(self.existing_merged_gap.clone(), self.track.clone());
        self.merged_gap_orphaned = true;
      } else if previous_is_a_gap {
        // Extend this gap to fill the space left by the block.
        self.existing_gap = previous.clone();
      } else if next_is_a_gap {
        // Extend this gap to fill the space left by the block.
        self.existing_gap = next;
      }

      if !self.existing_gap.is_null() {
        // Extend an existing gap.
        new_gap_length = new_gap_length + block_length(self.existing_gap.clone());
        block_set_length_and_media_out(self.existing_gap.clone(), new_gap_length);
        // SAFETY: `self.track`/`self.block` (copied) are valid handles.
        let _ = unsafe { oaknode_track_ripple_remove_block(self.track.clone(), self.block.clone()) };
        self.existing_gap_precedes = same_block(self.existing_gap.clone(), previous);
      } else {
        // No gap exists to fill this space, create a new one and swap it in.
        if self.our_gap.is_null() {
          // SAFETY: `oaknode_block_gap_create` returns a fresh gap handle.
          self.our_gap = unsafe { oaknode_block_gap_create() };
          block_set_length_and_media_out(self.our_gap.clone(), new_gap_length);
          self.our_gap_orphaned = true;
        }
        block_add_to_graph(self.our_gap.clone(), self.track.clone());
        // SAFETY: `self.track`/`self.block`/`self.our_gap` (copied) are valid handles.
        let _ = unsafe {
          oaknode_track_replace_block(self.track.clone(), self.block.clone(), self.our_gap.clone())
        };
        self.our_gap_orphaned = false;
      }
    } else {
      // The block is at the end of the track, simply remove it.
      let preceding = block_previous(self.block.clone());
      // SAFETY: `self.track`/`self.block` (copied) are valid handles.
      let _ = unsafe { oaknode_track_ripple_remove_block(self.track.clone(), self.block.clone()) };

      // Remove a preceding gap too, if there is one.
      let mut kind = OAKNODE_BLOCK_OTHER;
      if !preceding.is_null() {
        // SAFETY: `kind` is a valid out pointer.
        let _ = unsafe { oaknode_block_get_kind(preceding.clone(), &mut kind) };
      }
      if kind == OAKNODE_BLOCK_GAP {
        // SAFETY: `self.track`/`preceding` (copied) are valid handles.
        let _ = unsafe { oaknode_track_ripple_remove_block(self.track.clone(), preceding.clone()) };
        block_remove_from_graph(preceding.clone(), self.track.clone());
        self.existing_merged_gap = preceding;
        self.merged_gap_orphaned = true;
      }
    }
  }

  /// `undo`: replace the gap back with the block.
  pub fn undo(&mut self) {
    if !self.our_gap.is_null() || !self.existing_gap.is_null() {
      if !self.our_gap.is_null() {
        // We made this gap, simply swap it back.
        // SAFETY: `self.track`/`self.our_gap`/`self.block` (copied) are valid handles.
        let _ = unsafe {
          oaknode_track_replace_block(self.track.clone(), self.our_gap.clone(), self.block.clone())
        };
        block_remove_from_graph(self.our_gap.clone(), self.track.clone());
        self.our_gap_orphaned = true;
      } else {
        // We extended an existing gap; restore its original length.
        let mut original_gap_length =
          block_length(self.existing_gap.clone()) - block_length(self.block.clone());

        // If we merged two gaps together, restore the second one now.
        if !self.existing_merged_gap.is_null() {
          original_gap_length =
            original_gap_length - block_length(self.existing_merged_gap.clone());
          block_add_to_graph(self.existing_merged_gap.clone(), self.track.clone());
          // SAFETY: the three (copied) handles are valid.
          let _ = unsafe {
            oaknode_track_insert_block_after(
              self.track.clone(),
              self.existing_merged_gap.clone(),
              self.existing_gap.clone(),
            )
          };
          self.merged_gap_orphaned = false;
          self.existing_merged_gap = CHandle::null();
        }

        // Restore the original block.
        if self.existing_gap_precedes {
          // SAFETY: the three (copied) handles are valid.
          let _ = unsafe {
            oaknode_track_insert_block_after(
              self.track.clone(),
              self.block.clone(),
              self.existing_gap.clone(),
            )
          };
        } else {
          // NOTE: the C++ re-inserts before `existing_gap` via
          // `oaknode_track_insert_block_before`, which is absent from this
          // bridge, so this path cannot restore the block.
        }

        // Restore the gap's original length.
        block_set_length_and_media_out(self.existing_gap.clone(), original_gap_length);
        self.existing_gap = CHandle::null();
      }
    } else {
      // Both gaps null: the block was at the end of the track, so no gap
      // extension/replacement is needed. The C++ re-appends the merged gap and
      // the block with `oaknode_track_append_block`, which is absent from this
      // bridge, so nothing is executed.
    }

    for c in self.transition_remove_commands.iter_mut().rev() {
      c.undo();
    }
  }

  fn create_remove_transition_command_if_necessary(&mut self, next: bool) {
    let relevant_block = if next {
      block_next(self.block.clone())
    } else {
      block_previous(self.block.clone())
    };

    let mut kind = OAKNODE_BLOCK_OTHER;
    if !relevant_block.is_null() {
      // SAFETY: `kind` is a valid out pointer.
      let _ = unsafe { oaknode_block_get_kind(relevant_block.clone(), &mut kind) };
    }
    if kind != OAKNODE_BLOCK_TRANSITION {
      return;
    }
    // NOTE: the C++ checks whether the transition is connected only to this
    // block (via `oaknode_transition_get_connected_out_block`/`_in_block` +
    // `same_block`) before adding a `TransitionRemoveCommand`; those accessors
    // are absent from this bridge, so no child commands are produced.
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Drop for TrackReplaceBlockWithGapCommand {
  /// Free the gap/merged-gap handles still detached from the graph.
  fn drop(&mut self) {
    if self.our_gap_orphaned {
      free_detached_handle(&mut self.our_gap);
    }
    if self.merged_gap_orphaned {
      free_detached_handle(&mut self.existing_merged_gap);
    }
  }
}

impl Command for TrackReplaceBlockWithGapCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `BlockEnableDisableCommand` — flip a block's enabled flag
/// (timelineundogeneral.h). The old value is captured at construction.
pub struct BlockEnableDisableCommand {
  /// Block whose enabled flag changes.
  block: CHandle,
  /// New enabled value.
  new_enabled: i32,
  /// Enabled value captured at construction, restored by `undo`.
  old_enabled: i32,
}

impl BlockEnableDisableCommand {
  /// Construct from block + new enabled value (captures old at ctor).
  pub fn new(block: CHandle, enabled: bool) -> Self {
    let mut old_enabled = 0;
    // SAFETY: `old_enabled` is a valid out pointer.
    let _ = unsafe { oaknode_block_get_enabled(block.clone(), &mut old_enabled) };
    Self {
      block,
      new_enabled: enabled as i32,
      old_enabled,
    }
  }

  /// `redo`: `oaknode_block_set_enabled(block, new)`.
  pub fn redo(&mut self) {
    // SAFETY: `self.block` (copied) is a valid block handle.
    let _ = unsafe { oaknode_block_set_enabled(self.block.clone(), self.new_enabled) };
  }

  /// `undo`: `oaknode_block_set_enabled(block, old)`.
  pub fn undo(&mut self) {
    // SAFETY: `self.block` (copied) is a valid block handle.
    let _ = unsafe { oaknode_block_set_enabled(self.block.clone(), self.old_enabled) };
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Command for BlockEnableDisableCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `TrackListInsertGaps` — insert a gap of `length` at `point` on every track
/// in a list (timelineundogeneral.h), splitting blocks as needed and extending
/// existing gaps rather than adding duplicates where possible.
pub struct TrackListInsertGaps {
  /// Track list to operate on.
  track_list: CHandle,
  /// Insertion point.
  point: Rational,
  /// Gap length to insert.
  length: Rational,
  /// Unlocked tracks gathered at `prepare` time.
  working_tracks: Vec<CHandle>,
  /// Existing gaps crossed by `point`, extended by `length` on `redo`.
  gaps_to_extend: Vec<CHandle>,
  /// Gaps inserted after their `before` blocks on their tracks.
  gaps_added: Vec<AddGap>,
  /// Optional per-track split command built at `prepare` time.
  split_command: Option<BlockSplitPreservingLinksCommand>,
}

/// One gap inserted by [`TrackListInsertGaps`] on `redo` and removed on
/// `undo`; the C++ `TrackListInsertGaps::AddGap` struct.
struct AddGap {
  /// The inserted gap block.
  gap: CHandle,
  /// Whether `gap` is detached and owned by its parent command.
  orphaned: bool,
  /// Block the gap is inserted after.
  before: CHandle,
  /// Track the gap is inserted on.
  track: CHandle,
}

impl TrackListInsertGaps {
  /// Construct from track list + point + length.
  pub fn new(track_list: CHandle, point: Rational, length: Rational) -> Self {
    Self {
      track_list,
      point,
      length,
      working_tracks: Vec::new(),
      gaps_to_extend: Vec::new(),
      gaps_added: Vec::new(),
      split_command: None,
    }
  }

  /// `prepare`: build the per-track split/gap commands.
  pub fn prepare(&mut self) {
    let mut track_count = 0;
    // SAFETY: `track_count` is a valid out pointer.
    let _ = unsafe { oaknode_tracklist_get_track_count(self.track_list.clone(), &mut track_count) };
    for i in 0..track_count {
      let mut track = CHandle::null();
      // SAFETY: `track` is a valid out pointer.
      let _ = unsafe { oaknode_tracklist_get_track_at(self.track_list.clone(), i, &mut track) };
      if track.is_null() {
        continue;
      }
      let mut locked = 0;
      // SAFETY: `locked` is a valid out pointer.
      let _ = unsafe { oaknode_track_get_locked(track.clone(), &mut locked) };
      if locked != 0 {
        continue;
      }
      self.working_tracks.push(track);
    }

    let mut blocks_to_split: Vec<CHandle> = Vec::new();
    let mut blocks_to_append_gap_to: Vec<CHandle> = Vec::new();
    let mut tracks_to_append_gap_to: Vec<CHandle> = Vec::new();

    for track in self.working_tracks.clone() {
      let mut block_count = 0;
      // SAFETY: `block_count` is a valid out pointer.
      let _ = unsafe { oaknode_track_get_block_count(track.clone(), &mut block_count) };
      for i in 0..block_count {
        let mut b = CHandle::null();
        // SAFETY: `b` is a valid out pointer.
        let _ = unsafe { oaknode_track_get_block_at(track.clone(), i, &mut b) };
        if b.is_null() {
          continue;
        }

        let mut kind = OAKNODE_BLOCK_OTHER;
        // SAFETY: `kind` is a valid out pointer.
        let _ = unsafe { oaknode_block_get_kind(b.clone(), &mut kind) };

        if kind == OAKNODE_BLOCK_GAP && block_in(b.clone()) <= self.point
          && block_out(b.clone()) >= self.point
        {
          // Found a gap at the location.
          self.gaps_to_extend.push(b);
          break;
        } else if kind == OAKNODE_BLOCK_CLIP && block_out(b.clone()) >= self.point {
          let mut append_gap = true;

          if block_in(b.clone()) == self.point {
            // The block is at the start of the track; no split needs to occur.
            b = CHandle::null();
          } else if block_out(b.clone()) > self.point {
            // The block must be split as well as having a gap appended to it.
            blocks_to_split.push(b.clone());
          } else if block_next(b.clone()).is_null() {
            // At the end of a track, no gap needs to be added at all.
            append_gap = false;
          }

          if append_gap {
            tracks_to_append_gap_to.push(track.clone());
            blocks_to_append_gap_to.push(b);
          }
          break;
        }
      }
    }

    if !blocks_to_split.is_empty() {
      self.split_command =
        Some(BlockSplitPreservingLinksCommand::new(blocks_to_split, vec![self.point]));
    }

    for i in 0..blocks_to_append_gap_to.len() {
      // SAFETY: `oaknode_block_gap_create` returns a fresh gap handle.
      let gap = unsafe { oaknode_block_gap_create() };
      block_set_length_and_media_out(gap.clone(), self.length);
      self.gaps_added.push(AddGap {
        gap,
        orphaned: true,
        before: blocks_to_append_gap_to[i].clone(),
        track: tracks_to_append_gap_to[i].clone(),
      });
    }
  }

  /// `redo`: apply the gap insertions.
  pub fn redo(&mut self) {
    for gap in self.gaps_to_extend.clone() {
      block_set_length_and_media_out(gap.clone(), block_length(gap.clone()) + self.length);
    }

    if let Some(s) = self.split_command.as_mut() {
      s.redo();
    }

    for g in self.gaps_added.iter_mut() {
      block_add_to_graph(g.gap.clone(), g.track.clone());
      // SAFETY: `g.track`/`g.gap`/`g.before` (copied) are valid handles.
      let _ = unsafe {
        oaknode_track_insert_block_after(g.track.clone(), g.gap.clone(), g.before.clone())
      };
      g.orphaned = false;
    }
  }

  /// `undo`: remove the inserted gaps.
  pub fn undo(&mut self) {
    // Remove added gaps.
    for g in self.gaps_added.iter_mut() {
      let t = block_track(g.gap.clone());
      if !t.is_null() {
        // SAFETY: `t`/`g.gap` (copied) are valid handles.
        let _ = unsafe { oaknode_track_ripple_remove_block(t.clone(), g.gap.clone()) };
        block_remove_from_graph(g.gap.clone(), t);
      }
      g.orphaned = true;
    }

    // Un-split blocks.
    if let Some(s) = self.split_command.as_mut() {
      s.undo();
    }

    // Restore the original length of the extended gaps.
    for gap in self.gaps_to_extend.clone() {
      block_set_length_and_media_out(gap.clone(), block_length(gap.clone()) - self.length);
    }
  }

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Drop for TrackListInsertGaps {
  /// Free any inserted gaps still detached from the graph.
  fn drop(&mut self) {
    for g in self.gaps_added.iter_mut() {
      if g.orphaned {
        free_detached_handle(&mut g.gap);
      }
    }
  }
}

impl Command for TrackListInsertGaps {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}

/// `TimelineAddDefaultTransitionCommand` — add the default transition at the
/// start/end (or both) of the given clips (timelineundogeneral.h). Composes a
/// set of child commands driven by `timebase_`.
pub struct TimelineAddDefaultTransitionCommand {
  /// Clips to receive a transition.
  clips: Vec<CHandle>,
  /// Timeline timebase.
  timebase: Rational,
  /// Child commands, executed in order on `redo` (empty: see `prepare`).
  commands: Vec<Box<dyn Command>>,
  /// Clip length bookkeeping used by the C++ length adjustment (unused:
  /// see `add_transition`).
  lengths: Vec<(CHandle, Rational)>,
}

/// The side(s) of a clip a transition is added to (the C++
/// `CreateTransitionMode` enum).
enum CreateTransitionMode {
  /// Transition at the clip's in point.
  In,
  /// Transition at the clip's out point.
  Out,
  /// Transitions at the in point of the next clip and the out point of this
  /// one (dual).
  OutDual,
}

impl TimelineAddDefaultTransitionCommand {
  /// Construct from clips + timebase.
  pub fn new(clips: Vec<CHandle>, timebase: Rational) -> Self {
    Self {
      clips,
      timebase,
      commands: Vec::new(),
      lengths: Vec::new(),
    }
  }

  /// `prepare`: build the per-clip transition commands.
  pub fn prepare(&mut self) {
    let selection = self.clips.clone();
    for c in self.clips.clone() {
      let previous = block_previous(c.clone());
      let next = block_next(c.clone());

      let is_clip_in_selection = |b: CHandle| -> bool {
        if b.is_null() {
          return false;
        }
        selection.iter().any(|clip| same_block(clip.clone(), b.clone()))
      };

      let mut prev_kind = OAKNODE_BLOCK_OTHER;
      let mut next_kind = OAKNODE_BLOCK_OTHER;
      if !previous.is_null() {
        // SAFETY: `prev_kind` is a valid out pointer.
        let _ = unsafe { oaknode_block_get_kind(previous.clone(), &mut prev_kind) };
      }
      if !next.is_null() {
        // SAFETY: `next_kind` is a valid out pointer.
        let _ = unsafe { oaknode_block_get_kind(next.clone(), &mut next_kind) };
      }

      // Handle the in transition.
      if is_clip_in_selection(previous.clone()) {
        // Do nothing; assume this will be handled by a dual transition from
        // that clip.
      } else if prev_kind == OAKNODE_BLOCK_GAP || previous.is_null() {
        // Create an in transition.
        self.add_transition(c.clone(), CreateTransitionMode::In);
      }

      // Handle the out transition.
      if is_clip_in_selection(next.clone()) {
        self.add_transition(c.clone(), CreateTransitionMode::OutDual);
      } else if next_kind == OAKNODE_BLOCK_GAP || next.is_null() {
        // Create an out transition.
        self.add_transition(c.clone(), CreateTransitionMode::Out);
      }
    }
  }

  fn add_transition(&mut self, c: CHandle, mode: CreateTransitionMode) {
    let _ = (c, mode);
    // NOTE: the C++ builds one child command per transition: it looks up the
    // default transition id in the config (`oakcommon_config_get`), creates
    // the node (`oaknode_factory_create_from_id`), clips the neighbours'
    // lengths (`BlockResizeCommand`/`BlockResizeWithMediaInCommand`), adds the
    // node (`oaknode_command_create_add_node`) and connects it
    // (`oaknode_node_connect_undoable`); those symbols are absent from this
    // bridge, so no child commands are produced.
  }

  /// `redo`: redo every child command in order.
  pub fn redo(&mut self) {
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

  /// Wrap as an oakundo vtable command handle.
  pub fn to_command(self) -> CHandle {
    box_command(self)
  }
}

impl Command for TimelineAddDefaultTransitionCommand {
  /// `Command::redo` — the inherent method takes precedence.
  fn redo(&mut self) {
    self.redo();
  }

  /// `Command::undo` — the inherent method takes precedence.
  fn undo(&mut self) {
    self.undo();
  }
}
