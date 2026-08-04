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

#ifndef OAK_TIMELINEVIEWBASE_H
#define OAK_TIMELINEVIEWBASE_H

#include <QGraphicsView>
#include <cstdint>
#include <vector>

#include "core.h"
#include "timescaledobject.h"
#include "widget/handmovableview/handmovableview.h"

namespace olive
{

class TimeBasedWidget;

class TimeBasedView : public HandMovableView, public TimeScaledObject {
	Q_OBJECT
public:
	TimeBasedView(QWidget *parent = nullptr);

	void enable_snap(const std::vector<Rational> &points);
	void disable_snap();
	bool is_snapped() const
	{
		return snapped_;
	}

	TimeBasedWidget *get_snap_service() const
	{
		return snap_service_;
	}
	void set_snap_service(TimeBasedWidget *service)
	{
		snap_service_ = service;
	}

	const double &get_y_scale() const;
	void set_y_scale(const double &y_scale);

	virtual bool is_dragging_playhead() const
	{
		return dragging_playhead_;
	}

	// To be called only by selection managers
	virtual void SelectionManagerSelectEvent(void *obj)
	{
	}
	virtual void SelectionManagerDeselectEvent(void *obj)
	{
	}

	OakEngineNode *get_viewer_node() const
	{
		return viewer_;
	}

	void set_viewer_node(OakEngineNode *v);

	QPointF scale_point(const QPointF &p) const;
	QPointF unscale_point(const QPointF &p) const;

public slots:
	void set_end_time(const Rational &length);

	/**
   * @brief Slot called whenever the view resizes or the scene contents change to enforce minimum scene sizes
   */
	void update_scene_rect();

signals:
	void scale_changed(double scale);

protected:
	virtual void drawForeground(QPainter *painter, const QRectF &rect) override;

	virtual void resizeEvent(QResizeEvent *event) override;

	virtual void ScaleChangedEvent(const double &scale) override;

	virtual void SceneRectUpdateEvent(QRectF &)
	{
	}

	virtual void VerticalScaleChangedEvent(double scale);

	virtual void zoom_into_cursor_position(QWheelEvent *event, double multiplier,
										const QPointF &cursor_pos) override;

	bool playhead_press(QMouseEvent *event);
	bool playhead_move(QMouseEvent *event);
	bool playhead_release(QMouseEvent *event);

	virtual void TimebaseChangedEvent(const Rational &) override;

	bool is_y_axis_enabled() const
	{
		return y_axis_enabled_;
	}

	void set_y_axis_enabled(bool e)
	{
		y_axis_enabled_ = e;
	}

private:
	qreal get_playhead_x();

	double playhead_scene_left_;
	double playhead_scene_right_;

	bool dragging_playhead_;

	QGraphicsScene scene_;

	bool snapped_;
	std::vector<Rational> snap_time_;

	Rational end_time_;

	TimeBasedWidget *snap_service_;

	bool y_axis_enabled_;

	double y_scale_;

	OakEngineNode *viewer_;

	QMetaObject::Connection viewer_conn_;
};

}

#endif // OAK_TIMELINEVIEWBASE_H
