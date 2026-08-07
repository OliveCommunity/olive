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

#ifndef OAK_TIMELINEUNDOGENERAL_H
#define OAK_TIMELINEUNDOGENERAL_H

#include <map>
#include <string>
#include <vector>

#include <olive/core/core.h>

#include "node/block.h"
#include "node/sequence.h"
#include "node/track.h"
#include "timelineundosplit.h"
#include "undocommand.h"

using namespace olive::core;

namespace olive
{

class BlockResizeCommand : public UndoCommand {
public:
	BlockResizeCommand(OakNodeBlock *block, Rational new_length)
		: block_(block)
		, new_length_(new_length)
	{
	}

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	OakNodeBlock *block_;
	Rational old_length_;
	Rational new_length_;
};

class BlockResizeWithMediaInCommand : public UndoCommand {
public:
	BlockResizeWithMediaInCommand(OakNodeBlock *block, Rational new_length)
		: block_(block)
		, new_length_(new_length)
	{
	}

protected:
	virtual void redo();
	virtual void undo();

private:
	OakNodeBlock *block_;
	Rational old_length_;
	Rational new_length_;
};

class BlockSetMediaInCommand : public UndoCommand {
public:
	BlockSetMediaInCommand(OakNodeBlock *block, Rational new_media_in)
		: block_(block)
		, new_media_in_(new_media_in)
	{
	}

protected:
	virtual void redo();
	virtual void undo();

private:
	OakNodeBlock *block_;
	Rational old_media_in_;
	Rational new_media_in_;
};

class TimelineAddTrackCommand : public UndoCommand {
public:
	TimelineAddTrackCommand(OakNodeTrackList *timeline);
	TimelineAddTrackCommand(OakNodeTrackList *timeline, bool automerge_tracks);

	virtual ~TimelineAddTrackCommand() override;

	static OakNodeTrack *run_immediately(OakNodeTrackList *timeline)
	{
		TimelineAddTrackCommand c(timeline);
		c.redo();
		return c.track();
	}

	static OakNodeTrack *run_immediately(OakNodeTrackList *timeline,
										 bool automerge)
	{
		TimelineAddTrackCommand c(timeline, automerge);
		c.redo();
		return c.track();
	}

	OakNodeTrack *track() const
	{
		return track_;
	}

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeTrackList *timeline_;

	OakNodeTrack *track_;
	OakNodeNode *merge_;
	std::string base_input_;
	std::string blend_input_;

	std::string direct_input_;

	OakUndoCommand position_command_;

	bool automerge_tracks_;

	// Ownership: true while the node is detached from the graph and this
	// command must free it on destruction
	bool track_orphaned_;
	bool merge_orphaned_;
};

class TimelineRemoveTrackCommand : public UndoCommand {
public:
	TimelineRemoveTrackCommand(OakNodeTrack *track)
		: track_(track)
		, list_(nullptr)
		, index_(0)
		, remove_command_({})
	{
	}

	virtual ~TimelineRemoveTrackCommand() override;

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeTrack *track_;

	OakNodeTrackList *list_;

	int index_;

	OakUndoCommand remove_command_;
};

class TransitionRemoveCommand : public UndoCommand {
public:
	TransitionRemoveCommand(OakNodeBlock *block, bool remove_from_graph)
		: block_(block)
		, track_(nullptr)
		, out_block_(nullptr)
		, in_block_(nullptr)
		, remove_from_graph_(remove_from_graph)
		, remove_command_({})
	{
	}

	virtual ~TransitionRemoveCommand() override;

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeBlock *block_;

	OakNodeTrack *track_;

	OakNodeBlock *out_block_;
	OakNodeBlock *in_block_;

	bool remove_from_graph_;
	OakUndoCommand remove_command_;
};

class TrackReplaceBlockWithGapCommand : public UndoCommand {
public:
	TrackReplaceBlockWithGapCommand(OakNodeTrack *track, OakNodeBlock *block,
									bool handle_transitions = true)
		: track_(track)
		, block_(block)
		, existing_gap_(nullptr)
		, existing_merged_gap_(nullptr)
		, existing_gap_precedes_(false)
		, our_gap_(nullptr)
		, handle_transitions_(handle_transitions)
		, our_gap_orphaned_(false)
		, merged_gap_orphaned_(false)
	{
	}

	virtual ~TrackReplaceBlockWithGapCommand() override;

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	void create_remove_transition_command_if_necessary(bool next);

	OakNodeTrack *track_;
	OakNodeBlock *block_;

	OakNodeBlock *existing_gap_;
	OakNodeBlock *existing_merged_gap_;
	bool existing_gap_precedes_;
	OakNodeBlock *our_gap_;

	bool handle_transitions_;

	bool our_gap_orphaned_;
	bool merged_gap_orphaned_;

	std::vector<TransitionRemoveCommand *> transition_remove_commands_;
};

class BlockEnableDisableCommand : public UndoCommand {
public:
	BlockEnableDisableCommand(OakNodeBlock *block, bool enabled)
		: block_(block)
		, new_enabled_(enabled)
	{
		old_enabled_ = false;
		oaknode_block_get_enabled(block_, &old_enabled_);
	}

protected:
	virtual void redo() override
	{
		oaknode_block_set_enabled(block_, new_enabled_);
	}

	virtual void undo() override
	{
		oaknode_block_set_enabled(block_, old_enabled_);
	}

private:
	OakNodeBlock *block_;

	int old_enabled_;

	int new_enabled_;
};

class TrackListInsertGaps : public UndoCommand {
public:
	TrackListInsertGaps(OakNodeTrackList *track_list, const Rational &point,
						const Rational &length)
		: track_list_(track_list)
		, point_(point)
		, length_(length)
		, split_command_(nullptr)
	{
	}

	virtual ~TrackListInsertGaps() override;

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeTrackList *track_list_;

	Rational point_;

	Rational length_;

	std::vector<OakNodeTrack *> working_tracks_;

	std::vector<OakNodeBlock *> gaps_to_extend_;

	struct AddGap {
		OakNodeBlock *gap;
		bool orphaned;
		OakNodeBlock *before;
		OakNodeTrack *track;
	};

	std::vector<AddGap> gaps_added_;

	BlockSplitPreservingLinksCommand *split_command_;
};

class TimelineAddDefaultTransitionCommand : public UndoCommand {
public:
	TimelineAddDefaultTransitionCommand(
		const std::vector<OakNodeBlock *> &clips, const Rational &timebase)
		: clips_(clips)
		, timebase_(timebase)
	{
	}

	virtual ~TimelineAddDefaultTransitionCommand() override
	{
		for (UndoCommand *c : commands_) {
			delete c;
		}
	}

protected:
	virtual void prepare() override;

	virtual void redo() override
	{
		for (UndoCommand *c : commands_) {
			c->redo_now();
		}
	}

	virtual void undo() override
	{
		for (auto it = commands_.rbegin(); it != commands_.rend(); it++) {
			(*it)->undo_now();
		}
	}

private:
	enum CreateTransitionMode { k_in, k_out, k_out_dual };

	void add_transition(OakNodeBlock *c, CreateTransitionMode mode);
	void adjust_clip_length(OakNodeBlock *c, const Rational &transition_length,
							bool out);
	void validate_transition_length(OakNodeBlock *c,
									Rational &transition_length);

	std::vector<OakNodeBlock *> clips_;
	Rational timebase_;
	std::vector<UndoCommand *> commands_;

	std::map<OakNodeBlock *, Rational> lengths_;
};

}

#endif // OAK_TIMELINEUNDOGENERAL_H
