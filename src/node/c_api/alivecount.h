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

#ifndef OAK_EDITOR_NODE_ALIVECOUNT_H
#define OAK_EDITOR_NODE_ALIVECOUNT_H

/**
 * @brief Shared live-object counter hooks (internal, not installed).
 *
 * The counter itself and the public oaknode_debug_alive_count() live in
 * the node family (src/node/c_api/node.cpp); these hooks have external
 * linkage so the other families' create/free functions can participate.
 */
namespace oaknode_c_api
{

void alive_inc();
void alive_dec();

}

#endif //OAK_EDITOR_NODE_ALIVECOUNT_H
