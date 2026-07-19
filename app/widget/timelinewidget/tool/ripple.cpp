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

#include "node/block/gap/gap.h"
#include "timeline/timelineundoripple.h"
#include "ripple.h"

namespace olive
{

RippleTool::RippleTool(TimelineWidget *parent)
	: PointerTool(parent)
{
	set_movement_allowed(false);
	set_gap_trimming_allowed(true);
}

void RippleTool::initiate_drag(Block *clicked_item,
							  Timeline::MovementMode trim_mode,
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

		if (trim_mode == Timeline::k_trim_in) {
			ghost_ripple_point = ghost->get_in();
		} else {
			ghost_ripple_point = ghost->get_out();
		}

		earliest_ripple = qMin(earliest_ripple, ghost_ripple_point);
	}

	// For each track that does NOT have a ghost, we need to make one for Gaps
	foreach (Track *track, sequence()->get_tracks()) {
		if (track->is_locked()) {
			continue;
		}

		// Determine if we've already created a ghost on this track
		bool ghost_on_this_track_exists = false;

		foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
			if (parent()->get_track_from_reference(ghost->get_track()) == track) {
				ghost_on_this_track_exists = true;
				break;
			}
		}

		// If there's no ghost on this track, create one
		if (!ghost_on_this_track_exists) {
			// Find the block that starts just after or at the ripple point
			Block *block_after_ripple =
				track->nearest_block_after_or_at(earliest_ripple);

			// Exception for out-transitions, do not create a gap between them
			if (block_after_ripple) {
				if (ClipBlock *prev_clip = dynamic_cast<ClipBlock *>(
						block_after_ripple->previous())) {
					if (prev_clip->out_transition() == block_after_ripple) {
						block_after_ripple = block_after_ripple->next();
					}
				}
			}

			// If block is null, there will be no blocks after to ripple
			if (block_after_ripple) {
				TimelineViewGhostItem *ghost;

				if (dynamic_cast<GapBlock *>(block_after_ripple)) {
					// If this Block is already a Gap, ghost it now
					ghost = add_ghost_from_block(block_after_ripple, trim_mode);
				} else {
					// Well we need to ripple SOMETHING, it'll either be the previous block if it's a gap
					// or we'll have to create a new gap ourselves
					Block *previous = block_after_ripple->previous();

					if (dynamic_cast<GapBlock *>(previous)) {
						// Previous is a gap, that'll make a fine substitute
						ghost = add_ghost_from_block(previous, trim_mode);
					} else {
						// Previous is not a gap, we'll have to insert one there ourselves
						ghost = add_ghost_from_null(block_after_ripple->in(),
												 block_after_ripple->in(),
												 track->to_reference(),
												 trim_mode);
						ghost->set_data(TimelineViewGhostItem::k_reference_block,
									   QtUtils::ptr_to_value(block_after_ripple));
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
		QVector<QHash<Track *, TrackListRippleToolCommand::RippleInfo>>
			info_list(Track::k_count);

		foreach (TimelineViewGhostItem *ghost, parent()->get_ghost_items()) {
			if (!ghost->has_been_adjusted()) {
				continue;
			}

			Track *track = parent()->get_track_from_reference(ghost->get_track());

			TrackListRippleToolCommand::RippleInfo info;
			Block *b = QtUtils::value_to_ptr<Block>(
				ghost->get_data(TimelineViewGhostItem::k_attached_block));

			if (b) {
				info.block = b;
				info.append_gap = false;
			} else {
				info.block = QtUtils::value_to_ptr<Block>(
					ghost->get_data(TimelineViewGhostItem::k_reference_block));
				info.append_gap = true;
			}

			info_list[track->type()].insert(track, info);
		}

		MultiUndoCommand *command = new MultiUndoCommand();

		Rational movement;

		if (drag_movement_mode() == Timeline::k_trim_out) {
			movement = parent()->get_ghost_items().first()->get_out_adjustment();
		} else {
			movement = parent()->get_ghost_items().first()->get_in_adjustment();
		}

		for (int i = 0; i < info_list.size(); i++) {
			if (!info_list.at(i).isEmpty()) {
				command->add_child(new TrackListRippleToolCommand(
					sequence()->track_list(static_cast<Track::Type>(i)),
					info_list.at(i), movement, drag_movement_mode()));
			}
		}

		if (command->child_count() > 0) {
			TimelineWidgetSelections new_sel = parent()->get_selections();
			TimelineViewGhostItem *reference_ghost =
				parent()->get_ghost_items().first();
			if (drag_movement_mode() == Timeline::k_trim_in) {
				new_sel.trim_out(-reference_ghost->get_in_adjustment());
			} else {
				new_sel.trim_out(reference_ghost->get_out_adjustment());
			}
			command->add_child(new TimelineWidget::SetSelectionsCommand(
				parent(), new_sel, parent()->get_selections(), false));

			Core::instance()->undo_stack()->push(
				command, qApp->translate("RippleTool", "Rippled Clips"));
		} else {
			delete command;
		}
	}
}

}
