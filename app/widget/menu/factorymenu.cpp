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
					   Node::CategoryID restrict_to, uint64_t restrict_flags)
{
	const int library_size = oakengine_node_factory_id_count();

	Menu *menu = new Menu(parent);
	menu->setToolTipsVisible(true);

	for (int i = 0; i < library_size; i++) {
		olive::Node *n = reinterpret_cast<olive::Node *>(
			oakengine_node_factory_node_at(i));

		if (restrict_to != Node::k_category_unknown &&
			!n->category().contains(restrict_to)) {
			// Skip this node
			continue;
		}

		if (restrict_flags && !(n->get_flags() & restrict_flags)) {
			continue;
		}

		if (n->get_flags() & Node::k_dont_show_in_create_menu) {
			continue;
		}

		// Make sure nodes are up-to-date with the current translation
		n->retranslate();

		char cat_buf[256];
		oakengine_node_category_name(
			n->category().isEmpty() ? 0 : n->category().first(),
			cat_buf, sizeof(cat_buf));
		QString category_name = QString::fromUtf8(cat_buf);

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
		QString sub = n->sub_category();
		if (!sub.isEmpty() && n->category().contains(Node::k_category_open_fx)) {
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
		QAction *a = destination->insert_alphabetically(n->name());
		a->setData(i);
		a->setToolTip(n->description());
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

Node *create_node_from_menu_action(QAction *action)
{
	int index = action->data().toInt();

	if (index == -1) {
		return nullptr;
	}

	olive::Node *proto = reinterpret_cast<olive::Node *>(
		oakengine_node_factory_node_at(index));
	return proto ? proto->copy() : nullptr;
}

QString get_node_id_from_menu_action(QAction *action)
{
	int index = action->data().toInt();

	if (index == -1) {
		return QString();
	}

	olive::Node *proto = reinterpret_cast<olive::Node *>(
		oakengine_node_factory_node_at(index));
	return proto ? proto->id() : QString();
}

}
