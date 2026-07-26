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

#include "slip.h"

#include <QToolTip>

#include "common/configwrapper.h"
#include "timeline/timelineundogeneral.h"
#include "widget/timelinewidget/timelinewidget.h"

#include "oakengine/timeline.h"
#include "oakengine/undo.h"
namespace olive
{

SlipTool::SlipTool(TimelineWidget *parent)
	: PointerTool(parent)
{
	set_trimming_allowed(false);
	set_track_movement_allowed(false);
}

void SlipTool::process_drag(const TimelineCoordinate &mouse_pos)
{
	// Determine frame movement
	Rational time_movement = drag_start_.get_frame() - mouse_pos.get_frame();

	// Validate slip (enforce all ghosts moving in legal ways)
	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		if (ghost->get_media_in() + time_movement < 0) {
			time_movement = -ghost->get_media_in();
		}
	}

	// Perform slip
	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		ghost->set_media_in_adjustment(time_movement);
	}

	// Generate tooltip and force it to to update (otherwise the tooltip won't move as written in the
	// documentation, and could get in the way of the cursor)
	Rational tooltip_timebase =
		parent()->get_timebase_for_track_type(drag_start_.get_track().type());
	QToolTip::hideText();
	QToolTip::showText(QCursor::pos(),
					   QString::fromStdString(Timecode::time_to_timecode(
						   time_movement, tooltip_timebase,
						   Core::instance()->get_timecode_display(), true)),
					   parent());
}

void SlipTool::finish_drag(TimelineViewMouseEvent *event)
{
	Q_UNUSED(event)

	void *command = oakengine_undo_command_create_multi();

	// Find earliest point to ripple around
	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		Block *b = QtUtils::value_to_ptr<Block>(
			ghost->get_data(TimelineViewGhostItem::k_attached_block));

		ClipBlock *cb = dynamic_cast<ClipBlock *>(b);
		if (cb) {
			oakengine_undo_command_multi_add_child(command, oakengine_block_set_media_in_command(reinterpret_cast<void *>(cb), ghost->get_adjusted_media_in().numerator(), ghost->get_adjusted_media_in().denominator()));
		}
	}

	oakengine_undo_push(
		command, qApp->translate("SlipTool", "Slipped %1 Clip(s)")
					 .arg(parent()->get_ghost_items().size()).toUtf8().constData());
}

}
