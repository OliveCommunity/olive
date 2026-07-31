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

#include "oakengine/timeline.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

TrackSelectTool::TrackSelectTool(TimelineWidget *parent)
	: PointerTool(parent)
{
}

void TrackSelectTool::mouse_press(TimelineViewMouseEvent *event)
{
	QVector<OakEngineBlock *> blocks;
	bool forward = !(event->get_modifiers() & Qt::ControlModifier);

	parent()->deselect_all();

	if (event->get_modifiers() & Qt::ShiftModifier) {
		// Track only
		OakEngineTrack *track =
			parent()->get_track_from_reference(event->get_track());
		if (track) {
			select_blocks_on_track(track, event, &blocks, forward);
		}
	} else {
		// All tracks (engine C ABI: per-type count + indexed access; type
		// ordinals match TrackReference::Type/OAKENGINE_TRACK_TYPE_*)
		auto *seq_handle = parent()->sequence();
		int counts[3] = { 0, 0, 0 };
		oakengine_sequence_track_count(seq_handle, &counts[0], &counts[1],
									   &counts[2]);
		for (int type = 0; type < 3; type++) {
			for (int i = 0; i < counts[type]; i++) {
				OakEngineTrack *track =
					oakengine_sequence_track_at(seq_handle, type, i);
				select_blocks_on_track(track, event, &blocks, forward);
			}
		}
	}

	if (!blocks.isEmpty()) {
		parent()->signal_selected_blocks(blocks);
		set_drag_movement_mode(TimelineApp::k_move);
		set_clicked_item(blocks.first());
		drag_start_ = event->get_coordinates();
	} else {
		set_drag_movement_mode(TimelineApp::k_none);
	}
}

void TrackSelectTool::select_blocks_on_track(OakEngineTrack *track,
										  TimelineViewMouseEvent *event,
										  QVector<OakEngineBlock *> *blocks,
										  bool forward)
{
	OakEngineBlock *b =
		oakengine_track_nearest_block_before_or_at(
			track,
			core::Timecode::time_to_timestamp(event->get_frame(),
											parent()->timebase()));

	if (!b && oakengine_track_block_count(track) > 0 && !forward) {
		// Fallback to first or last block in track
		b = oakengine_track_block_at(track,
								   oakengine_track_block_count(track) - 1);
	}

	while (b) {
		if (!oakengine_block_is_gap(b)) {
			if (!blocks->contains(b)) {
				parent()->add_selection(b);
				blocks->append(b);
			}

			if (!(event->get_modifiers() & Qt::AltModifier)) {
				if (oakengine_node_is_clip(
						reinterpret_cast<OakEngineNode *>(b))) {
					// ClipBlock::block_links() via the C ABI (linked blocks)
					const int link_count =
						oakengine_block_link_count(b);
					for (int i = 0; i < link_count; i++) {
						OakEngineBlock *link =
							oakengine_block_link_at(b, i);
						if (!blocks->contains(link)) {
							parent()->add_selection(link);
							blocks->append(link);
						}
					}
				}
			}
		}

		b = forward ? oakengine_block_next(b) : oakengine_block_prev(b);
	}
}

}
