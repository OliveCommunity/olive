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

#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

const int TimelineTool::k_default_distance_from_output = -4;

TimelineTool::TimelineTool(TimelineWidget *parent)
	: dragging_(false)
	, parent_(parent)
{
}

TimelineTool::~TimelineTool()
{
}

TimelineWidget *TimelineTool::parent()
{
	return parent_;
}

OakEngineSequence *TimelineTool::sequence()
{
	return parent_->sequence();
}

TimelineApp::MovementMode
TimelineTool::flip_trim_mode(const TimelineApp::MovementMode &trim_mode)
{
	if (trim_mode == TimelineApp::k_trim_in) {
		return TimelineApp::k_trim_out;
	}

	if (trim_mode == TimelineApp::k_trim_out) {
		return TimelineApp::k_trim_in;
	}

	return trim_mode;
}

Rational TimelineTool::snap_movement_to_timebase(const Rational &start,
											  Rational movement,
											  const Rational &timebase)
{
	Rational proposed_position = start + movement;
	Rational snapped =
		Timecode::snap_time_to_timebase(proposed_position, timebase);

	if (proposed_position != snapped) {
		movement += snapped - proposed_position;
	}

	return movement;
}

Rational TimelineTool::validate_time_movement(Rational movement)
{
	bool first_ghost = true;

	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		if (ghost->get_mode() != TimelineApp::k_move) {
			continue;
		}

		// Prevents any ghosts from going below 0:00:00 time
		if (ghost->get_in() + movement < 0) {
			movement = -ghost->get_in();
		} else if (first_ghost) {
			// Ensure ghost is snapped to a grid
			movement = snap_movement_to_timebase(
				ghost->get_in(), movement,
				parent()->get_timebase_for_track_type(ghost->get_track().type()));

			first_ghost = false;
		}
	}

	return movement;
}

int TimelineTool::validate_track_movement(
	int movement, const QVector<TimelineViewGhostItem *> &ghosts)
{
	foreach (TimelineViewGhostItem *ghost, ghosts) {
		if (ghost->get_mode() != TimelineApp::k_move) {
			continue;
		}

		if (!ghost->get_can_move_tracks()) {
			return 0;

		} else if (ghost->get_track().index() + movement < 0) {
			// Prevents any ghosts from going to a non-existent negative track
			movement = -ghost->get_track().index();
		}
	}

	return movement;
}

void TimelineTool::get_ghost_data(Rational *earliest_point,
								Rational *latest_point)
{
	Rational ep = RATIONAL_MAX;
	Rational lp = RATIONAL_MIN;

	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		ep = qMin(ep, ghost->get_adjusted_in());
		lp = qMax(lp, ghost->get_adjusted_out());
	}

	if (earliest_point) {
		*earliest_point = ep;
	}

	if (latest_point) {
		*latest_point = lp;
	}
}

void TimelineTool::insert_gaps_at_ghost_destination(void *command)
{
	Rational earliest_point, latest_point;

	get_ghost_data(&earliest_point, &latest_point);

	parent()->insert_gaps_at(earliest_point, latest_point - earliest_point,
						   command);
}

}
