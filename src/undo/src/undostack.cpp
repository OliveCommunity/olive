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

#include "undostack.h"

namespace olive
{

const int UndoStack::k_max_undo_commands = 200;

class EmptyCommand : public UndoCommand {
public:
	EmptyCommand()
	{
	}

protected:
	virtual void redo() override
	{
	}
	virtual void undo() override
	{
	}
};

UndoStack::UndoStack()
{
	clear();
}

UndoStack::~UndoStack()
{
	for (auto it = commands_.cbegin(); it != commands_.cend(); it++) {
		delete it->command;
	}
	for (auto it = undone_commands_.cbegin(); it != undone_commands_.cend();
		 it++) {
		delete it->command;
	}
}

void UndoStack::push(UndoCommand *command, const std::string &name)
{
	MultiUndoCommand *mcu = dynamic_cast<MultiUndoCommand *>(command);
	if (mcu && mcu->child_count() == 0) {
		delete command;
		return;
	}

	// Clear any redoable commands
	if (can_redo()) {
		for (auto it = undone_commands_.cbegin(); it != undone_commands_.cend();
			 it++) {
			delete it->command;
		}
		undone_commands_.clear();
	}

	// Do command and push
	command->redo_and_set_modified();
	commands_.push_back({ command, name });

	// Delete oldest
	if (commands_.size() > k_max_undo_commands) {
		delete commands_.front().command;
		commands_.pop_front();
	}

	emit_index_changed();
}

void UndoStack::push_pre_executed(UndoCommand *command, const std::string &name)
{
	MultiUndoCommand *mcu = dynamic_cast<MultiUndoCommand *>(command);
	if (mcu && mcu->child_count() == 0) {
		delete command;
		return;
	}

	// Clear any redoable commands
	if (can_redo()) {
		for (auto it = undone_commands_.cbegin(); it != undone_commands_.cend();
			 it++) {
			delete it->command;
		}
		undone_commands_.clear();
	}

	// Push without redoing: the caller already executed the children.
	// Mark the command done so it stays undoable.
	command->set_done(true);
	commands_.push_back({ command, name });

	// Delete oldest
	if (commands_.size() > k_max_undo_commands) {
		delete commands_.front().command;
		commands_.pop_front();
	}

	emit_index_changed();
}

void UndoStack::jump(size_t index)
{
	// Guard with can_undo/can_redo: the bottom EmptyCommand is not
	// undoable, so jumping to 0 must stop at it instead of spinning.
	while (commands_.size() > index && can_undo()) {
		undo();
	}
	while (commands_.size() < index && can_redo()) {
		redo();
	}
}

void UndoStack::undo()
{
	if (can_undo()) {
		// Undo most recently done command
		commands_.back().command->undo_and_set_modified();

		// Place at the front of the "undone commands" list
		undone_commands_.push_front(commands_.back());

		// Remove undone command from the commands list
		commands_.pop_back();

		emit_index_changed();
	}
}

void UndoStack::redo()
{
	if (can_redo()) {
		// Redo most recently undone command
		undone_commands_.front().command->redo_and_set_modified();

		// Place at the back of the done commands list
		commands_.push_back(undone_commands_.front());

		// Remove done command from undone list
		undone_commands_.pop_front();

		emit_index_changed();
	}
}

void UndoStack::clear()
{
	for (auto it = commands_.cbegin(); it != commands_.cend(); it++) {
		delete it->command;
	}
	commands_.clear();
	for (auto it = undone_commands_.cbegin(); it != undone_commands_.cend();
		 it++) {
		delete it->command;
	}
	undone_commands_.clear();

	push(new EmptyCommand(), "New/Open Project");
}

bool UndoStack::can_undo() const
{
	return !commands_.empty() &&
		   !dynamic_cast<EmptyCommand *>(commands_.back().command);
}

void UndoStack::emit_index_changed()
{
	if (index_changed_callback_) {
		index_changed_callback_(int(commands_.size()));
	}
}

}
