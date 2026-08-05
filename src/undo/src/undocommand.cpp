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

#include "undocommand.h"

namespace olive
{

MultiUndoCommand::~MultiUndoCommand()
{
	for (auto it = children_.begin(); it != children_.end(); it++) {
		delete *it;
	}
}

void MultiUndoCommand::redo()
{
	for (auto it = children_.cbegin(); it != children_.cend(); it++) {
		(*it)->redo_and_set_modified();
	}
}

void MultiUndoCommand::undo()
{
	for (auto it = children_.crbegin(); it != children_.crend(); it++) {
		(*it)->undo_and_set_modified();
	}
}

UndoCommand::UndoCommand()
{
	modified_ = false;
	prepared_ = false;
	done_ = false;
}

void UndoCommand::set_modified_callbacks(
	std::function<bool()> is_modified, std::function<void(bool)> set_modified)
{
	is_modified_ = std::move(is_modified);
	set_modified_ = std::move(set_modified);
}

void UndoCommand::redo_and_set_modified()
{
	redo_now();

	if (is_modified_ && set_modified_) {
		modified_ = is_modified_();
		set_modified_(true);
	}
}

void UndoCommand::undo_and_set_modified()
{
	undo_now();

	if (set_modified_) {
		set_modified_(modified_);
	}
}

void UndoCommand::redo_now()
{
	if (!done_) {
		if (!prepared_) {
			prepare();
			prepared_ = true;
		}

		redo();
		done_ = true;
	}
}

void UndoCommand::undo_now()
{
	if (done_) {
		undo();
		done_ = false;
	}
}

}
