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

#ifndef OAK_NODEVIEWITEMCONNECTOR_H
#define OAK_NODEVIEWITEMCONNECTOR_H

#include <QGraphicsPolygonItem>

#include "nodeviewcommon.h"

namespace olive
{

class NodeViewItemConnector : public QGraphicsPolygonItem {
public:
	NodeViewItemConnector(bool is_output, QGraphicsItem *parent = nullptr);

	void set_flow_direction(NodeViewCommon::FlowDirection dir);

	bool is_output() const
	{
		return output_;
	}

	virtual QPainterPath shape() const override;
	virtual QRectF boundingRect() const override;

private:
	bool output_;
};

}

#endif // OAK_NODEVIEWITEMCONNECTOR_H
