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

#include "node/output/track/track.h"

namespace olive
{

class BlockSplitCommand : public UndoCommand {
public:
	BlockSplitCommand(Block *block, Rational point)
		: block_(block)
		, new_block_(nullptr)
		, point_(point)
		, reconnect_tree_command_(nullptr)
	{
	}

	virtual ~BlockSplitCommand() override
	{
		delete reconnect_tree_command_;
	}

	virtual Project *get_relevant_project() const override
	{
		return block_->project();
	}

	/**
   * @brief Access the second block created as a result. Only valid after redo().
   */
	Block *new_block()
	{
		return new_block_;
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	Block *block_;
	Block *new_block_;

	Rational old_length_;
	Rational point_;

	MultiUndoCommand *reconnect_tree_command_;

	NodeInput moved_transition_;
};

class BlockSplitPreservingLinksCommand : public UndoCommand {
public:
	BlockSplitPreservingLinksCommand(const QVector<Block *> &blocks,
									 const QList<Rational> &times)
		: blocks_(blocks)
		, times_(times)
	{
	}

	virtual ~BlockSplitPreservingLinksCommand() override
	{
		qDeleteAll(commands_);
	}

	virtual Project *get_relevant_project() const override
	{
		return blocks_.first()->project();
	}

	Block *get_split(Block *original, int time_index) const;

protected:
	virtual void prepare() override;

	virtual void redo() override
	{
		for (int i = 0; i < commands_.size(); i++) {
			commands_.at(i)->redo_now();
		}
	}

	virtual void undo() override
	{
		for (int i = commands_.size() - 1; i >= 0; i--) {
			commands_.at(i)->undo_now();
		}
	}

private:
	QVector<Block *> blocks_;

	QList<Rational> times_;

	QVector<UndoCommand *> commands_;

	QVector<QVector<Block *>> splits_;
};

class TrackSplitAtTimeCommand : public UndoCommand {
public:
	TrackSplitAtTimeCommand(Track *track, Rational point)
		: track_(track)
		, point_(point)
		, command_(nullptr)
	{
	}

	virtual ~TrackSplitAtTimeCommand() override
	{
		delete command_;
	}

	virtual Project *get_relevant_project() const override
	{
		return track_->project();
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
	Track *track_;

	Rational point_;

	UndoCommand *command_;
};

}

#endif // OAK_TIMELINEUNDOSPLIT_H
