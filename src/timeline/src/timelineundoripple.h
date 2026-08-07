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

#ifndef OAK_TIMELINEUNDORIPPLE_H
#define OAK_TIMELINEUNDORIPPLE_H

#include <map>
#include <utility>
#include <vector>

#include <olive/core/core.h>

#include "node/block.h"
#include "node/sequence.h"
#include "node/track.h"
#include "timelinecommon.h"
#include "timelineundogeneral.h"
#include "timelineundosplit.h"
#include "timelineundotrack.h"
#include "undocommand.h"

using namespace olive::core;

namespace olive
{

/**
 * @brief Clears the area between in and out
 *
 * The area between `in` and `out` is guaranteed to be freed. Blocks are trimmed and removed to free this space.
 * By default, nothing takes this area meaning all subsequent clips are pushed backward, however you can specify
 * a block to insert at the `in` point. No checking is done to ensure `insert` is the same length as `in` to `out`.
 */
class TrackRippleRemoveAreaCommand : public UndoCommand {
public:
	TrackRippleRemoveAreaCommand(OakNodeTrack track, const TimeRange &range);

	virtual ~TrackRippleRemoveAreaCommand() override;

	/**
	 * @brief Block to insert after if you want to insert something between this ripple
	 */
	OakNodeBlock get_insertion_index() const
	{
		return insert_previous_;
	}

	OakNodeBlock get_spliced_block() const
	{
		if (splice_split_command_) {
			return splice_split_command_->new_block();
		}

		return OakNodeBlock{};
	}

	void set_allow_splitting_gaps(bool e)
	{
		allow_splitting_gaps_ = e;
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	struct TrimOperation {
		OakNodeBlock block;
		Rational old_length;
		Rational new_length;
	};

	struct RemoveOperation {
		OakNodeBlock block;
		OakNodeBlock before;
	};

	OakNodeTrack track_;
	TimeRange range_;

	TrimOperation trim_out_;
	std::vector<RemoveOperation> removals_;
	TrimOperation trim_in_;
	OakNodeBlock insert_previous_;
	bool allow_splitting_gaps_;

	BlockSplitCommand *splice_split_command_;
	std::vector<OakUndoCommand > remove_block_commands_;
};

class TrackListRippleRemoveAreaCommand : public UndoCommand {
public:
	TrackListRippleRemoveAreaCommand(OakNodeTrackList list, Rational in,
									 Rational out)
		: list_(list)
		, range_(in, out)
	{
	}

	virtual ~TrackListRippleRemoveAreaCommand() override
	{
		for (TrackRippleRemoveAreaCommand *c : commands_) {
			delete c;
		}
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeTrackList list_;

	std::vector<OakNodeTrack> working_tracks_;

	TimeRange range_;

	std::vector<TrackRippleRemoveAreaCommand *> commands_;
};

class TimelineRippleRemoveAreaCommand : public MultiUndoCommand {
public:
	TimelineRippleRemoveAreaCommand(OakNodeSequence timeline, Rational in,
									Rational out);
};

class TrackListRippleToolCommand : public UndoCommand {
public:
	struct RippleInfo {
		OakNodeBlock block;
		bool append_gap;
	};

	TrackListRippleToolCommand(
		OakNodeTrackList track_list,
		const std::map<OakNodeTrack, RippleInfo, TrackHandleLess> &info,
		const Rational &ripple_movement,
		const Timeline::MovementMode &movement_mode);

	virtual ~TrackListRippleToolCommand() override;

protected:
	virtual void redo() override
	{
		ripple(true);
	}

	virtual void undo() override
	{
		ripple(false);
	}

private:
	void ripple(bool redo);

	OakNodeTrackList track_list_;

	std::map<OakNodeTrack, RippleInfo, TrackHandleLess> info_;
	Rational ripple_movement_;
	Timeline::MovementMode movement_mode_;

	struct WorkingData {
		OakNodeBlock created_gap{};
		bool created_gap_orphaned = false;
		OakNodeBlock removed_gap{};
		bool removed_gap_orphaned = false;
		OakNodeBlock removed_gap_after{};
		Rational old_length;
		Rational earliest_point_of_change;
	};

	std::map<OakNodeTrack, WorkingData, TrackHandleLess> working_data_;
};

class TimelineRippleDeleteGapsAtRegionsCommand : public UndoCommand {
public:
	using RangeList = std::vector<std::pair<OakNodeTrack, TimeRange>>;

	TimelineRippleDeleteGapsAtRegionsCommand(OakNodeSequence vo,
											 const RangeList &regions)
		: timeline_(vo)
		, regions_(regions)
	{
	}

	virtual ~TimelineRippleDeleteGapsAtRegionsCommand() override
	{
		for (UndoCommand *c : commands_) {
			delete c;
		}
	}

	bool has_commands() const
	{
		return !commands_.empty();
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeSequence timeline_;
	RangeList regions_;

	std::vector<UndoCommand *> commands_;

	struct RemovalRequest {
		OakNodeBlock gap;
		TimeRange range;
	};
};

}

#endif // OAK_TIMELINEUNDORIPPLE_H
