/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "timelineundoripple.h"

#include <algorithm>
#include <cstdio>

#include "timelineundocommon.h"
#include "timelineutil.h"
#include "undo/undocommand.h"

namespace olive
{

//
// TrackRippleRemoveAreaCommand
//
TrackRippleRemoveAreaCommand::TrackRippleRemoveAreaCommand(
	OakNodeTrack *track, const TimeRange &range)
	: track_(track)
	, range_(range)
	, insert_previous_(nullptr)
	, allow_splitting_gaps_(false)
	, splice_split_command_(nullptr)
{
	trim_out_.block = nullptr;
	trim_in_.block = nullptr;
}

TrackRippleRemoveAreaCommand::~TrackRippleRemoveAreaCommand()
{
	delete splice_split_command_;
	for (OakUndoCommand &c : remove_block_commands_) {
		free_command_handle(&c);
	}
}

void TrackRippleRemoveAreaCommand::prepare()
{
	int n, d;
	rat_nd(range_.in(), &n, &d);

	// Determine precisely what will be happening to these tracks
	OakNodeBlock *first_block = nullptr;
	oaknode_track_get_nearest_block_before_or_at(track_, n, d, &first_block);

	if (!first_block) {
		// No blocks at this time, nothing to be done on this track
		return;
	}

	// Determine if this first block is getting trimmed or removed
	bool first_block_is_out_trimmed = block_in(first_block) < range_.in();
	bool first_block_is_in_trimmed = block_out(first_block) > range_.out();

	// Set's the block that any insert command should insert AFTER. If the first block is not
	// getting out-trimmed, that means first block is either getting removed or in-trimmed, which
	// means any insert should happen before it
	insert_previous_ =
		first_block_is_out_trimmed ? first_block : block_previous(first_block);

	// If it's getting trimmed, determine if it's actually getting spliced
	if (first_block_is_out_trimmed && first_block_is_in_trimmed) {
		int kind = OAKNODE_BLOCK_OTHER;
		oaknode_block_get_kind(first_block, &kind);
		if (!allow_splitting_gaps_ && kind == OAKNODE_BLOCK_GAP) {
			// As a rule, we don't split gaps, so we just treat it as a trim of the range requested
			trim_out_ = { first_block, block_length(first_block),
						  block_length(first_block) - range_.length() };
		} else {
			// This block is getting spliced, so we'll handle that later
			splice_split_command_ =
				new BlockSplitCommand(first_block, range_.in());
		}
	} else {
		// It's just getting trimmed or removed, so we'll append that operation
		if (first_block_is_out_trimmed) {
			trim_out_ = { first_block, block_length(first_block),
						  block_length(first_block) -
							  (block_out(first_block) - range_.in()) };
		} else if (first_block_is_in_trimmed) {
			// Block is getting in trimmed
			trim_in_ = { first_block, block_length(first_block),
						 block_length(first_block) -
							 (range_.out() - block_in(first_block)) };
		} else {
			// We know for sure this block is within the range so it will be removed
			removals_.push_back(
				RemoveOperation({ first_block, block_previous(first_block) }));
		}

		// If the first block is getting in trimmed, we're already at the end of our range
		if (!first_block_is_in_trimmed) {
			// Loop through the rest of the blocks and determine what to do with those
			for (OakNodeBlock *next = block_next(first_block); next;
				 next = block_next(next)) {
				bool trimming = (block_out(next) > range_.out());

				if (trimming) {
					trim_in_ = { next, block_length(next),
								 block_length(next) -
									 (range_.out() - block_in(next)) };
					break;
				} else {
					removals_.push_back(
						RemoveOperation({ next, block_previous(next) }));

					if (block_out(next) == range_.out()) {
						break;
					}
				}
			}
		}
	}
}

void TrackRippleRemoveAreaCommand::redo()
{
	if (splice_split_command_) {
		// We're just splicing
		splice_split_command_->redo_now();

		// Trim the in of the split
		OakNodeBlock *split = splice_split_command_->new_block();
		block_set_length_and_media_in(
			split, block_length(split) - (range_.out() - block_in(split)));
	} else {
		if (trim_out_.block) {
			block_set_length_and_media_out(trim_out_.block,
										   trim_out_.new_length);
		}

		if (trim_in_.block) {
			block_set_length_and_media_in(trim_in_.block, trim_in_.new_length);
		}

		// Perform removals
		if (!removals_.empty()) {
			for (const RemoveOperation &op : removals_) {
				// Ripple remove them all first
				oaknode_track_ripple_remove_block(track_, op.block);
			}

			// Create undo commands for node removals where possible
			if (remove_block_commands_.empty()) {
				for (const RemoveOperation &op : removals_) {
					if (node_can_be_removed(op.block)) {
						remove_block_commands_.push_back(
							create_remove_command(op.block));
					}
				}
			}

			for (OakUndoCommand &c : remove_block_commands_) {
				oakundo_command_redo_now(c);
			}
		}
	}
}

void TrackRippleRemoveAreaCommand::undo()
{
	if (splice_split_command_) {
		splice_split_command_->undo_now();
	} else {
		if (trim_out_.block) {
			block_set_length_and_media_out(trim_out_.block,
										   trim_out_.old_length);
		}

		if (trim_in_.block) {
			block_set_length_and_media_in(trim_in_.block, trim_in_.old_length);
		}

		// Un-remove any blocks
		for (auto it = remove_block_commands_.rbegin();
			 it != remove_block_commands_.rend(); it++) {
			oakundo_command_undo_now(*it);
		}

		for (const RemoveOperation &op : removals_) {
			oaknode_track_insert_block_after(track_, op.block, op.before);
		}
	}
}

//
// TrackListRippleRemoveAreaCommand
//
void TrackListRippleRemoveAreaCommand::prepare()
{
	int count = 0;
	oaknode_tracklist_get_track_count(list_, &count);

	for (int i = 0; i < count; i++) {
		OakNodeTrack *track = nullptr;
		oaknode_tracklist_get_track_at(list_, i, &track);
		if (!track) {
			continue;
		}

		int locked = 0;
		oaknode_track_get_locked(track, &locked);
		if (locked) {
			continue;
		}

		TrackRippleRemoveAreaCommand *c =
			new TrackRippleRemoveAreaCommand(track, range_);
		commands_.push_back(c);
		working_tracks_.push_back(track);
	}
}

void TrackListRippleRemoveAreaCommand::redo()
{
	for (TrackRippleRemoveAreaCommand *c : commands_) {
		c->redo_now();
	}
}

void TrackListRippleRemoveAreaCommand::undo()
{
	for (TrackRippleRemoveAreaCommand *c : commands_) {
		c->undo_now();
	}
}

//
// TimelineRippleRemoveAreaCommand
//
TimelineRippleRemoveAreaCommand::TimelineRippleRemoveAreaCommand(
	OakNodeSequence *timeline, Rational in, Rational out)
{
	for (int i = 0; i < OAKNODE_TRACK_TYPE_COUNT; i++) {
		OakNodeTrackList *list = nullptr;
		oaknode_sequence_get_track_list(timeline, i, &list);
		if (list) {
			add_child(new TrackListRippleRemoveAreaCommand(list, in, out));
		}
	}
}

//
// TrackListRippleToolCommand
//
TrackListRippleToolCommand::TrackListRippleToolCommand(
	OakNodeTrackList *track_list,
	const std::map<OakNodeTrack *, RippleInfo> &info,
	const Rational &ripple_movement,
	const Timeline::MovementMode &movement_mode)
	: track_list_(track_list)
	, info_(info)
	, ripple_movement_(ripple_movement)
	, movement_mode_(movement_mode)
{
}

TrackListRippleToolCommand::~TrackListRippleToolCommand()
{
	// Free any gaps still owned by this command (detached from the graph)
	for (auto &pair : working_data_) {
		WorkingData &wd = pair.second;
		if (wd.created_gap && wd.created_gap_orphaned) {
			oaknode_block_free(wd.created_gap);
		}
		if (wd.removed_gap && wd.removed_gap_orphaned) {
			oaknode_block_free(wd.removed_gap);
		}
	}
}

void TrackListRippleToolCommand::ripple(bool redo)
{
	if (info_.empty()) {
		return;
	}

	// The following variables are used to determine how much of the cache to invalidate

	// If we can shift, we will shift from the latest out before the ripple to the latest out after,
	// since those sections will be unchanged by this ripple
	Rational pre_latest_out = RATIONAL_MIN;
	Rational post_latest_out = RATIONAL_MIN;

	// Make timeline changes
	for (const auto &pair : info_) {
		OakNodeTrack *track = pair.first;
		const RippleInfo &info = pair.second;
		WorkingData working_data = working_data_[track];
		OakNodeBlock *b = info.block;

		// Generate block length
		Rational new_block_length;
		Rational operation_movement = ripple_movement_;

		if (movement_mode_ == Timeline::k_trim_in) {
			operation_movement = -operation_movement;
		}

		if (!redo) {
			operation_movement = -operation_movement;
		}

		if (b) {
			new_block_length = block_length(b) + operation_movement;
		}

		Rational pre_shift;
		Rational post_shift;

		if (info.append_gap) {
			// Rather than rippling the referenced block, we'll insert a gap and ripple with that
			OakNodeBlock *gap = working_data.created_gap;

			if (redo) {
				if (!gap) {
					gap = oaknode_block_gap_create();
					block_set_length_and_media_out(
						gap, ripple_movement_ < Rational(0)
								 ? -ripple_movement_
								 : ripple_movement_);
					working_data.created_gap = gap;
					working_data.created_gap_orphaned = true;
				}

				block_add_to_graph(gap, track);
				oaknode_track_insert_block_before(track, gap, b);
				working_data.created_gap_orphaned = false;

				// As an insertion, we will shift from the gap's in to the gap's out
				pre_shift = block_in(gap);
				post_shift = block_out(gap);
				working_data.earliest_point_of_change = block_in(gap);
			} else {
				// As a removal, we will shift from the gap's out to the gap's in
				pre_shift = block_out(gap);
				post_shift = block_in(gap);

				oaknode_track_ripple_remove_block(track, gap);
				block_remove_from_graph(gap, track);
				working_data.created_gap_orphaned = true;
			}

		} else if ((redo && new_block_length.isNull()) ||
				   (!redo && !block_track(b))) {
			// The ripple is the length of this block. We assume that for this to happen, it must have
			// been a gap that we will now remove.

			if (redo) {
				// The earliest point changes will happen is at the start of this block
				working_data.earliest_point_of_change = block_in(b);

				// As a removal, we will be shifting from the out point to the in point
				pre_shift = block_out(b);
				post_shift = block_in(b);

				// Remove gap from track and from graph
				working_data.removed_gap = b;
				working_data.removed_gap_after = block_previous(b);
				oaknode_track_ripple_remove_block(track, b);
				block_remove_from_graph(b, track);
				working_data.removed_gap_orphaned = true;
			} else {
				// Restore gap to graph and track
				block_add_to_graph(b, track);
				oaknode_track_insert_block_after(
					track, b, working_data.removed_gap_after);
				working_data.removed_gap_orphaned = false;

				// The earliest point changes will happen is at the start of this block
				working_data.earliest_point_of_change = block_in(b);

				// As an insert, we will be shifting from the block's in point to its out point
				pre_shift = block_in(b);
				post_shift = block_out(b);
			}

		} else {
			// Store old length
			working_data.old_length = block_length(b);

			if (movement_mode_ == Timeline::k_trim_in) {
				// The earliest point changes will occur is in point of this bloc
				working_data.earliest_point_of_change = block_in(b);

				// Undo the trim in inversion we do above, this will still be inverted accurately for
				// undoing where appropriate
				Rational inverted = -operation_movement;
				if (inverted > 0) {
					pre_shift = block_in(b) + inverted;
					post_shift = block_in(b);
				} else {
					pre_shift = block_in(b);
					post_shift = block_in(b) - inverted;
				}

				// Update length
				block_set_length_and_media_in(b, new_block_length);
			} else {
				// The earliest point changes will occur is the out point if trimming out or the in point
				// if trimming in
				working_data.earliest_point_of_change = block_out(b);

				// The latest out before the ripple is this block's current out point
				pre_shift = block_out(b);

				// Update length
				block_set_length_and_media_out(b, new_block_length);

				// The latest out after the ripple is this block's out point after the length change
				post_shift = block_out(b);
			}
		}

		working_data_[track] = working_data;

		pre_latest_out = std::max(pre_latest_out, pre_shift);
		post_latest_out = std::max(post_latest_out, post_shift);
	}
}

//
// TimelineRippleDeleteGapsAtRegionsCommand
//
namespace
{

bool is_gap(OakNodeBlock *b)
{
	if (!b) {
		return false;
	}
	int kind = OAKNODE_BLOCK_OTHER;
	oaknode_block_get_kind(b, &kind);
	return kind == OAKNODE_BLOCK_GAP;
}

} // namespace

void TimelineRippleDeleteGapsAtRegionsCommand::prepare()
{
	size_t max_gaps = 0;
	std::map<OakNodeTrack *, std::vector<RemovalRequest>> requested_gaps;

	// Convert regions to gaps
	for (const auto &region : regions_) {
		OakNodeTrack *track = region.first;
		const TimeRange &range = region.second;

		int n, d;
		rat_nd(range.in(), &n, &d);
		OakNodeBlock *block = nullptr;
		oaknode_track_get_nearest_block_before_or_at(track, n, d, &block);

		if (is_gap(block)) {
			std::vector<RemovalRequest> &gaps_on_track =
				requested_gaps[track];

			RemovalRequest this_req = { block, range };

			// Insertion sort
			bool inserted = false;
			for (size_t i = 0; i < gaps_on_track.size(); i++) {
				if (gaps_on_track.at(i).range.in() < range.in()) {
					gaps_on_track.insert(gaps_on_track.begin() + i, this_req);
					inserted = true;
					break;
				}
			}
			if (!inserted) {
				gaps_on_track.push_back(this_req);
			}

			max_gaps = std::max(max_gaps, gaps_on_track.size());
		} else {
			fprintf(stderr, "Failed to find corresponding gap to region\n");
		}
	}

	// For each gap on each track, find a corresponding gap on every other track (which may include
	// a requested gap) to ripple in order to keep everything synchronized
	std::map<OakNodeBlock *, Rational> gap_lengths;
	for (size_t gap_index = 0; gap_index < max_gaps; gap_index++) {
		Rational earliest_point = RATIONAL_MAX;
		Rational ripple_length = RATIONAL_MAX;
		Rational latest_point = RATIONAL_MIN;

		for (const auto &pair : requested_gaps) {
			const std::vector<RemovalRequest> &gaps_on_track = pair.second;
			if (gap_index < gaps_on_track.size()) {
				const RemovalRequest &gap = gaps_on_track.at(gap_index);
				earliest_point = std::min(earliest_point, gap.range.in());
				ripple_length = std::min(ripple_length, gap.range.length());
				latest_point = std::max(latest_point, gap.range.out());
			}
		}

		// Determine which gaps will be involved in this operation
		std::vector<OakNodeBlock *> gaps;

		int track_count = 0;
		oaknode_sequence_get_all_track_count(timeline_, &track_count);
		for (int ti = 0; ti < track_count; ti++) {
			OakNodeTrack *track = nullptr;
			oaknode_sequence_get_all_track_at(timeline_, ti, &track);
			if (!track) {
				continue;
			}

			int locked = 0;
			oaknode_track_get_locked(track, &locked);
			if (locked) {
				continue;
			}

			auto req_it = requested_gaps.find(track);
			const std::vector<RemovalRequest> empty_list;
			const std::vector<RemovalRequest> &requested_gaps_on_track =
				req_it != requested_gaps.end() ? req_it->second : empty_list;

			OakNodeBlock *gap = nullptr;
			if (gap_index < requested_gaps_on_track.size()) {
				// A requested gap was at this index, use it
				gap = requested_gaps_on_track.at(gap_index).gap;
			} else {
				// No requested gap was at this index, find one
				int n, d;
				rat_nd(earliest_point, &n, &d);
				OakNodeBlock *block = nullptr;
				oaknode_track_get_nearest_block_after_or_at(track, n, d,
															&block);

				if (block) {
					// Found a block, test if it's a gap
					if (is_gap(block)) {
						gap = block;
					} else {
						if (block_in(block) == earliest_point) {
							OakNodeBlock *next = block_next(block);
							if (is_gap(next)) {
								gap = next;
							} else {
								ripple_length = 0;
							}
						} else {
							OakNodeBlock *prev = block_previous(block);
							if (is_gap(prev)) {
								gap = prev;
							} else {
								ripple_length = 0;
							}
						}
					}
				} else {
					// Assume track finishes here and track won't be affected by this operation
				}
			}

			if (gap) {
				gaps.push_back(gap);

				if (!gap_lengths.count(gap)) {
					gap_lengths[gap] = block_length(gap);
				}

				ripple_length = std::min(ripple_length, gap_lengths[gap]);
			}

			if (ripple_length == 0) {
				break;
			}
		}

		if (ripple_length > 0) {
			for (OakNodeBlock *gap : gaps) {
				if (gap_lengths[gap] == ripple_length) {
					commands_.push_back(new TrackRippleRemoveBlockCommand(
						block_track(gap), gap));
				} else {
					gap_lengths[gap] -= ripple_length;
					commands_.push_back(
						new BlockResizeCommand(gap, gap_lengths[gap]));
				}
			}
		}
	}
}

void TimelineRippleDeleteGapsAtRegionsCommand::redo()
{
	for (UndoCommand *c : commands_) {
		c->redo_now();
	}
}

void TimelineRippleDeleteGapsAtRegionsCommand::undo()
{
	for (auto it = commands_.rbegin(); it != commands_.rend(); it++) {
		(*it)->undo_now();
	}
}

}
