/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_UNDOWRAPPER_H
#define OAK_UNDOWRAPPER_H

#include "oakengine/undo.h"

namespace olive
{

/**
 * Wrap an app-side undo command object in the facade custom-command API.
 *
 * `Cmd` must provide public `redo()` and `undo()` methods. Ownership of `cmd`
 * is transferred to the returned opaque command pointer; the wrapper deletes
 * `cmd` when the engine command is destroyed.
 *
 * This helper lets app code keep small app-state undo commands (selections,
 * splitter sizes, etc.) without defining new subclasses of olive::UndoCommand,
 * which would keep olive::UndoCommand symbols in the editor binary.
 */
template <typename Cmd>
void *wrap_app_undo_command(const char *name, Cmd *cmd)
{
	return oakengine_undo_command_create(
			name,
			[](void *userdata) {
				static_cast<Cmd *>(userdata)->redo();
			},
			[](void *userdata) {
				static_cast<Cmd *>(userdata)->undo();
			},
			[](void *userdata) {
				delete static_cast<Cmd *>(userdata);
			},
			cmd);
}

} // namespace olive

#endif // OAK_UNDOWRAPPER_H
