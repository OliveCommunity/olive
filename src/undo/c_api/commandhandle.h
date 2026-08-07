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

#ifndef OAK_UNDO_COMMANDHANDLE_H
#define OAK_UNDO_COMMANDHANDLE_H

#include <atomic>

#include "undo/undocommand.h"

#include "../src/undocommand.h"

/**
 * @brief Internal control block behind OakUndoCommand, shared between
 * the c_api translation units.
 *
 * `owns` is true for commands created through init factories (the box
 * deletes the command when the count reaches zero) and false for
 * references into library-owned structures (multi children, stack
 * entries): releasing those only destroys the box.
 */
struct OakUndoCommandBox {
	olive::UndoCommand *command;
	bool owns;
	std::atomic<uint32_t> refs;

	OakUndoCommandBox(olive::UndoCommand *c, bool o)
		: command(c)
		, owns(o)
		, refs(1)
	{
	}
};

namespace oakundo_capi
{

inline OakUndoCommand make_command_handle(olive::UndoCommand *command,
										  bool owns);

inline void command_addref(void *ctx)
{
	if (ctx) {
		static_cast<OakUndoCommandBox *>(ctx)->refs.fetch_add(1);
	}
}

inline void command_release(void *ctx)
{
	if (!ctx) {
		return;
	}
	OakUndoCommandBox *box = static_cast<OakUndoCommandBox *>(ctx);
	if (box->refs.fetch_sub(1) == 1) {
		if (box->owns) {
			delete box->command;
		}
		delete box;
	}
}

inline OakUndoCommand make_command_handle(olive::UndoCommand *command,
										  bool owns)
{
	OakUndoCommand handle = {};
	if (!command) {
		return handle;
	}

	OakUndoCommandBox *box = new (std::nothrow) OakUndoCommandBox(command,
																  owns);
	if (!box) {
		if (owns) {
			delete command;
		}
		return handle;
	}

	handle.ctx = box;
	handle.addref = command_addref;
	handle.release = command_release;
	handle.abi_version = OAKUNDO_ABI_VERSION;
	return handle;
}

inline olive::UndoCommand *to_command(OakUndoCommand h)
{
	if (!h.ctx) {
		return nullptr;
	}
	return static_cast<OakUndoCommandBox *>(h.ctx)->command;
}

inline void mark_container_owned(OakUndoCommand h)
{
	if (h.ctx) {
		static_cast<OakUndoCommandBox *>(h.ctx)->owns = false;
	}
}

} // namespace oakundo_capi

#endif // OAK_UNDO_COMMANDHANDLE_H
