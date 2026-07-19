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

#include "node/block/transition/crossdissolve/crossdissolvetransition.h"
#include "node/block/transition/transition.h"
#include "node/factory.h"
#include "node/nodeundo.h"
#include "timeline/timelineundopointer.h"
#include "transition.h"

namespace olive
{

TransitionTool::TransitionTool(TimelineWidget *parent)
	: AddTool(parent)
{
}

void TransitionTool::hover_move(TimelineViewMouseEvent *event)
{
	ClipBlock *primary = nullptr;
	ClipBlock *secondary = nullptr;
	Timeline::MovementMode trim_mode = Timeline::k_none;
	Rational transition_start_point;

	get_blocks_at_coord(event->get_coordinates(), &primary, &secondary, &trim_mode,
					 &transition_start_point);

	if (trim_mode == Timeline::k_trim_in) {
		std::swap(primary, secondary);
	}

	parent()->set_view_transition_overlay(primary, secondary);
}

void TransitionTool::mouse_press(TimelineViewMouseEvent *event)
{
	ClipBlock *primary, *secondary;
	Timeline::MovementMode trim_mode;
	Rational transition_start_point;
	if (!get_blocks_at_coord(event->get_coordinates(), &primary, &secondary,
						  &trim_mode, &transition_start_point)) {
		return;
	}

	// Create ghost
	ghost_ = new TimelineViewGhostItem();
	ghost_->set_track(event->get_track());
	ghost_->set_in(transition_start_point);
	ghost_->set_out(transition_start_point);
	ghost_->set_mode(trim_mode);
	ghost_->set_data(TimelineViewGhostItem::k_attached_block,
					QtUtils::ptr_to_value(primary));

	dual_transition_ = (secondary);
	if (secondary)
		ghost_->set_data(TimelineViewGhostItem::k_reference_block,
						QtUtils::ptr_to_value(secondary));

	parent()->add_ghost(ghost_);

	snap_points_.push_back(transition_start_point);

	// Set the drag start point
	drag_start_point_ = event->get_frame();
}

void TransitionTool::mouse_move(TimelineViewMouseEvent *event)
{
	if (!ghost_) {
		return;
	}

	mouse_move_internal(event->get_frame(), dual_transition_);
}

void TransitionTool::mouse_release(TimelineViewMouseEvent *event)
{
	const Track::Reference &track = ghost_->get_track();

	if (ghost_) {
		if (!ghost_->get_adjusted_length().isNull()) {
			TransitionBlock *transition;

			if (Core::instance()->get_selected_transition().isEmpty()) {
				// Fallback if the user hasn't selected one yet
				transition = new CrossDissolveTransition();
			} else {
				transition =
					static_cast<TransitionBlock *>(NodeFactory::create_from_id(
						Core::instance()->get_selected_transition()));
			}

			// Set transition length
			Rational len = ghost_->get_adjusted_length();
			transition->set_length_and_media_out(len);

			MultiUndoCommand *command = new MultiUndoCommand();

			// Place transition in place
			command->add_child(new NodeAddCommand(
				parent()->get_connected_node()->parent(), transition));

			command->add_child(new NodeSetPositionCommand(
				transition, transition, QPointF(0, 0)));

			command->add_child(new TrackPlaceBlockCommand(
				sequence()->track_list(track.type()), track.index(), transition,
				ghost_->get_adjusted_in()));

			if (dual_transition_) {
				// Block mouse is hovering over
				Block *active_block = QtUtils::value_to_ptr<Block>(
					ghost_->get_data(TimelineViewGhostItem::k_attached_block));

				// Block mouse is next to
				Block *friend_block = QtUtils::value_to_ptr<Block>(
					ghost_->get_data(TimelineViewGhostItem::k_reference_block));

				// Use ghost mode to determine which block is which
				Block *out_block = (ghost_->get_mode() == Timeline::k_trim_in) ?
									   friend_block :
									   active_block;
				Block *in_block = (ghost_->get_mode() == Timeline::k_trim_in) ?
									  active_block :
									  friend_block;

				// Connect block to transition
				command->add_child(new NodeEdgeAddCommand(
					out_block,
					NodeInput(transition, TransitionBlock::k_out_block_input)));

				command->add_child(new NodeEdgeAddCommand(
					in_block,
					NodeInput(transition, TransitionBlock::k_in_block_input)));

				command->add_child(new NodeSetPositionCommand(
					out_block, transition, QPointF(-1, -0.5)));
				command->add_child(new NodeSetPositionCommand(
					in_block, transition, QPointF(-1, 0.5)));
			} else {
				Block *block_to_transition = QtUtils::value_to_ptr<Block>(
					ghost_->get_data(TimelineViewGhostItem::k_attached_block));
				QString transition_input_to_connect;

				if (ghost_->get_mode() == Timeline::k_trim_in) {
					transition_input_to_connect =
						TransitionBlock::k_in_block_input;
				} else {
					transition_input_to_connect =
						TransitionBlock::k_out_block_input;
				}

				// Connect block to transition
				command->add_child(new NodeEdgeAddCommand(
					block_to_transition,
					NodeInput(transition, transition_input_to_connect)));

				command->add_child(new NodeSetPositionCommand(
					block_to_transition, transition, QPointF(-1, 0)));
			}

			Core::instance()->undo_stack()->push(
				command,
				qApp->translate("TransitionTool", "Created Transition"));

			parent()->set_view_transition_overlay(nullptr, nullptr);
		}

		parent()->clear_ghosts();
		snap_points_.clear();
		ghost_ = nullptr;
	}
}

bool TransitionTool::get_blocks_at_coord(const TimelineCoordinate &coord,
									  ClipBlock **primary,
									  ClipBlock **secondary,
									  Timeline::MovementMode *ptrim_mode,
									  Rational *start_point)
{
	const Track::Reference &track = coord.get_track();
	Track *t = parent()->get_track_from_reference(track);
	Rational cursor_frame = coord.get_frame();

	if (!t || t->is_locked()) {
		return false;
	}

	Block *block_at_time = t->nearest_block_before_or_at(coord.get_frame());
	if (!dynamic_cast<ClipBlock *>(block_at_time)) {
		return false;
	}

	// Determine which side of the clip the transition belongs to
	Rational transition_start_point;
	Timeline::MovementMode trim_mode;
	Rational tenth_point = block_at_time->length() / 10;
	Block *other_block = nullptr;
	if (cursor_frame < (block_at_time->in() + block_at_time->length() / 2)) {
		if (static_cast<ClipBlock *>(block_at_time)->in_transition()) {
			// This clip already has a transition here
			return false;
		}

		ClipBlock *adjacent =
			dynamic_cast<ClipBlock *>(block_at_time->previous());
		if (adjacent) {
			tenth_point = std::min(tenth_point, adjacent->length() / 10);
		}

		transition_start_point = block_at_time->in();
		trim_mode = Timeline::k_trim_in;

		if (cursor_frame < (block_at_time->in() + tenth_point) && adjacent) {
			other_block = adjacent;
		}
	} else {
		if (static_cast<ClipBlock *>(block_at_time)->out_transition()) {
			// This clip already has a transition here
			return false;
		}

		ClipBlock *adjacent = dynamic_cast<ClipBlock *>(block_at_time->next());
		if (adjacent) {
			tenth_point = std::min(tenth_point, adjacent->length() / 10);
		}

		transition_start_point = block_at_time->out();
		trim_mode = Timeline::k_trim_out;

		if (cursor_frame > block_at_time->out() - tenth_point && adjacent) {
			other_block = block_at_time->next();
		}
	}

	*primary = static_cast<ClipBlock *>(block_at_time);
	*secondary = static_cast<ClipBlock *>(other_block);
	*ptrim_mode = trim_mode;
	*start_point = transition_start_point;

	return true;
}

}
