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

#ifndef OAK_FACTORYMENU_H
#define OAK_FACTORYMENU_H

#include <QAction>

#include "oakutil/oaknode.h"
#include "widget/menu/menu.h"

namespace olive
{

/**
 * @brief Create a menu of all available nodes, grouped by category
 *
 * UI-side counterpart of the node library: NodeFactory stays UI-independent
 * while this builds an olive::Menu from NodeFactory::get_library(). Each
 * leaf action carries the node's library index in its data, so it can be
 * resolved back with create_node_from_menu_action() or
 * get_node_id_from_menu_action().
 */
Menu *create_node_menu(QWidget *parent, bool create_none_item = false,
					   oak::NodeCategory restrict_to = oak::k_category_unknown,
					   uint64_t restrict_flags = 0);

/**
 * @brief Create a node from an action of a menu built by create_node_menu()
 *
 * Returns a null node for the "None" item.
 *
 * NOTE: the returned handle is OWNED by the caller (not added to any
 * project); hand it on to a project/undo command.
 */
oak::Node create_node_from_menu_action(QAction *action);

/**
 * @brief Get the node ID from an action of a menu built by create_node_menu()
 *
 * Returns an empty string for the "None" item.
 */
QString get_node_id_from_menu_action(QAction *action);

}

#endif // OAK_FACTORYMENU_H
