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

#ifndef OAK_NODETABLEVIEW_H
#define OAK_NODETABLEVIEW_H

#include <QTreeWidget>

#include "olive/core/util/rational.h"
#include "oakengine/node.h"

namespace olive
{

using core::Rational;

class NodeTableView : public QTreeWidget {
	Q_OBJECT
public:
	NodeTableView(QWidget *parent = nullptr);

	void select_nodes(const QVector<OakEngineNode *> &nodes);

	void deselect_nodes(const QVector<OakEngineNode *> &nodes);

	void set_time(const Rational &time);

private:
	QMap<OakEngineNode *, QTreeWidgetItem *> top_level_item_map_;

	Rational last_time_;
};

}

#endif // OAK_NODETABLEVIEW_H
