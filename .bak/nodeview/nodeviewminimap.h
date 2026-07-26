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

#ifndef OAK_NODEVIEWMINIMAP_H
#define OAK_NODEVIEWMINIMAP_H

#include <QGraphicsView>

#include "nodeviewscene.h"

namespace olive
{

class NodeViewMiniMap : public QGraphicsView {
	Q_OBJECT
public:
	NodeViewMiniMap(NodeViewScene *scene, QWidget *parent = nullptr);

public slots:
	void set_viewport_rect(const QPolygonF &rect);

signals:
	void resized();

	void move_to_scene_point(const QPointF &pos);

protected:
	virtual void drawForeground(QPainter *painter, const QRectF &rect) override;

	virtual void resizeEvent(QResizeEvent *event) override;

	virtual void mousePressEvent(QMouseEvent *event) override;
	virtual void mouseMoveEvent(QMouseEvent *event) override;
	virtual void mouseReleaseEvent(QMouseEvent *event) override;
	virtual void mouseDoubleClickEvent(QMouseEvent *event) override
	{
	}

private slots:
	void scene_changed(const QRectF &bounding);

	void set_default_size();

private:
	bool mouse_inside_resize_triangle(QMouseEvent *event);

	void emit_move_signal(QMouseEvent *event);

	int resize_triangle_sz_;

	QPolygonF viewport_rect_;

	bool resizing_;

	QPoint resize_anchor_;
};

}

#endif // OAK_NODEVIEWMINIMAP_H
