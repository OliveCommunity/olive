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

#include "resizablescrollbar.h"

#include <QDebug>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

#include "oakutil/range.h"

namespace olive
{

const int ResizableScrollBar::k_handle_width = 10;

ResizableScrollBar::ResizableScrollBar(QWidget *parent)
	: QScrollBar(parent)
{
	init();
}

ResizableScrollBar::ResizableScrollBar(Qt::Orientation orientation,
									   QWidget *parent)
	: QScrollBar(orientation, parent)
{
	init();
}

void ResizableScrollBar::mousePressEvent(QMouseEvent *event)
{
	if (mouse_handle_state_ == k_not_in_handle) {
		QScrollBar::mousePressEvent(event);
	} else {
		dragging_ = true;

		drag_start_point_ = get_active_mouse_pos(event);

		emit resize_began(get_active_bar_size(),
						 (mouse_handle_state_ == k_in_top_handle));
	}
}

void ResizableScrollBar::mouseMoveEvent(QMouseEvent *event)
{
	QRect sr = get_scroll_bar_rect();

	if (dragging_) {
		// Determine how much the cursor has moved
		int mouse_movement = get_active_mouse_pos(event) - drag_start_point_;

		emit resize_moved(mouse_movement);

	} else {
		int mouse_pos, top, bottom;
		Qt::CursorShape target_cursor;
		mouse_pos = get_active_mouse_pos(event);

		if (orientation() == Qt::Horizontal) {
			top = sr.left();
			bottom = sr.right();
			target_cursor = Qt::SizeHorCursor;
		} else {
			top = sr.top();
			bottom = sr.bottom();
			target_cursor = Qt::SizeVerCursor;
		}

		if (in_range(mouse_pos, top, k_handle_width)) {
			mouse_handle_state_ = k_in_top_handle;
		} else if (in_range(mouse_pos, bottom, k_handle_width)) {
			mouse_handle_state_ = k_in_bottom_handle;
		} else {
			mouse_handle_state_ = k_not_in_handle;
		}

		if (mouse_handle_state_ == k_not_in_handle) {
			unsetCursor();
		} else {
			setCursor(target_cursor);
		}

		QScrollBar::mouseMoveEvent(event);
	}
}

void ResizableScrollBar::mouseReleaseEvent(QMouseEvent *event)
{
	if (dragging_) {
		dragging_ = false;

		emit resize_ended();
	} else {
		QScrollBar::mouseReleaseEvent(event);
	}
}

QRect ResizableScrollBar::get_scroll_bar_rect()
{
	// Initialize "style option". I don't know what this does, I just ripped it straight from
	// Qt source code
	QStyleOptionSlider opt;
	initStyleOption(&opt);

	// Determine rect of slider bar
	return style()->subControlRect(QStyle::CC_ScrollBar, &opt,
								   QStyle::SC_ScrollBarSlider, this);
}

void ResizableScrollBar::init()
{
	setSingleStep(20);
	setMaximum(0);
	setMouseTracking(true);

	mouse_handle_state_ = k_not_in_handle;
	dragging_ = false;
}

int ResizableScrollBar::get_active_mouse_pos(QMouseEvent *event)
{
	if (orientation() == Qt::Horizontal) {
		return event->pos().x();
	} else {
		return event->pos().y();
	}
}

int ResizableScrollBar::get_active_bar_size()
{
	QRect sr = get_scroll_bar_rect();

	if (orientation() == Qt::Horizontal) {
		return sr.width();
	} else {
		return sr.height();
	}
}

}
