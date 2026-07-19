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

#include "nodeviewitemconnector.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPalette>
#include <QPen>

#include "nodeviewitem.h"

namespace olive
{

NodeViewItemConnector::NodeViewItemConnector(bool is_output,
											 QGraphicsItem *parent)
	: QGraphicsPolygonItem(parent)
	, output_(is_output)
{
	QColor c = qApp->palette().text().color();
	setPen(QPen(c, NodeViewItem::default_item_border()));
	setBrush(c);
}

void NodeViewItemConnector::set_flow_direction(NodeViewCommon::FlowDirection dir)
{
	QFont f;
	QFontMetricsF fm(f);

	int triangle_sz = fm.height() / 2;
	int triangle_sz_half = triangle_sz / 2;

	QPolygonF p;
	p.resize(3);

	switch (dir) {
	case NodeViewCommon::k_left_to_right:
		// Triangle pointing right
		p[0] = QPointF(0, -triangle_sz_half);
		p[1] = QPointF(triangle_sz_half, 0);
		p[2] = QPointF(0, triangle_sz_half);
		break;
	case NodeViewCommon::k_top_to_bottom:
		// Triangle pointing down
		p[0] = QPointF(-triangle_sz_half, 0);
		p[1] = QPointF(0, triangle_sz_half);
		p[2] = QPointF(triangle_sz_half, 0);
		break;
	case NodeViewCommon::k_bottom_to_top:
		// Triangle pointing up
		p[0] = QPointF(-triangle_sz_half, 0);
		p[1] = QPointF(0, -triangle_sz_half);
		p[2] = QPointF(triangle_sz_half, 0);
		break;
	case NodeViewCommon::k_right_to_left:
		// Triangle pointing left
		p[0] = QPointF(0, -triangle_sz_half);
		p[1] = QPointF(-triangle_sz_half, 0);
		p[2] = QPointF(0, triangle_sz_half);
		break;
	case NodeViewCommon::k_invalid_direction:
		break;
	}

	setPolygon(p);
}

QPainterPath NodeViewItemConnector::shape() const
{
	// Yes, we skip QGraphicsPolygonItem because it adds the polygon. QGraphicsItem adds the
	// boundingRect which we modify below
	return QGraphicsItem::shape(); // clazy:exclude=skipped-base-method
}

QRectF NodeViewItemConnector::boundingRect() const
{
	QRectF b = this->polygon().boundingRect();
	const int radius = QFontMetrics(QFont()).height() / 2;
	b.adjust(-radius, -radius, radius, radius);
	return b;
}

}
