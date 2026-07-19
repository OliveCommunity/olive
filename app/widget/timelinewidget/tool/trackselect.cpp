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

#include "trackselect.h"

#include "node/block/gap/gap.h"
#include "node/output/track/track.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

TrackSelectTool::TrackSelectTool(TimelineWidget *parent)
	: PointerTool(parent)
{
}

void TrackSelectTool::mouse_press(TimelineViewMouseEvent *event)
{
	QVector<Block *> blocks;
	bool forward = !(event->get_modifiers() & Qt::ControlModifier);

	parent()->deselect_all();

	if (event->get_modifiers() & Qt::ShiftModifier) {
		// Track only
		Track *track = parent()->get_track_from_reference(event->get_track());
		if (track) {
			select_blocks_on_track(track, event, &blocks, forward);
		}
	} else {
		// All tracks
		foreach (Track *track, parent()->sequence()->get_tracks()) {
			select_blocks_on_track(track, event, &blocks, forward);
		}
	}

	if (!blocks.isEmpty()) {
		parent()->signal_selected_blocks(blocks);
		set_drag_movement_mode(Timeline::k_move);
		set_clicked_item(blocks.first());
		drag_start_ = event->get_coordinates();
	} else {
		set_drag_movement_mode(Timeline::k_none);
	}
}

void TrackSelectTool::select_blocks_on_track(Track *track,
										  TimelineViewMouseEvent *event,
										  QVector<Block *> *blocks,
										  bool forward)
{
	Block *b = track->nearest_block_before_or_at(event->get_frame());

	if (!b && !track->blocks().isEmpty() && !forward) {
		// Fallback to first or last block in track
		b = track->blocks().last();
	}

	while (b) {
		if (!dynamic_cast<GapBlock *>(b)) {
			if (!blocks->contains(b)) {
				parent()->add_selection(b);
				blocks->append(b);
			}

			if (!(event->get_modifiers() & Qt::AltModifier)) {
				if (ClipBlock *clip = dynamic_cast<ClipBlock *>(b)) {
					foreach (Block *link, clip->block_links()) {
						if (!blocks->contains(link)) {
							parent()->add_selection(link);
							blocks->append(link);
						}
					}
				}
			}
		}

		b = forward ? b->next() : b->previous();
	}
}

}
