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

#ifndef OAK_TIMELINEUNDOCOMMON_H
#define OAK_TIMELINEUNDOCOMMON_H

#include "node/node.h"
#include "undo/undocommand.h"

namespace olive
{

inline bool node_can_be_removed(OakNodeNode *n)
{
	int count = 0;
	oaknode_node_output_connection_count(n, &count);
	return count == 0;
}

inline bool node_can_be_removed(OakNodeBlock *b)
{
	return node_can_be_removed(oaknode_block_as_node(b));
}

inline OakUndoCommand create_remove_command(OakNodeNode *n)
{
	return oaknode_command_create_remove_node(n);
}

inline OakUndoCommand create_remove_command(OakNodeBlock *b)
{
	return oaknode_command_create_remove_node(oaknode_block_as_node(b));
}

inline OakUndoCommand create_and_run_remove_command(OakNodeNode *n)
{
	OakUndoCommand command = create_remove_command(n);
	oakundo_command_redo_now(command);
	return command;
}

inline OakUndoCommand create_and_run_remove_command(OakNodeBlock *b)
{
	return create_and_run_remove_command(oaknode_block_as_node(b));
}

inline void free_command_handle(OakUndoCommand *command)
{
	oakundo_command_free(command);
}

/**
 * @brief Wrap an oakundo C command handle as an olive::UndoCommand so it
 * can live in this module's C++ command trees
 */
class CHandleCommandWrapper : public UndoCommand {
public:
	CHandleCommandWrapper(OakUndoCommand command)
		: command_(command)
	{
	}

	virtual ~CHandleCommandWrapper() override
	{
		if (command_.ctx) {
			oakundo_command_free(&command_);
		}
	}

	bool is_valid() const
	{
		return command_.ctx != nullptr;
	}

protected:
	virtual void redo() override
	{
		if (command_.ctx) {
			oakundo_command_redo_now(command_);
		}
	}

	virtual void undo() override
	{
		if (command_.ctx) {
			oakundo_command_undo_now(command_);
		}
	}

private:
	OakUndoCommand command_;
};

}

#endif // OAK_TIMELINEUNDOCOMMON_H
