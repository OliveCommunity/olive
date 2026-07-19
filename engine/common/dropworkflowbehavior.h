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

#ifndef OAK_DROPWORKFLOWBEHAVIOR_H
#define OAK_DROPWORKFLOWBEHAVIOR_H

namespace olive
{

/**
 * @brief Behavior when media is dropped onto a timeline without a sequence
 *
 * Shared by the config defaults (engine layer) and the timeline import
 * tool (UI layer). Enumerator order matches the previous
 * ImportTool::DropWithoutSequenceBehavior.
 */
enum DropWithoutSequenceBehavior {
	k_dws_ask,
	k_dws_auto,
	k_dws_manual,
	k_dws_disable
};

}

#endif // OAK_DROPWORKFLOWBEHAVIOR_H
