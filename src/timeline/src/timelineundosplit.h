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

#ifndef OAK_TIMELINEUNDOSPLIT_H
#define OAK_TIMELINEUNDOSPLIT_H

#include <string>
#include <vector>

#include <olive/core/core.h>

#include "node/block.h"
#include "node/track.h"
#include "undo/undocommand.h"
#include "undocommand.h"

using namespace olive::core;

namespace olive
{

/**
 * @brief Undo commands for splitting blocks, over oaknode C handles
 *
 * All graph operations go through the oaknode C ABI (01 §0 rule 6).
 */
class BlockSplitCommand : public UndoCommand {
public:
	BlockSplitCommand(OakNodeBlock *block, Rational point)
		: block_(block)
		, new_block_(nullptr)
		, point_(point)
		, reconnect_tree_command_({})
		, moved_transition_(nullptr)
	{
	}

	virtual ~BlockSplitCommand() override;

	/**
	 * @brief Access the second block created as a result. Only valid after redo().
	 */
	OakNodeBlock *new_block()
	{
		return new_block_;
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	OakNodeBlock *block_;
	OakNodeBlock *new_block_;

	Rational old_length_;
	Rational point_;

	OakUndoCommand reconnect_tree_command_;

	OakNodeBlock *moved_transition_;
	std::string moved_transition_input_;
};

class BlockSplitPreservingLinksCommand : public UndoCommand {
public:
	BlockSplitPreservingLinksCommand(
		const std::vector<OakNodeBlock *> &blocks,
		const std::vector<Rational> &times)
		: blocks_(blocks)
		, times_(times)
	{
	}

	virtual ~BlockSplitPreservingLinksCommand() override;

	OakNodeBlock *get_split(OakNodeBlock *original, int time_index) const;

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
	std::vector<OakNodeBlock *> blocks_;

	std::vector<Rational> times_;

	std::vector<UndoCommand *> commands_;

	std::vector<std::vector<OakNodeBlock *>> splits_;
};

class TrackSplitAtTimeCommand : public UndoCommand {
public:
	TrackSplitAtTimeCommand(OakNodeTrack *track, Rational point)
		: track_(track)
		, point_(point)
		, command_(nullptr)
	{
	}

	virtual ~TrackSplitAtTimeCommand() override
	{
		delete command_;
	}

protected:
	virtual void prepare() override;

	virtual void redo() override
	{
		if (command_) {
			command_->redo_now();
		}
	}

	virtual void undo() override
	{
		if (command_) {
			command_->undo_now();
		}
	}

private:
	OakNodeTrack *track_;

	Rational point_;

	UndoCommand *command_;
};

}

#endif // OAK_TIMELINEUNDOSPLIT_H
