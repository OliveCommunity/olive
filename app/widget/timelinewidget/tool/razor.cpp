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

#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "widget/timelinewidget/timelinewidget.h"
#include "widget/timelinewidget/trackhandle.h"

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
	TrackReference split_track = event->get_track();

	if (!split_tracks_.contains(split_track)) {
		split_tracks_.append(split_track);
	}
}

void RazorTool::mouse_release(TimelineViewMouseEvent *event)
{
	Q_UNUSED(event)

	// Always split at the same time
	Rational split_time = drag_start_.get_frame();

	QVector<OakEngineBlock *> blocks_to_split;

	foreach (const TrackReference &track_ref, split_tracks_) {
		OakEngineTrack *track = parent()->get_track_from_reference(track_ref);

		if (track == nullptr || track_is_locked(track)) {
			continue;
		}

		OakEngineBlock *block_at_time = oakengine_track_nearest_block_before(
			track,
			Timecode::time_to_timestamp(split_time, parent()->timebase(),
										Timecode::k_round));

		// Ensure there's a valid block here
		if (block_at_time &&
			oakengine_node_is_clip(
				reinterpret_cast<OakEngineNode *>(block_at_time))) {
			int out_num = 0, out_den = 1;
			oakengine_block_get_out_rational(
				reinterpret_cast<const OakEngineNode *>(block_at_time),
				&out_num, &out_den);
			if (Rational(out_num, out_den) != split_time &&
				!blocks_to_split.contains(block_at_time)) {
				blocks_to_split.append(block_at_time);

				// Add links if no alt is held
				if (!(event->get_modifiers() & Qt::AltModifier)) {
					const int link_count =
						oakengine_block_link_count(block_at_time);
					for (int i = 0; i < link_count; i++) {
						OakEngineBlock *link =
							oakengine_block_link_at(block_at_time, i);
						if (!blocks_to_split.contains(link)) {
							blocks_to_split.append(link);
						}
					}
				}
			}
		}
	}

	split_tracks_.clear();

	if (!blocks_to_split.isEmpty()) {
		// Split through the liboakengine C ABI facade: one undoable,
		// link-preserving command with the same semantics as the old
		// app-side BlockSplitPreservingLinksCommand push.
		QVector<OakEngineClip *> clips;
		clips.reserve(blocks_to_split.size());
		foreach (OakEngineBlock *b, blocks_to_split) {
			if (oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(b))) {
				clips.append(reinterpret_cast<OakEngineClip *>(b));
			}
		}
		oakengine_sequence_split_clips(
			parent()->sequence(),
			clips.data(), clips.size(),
			Timecode::time_to_timestamp(split_time, parent()->timebase(),
										Timecode::k_round));
	}

	dragging_ = false;
}

}
