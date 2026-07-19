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

#include "beam.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

BeamTool::BeamTool(TimelineWidget *parent)
	: TimelineTool(parent)
{
}

void BeamTool::hover_move(TimelineViewMouseEvent *event)
{
	parent()->set_view_beam_cursor(
		validated_coordinate(event->get_coordinates(true)));
}

TimelineCoordinate BeamTool::validated_coordinate(TimelineCoordinate coord)
{
	if (Core::instance()->snapping()) {
		Rational movement;
		parent()->snap_point({ coord.get_frame() }, &movement);
		if (!movement.isNull()) {
			coord.set_frame(coord.get_frame() + movement);
		}
	}

	return coord;
}

}
