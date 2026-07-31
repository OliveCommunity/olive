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

#ifndef OAK_NODETABLEPANEL_H
#define OAK_NODETABLEPANEL_H

#include "panel/timebased/timebased.h"
#include "widget/nodetableview/nodetablewidget.h"

namespace olive
{

class NodeTablePanel : public TimeBasedPanel {
	Q_OBJECT
public:
	NodeTablePanel();

public:
	void select_nodes(const QVector<OakEngineNode *> &nodes)
	{
		static_cast<NodeTableWidget *>(get_time_based_widget())->select_nodes(nodes);
	}

	void deselect_nodes(const QVector<OakEngineNode *> &nodes)
	{
		static_cast<NodeTableWidget *>(get_time_based_widget())
			->deselect_nodes(nodes);
	}

private:
	virtual void retranslate() override;
};

}

#endif // OAK_NODETABLEPANEL_H
