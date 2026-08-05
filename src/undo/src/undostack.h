/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef OAK_UNDOSTACK_H
#define OAK_UNDOSTACK_H

#include <functional>
#include <list>
#include <string>

#include "define.h"
#include "undocommand.h"

namespace olive
{

/**
 * @brief History stack of undoable commands.
 *
 * The Qt version inherited QAbstractItemModel and owned undo/redo
 * QActions; both are UI concerns and were removed in oakundo (they belong
 * to the app layer). The index_changed signal is replaced by an optional
 * std::function callback (set_index_changed_callback()).
 */
class UndoStack {
public:
	UndoStack();

	virtual ~UndoStack();

	DISABLE_COPY_MOVE(UndoStack)

	void push(UndoCommand *command, const std::string &name);

	/**
	 * @brief Push a command that has already been executed (redo skipped).
	 *
	 * Used by the facade undo-group: child commands are added to the group
	 * and executed eagerly, then the whole group is pushed with this method
	 * so it is not redone again. Empty commands are discarded.
	 */
	void push_pre_executed(UndoCommand *command, const std::string &name);

	void jump(size_t index);

	void clear();

	bool can_undo() const;

	bool can_redo() const
	{
		return !undone_commands_.empty();
	}

	void undo();

	void redo();

	/**
	 * @brief Set the callback fired after every stack mutation with the
	 * current done-command count (replaces the Qt index_changed signal).
	 */
	void set_index_changed_callback(std::function<void(int)> callback)
	{
		index_changed_callback_ = std::move(callback);
	}

	// Facade accessors (oakengine/undo.h C ABI): row-based history queries.
	// Rows 0..done_count()-1 are done commands (commands_ in order), rows
	// done_count()..command_count()-1 are undone commands (undone_commands_
	// in order, most recently undone first).
	int command_count() const
	{
		return int(commands_.size() + undone_commands_.size());
	}

	int done_count() const
	{
		return int(commands_.size());
	}

	bool command_is_done(int row) const
	{
		return row >= 0 && row < done_count();
	}

	std::string command_name(int row) const
	{
		if (row < 0 || row >= command_count()) {
			return std::string();
		}
		if (row < done_count()) {
			auto it = commands_.begin();
			std::advance(it, row);
			return it->name;
		}
		auto it = undone_commands_.begin();
		std::advance(it, row - done_count());
		return it->name;
	}

private:
	static const int k_max_undo_commands;

	struct CommandEntry {
		UndoCommand *command;
		std::string name;
	};

	void emit_index_changed();

	std::list<CommandEntry> commands_;

	std::list<CommandEntry> undone_commands_;

	std::function<void(int)> index_changed_callback_;
};

}

#endif // OAK_UNDOSTACK_H
