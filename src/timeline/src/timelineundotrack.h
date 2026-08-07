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

#ifndef OAK_TIMELINEUNDOTRACK_H
#define OAK_TIMELINEUNDOTRACK_H

#include "node/block.h"
#include "node/track.h"
#include "undocommand.h"

namespace olive
{

/**
 * @brief Undo commands over oaknode track/block handles
 *
 * All graph operations go through the oaknode C ABI (01 §0 rule 6: no
 * cross-module C++ member calls).
 */
class TrackRippleRemoveBlockCommand : public UndoCommand {
public:
	TrackRippleRemoveBlockCommand(OakNodeTrack track, OakNodeBlock block)
		: track_(track)
		, block_(block)
		, before_{}
	{
	}

protected:
	virtual void redo() override
	{
		oaknode_block_get_previous(block_, &before_);
		oaknode_track_ripple_remove_block(track_, block_);
	}

	virtual void undo() override
	{
		oaknode_track_insert_block_after(track_, block_, before_);
	}

private:
	OakNodeTrack track_;

	OakNodeBlock block_;

	OakNodeBlock before_;
};

class TrackPrependBlockCommand : public UndoCommand {
public:
	TrackPrependBlockCommand(OakNodeTrack track, OakNodeBlock block)
		: track_(track)
		, block_(block)
	{
	}

protected:
	virtual void redo() override
	{
		oaknode_track_prepend_block(track_, block_);
	}

	virtual void undo() override
	{
		oaknode_track_ripple_remove_block(track_, block_);
	}

private:
	OakNodeTrack track_;
	OakNodeBlock block_;
};

class TrackInsertBlockAfterCommand : public UndoCommand {
public:
	TrackInsertBlockAfterCommand(OakNodeTrack track, OakNodeBlock block,
								 OakNodeBlock before)
		: track_(track)
		, block_(block)
		, before_(before)
	{
	}

protected:
	virtual void redo() override
	{
		oaknode_track_insert_block_after(track_, block_, before_);
	}

	virtual void undo() override
	{
		oaknode_track_ripple_remove_block(track_, block_);
	}

private:
	OakNodeTrack track_;

	OakNodeBlock block_;

	OakNodeBlock before_;
};

/**
 * @brief Replaces Block `old` with Block `replace`
 *
 * Both blocks must have equal lengths.
 */
class TrackReplaceBlockCommand : public UndoCommand {
public:
	TrackReplaceBlockCommand(OakNodeTrack track, OakNodeBlock old_block,
							 OakNodeBlock replace)
		: track_(track)
		, old_(old_block)
		, replace_(replace)
	{
	}

protected:
	virtual void redo() override
	{
		oaknode_track_replace_block(track_, old_, replace_);
	}

	virtual void undo() override
	{
		oaknode_track_replace_block(track_, replace_, old_);
	}

private:
	OakNodeTrack track_;
	OakNodeBlock old_;
	OakNodeBlock replace_;
};

}

#endif // OAK_TIMELINEUNDOTRACK_H
