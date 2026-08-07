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

#include <atomic>
#include <cstring>
#include <new>

#include "../src/undostack.h"

#include "commandhandle.h"

using oakundo_capi::mark_container_owned;
using oakundo_capi::to_command;

namespace
{

struct StackBox {
	olive::UndoStack impl;
	std::atomic<uint32_t> refs;

	StackBox()
		: refs(1)
	{
	}
};

void stack_addref(void *ctx)
{
	if (ctx) {
		static_cast<StackBox *>(ctx)->refs.fetch_add(1);
	}
}

void stack_release(void *ctx)
{
	if (!ctx) {
		return;
	}
	StackBox *box = static_cast<StackBox *>(ctx);
	if (box->refs.fetch_sub(1) == 1) {
		delete box;
	}
}

olive::UndoStack *to_stack(OakUndoStack h)
{
	if (!h.ctx) {
		return nullptr;
	}
	return &static_cast<StackBox *>(h.ctx)->impl;
}

} // namespace

OakUndoStack oakundo_undostack_init(void)
{
	OakUndoStack handle = {};

	StackBox *box = new (std::nothrow) StackBox();
	if (!box) {
		return handle;
	}

	handle.ctx = box;
	handle.addref = stack_addref;
	handle.release = stack_release;
	handle.abi_version = OAKUNDO_ABI_VERSION;
	return handle;
}

void oakundo_undostack_free(OakUndoStack *stack)
{
	if (!stack || !stack->ctx) {
		return;
	}

	stack->release(stack->ctx);
	stack->ctx = NULL;
}

int oakundo_undostack_push(OakUndoStack stack, OakUndoCommand command,
						   const char *name)
{
	olive::UndoStack *s = to_stack(stack);
	olive::UndoCommand *cmd = to_command(command);
	if (!s || !cmd) {
		return OAKUNDO_E_INVALID;
	}

	try {
		s->push(cmd, name ? name : "");
		// The stack now owns the C++ object (or deleted it as an empty
		// multi command); the caller's box becomes a non-owning reference.
		mark_container_owned(command);
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_push_pre_executed(OakUndoStack stack,
										OakUndoCommand command,
										const char *name)
{
	olive::UndoStack *s = to_stack(stack);
	olive::UndoCommand *cmd = to_command(command);
	if (!s || !cmd) {
		return OAKUNDO_E_INVALID;
	}

	try {
		s->push_pre_executed(cmd, name ? name : "");
		mark_container_owned(command);
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_undo(OakUndoStack stack)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s) {
		return OAKUNDO_E_INVALID;
	}

	try {
		s->undo();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_redo(OakUndoStack stack)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s) {
		return OAKUNDO_E_INVALID;
	}

	try {
		s->redo();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_jump(OakUndoStack stack, int64_t index)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s) {
		return OAKUNDO_E_INVALID;
	}

	try {
		s->jump(index < 0 ? 0 : static_cast<size_t>(index));
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_clear(OakUndoStack stack)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s) {
		return OAKUNDO_E_INVALID;
	}

	try {
		s->clear();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_can_undo(OakUndoStack stack, int *out_value)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s || !out_value) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_value = s->can_undo() ? 1 : 0;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_can_redo(OakUndoStack stack, int *out_value)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s || !out_value) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_value = s->can_redo() ? 1 : 0;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_count(OakUndoStack stack, int64_t *out_count)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s || !out_count) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_count = s->command_count();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_index(OakUndoStack stack, int64_t *out_index)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s || !out_index) {
		return OAKUNDO_E_INVALID;
	}

	try {
		*out_index = s->done_count();
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}

int oakundo_undostack_command_text(OakUndoStack stack, int64_t row,
								   char *buf, int buf_size)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s) {
		return OAKUNDO_E_INVALID;
	}

	try {
		if (row < 0 || row >= s->command_count()) {
			return OAKUNDO_E_NOT_FOUND;
		}

		std::string name = s->command_name(int(row));

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

int oakundo_undostack_command_is_done(OakUndoStack stack, int64_t row,
									  int *out_value)
{
	olive::UndoStack *s = to_stack(stack);
	if (!s || !out_value) {
		return OAKUNDO_E_INVALID;
	}

	try {
		if (row < 0 || row >= s->command_count()) {
			return OAKUNDO_E_NOT_FOUND;
		}

		*out_value = s->command_is_done(int(row)) ? 1 : 0;
		return OAKUNDO_OK;
	} catch (...) {
		return OAKUNDO_E_FAILED;
	}
}
