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

#include "nodecombobox.h"

#include <QAction>
#include <QEvent>
#include <QDebug>

#include "node/factory.h"
#include "ui/icons/icons.h"
#include "widget/menu/factorymenu.h"
#include "widget/menu/menu.h"

namespace olive
{

NodeComboBox::NodeComboBox(QWidget *parent)
	: QComboBox(parent)
{
}

void NodeComboBox::showPopup()
{
	Menu *m = create_node_menu(this, true);

	QAction *selected = m->exec(parentWidget()->mapToGlobal(pos()));

	if (selected) {
		QString new_id = get_node_id_from_menu_action(selected);

		set_node_internal(new_id, true);
	}

	delete m;
}

const QString &NodeComboBox::get_selected_node() const
{
	return selected_id_;
}

void NodeComboBox::set_node(const QString &id)
{
	set_node_internal(id, false);
}

void NodeComboBox::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange) {
		update_text();
	}

	QComboBox::changeEvent(e);
}

void NodeComboBox::update_text()
{
	clear();

	if (!selected_id_.isEmpty()) {
		addItem(NodeFactory::get_name_from_id(selected_id_));
	}
}

void NodeComboBox::set_node_internal(const QString &id, bool emit_signal)
{
	if (selected_id_ != id) {
		selected_id_ = id;

		update_text();

		if (emit_signal) {
			emit node_changed(selected_id_);
		}
	}
}

}
