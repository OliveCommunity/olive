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

#ifndef OAK_UNDOCOMMAND_H
#define OAK_UNDOCOMMAND_H

#include <functional>
#include <vector>

#include "define.h"

namespace olive
{

/**
 * @brief Base class for an undoable operation.
 *
 * Subclasses implement redo() and undo(). The Qt version coupled this
 * class to olive::Project through get_relevant_project(); oakundo instead
 * lets the caller attach a modified-state callback pair with
 * set_modified_callbacks(). When callbacks are attached,
 * redo_and_set_modified() records the current modified state and forces
 * "modified", and undo_and_set_modified() restores the recorded state.
 * Without callbacks those functions simply redo/undo.
 */
class UndoCommand {
public:
	UndoCommand();

	virtual ~UndoCommand()
	{
	}

	DISABLE_COPY_MOVE(UndoCommand)

	bool has_prepared() const
	{
		return prepared_;
	}
	void set_prepared(bool e)
	{
		prepared_ = true;
	}

	void redo_now();
	void undo_now();

	/**
	 * @brief Mark the command as already executed (done) without running
	 * redo. Used by UndoStack::push_pre_executed() so the command remains
	 * undoable.
	 */
	void set_done(bool e)
	{
		done_ = e;
	}

	void redo_and_set_modified();
	void undo_and_set_modified();

	/**
	 * @brief Attach project modified-state accessors.
	 *
	 * @param is_modified Getter for the current modified flag.
	 * @param set_modified Setter for the modified flag.
	 *
	 * Either may be empty; both directions are guarded. Passing two empty
	 * functions detaches the callbacks.
	 */
	void set_modified_callbacks(std::function<bool()> is_modified,
								std::function<void(bool)> set_modified);

protected:
	virtual void prepare()
	{
	}
	virtual void redo() = 0;
	virtual void undo() = 0;

private:
	bool modified_;

	bool prepared_;

	bool done_;

	std::function<bool()> is_modified_;

	std::function<void(bool)> set_modified_;
};

class MultiUndoCommand : public UndoCommand {
public:
	MultiUndoCommand() = default;

	/**
	 * @brief Destructor. Deletes all owned children (added with
	 * add_child() and not yet pushed elsewhere).
	 */
	virtual ~MultiUndoCommand() override;

	void add_child(UndoCommand *command)
	{
		children_.push_back(command);
	}

	int child_count() const
	{
		return children_.size();
	}

	UndoCommand *child(int i) const
	{
		return children_[i];
	}

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	std::vector<UndoCommand *> children_;
};

}

#endif // OAK_UNDOCOMMAND_H
