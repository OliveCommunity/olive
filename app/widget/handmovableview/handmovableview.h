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

#ifndef OAK_HANDMOVABLEVIEW_H
#define OAK_HANDMOVABLEVIEW_H

#include <QGraphicsView>
#include <QMenu>

#include "common/tooltypes.h"

namespace olive
{

class HandMovableView : public QGraphicsView {
	Q_OBJECT
public:
	HandMovableView(QWidget *parent = nullptr);

	static bool WheelEventIsAZoomEvent(QWheelEvent *event);

	static qreal get_scroll_zoom_multiplier(QWheelEvent *event);

	virtual void CatchUpScrollEvent()
	{
	}

protected:
	virtual void ToolChangedEvent(Tool::Item tool)
	{
		Q_UNUSED(tool)
	}

	bool hand_press(QMouseEvent *event);
	bool hand_move(QMouseEvent *event);
	bool hand_release(QMouseEvent *event);

	void set_default_drag_mode(DragMode mode);
	const DragMode &get_default_drag_mode() const;

	virtual void wheelEvent(QWheelEvent *event) override;

	virtual void zoom_into_cursor_position(QWheelEvent *event, double multiplier,
										const QPointF &cursor_pos);

	void set_is_timeline_axes(bool e)
	{
		is_timeline_axes_ = e;
	}

private:
	bool dragging_hand_;
	DragMode pre_hand_drag_mode_;

	DragMode default_drag_mode_;

	QPointF transformed_pos_;

	bool is_timeline_axes_;

private slots:
	void application_tool_changed(Tool::Item tool);
};

}

#endif // OAK_HANDMOVABLEVIEW_H
