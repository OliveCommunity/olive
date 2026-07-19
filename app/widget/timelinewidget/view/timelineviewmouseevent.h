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

#ifndef OAK_TIMELINEVIEWMOUSEEVENT_H
#define OAK_TIMELINEVIEWMOUSEEVENT_H

#include <QEvent>
#include <QMimeData>
#include <QPointF>
#include <QPoint>

#include "timeline/timelinecoordinate.h"
#include "widget/timebased/timescaledobject.h"

namespace olive
{

class TimelineViewMouseEvent {
public:
	TimelineViewMouseEvent(
		const QPointF &scene_pos, const QPoint &screen_pos,
		const double &scale_x, const Rational &timebase,
		const Track::Reference &track, const Qt::MouseButton &button,
		const Qt::KeyboardModifiers &modifiers = Qt::NoModifier)
		: scene_pos_(scene_pos)
		, screen_pos_(screen_pos)
		, scale_x_(scale_x)
		, timebase_(timebase)
		, track_(track)
		, button_(button)
		, modifiers_(modifiers)
		, source_event_(nullptr)
		, mime_data_(nullptr)
		, bypass_import_buffer_(false)
	{
	}

	TimelineCoordinate get_coordinates(bool round_time = false) const
	{
		return TimelineCoordinate(get_frame(round_time), track_);
	}

	const Qt::KeyboardModifiers &get_modifiers() const
	{
		return modifiers_;
	}

	/**
   * @brief Gets the time at this cursor point
   *
   * @param round
   *
   * If set to true, the time will be rounded to the nearest time. If set to false, the time is floored so the time is
   * always to the left of the cursor. The former behavior is better for clicking between frames (e.g. razor tool) and
   * the latter is better for clicking directly on frames (e.g. pointer tool).
   */
	Rational get_frame(bool round = false) const
	{
		return TimeScaledObject::scene_to_time(get_scene_x(), scale_x_, timebase_,
											 round);
	}

	const Track::Reference &get_track() const
	{
		return track_;
	}

	const QMimeData *get_mime_data()
	{
		return mime_data_;
	}

	void set_mime_data(const QMimeData *data)
	{
		mime_data_ = data;
	}

	void SetEvent(QEvent *event)
	{
		source_event_ = event;
	}

	qreal get_scene_x() const
	{
		return scene_pos_.x();
	}

	const QPointF &get_scene_pos() const
	{
		return scene_pos_;
	}
	const QPoint &get_screen_pos() const
	{
		return screen_pos_;
	}

	const Qt::MouseButton &get_button() const
	{
		return button_;
	}

	void accept()
	{
		if (source_event_ != nullptr)
			source_event_->accept();
	}

	void ignore()
	{
		if (source_event_ != nullptr)
			source_event_->ignore();
	}

	bool get_bypass_import_buffer() const
	{
		return bypass_import_buffer_;
	}
	void set_bypass_import_buffer(bool e)
	{
		bypass_import_buffer_ = e;
	}

private:
	QPointF scene_pos_;
	QPoint screen_pos_;
	double scale_x_;
	Rational timebase_;

	Track::Reference track_;

	Qt::MouseButton button_;

	Qt::KeyboardModifiers modifiers_;

	QEvent *source_event_;

	const QMimeData *mime_data_;

	bool bypass_import_buffer_;
};

}

#endif // OAK_TIMELINEVIEWMOUSEEVENT_H
