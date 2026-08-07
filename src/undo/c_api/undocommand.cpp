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

#include <atomic>
#include <new>

#include "../src/undocommand.h"

#include "commandhandle.h"

using oakundo_capi::make_command_handle;
using oakundo_capi::mark_container_owned;
using oakundo_capi::to_command;

namespace
{

/**
 * @brief olive::UndoCommand subclass forwarding to a C callback table.
 */
class CallbackUndoCommand : public olive::UndoCommand {
public:
	CallbackUndoCommand(const OakUndoCommandVtable &vtable, void *userdata)
		: vtable_(vtable)
		, userdata_(userdata)
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

} // namespace

OakUndoCommand oakundo_command_init(const OakUndoCommandVtable *vtable,
									void *userdata)
{
	if (!vtable) {
		return OakUndoCommand{};
	}

	try {
		return make_command_handle(new CallbackUndoCommand(*vtable, userdata), true);
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oakundo_command_init_multi(void)
{
	try {
		return make_command_handle(new olive::MultiUndoCommand(), true);
	} catch (...) {
		return OakUndoCommand{};
	}
}

int oakundo_command_multi_add_child(OakUndoCommand multi,
									OakUndoCommand child)
{
	olive::UndoCommand *parent = to_command(multi);
	olive::UndoCommand *child_cmd = to_command(child);
	if (!parent || !child_cmd) {
		return OAKUNDO_E_INVALID;
	}

	olive::MultiUndoCommand *mcu =
		dynamic_cast<olive::MultiUndoCommand *>(parent);
	if (!mcu) {
		return OAKUNDO_E_INVALID;
	}

	try {
		mcu->add_child(child_cmd);
		// The multi command now owns the C++ object; the caller's box
		// becomes a non-owning reference (its release no longer deletes).
		mark_container_owned(child);
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_command_multi_child_count(OakUndoCommand multi, int *out_count)
{
	olive::UndoCommand *parent = to_command(multi);
	if (!parent || !out_count) {
		return OAKUNDO_E_INVALID;
	}

	olive::MultiUndoCommand *mcu =
		dynamic_cast<olive::MultiUndoCommand *>(parent);
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

int oakundo_command_multi_child(OakUndoCommand multi, int index,
								OakUndoCommand *out_child)
{
	olive::UndoCommand *parent = to_command(multi);
	if (!parent || !out_child) {
		return OAKUNDO_E_INVALID;
	}

	olive::MultiUndoCommand *mcu =
		dynamic_cast<olive::MultiUndoCommand *>(parent);
	if (!mcu) {
		return OAKUNDO_E_INVALID;
	}

	try {
		if (index < 0 || index >= mcu->child_count()) {
			return OAKUNDO_E_NOT_FOUND;
		}

		// Non-owning reference; the child stays owned by the multi command
		*out_child = make_command_handle(mcu->child(index), false);
		return out_child->ctx ? OAKUNDO_OK : OAKUNDO_E_NOMEM;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_command_redo_now(OakUndoCommand command)
{
	olive::UndoCommand *c = to_command(command);
	if (!c) {
		return OAKUNDO_E_INVALID;
	}

	try {
		c->redo_now();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_command_undo_now(OakUndoCommand command)
{
	olive::UndoCommand *c = to_command(command);
	if (!c) {
		return OAKUNDO_E_INVALID;
	}

	try {
		c->undo_now();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

void oakundo_command_free(OakUndoCommand *command)
{
	if (!command || !command->ctx) {
		return;
	}

	command->release(command->ctx);
	command->ctx = NULL;
}
