/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "undo/undocommand.h"

#include <new>

#include "commandhandle.h"

namespace
{

/**
 * @brief olive::UndoCommand subclass forwarding to a C callback table.
 */
class CallbackUndoCommand : public olive::UndoCommand {
public:
	CallbackUndoCommand(const OakUndoCommandVtable &vtable, void *userdata)
		: vtable_(vtable), userdata_(userdata)
	{
	}

	virtual ~CallbackUndoCommand() override
	{
		if (vtable_.free_fn) {
			vtable_.free_fn(userdata_);
		}
	}

protected:
	virtual void redo() override
	{
		if (vtable_.redo) {
			vtable_.redo(userdata_);
		}
	}

	virtual void undo() override
	{
		if (vtable_.undo) {
			vtable_.undo(userdata_);
		}
	}

private:
	OakUndoCommandVtable vtable_;
	void *userdata_;
};

}

OakUndoCommand *oakundo_command_init(const OakUndoCommandVtable *vtable,
									 void *userdata)
{
	if (!vtable) {
		return NULL;
	}

	try {
		OakUndoCommand *handle = new (std::nothrow) OakUndoCommand();
		if (!handle) {
			return NULL;
		}
		handle->command = new CallbackUndoCommand(*vtable, userdata);
		handle->owned = true;
		return handle;
	} catch (...) {
		return NULL;
	}
}

OakUndoCommand *oakundo_command_init_multi(void)
{
	try {
		OakUndoCommand *handle = new (std::nothrow) OakUndoCommand();
		if (!handle) {
			return NULL;
		}
		handle->command = new olive::MultiUndoCommand();
		handle->owned = true;
		return handle;
	} catch (...) {
		return NULL;
	}
}

int oakundo_command_multi_add_child(OakUndoCommand *multi,
									OakUndoCommand *child)
{
	if (!multi || !multi->command || !child || !child->command) {
		return OAKUNDO_E_INVALID;
	}

	olive::MultiUndoCommand *mcu =
		dynamic_cast<olive::MultiUndoCommand *>(multi->command);
	if (!mcu) {
		return OAKUNDO_E_INVALID;
	}

	try {
		mcu->add_child(child->command);
		// Ownership of the underlying command moved to the multi command;
		// consume the wrapper.
		delete child;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_command_multi_child_count(OakUndoCommand *multi, int *out_count)
{
	if (!multi || !multi->command || !out_count) {
		return OAKUNDO_E_INVALID;
	}

	olive::MultiUndoCommand *mcu =
		dynamic_cast<olive::MultiUndoCommand *>(multi->command);
	if (!mcu) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_count = mcu->child_count();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_command_multi_child(OakUndoCommand *multi, int index,
								OakUndoCommand **out_child)
{
	if (!multi || !multi->command || !out_child) {
		return OAKUNDO_E_INVALID;
	}

	olive::MultiUndoCommand *mcu =
		dynamic_cast<olive::MultiUndoCommand *>(multi->command);
	if (!mcu) {
		return OAKUNDO_E_INVALID;
	}

	try {
		if (index < 0 || index >= mcu->child_count()) {
			return OAKUNDO_E_NOT_FOUND;
		}

		OakUndoCommand *handle = new (std::nothrow) OakUndoCommand();
		if (!handle) {
			return OAKUNDO_E_NOMEM;
		}
		handle->command = mcu->child(index);
		handle->owned = false;
		*out_child = handle;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_command_redo_now(OakUndoCommand *command)
{
	if (!command || !command->command) {
		return OAKUNDO_E_INVALID;
	}

	try {
		command->command->redo_now();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_command_undo_now(OakUndoCommand *command)
{
	if (!command || !command->command) {
		return OAKUNDO_E_INVALID;
	}

	try {
		command->command->undo_now();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

void oakundo_command_free(OakUndoCommand *command)
{
	if (!command) {
		return;
	}

	if (command->owned) {
		delete command->command;
	}
	delete command;
}
