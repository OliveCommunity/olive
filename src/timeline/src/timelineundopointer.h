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

#ifndef OAK_TIMELINEUNDOPOINTER_H
#define OAK_TIMELINEUNDOPOINTER_H

#include <vector>

#include <olive/core/core.h>

#include "node/block.h"
#include "node/track.h"
#include "timelinecommon.h"
#include "timelineundogeneral.h"
#include "timelineundoripple.h"
#include "undocommand.h"

using namespace olive::core;

namespace olive
{

/**
 * @brief Performs a trim in the timeline that only affects the block and the block adjacent
 *
 * Changes the length of one block while also changing the length of the block directly adjacent
 * to compensate so that the rest of the track is unaffected.
 *
 * By default, this will only affect the length of gaps. If the adjacent needs to increase its
 * length and is not a gap, a gap will be created and inserted to fill that time. This command can
 * be set to always trim even if the adjacent clip isn't a gap with set_trim_is_a_roll_edit()
 */
class BlockTrimCommand : public UndoCommand {
public:
	BlockTrimCommand(OakNodeTrack *track, OakNodeBlock *block,
					 Rational new_length, Timeline::MovementMode mode)
		: track_(track)
		, block_(block)
		, new_length_(new_length)
		, mode_(mode)
		, deleted_adjacent_command_({})
		, trim_is_a_roll_edit_(false)
		, remove_block_from_graph_(true)
		, doing_nothing_(false)
		, adjacent_(nullptr)
		, needs_adjacent_(false)
		, we_created_adjacent_(false)
		, we_removed_adjacent_(false)
		, adjacent_orphaned_(false)
	{
	}

	virtual ~BlockTrimCommand() override;

	/**
	 * @brief Set this if the trim should always affect the adjacent clip and not create a gap
	 */
	void set_trim_is_a_roll_edit(bool e)
	{
		trim_is_a_roll_edit_ = e;
	}

	/**
	 * @brief Set whether adjacent blocks set to zero length should be removed from the whole graph
	 *
	 * If an adjacent block's length is set to 0, it's automatically removed from the track. By
	 * default it also gets removed from the whole graph. Set this to FALSE to disable that
	 * functionality.
	 */
	void set_remove_zero_length_from_graph(bool e)
	{
		remove_block_from_graph_ = e;
	}

protected:
	virtual void prepare() override;
	virtual void redo() override;
	virtual void undo() override;

private:
	bool doing_nothing_;
	Rational trim_diff_;

	OakNodeTrack *track_;
	OakNodeBlock *block_;
	Rational old_length_;
	Rational new_length_;
	Timeline::MovementMode mode_;

	OakNodeBlock *adjacent_;
	bool needs_adjacent_;
	bool we_created_adjacent_;
	bool we_removed_adjacent_;
	OakUndoCommand deleted_adjacent_command_;

	bool trim_is_a_roll_edit_;
	bool remove_block_from_graph_;

	// Owned while the created adjacent gap is detached from the graph
	bool adjacent_orphaned_;
};

class TrackSlideCommand : public UndoCommand {
public:
	TrackSlideCommand(OakNodeTrack *track,
					  const std::vector<OakNodeBlock *> &moving_blocks,
					  OakNodeBlock *in_adjacent, OakNodeBlock *out_adjacent,
					  const Rational &movement)
		: track_(track)
		, blocks_(moving_blocks)
		, movement_(movement)
		, we_created_in_adjacent_(false)
		, we_removed_in_adjacent_(false)
		, in_adjacent_(in_adjacent)
		, in_adjacent_remove_command_({})
		, in_adjacent_orphaned_(false)
		, we_created_out_adjacent_(false)
		, we_removed_out_adjacent_(false)
		, out_adjacent_(out_adjacent)
		, out_adjacent_remove_command_({})
		, out_adjacent_orphaned_(false)
	{
	}

	virtual ~TrackSlideCommand() override;

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeTrack *track_;
	std::vector<OakNodeBlock *> blocks_;
	Rational movement_;

	bool we_created_in_adjacent_;
	bool we_removed_in_adjacent_;
	OakNodeBlock *in_adjacent_;
	OakUndoCommand in_adjacent_remove_command_;
	bool in_adjacent_orphaned_;
	bool we_created_out_adjacent_;
	bool we_removed_out_adjacent_;
	OakNodeBlock *out_adjacent_;
	OakUndoCommand out_adjacent_remove_command_;
	bool out_adjacent_orphaned_;
};

/**
 * @brief Destructively places `block` at the in point `start`
 *
 * The Block is guaranteed to be placed at the starting point specified. If there are Blocks in this area, they are
 * either trimmed or removed to make space for this Block. Additionally, if the Block is placed beyond the end of
 * the Sequence, a GapBlock is inserted to compensate.
 */
class TrackPlaceBlockCommand : public UndoCommand {
public:
	TrackPlaceBlockCommand(OakNodeTrackList *timeline, int track,
						   OakNodeBlock *block, Rational in)
		: timeline_(timeline)
		, track_index_(track)
		, in_(in)
		, gap_(nullptr)
		, gap_orphaned_(false)
		, insert_(block)
		, ripple_remove_command_(nullptr)
	{
	}

	virtual ~TrackPlaceBlockCommand() override;

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeTrackList *timeline_;
	int track_index_;
	Rational in_;
	OakNodeBlock *gap_;
	bool gap_orphaned_;
	OakNodeBlock *insert_;
	std::vector<TimelineAddTrackCommand *> add_track_commands_;
	TrackRippleRemoveAreaCommand *ripple_remove_command_;
};

}

#endif // OAK_TIMELINEUNDOPOINTER_H
