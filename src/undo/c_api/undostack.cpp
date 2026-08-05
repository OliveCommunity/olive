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

#include "undo/undostack.h"

#include <cstring>
#include <new>

#include "../src/undostack.h"
#include "commandhandle.h"

struct OakUndoStack {
	olive::UndoStack impl;
};

OakUndoStack *oakundo_undostack_init(void)
{
	try {
		return new (std::nothrow) OakUndoStack();
	} catch (...) {
		return NULL;
	}
}

void oakundo_undostack_free(OakUndoStack *stack)
{
	delete stack;
}

int oakundo_undostack_push(OakUndoStack *stack, OakUndoCommand *command,
						   const char *name)
{
	if (!stack || !command || !command->command) {
		return OAKUNDO_E_INVALID;
	}

	try {
		olive::UndoCommand *impl = command->command;
		stack->impl.push(impl, name ? name : "");
		// Ownership of the underlying command moved to the stack (or was
		// deleted as an empty multi command); consume the wrapper.
		delete command;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_push_pre_executed(OakUndoStack *stack,
										OakUndoCommand *command,
										const char *name)
{
	if (!stack || !command || !command->command) {
		return OAKUNDO_E_INVALID;
	}

	try {
		olive::UndoCommand *impl = command->command;
		stack->impl.push_pre_executed(impl, name ? name : "");
		delete command;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_undo(OakUndoStack *stack)
{
	if (!stack) {
		return OAKUNDO_E_INVALID;
	}

	try {
		stack->impl.undo();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_redo(OakUndoStack *stack)
{
	if (!stack) {
		return OAKUNDO_E_INVALID;
	}

	try {
		stack->impl.redo();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_jump(OakUndoStack *stack, int64_t index)
{
	if (!stack) {
		return OAKUNDO_E_INVALID;
	}

	try {
		stack->impl.jump(index < 0 ? 0 : static_cast<size_t>(index));
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_clear(OakUndoStack *stack)
{
	if (!stack) {
		return OAKUNDO_E_INVALID;
	}

	try {
		stack->impl.clear();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_can_undo(OakUndoStack *stack, int *out_value)
{
	if (!stack || !out_value) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_value = stack->impl.can_undo() ? 1 : 0;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_can_redo(OakUndoStack *stack, int *out_value)
{
	if (!stack || !out_value) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_value = stack->impl.can_redo() ? 1 : 0;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_count(OakUndoStack *stack, int64_t *out_count)
{
	if (!stack || !out_count) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_count = stack->impl.command_count();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_index(OakUndoStack *stack, int64_t *out_index)
{
	if (!stack || !out_index) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_index = stack->impl.done_count();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_command_text(OakUndoStack *stack, int64_t row,
								   char *buf, int buf_size)
{
	if (!stack) {
		return OAKUNDO_E_INVALID;
	}

	try {
		if (row < 0 || row >= stack->impl.command_count()) {
			return OAKUNDO_E_NOT_FOUND;
		}

		std::string name = stack->impl.command_name(int(row));

		int required = static_cast<int>(name.size()) + 1;
		if (buf && buf_size > 0) {
			size_t copy_len = name.size();
			if (copy_len > static_cast<size_t>(buf_size) - 1) {
				copy_len = static_cast<size_t>(buf_size) - 1;
			}
			memcpy(buf, name.data(), copy_len);
			buf[copy_len] = '\0';
		}
		return required;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_command_is_done(OakUndoStack *stack, int64_t row,
									  int *out_value)
{
	if (!stack || !out_value) {
		return OAKUNDO_E_INVALID;
	}

	try {
		if (row < 0 || row >= stack->impl.command_count()) {
			return OAKUNDO_E_NOT_FOUND;
		}

		*out_value = stack->impl.command_is_done(int(row)) ? 1 : 0;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}
