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

#ifndef OAK_TIMELINETOOL_H
#define OAK_TIMELINETOOL_H

#include <QDragLeaveEvent>

#include "widget/timelinewidget/view/timelineviewghostitem.h"
#include "widget/timelinewidget/view/timelineviewmouseevent.h"

namespace olive
{

class TimelineWidget;

class TimelineTool {
public:
	TimelineTool(TimelineWidget *parent);
	virtual ~TimelineTool();

	virtual void mouse_press(TimelineViewMouseEvent *)
	{
	}
	virtual void mouse_move(TimelineViewMouseEvent *)
	{
	}
	virtual void mouse_release(TimelineViewMouseEvent *)
	{
	}
	virtual void mouse_double_click(TimelineViewMouseEvent *)
	{
	}

	virtual void hover_move(TimelineViewMouseEvent *)
	{
	}

	virtual void drag_enter(TimelineViewMouseEvent *)
	{
	}
	virtual void drag_move(TimelineViewMouseEvent *)
	{
	}
	virtual void drag_leave(QDragLeaveEvent *)
	{
	}
	virtual void drag_drop(TimelineViewMouseEvent *)
	{
	}

	TimelineWidget *parent();

	Sequence *sequence();

	static Timeline::MovementMode
	flip_trim_mode(const Timeline::MovementMode &trim_mode);

	static Rational snap_movement_to_timebase(const Rational &start,
										   Rational movement,
										   const Rational &timebase);

protected:
	/**
   * @brief Validates Ghosts that are moving horizontally (time-based)
   *
   * Validation is the process of ensuring that whatever movements the user is making are "valid" and "legal". This
   * function's validation ensures that no Ghost's in point ends up in a negative timecode.
   */
	Rational validate_time_movement(Rational movement);

	/**
   * @brief Validates Ghosts that are moving vertically (track-based)
   *
   * This function's validation ensures that no Ghost's track ends up in a negative (non-existent) track.
   */
	int validate_track_movement(int movement,
							  const QVector<TimelineViewGhostItem *> &ghosts);

	void get_ghost_data(Rational *earliest_point, Rational *latest_point);

	void insert_gaps_at_ghost_destination(void *command);

	std::vector<Rational> snap_points_;

	bool dragging_;

	TimelineCoordinate drag_start_;

	static const int k_default_distance_from_output;

private:
	TimelineWidget *parent_;
};

}

#endif // OAK_TIMELINETOOL_H
