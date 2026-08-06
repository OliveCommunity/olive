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

#include "timelineundopointer.h"

#include "timelineundocommon.h"
#include "timelineutil.h"
#include "undo/undocommand.h"

namespace olive
{

//
// BlockTrimCommand
//
BlockTrimCommand::~BlockTrimCommand()
{
	if (deleted_adjacent_command_) {
		free_remove_command(deleted_adjacent_command_);
	}
	if (adjacent_orphaned_ && adjacent_) {
		oaknode_block_free(adjacent_);
	}
}

void BlockTrimCommand::redo()
{
	if (doing_nothing_) {
		return;
	}

	if (mode_ == Timeline::k_trim_in) {
		block_set_length_and_media_in(block_, new_length_);
	} else {
		block_set_length_and_media_out(block_, new_length_);
	}

	if (needs_adjacent_) {
		if (we_created_adjacent_) {
			// Add adjacent and insert it
			block_add_to_graph(adjacent_, track_);
			if (mode_ == Timeline::k_trim_in) {
				oaknode_track_insert_block_before(track_, adjacent_, block_);
			} else {
				oaknode_track_insert_block_after(track_, adjacent_, block_);
			}
			adjacent_orphaned_ = false;
		} else if (we_removed_adjacent_) {
			oaknode_track_ripple_remove_block(track_, adjacent_);

			// It no longer inputs/outputs anything, remove it
			if (remove_block_from_graph_ && node_can_be_removed(adjacent_)) {
				if (!deleted_adjacent_command_) {
					deleted_adjacent_command_ =
						create_and_run_remove_command(adjacent_);
				} else {
					oakundo_command_redo_now(deleted_adjacent_command_);
				}
			}
		} else {
			Rational adjacent_length = block_length(adjacent_) + trim_diff_;

			if (mode_ == Timeline::k_trim_in) {
				block_set_length_and_media_out(adjacent_, adjacent_length);
			} else {
				block_set_length_and_media_in(adjacent_, adjacent_length);
			}
		}
	}
}

void BlockTrimCommand::undo()
{
	if (doing_nothing_) {
		return;
	}

	// Will be POSITIVE if trimming shorter and NEGATIVE if trimming longer
	if (needs_adjacent_) {
		if (we_created_adjacent_) {
			// Adjacent is ours, just delete it
			oaknode_track_ripple_remove_block(track_, adjacent_);
			block_remove_from_graph(adjacent_, track_);
			adjacent_orphaned_ = true;
		} else {
			if (we_removed_adjacent_) {
				if (deleted_adjacent_command_) {
					// We deleted adjacent, restore it now
					oakundo_command_undo_now(deleted_adjacent_command_);
				}

				if (mode_ == Timeline::k_trim_in) {
					oaknode_track_insert_block_before(track_, adjacent_,
													  block_);
				} else {
					oaknode_track_insert_block_after(track_, adjacent_,
													 block_);
				}
			} else {
				Rational adjacent_length =
					block_length(adjacent_) - trim_diff_;

				if (mode_ == Timeline::k_trim_in) {
					block_set_length_and_media_out(adjacent_,
												   adjacent_length);
				} else {
					block_set_length_and_media_in(adjacent_, adjacent_length);
				}
			}
		}
	}

	if (mode_ == Timeline::k_trim_in) {
		block_set_length_and_media_in(block_, old_length_);
	} else {
		block_set_length_and_media_out(block_, old_length_);
	}
}

void BlockTrimCommand::prepare()
{
	// Store old length
	old_length_ = block_length(block_);

	// Determine if the length isn't changing, in which case we set a flag to do nothing
	if ((doing_nothing_ = (old_length_ == new_length_))) {
		return;
	}

	// Will be POSITIVE if trimming shorter and NEGATIVE if trimming longer
	trim_diff_ = old_length_ - new_length_;

	// Retrieve our adjacent block (or nullptr if none)
	if (mode_ == Timeline::k_trim_in) {
		adjacent_ = block_previous(block_);
	} else {
		adjacent_ = block_next(block_);
	}

	// Ignore when trimming the out with no adjacent, because the user must have trimmed the end
	// of the last block in the track, so we don't need to do anything elses
	needs_adjacent_ = (mode_ == Timeline::k_trim_in || adjacent_);

	if (needs_adjacent_) {
		// If we're trimming shorter, we need an adjacent, so check if we have a viable one.
		int adjacent_kind = OAKNODE_BLOCK_OTHER;
		if (adjacent_) {
			oaknode_block_get_kind(adjacent_, &adjacent_kind);
		}
		we_created_adjacent_ =
			(trim_diff_ > 0 && (!adjacent_ || (adjacent_kind !=
												   OAKNODE_BLOCK_GAP &&
											   !trim_is_a_roll_edit_)));

		if (we_created_adjacent_) {
			// We shortened but don't have a viable adjacent to lengthen, so we create one
			adjacent_ = oaknode_block_gap_create();
			block_set_length_and_media_out(adjacent_, trim_diff_);
			adjacent_orphaned_ = true;
		} else {
			// Determine if we're removing the adjacent
			Rational adjacent_length = block_length(adjacent_) + trim_diff_;
			we_removed_adjacent_ = adjacent_length.isNull();
		}
	}
}

//
// TrackSlideCommand
//
TrackSlideCommand::~TrackSlideCommand()
{
	if (in_adjacent_remove_command_) {
		free_remove_command(in_adjacent_remove_command_);
	}
	if (out_adjacent_remove_command_) {
		free_remove_command(out_adjacent_remove_command_);
	}
	if (in_adjacent_orphaned_ && in_adjacent_) {
		oaknode_block_free(in_adjacent_);
	}
	if (out_adjacent_orphaned_ && out_adjacent_) {
		oaknode_block_free(out_adjacent_);
	}
}

void TrackSlideCommand::redo()
{
	// We will always have an in adjacent if there was a valid slide
	if (we_created_in_adjacent_) {
		// We created in adjacent, so all we have to do is insert it
		block_add_to_graph(in_adjacent_, track_);
		oaknode_track_insert_block_before(track_, in_adjacent_,
										  blocks_.front());
		in_adjacent_orphaned_ = false;
	} else if (-movement_ == block_length(in_adjacent_)) {
		// Movement will remove in adjacent
		oaknode_track_ripple_remove_block(track_, in_adjacent_);

		if (node_can_be_removed(in_adjacent_)) {
			if (!in_adjacent_remove_command_) {
				in_adjacent_remove_command_ =
					create_remove_command(in_adjacent_);
			}

			oakundo_command_redo_now(in_adjacent_remove_command_);
		}

		we_removed_in_adjacent_ = true;
	} else {
		// Simply resize adjacent
		block_set_length_and_media_out(
			in_adjacent_, block_length(in_adjacent_) + movement_);
	}

	// We may not have an out adjacent if the slide was at the end of the track
	if (out_adjacent_) {
		if (we_created_out_adjacent_) {
			// We created out adjacent, so we just have to insert it
			block_add_to_graph(out_adjacent_, track_);
			oaknode_track_insert_block_after(track_, out_adjacent_,
											 blocks_.back());
			out_adjacent_orphaned_ = false;
		} else if (movement_ == block_length(out_adjacent_)) {
			// Movement will remove out adjacent
			oaknode_track_ripple_remove_block(track_, out_adjacent_);

			if (node_can_be_removed(out_adjacent_)) {
				if (!out_adjacent_remove_command_) {
					out_adjacent_remove_command_ =
						create_remove_command(out_adjacent_);
				}

				oakundo_command_redo_now(out_adjacent_remove_command_);
			}

			we_removed_out_adjacent_ = true;
		} else {
			// Simply resize adjacent
			block_set_length_and_media_in(
				out_adjacent_,
				block_length(out_adjacent_) - movement_);
		}
	}
}

void TrackSlideCommand::undo()
{
	if (we_created_in_adjacent_) {
		// We created this, so we can remove it now
		oaknode_track_ripple_remove_block(track_, in_adjacent_);
		block_remove_from_graph(in_adjacent_, track_);
		in_adjacent_orphaned_ = true;
	} else if (we_removed_in_adjacent_) {
		if (in_adjacent_remove_command_) {
			// We removed this, so we can restore it now
			oakundo_command_undo_now(in_adjacent_remove_command_);
		}

		oaknode_track_insert_block_before(track_, in_adjacent_,
										  blocks_.front());
	} else {
		// Simply resize adjacent
		block_set_length_and_media_out(
			in_adjacent_, block_length(in_adjacent_) - movement_);
	}

	if (out_adjacent_) {
		if (we_created_out_adjacent_) {
			// We created this, so we can remove it now
			oaknode_track_ripple_remove_block(track_, out_adjacent_);
			block_remove_from_graph(out_adjacent_, track_);
			out_adjacent_orphaned_ = true;
		} else if (we_removed_out_adjacent_) {
			if (out_adjacent_remove_command_) {
				oakundo_command_undo_now(out_adjacent_remove_command_);
			}

			oaknode_track_insert_block_after(track_, out_adjacent_,
											 blocks_.back());
		} else {
			// Simply resize adjacent
			block_set_length_and_media_in(
				out_adjacent_,
				block_length(out_adjacent_) + movement_);
		}
	}
}

void TrackSlideCommand::prepare()
{
	if (!in_adjacent_) {
		in_adjacent_ = oaknode_block_gap_create();
		block_set_length_and_media_out(in_adjacent_, movement_);
		in_adjacent_orphaned_ = true;
		we_created_in_adjacent_ = true;
	} else {
		we_created_in_adjacent_ = false;
	}

	if (!out_adjacent_ && block_next(blocks_.back())) {
		out_adjacent_ = oaknode_block_gap_create();
		block_set_length_and_media_out(out_adjacent_, -movement_);
		out_adjacent_orphaned_ = true;
		we_created_out_adjacent_ = true;
	} else {
		we_created_out_adjacent_ = false;
	}
}

//
// TrackPlaceBlockCommand
//
TrackPlaceBlockCommand::~TrackPlaceBlockCommand()
{
	delete ripple_remove_command_;
	for (TimelineAddTrackCommand *c : add_track_commands_) {
		delete c;
	}
	if (gap_orphaned_ && gap_) {
		oaknode_block_free(gap_);
	}
}

void TrackPlaceBlockCommand::redo()
{
	// Determine if we need to add tracks
	int track_count = 0;
	oaknode_tracklist_get_track_count(timeline_, &track_count);

	if (track_index_ >= track_count) {
		if (add_track_commands_.empty()) {
			// First redo, create tracks now
			add_track_commands_.resize(track_index_ - track_count + 1);

			for (size_t i = 0; i < add_track_commands_.size(); i++) {
				add_track_commands_[i] =
					new TimelineAddTrackCommand(timeline_);
			}
		}

		for (TimelineAddTrackCommand *c : add_track_commands_) {
			c->redo_now();
		}
	}

	OakNodeTrack *track = nullptr;
	oaknode_tracklist_get_track_at(timeline_, track_index_, &track);

	bool append = (in_ >= track_length(track));

	// Check if the placement location is past the end of the timeline
	if (append) {
		if (in_ > track_length(track)) {
			// If so, insert a gap here
			if (!gap_) {
				gap_ = oaknode_block_gap_create();
				block_set_length_and_media_out(gap_,
											   in_ - track_length(track));
			}
			block_add_to_graph(gap_, track);
			oaknode_track_append_block(track, gap_);
			gap_orphaned_ = false;
		}

		oaknode_track_append_block(track, insert_);
	} else {
		// Place the Block at this point
		if (!ripple_remove_command_) {
			ripple_remove_command_ = new TrackRippleRemoveAreaCommand(
				track, TimeRange(in_, in_ + block_length(insert_)));
			ripple_remove_command_->set_allow_splitting_gaps(true);
		}

		ripple_remove_command_->redo_now();
		oaknode_track_insert_block_after(
			track, insert_, ripple_remove_command_->get_insertion_index());
	}
}

void TrackPlaceBlockCommand::undo()
{
	OakNodeTrack *t = nullptr;
	oaknode_tracklist_get_track_at(timeline_, track_index_, &t);

	// Firstly, remove our insert
	oaknode_track_ripple_remove_block(t, insert_);

	if (ripple_remove_command_) {
		// If we ripple removed, just undo that
		ripple_remove_command_->undo_now();
	} else if (gap_) {
		oaknode_track_ripple_remove_block(t, gap_);
		block_remove_from_graph(gap_, t);
		gap_orphaned_ = true;
	}

	// Remove tracks if we added them
	for (auto it = add_track_commands_.rbegin();
		 it != add_track_commands_.rend(); it++) {
		(*it)->undo_now();
	}
}

}
