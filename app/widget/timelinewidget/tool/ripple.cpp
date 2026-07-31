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

#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "oakengine/undo.h"
#include "ripple.h"
#include "widget/timelinewidget/trackhandle.h"

namespace olive
{

RippleTool::RippleTool(TimelineWidget *parent)
	: PointerTool(parent)
{
	set_movement_allowed(false);
	set_gap_trimming_allowed(true);
}

void RippleTool::initiate_drag(OakEngineBlock *clicked_item,
							  TimelineApp::MovementMode trim_mode,
							  Qt::KeyboardModifiers modifiers)
{
	initiate_drag_internal(clicked_item, trim_mode, modifiers, true, true, false);

	if (!parent()->has_ghosts()) {
		return;
	}

	// Find the earliest ripple
	Rational earliest_ripple = RATIONAL_MAX;

	foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
		Rational ghost_ripple_point;

		if (trim_mode == TimelineApp::k_trim_in) {
			ghost_ripple_point = ghost->get_in();
		} else {
			ghost_ripple_point = ghost->get_out();
		}

		earliest_ripple = qMin(earliest_ripple, ghost_ripple_point);
	}

	// For each track that does NOT have a ghost, we need to make one for Gaps
	// (engine C ABI: per-type count + indexed access; type ordinals match
	// TrackReference::Type/OAKENGINE_TRACK_TYPE_*)
	auto *seq_handle = sequence();
	int track_counts[3] = { 0, 0, 0 };
	oakengine_sequence_track_count(seq_handle, &track_counts[0],
								   &track_counts[1], &track_counts[2]);
	for (int track_type = 0; track_type < 3; track_type++) {
		for (int track_index = 0; track_index < track_counts[track_type];
			 track_index++) {
			OakEngineTrack *track =
				oakengine_sequence_track_at(seq_handle, track_type,
											track_index);
			if (track_is_locked(track)) {
				continue;
			}

			// Determine if we've already created a ghost on this track
			bool ghost_on_this_track_exists = false;

			foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
				if (parent()->get_track_from_reference(ghost->get_track()) ==
					track) {
					ghost_on_this_track_exists = true;
					break;
				}
			}

		// If there's no ghost on this track, create one
		if (!ghost_on_this_track_exists) {
			// Find the block that starts just after or at the ripple point
			OakEngineBlock *block_after_ripple =
				oakengine_track_nearest_block_after_or_at(
					track,
					core::Timecode::time_to_timestamp(earliest_ripple,
													parent()->timebase()));

			// Exception for out-transitions, do not create a gap between them
			if (block_after_ripple) {
				OakEngineBlock *prev_block =
					oakengine_block_prev(block_after_ripple);
				if (oakengine_node_is_clip(
						reinterpret_cast<OakEngineNode *>(prev_block))) {
					if (oakengine_clip_out_transition(prev_block) ==
						block_after_ripple) {
						block_after_ripple =
							oakengine_block_next(block_after_ripple);
					}
				}
			}

			// If block is null, there will be no blocks after to ripple
			if (block_after_ripple) {
				TimelineViewGhostItem *ghost;

				if (oakengine_block_is_gap(block_after_ripple)) {
					// If this Block is already a Gap, ghost it now
					ghost = add_ghost_from_block(block_after_ripple, trim_mode);
				} else {
					// Well we need to ripple SOMETHING, it'll either be the previous block if it's a gap
					// or we'll have to create a new gap ourselves
					OakEngineBlock *previous =
						oakengine_block_prev(block_after_ripple);

					if (oakengine_block_is_gap(previous)) {
						// Previous is a gap, that'll make a fine substitute
						ghost = add_ghost_from_block(previous, trim_mode);
					} else {
						// Previous is not a gap, we'll have to insert one there ourselves
						int in_num = 0, in_den = 1;
						oakengine_block_get_in_rational(
							reinterpret_cast<const OakEngineNode *>(
								block_after_ripple),
							&in_num, &in_den);
						Rational ripple_in(in_num, in_den);
						ghost = add_ghost_from_null(ripple_in,
												 ripple_in,
												 ghost_block_track_reference(
													 block_after_ripple),
												 trim_mode);
						ghost->set_data(TimelineViewGhostItem::k_reference_block,
									   QtUtils::ptr_to_value(block_after_ripple));
					}
				}
			}
		}
	}
}
}

void RippleTool::finish_drag(TimelineViewMouseEvent *event)
{
	Q_UNUSED(event)

	if (parent()->has_ghosts()) {
		QVector<QVector<oakengine_ripple_info>> info_list(TrackReference::k_count);

		foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
			if (!ghost->has_been_adjusted()) {
				continue;
			}

			OakEngineTrack *track =
				parent()->get_track_from_reference(ghost->get_track());

			oakengine_ripple_info info;
			OakEngineBlock *b = QtUtils::value_to_ptr<OakEngineBlock>(
				ghost->get_data(TimelineViewGhostItem::k_attached_block));

			if (b) {
				info.block = b;
				info.append_gap = 0;
			} else {
				info.block = QtUtils::value_to_ptr<OakEngineBlock>(
					ghost->get_data(TimelineViewGhostItem::k_reference_block));
				info.append_gap = 1;
			}
			info.track = track;

			info_list[oakengine_track_type(track)].append(info);
		}

		void *command = oakengine_undo_command_create_multi();

		Rational movement;

		if (drag_movement_mode() == TimelineApp::k_trim_out) {
			movement = parent()->get_ghost_items().first()->get_out_adjustment();
		} else {
			movement = parent()->get_ghost_items().first()->get_in_adjustment();
		}

		for (int i = 0; i < info_list.size(); i++) {
			if (!info_list.at(i).isEmpty()) {
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_sequence_ripple_tracks_command(
						sequence(), i,
						info_list.at(i).constData(), info_list.at(i).size(),
						movement.numerator(), movement.denominator(),
						drag_movement_mode()));
			}
		}

		if (oakengine_undo_command_multi_child_count(command) > 0) {
			TimelineWidgetSelections new_sel = parent()->get_selections();
			TimelineViewGhostItem *reference_ghost =
				parent()->get_ghost_items().first();
			if (drag_movement_mode() == TimelineApp::k_trim_in) {
				new_sel.trim_out(-reference_ghost->get_in_adjustment());
			} else {
				new_sel.trim_out(reference_ghost->get_out_adjustment());
			}
			oakengine_undo_command_multi_add_child(command, parent()->create_set_selections_command(new_sel, parent()->get_selections(), false));

			oakengine_undo_push(
				command, qApp->translate("RippleTool", "Rippled Clips").toUtf8().constData());
		} else {
			oakengine_undo_command_free(command);
		}
	}
}

}
