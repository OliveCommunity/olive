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

#include "razor.h"

#include "node/nodeundo.h"
#include "timeline/timelineundosplit.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

RazorTool::RazorTool(TimelineWidget *parent)
	: BeamTool(parent)
{
}

void RazorTool::mouse_press(TimelineViewMouseEvent *event)
{
	split_tracks_.clear();

	mouse_move(event);
}

void RazorTool::mouse_move(TimelineViewMouseEvent *event)
{
	if (!dragging_) {
		drag_start_ = validated_coordinate(event->get_coordinates(true));
		dragging_ = true;
	}

	// Split at the current cursor track
	Track::Reference split_track = event->get_track();

	if (!split_tracks_.contains(split_track)) {
		split_tracks_.append(split_track);
	}
}

void RazorTool::mouse_release(TimelineViewMouseEvent *event)
{
	Q_UNUSED(event)

	// Always split at the same time
	Rational split_time = drag_start_.get_frame();

	QVector<Block *> blocks_to_split;

	foreach (const Track::Reference &track_ref, split_tracks_) {
		Track *track = parent()->get_track_from_reference(track_ref);

		if (track == nullptr || track->is_locked()) {
			continue;
		}

		Block *block_at_time = track->nearest_block_before(split_time);

		// Ensure there's a valid block here
		ClipBlock *clip_at_time;
		if (block_at_time && block_at_time->out() != split_time &&
			(clip_at_time = dynamic_cast<ClipBlock *>(block_at_time)) &&
			!blocks_to_split.contains(block_at_time)) {
			blocks_to_split.append(block_at_time);

			// Add links if no alt is held
			if (!(event->get_modifiers() & Qt::AltModifier)) {
				foreach (Block *link, clip_at_time->block_links()) {
					if (!blocks_to_split.contains(link)) {
						blocks_to_split.append(link);
					}
				}
			}
		}
	}

	split_tracks_.clear();

	if (!blocks_to_split.isEmpty()) {
		Core::instance()->undo_stack()->push(
			new BlockSplitPreservingLinksCommand(blocks_to_split,
												 { split_time }),
			qApp->translate("RazorTool", "Split Clips"));
	}

	dragging_ = false;
}

}
