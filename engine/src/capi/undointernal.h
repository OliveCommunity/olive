/***

  Oak - Non-Linear Video Editor
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

#ifndef OAKENGINE_UNDOINTERNAL_H
#define OAKENGINE_UNDOINTERNAL_H

// Internal (not installed) shared declarations between undo.cpp and the
// other capi translation units.  The undo-group state lives in undo.cpp;
// these helpers let node/timeline/etc. push commands into an active group
// instead of directly onto the global undo stack.

namespace olive
{
class MultiUndoCommand;
class UndoCommand;
}

class QString;

// Returns the currently active undo group, or nullptr if no group is open.
olive::MultiUndoCommand *oakengine_undo_group_current(void);

// Push `command` into the active group (eager redo) or onto the global undo
// stack.  No-op if no engine core exists.
void oakengine_undo_push_or_run(olive::UndoCommand *command,
                                const QString &name);

#endif // OAKENGINE_UNDOINTERNAL_H
