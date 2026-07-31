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

#ifndef OAK_NODEVIEWSCENE_H
#define OAK_NODEVIEWSCENE_H

#include <QGraphicsScene>
#include <QTimer>

#include "nodeviewcontext.h"
#include "nodeviewedge.h"
#include "nodeviewitem.h"

namespace olive
{

class NodeViewScene : public QGraphicsScene {
	Q_OBJECT
public:
	NodeViewScene(QObject *parent = nullptr);

	void select_all();
	void deselect_all();

	QVector<NodeViewItem *> get_selected_items() const;

	const QHash<oak::Node, NodeViewContext *> &context_map() const
	{
		return context_map_;
	}

	Qt::Orientation get_flow_orientation() const;

	NodeViewCommon::FlowDirection get_flow_direction() const
	{
		return direction_;
	}

	void set_flow_direction(NodeViewCommon::FlowDirection direction);

	bool get_edges_are_curved() const
	{
		return curved_edges_;
	}

public:
	// Not slots: they are called directly, never used as connect() targets.
	NodeViewContext *add_context(oak::Node node);
	void remove_context(oak::Node node);

public slots:
	/**
   * @brief Set whether edges in this scene should be curved or not
   */
	void set_edges_are_curved(bool curved);

private:
	QHash<oak::Node, NodeViewContext *> context_map_;

	NodeViewCommon::FlowDirection direction_;

	bool curved_edges_;
};

}

#endif // OAK_NODEVIEWSCENE_H
