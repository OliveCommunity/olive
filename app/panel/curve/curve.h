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

#ifndef OAK_CURVEPANEL_H
#define OAK_CURVEPANEL_H

#include "panel/timebased/timebased.h"
#include "widget/curvewidget/curvewidget.h"

namespace olive
{

class CurvePanel : public TimeBasedPanel {
	Q_OBJECT
public:
	CurvePanel();

	virtual void delete_selected() override;

	virtual void select_all() override;

	virtual void deselect_all() override;

public slots:
	void set_node(OakEngineNode *node)
	{
		// Convert single pointer to either an empty vector or a vector of one
		QVector<Node *> nodes;

		if (node) {
			nodes.append(reinterpret_cast<Node *>(node));
		}

		set_nodes(nodes);
	}

public:
	// Not a slot: signature uses the engine C++ type Node*, which must not be
	// exposed to MOC (it would pull Node::staticMetaObject across the ABI
	// boundary). All connections use new-style member-function syntax.
	void set_nodes(const QVector<Node *> &nodes);

public slots:
	virtual void increase_track_height() override;

	virtual void decrease_track_height() override;

protected:
	virtual void retranslate() override;
};

}

#endif // OAK_CURVEPANEL_H
