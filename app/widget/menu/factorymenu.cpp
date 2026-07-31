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

#include "factorymenu.h"

#include <QCoreApplication>

#include "oakengine/node.h"

namespace olive
{

Menu *create_node_menu(QWidget *parent, bool create_none_item,
					   oak::NodeCategory restrict_to, uint64_t restrict_flags)
{
	const int library_size = oak::Node::factory_count();

	Menu *menu = new Menu(parent);
	menu->setToolTipsVisible(true);

	for (int i = 0; i < library_size; i++) {
		oak::Node n = oak::Node::factory_node_at(i);

		if (restrict_to != oak::k_category_unknown &&
			!n.in_category(restrict_to)) {
			continue;
		}

		const uint64_t flags = n.flags();
		if (restrict_flags && !(flags & restrict_flags)) {
			continue;
		}

		if (flags & oak::Node::flag_dont_show_in_create_menu()) {
			continue;
		}

		// Make sure nodes are up-to-date with the current translation
		n.retranslate();

		const int cat_count = n.category_count();
		QString category_name = oak::Node::category_name(cat_count == 0 ? 0 : n.category_at(0));

		// Find or create top-level category menu
		Menu *top_menu = nullptr;
		QList<QAction *> menu_actions = menu->actions();
		foreach (QAction *action, menu_actions) {
			if (action->menu() && action->menu()->title() == category_name) {
				top_menu = static_cast<Menu *>(action->menu());
				break;
			}
		}
		if (!top_menu) {
			top_menu = new Menu(category_name, menu);
			menu->insert_alphabetically(top_menu);
		}

		// Determine final destination (support secondary grouping)
		Menu *destination = top_menu;
		QString sub = n.sub_category();
		bool is_openfx = n.in_category(oak::k_category_open_fx);
		if (!sub.isEmpty() && is_openfx) {
			QList<QAction *> sub_actions = top_menu->actions();
			foreach (QAction *action, sub_actions) {
				if (action->menu() && action->menu()->title() == sub) {
					destination = static_cast<Menu *>(action->menu());
					break;
				}
			}
			if (destination == top_menu) {
				destination = new Menu(sub, top_menu);
				top_menu->insert_alphabetically(destination);
			}
		}

		// Add entry to menu
		QAction *a = destination->insert_alphabetically(n.name());
		a->setData(i);
		a->setToolTip(n.description());
	}

	if (create_none_item) {
		QAction *none_item = new QAction(
			QCoreApplication::translate("NodeFactory", "None"), menu);

		none_item->setData(-1);

		if (menu->actions().isEmpty()) {
			menu->addAction(none_item);
		} else {
			QAction *separator = menu->insertSeparator(menu->actions().first());
			menu->insertAction(separator, none_item);
		}
	}

	return menu;
}

oak::Node create_node_from_menu_action(QAction *action)
{
	int index = action->data().toInt();

	if (index == -1) {
		return oak::Node();
	}

	return oak::Node::factory_node_at(index).create_copy();
}

QString get_node_id_from_menu_action(QAction *action)
{
	int index = action->data().toInt();

	if (index == -1) {
		return QString();
	}

	oak::Node proto = oak::Node::factory_node_at(index);
	if (proto.is_null()) {
		return QString();
	}
	return proto.id();
}

}
