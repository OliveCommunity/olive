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

#include "undo/undocommand.h"

#include "../src/undocommand.h"

/**
 * @brief Internal layout of the OakUndoCommand handle, shared between the
 * c_api translation units.
 *
 * `owned` is true for handles created by oakundo_command_init() /
 * oakundo_command_init_multi() and false for borrowed wrappers handed out
 * by oakundo_command_multi_child().
 */
struct OakUndoCommand {
	olive::UndoCommand *command;
	bool owned;
};

#endif // OAK_UNDO_COMMANDHANDLE_H
