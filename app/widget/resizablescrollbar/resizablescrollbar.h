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

#ifndef OAK_RESIZABLESCROLLBAR_H
#define OAK_RESIZABLESCROLLBAR_H

#include <QScrollBar>

#include "common/define.h"

namespace olive
{

class ResizableScrollBar : public QScrollBar {
	Q_OBJECT
public:
	ResizableScrollBar(QWidget *parent = nullptr);
	ResizableScrollBar(Qt::Orientation orientation, QWidget *parent = nullptr);

signals:
	void resize_began(int old_bar_width, bool top_handle);

	void resize_moved(int movement);

	void resize_ended();

protected:
	virtual void mousePressEvent(QMouseEvent *event) override;

	virtual void mouseMoveEvent(QMouseEvent *event) override;

	virtual void mouseReleaseEvent(QMouseEvent *event) override;

private:
	QRect get_scroll_bar_rect();

	static const int k_handle_width;

	enum MouseHandleState { k_not_in_handle, k_in_top_handle, k_in_bottom_handle };

	void init();

	int get_active_mouse_pos(QMouseEvent *event);

	int get_active_bar_size();

	MouseHandleState mouse_handle_state_;

	bool dragging_;

	int drag_start_point_;
};

}

#endif // OAK_RESIZABLESCROLLBAR_H
