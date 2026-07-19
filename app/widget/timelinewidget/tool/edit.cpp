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

#include "edit.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

EditTool::EditTool(TimelineWidget *parent)
	: BeamTool(parent)
{
}

void EditTool::mouse_press(TimelineViewMouseEvent *event)
{
	if (!(event->get_modifiers() & Qt::ShiftModifier)) {
		parent()->deselect_all();
	}
}

void EditTool::mouse_move(TimelineViewMouseEvent *event)
{
	if (dragging_) {
		Rational end_frame = event->get_frame(true);

		if (Core::instance()->snapping()) {
			Rational movement;
			parent()->snap_point({ end_frame }, &movement);
			if (!movement.isNull()) {
				end_frame += movement;
			}
		}

		parent()->set_selections(start_selections_, false);
		parent()->add_selection(TimeRange(start_coord_.get_frame(), end_frame),
							   start_coord_.get_track());
	} else {
		start_selections_ = parent()->get_selections();

		dragging_ = true;

		start_coord_ = event->get_coordinates(true);

		// Snap if we're snapping
		if (Core::instance()->snapping()) {
			Rational movement;
			parent()->snap_point({ start_coord_.get_frame() }, &movement);
			if (!movement.isNull()) {
				start_coord_.set_frame(start_coord_.get_frame() + movement);
			}
		}

		dragging_ = true;
	}
}

void EditTool::mouse_release(TimelineViewMouseEvent *event)
{
	auto current_sel = parent()->get_selections();
	parent()->set_selections(start_selections_, false);
	parent()->set_selections(current_sel, true);

	dragging_ = false;
}

void EditTool::mouse_double_click(TimelineViewMouseEvent *event)
{
	Block *item = parent()->get_item_at_scene_pos(event->get_coordinates());

	if (item && !item->track()->is_locked()) {
		parent()->add_selection(item);
	}
}

}
