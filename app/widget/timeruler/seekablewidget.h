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

#ifndef OAK_SEEKABLEWIDGET_H
#define OAK_SEEKABLEWIDGET_H

#include <QHBoxLayout>
#include <QScrollBar>

#include "widget/menu/menu.h"
#include "widget/timebased/timebasedviewselectionmanager.h"

namespace olive
{

class SeekableWidget : public TimeBasedView {
	Q_OBJECT
public:
	SeekableWidget(QWidget *parent = nullptr);

	int get_scroll() const
	{
		return horizontalScrollBar()->value();
	}

	TimelineMarkerList *get_markers() const
	{
		return markers_;
	}
	TimelineWorkArea *get_work_area() const
	{
		return workarea_;
	}

	void set_markers(TimelineMarkerList *markers);
	void set_work_area(TimelineWorkArea *workarea);

	virtual bool is_dragging_playhead() const override
	{
		return dragging_;
	}

	bool is_marker_editing_enabled() const
	{
		return marker_editing_enabled_;
	}
	void set_marker_editing_enabled(bool e)
	{
		marker_editing_enabled_ = e;
	}

	void delete_selected();

	bool copy_selected(bool cut);

	bool paste_markers();

	void deselect_all_markers();

	void seek_to_scene_point(qreal scene);

	bool has_items_selected() const
	{
		return !selection_manager_.get_selected_objects().empty();
	}

	const std::vector<TimelineMarker *> &get_selected_markers() const
	{
		return selection_manager_.get_selected_objects();
	}

	virtual void SelectionManagerSelectEvent(void *obj) override;
	virtual void SelectionManagerDeselectEvent(void *obj) override;

	virtual void CatchUpScrollEvent() override;

public slots:
	void set_scroll(int i)
	{
		horizontalScrollBar()->setValue(i);
	}

	virtual void TimebaseChangedEvent(const Rational &) override;

signals:
	void drag_moved(int x, int y);

	void drag_released();

protected:
	virtual void mousePressEvent(QMouseEvent *event) override;
	virtual void mouseMoveEvent(QMouseEvent *event) override;
	virtual void mouseReleaseEvent(QMouseEvent *event) override;
	virtual void mouseDoubleClickEvent(QMouseEvent *event) override;

	virtual void focusOutEvent(QFocusEvent *event) override;

	void draw_markers(QPainter *p, int marker_bottom = 0);
	void draw_work_area(QPainter *p);

	void draw_playhead(QPainter *p, int x, int y);

	inline const int &text_height() const
	{
		return text_height_;
	}

	inline const int &playhead_width() const
	{
		return playhead_width_;
	}

	int get_left_limit() const;
	int get_right_limit() const;

protected slots:
	virtual bool show_context_menu(const QPoint &p);

private:
	enum ResizeMode { k_resize_none, k_resize_in, k_resize_out };

	bool find_resize_handle(QMouseEvent *event);

	void clear_resize_handle();

	void drag_resize_handle(const QPointF &scene_pos);

	void commit_resize_handle();

	TimelineMarkerList *markers_;
	TimelineWorkArea *workarea_;

	int text_height_;

	int playhead_width_;

	bool dragging_;

	bool ignore_next_focus_out_;

	TimeBasedViewSelectionManager<TimelineMarker> selection_manager_;

	QObject *resize_item_;
	ResizeMode resize_mode_;
	TimeRange resize_item_range_;
	QPointF resize_start_;
	uint32_t resize_snap_mask_;

	int marker_top_;
	int marker_bottom_;

	bool marker_editing_enabled_;

	QPolygon last_playhead_shape_;

private slots:
	void set_marker_color(int c);

	void show_marker_properties();
};

}

#endif // OAK_SEEKABLEWIDGET_H
