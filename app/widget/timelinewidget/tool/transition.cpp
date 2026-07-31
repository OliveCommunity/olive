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
#include "oakengine/undo.h"
#include "oakengine/timeline.h"
#include "transition.h"
#include "widget/timelinewidget/trackhandle.h"

namespace olive
{

namespace
{

/// Block::in() as rational seconds.
Rational ghost_block_in(const OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_in_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// Block::out() as rational seconds.
Rational ghost_block_out(const OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_out_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// Block::length() as rational seconds.
Rational ghost_block_length(const OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_length_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

} // namespace

TransitionTool::TransitionTool(TimelineWidget *parent)
	: AddTool(parent)
{
}

void TransitionTool::hover_move(TimelineViewMouseEvent *event)
{
	OakEngineClip *primary = nullptr;
	OakEngineClip *secondary = nullptr;
	TimelineApp::MovementMode trim_mode = TimelineApp::k_none;
	Rational transition_start_point;

	get_blocks_at_coord(event->get_coordinates(), &primary, &secondary, &trim_mode,
					 &transition_start_point);

	if (trim_mode == TimelineApp::k_trim_in) {
		std::swap(primary, secondary);
	}

	parent()->set_view_transition_overlay(primary, secondary);
}

void TransitionTool::mouse_press(TimelineViewMouseEvent *event)
{
	OakEngineClip *primary, *secondary;
	TimelineApp::MovementMode trim_mode;
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
	const TrackReference &track = ghost_->get_track();

	if (ghost_) {
		if (!ghost_->get_adjusted_length().isNull()) {
			OakEngineNode *transition;

			if (Core::instance()->get_selected_transition().isEmpty()) {
				// Fallback if the user hasn't selected one yet
				transition = oakengine_node_factory_create_from_id("org.olivevideoeditor.Olive.crossdissolve");
			} else {
				transition =
					oakengine_node_factory_create_from_id(
						Core::instance()->get_selected_transition().toUtf8().constData());
			}

			// Set transition length: routed through a trim command child
			// added before the placement child below (children redo in
			// order, so the length is set before the block is placed).
			// oakengine_block_set_length_and_media_out() itself cannot be
			// used here -- it requires the block to already be on a track
			// (OAKENGINE_E_STATE). Before placement the block has no
			// adjacent blocks, so a trim-out command reduces to
			// Block::set_length_and_media_out().
			Rational len = ghost_->get_adjusted_length();

			void *command = oakengine_undo_command_create_multi();

			auto *seq_handle = sequence();

			// Place transition in place
			oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			oakengine_node_parent(reinterpret_cast<OakEngineNode *>(
				parent()->get_connected_node())),
			transition));

			oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(transition), reinterpret_cast<void *>(transition), 0, 0, 0));

			oakengine_undo_command_multi_add_child(command,
				oakengine_block_trim_command(
					reinterpret_cast<void *>(oakengine_sequence_track_at(
						seq_handle, track.type(), track.index())),
					reinterpret_cast<void *>(transition),
					len.numerator(), len.denominator(),
					OAKENGINE_MOVEMENT_MODE_TRIM_OUT, 0));

			oakengine_undo_command_multi_add_child(command, oakengine_track_place_block_command(reinterpret_cast<void *>(oakengine_sequence_track_list(seq_handle, track.type())), track.index(), reinterpret_cast<void *>(transition), core::Timecode::time_to_timestamp(ghost_->get_adjusted_in(), parent()->timebase())));

			if (dual_transition_) {
				// Block mouse is hovering over
				OakEngineBlock *active_block = QtUtils::value_to_ptr<OakEngineBlock>(
					ghost_->get_data(TimelineViewGhostItem::k_attached_block));

				// Block mouse is next to
				OakEngineBlock *friend_block = QtUtils::value_to_ptr<OakEngineBlock>(
					ghost_->get_data(TimelineViewGhostItem::k_reference_block));

				// Use ghost mode to determine which block is which
				OakEngineBlock *out_block = (ghost_->get_mode() == TimelineApp::k_trim_in) ?
									   friend_block :
									   active_block;
				OakEngineBlock *in_block = (ghost_->get_mode() == TimelineApp::k_trim_in) ?
									  active_block :
									  friend_block;

				// Connect block to transition
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_node_connect_command(
						reinterpret_cast<OakEngineNode *>(out_block),
						transition,
						QLatin1String(oakengine_transition_out_block_input_id()).toUtf8().constData(),
						-1));

				oakengine_undo_command_multi_add_child(
					command,
					oakengine_node_connect_command(
						reinterpret_cast<OakEngineNode *>(in_block),
						transition,
						QLatin1String(oakengine_transition_in_block_input_id()).toUtf8().constData(),
						-1));

				oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(out_block), reinterpret_cast<void *>(transition), -1, -0.5, 0));
				oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(in_block), reinterpret_cast<void *>(transition), -1, 0.5, 0));
			} else {
				OakEngineBlock *block_to_transition = QtUtils::value_to_ptr<OakEngineBlock>(
					ghost_->get_data(TimelineViewGhostItem::k_attached_block));
				QString transition_input_to_connect;

				if (ghost_->get_mode() == TimelineApp::k_trim_in) {
					transition_input_to_connect =
						QLatin1String(oakengine_transition_in_block_input_id());
				} else {
					transition_input_to_connect =
						QLatin1String(oakengine_transition_out_block_input_id());
				}

				// Connect block to transition
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_node_connect_command(
						reinterpret_cast<OakEngineNode *>(block_to_transition),
						transition,
						transition_input_to_connect.toUtf8().constData(),
						-1));

				oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(block_to_transition), reinterpret_cast<void *>(transition), -1, 0, 0));
			}

			oakengine_undo_push(
				command,
				qApp->translate("TransitionTool", "Created Transition").toUtf8().constData());

			parent()->set_view_transition_overlay(nullptr, nullptr);
		}

		parent()->clear_ghosts();
		snap_points_.clear();
		ghost_ = nullptr;
	}
}

bool TransitionTool::get_blocks_at_coord(const TimelineCoordinate &coord,
									  OakEngineClip **primary,
									  OakEngineClip **secondary,
									  TimelineApp::MovementMode *ptrim_mode,
									  Rational *start_point)
{
	const TrackReference &coord_track = coord.get_track();
	OakEngineTrack *t = parent()->get_track_from_reference(coord_track);
	Rational cursor_frame = coord.get_frame();

	if (!t || track_is_locked(t)) {
		return false;
	}

	OakEngineBlock *block_at_time =
		oakengine_track_nearest_block_before_or_at(
			t,
			core::Timecode::time_to_timestamp(coord.get_frame(),
											parent()->timebase()));
	if (!oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(block_at_time))) {
		return false;
	}

	// Determine which side of the clip the transition belongs to
	Rational transition_start_point;
	TimelineApp::MovementMode trim_mode;
	Rational tenth_point = ghost_block_length(block_at_time) / 10;
	OakEngineBlock *other_block = nullptr;
	if (cursor_frame <
		(ghost_block_in(block_at_time) +
		 ghost_block_length(block_at_time) / 2)) {
		if (oakengine_clip_in_transition(block_at_time)) {
			// This clip already has a transition here
			return false;
		}

		OakEngineBlock *previous = oakengine_block_prev(block_at_time);
		OakEngineClip *adjacent =
			oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(previous)) ?
				reinterpret_cast<OakEngineClip *>(previous) :
				nullptr;
		if (adjacent) {
			tenth_point = std::min(tenth_point, ghost_block_length(reinterpret_cast<const OakEngineBlock *>(adjacent)) / 10);
		}

		transition_start_point = ghost_block_in(block_at_time);
		trim_mode = TimelineApp::k_trim_in;

		if (cursor_frame < (ghost_block_in(block_at_time) + tenth_point) &&
			adjacent) {
			other_block = reinterpret_cast<OakEngineBlock *>(adjacent);
		}
	} else {
		if (oakengine_clip_out_transition(block_at_time)) {
			// This clip already has a transition here
			return false;
		}

		OakEngineBlock *next = oakengine_block_next(block_at_time);
		OakEngineClip *adjacent =
			oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(next)) ?
				reinterpret_cast<OakEngineClip *>(next) :
				nullptr;
		if (adjacent) {
			tenth_point = std::min(tenth_point, ghost_block_length(reinterpret_cast<const OakEngineBlock *>(adjacent)) / 10);
		}

		transition_start_point = ghost_block_out(block_at_time);
		trim_mode = TimelineApp::k_trim_out;

		if (cursor_frame > ghost_block_out(block_at_time) - tenth_point &&
			adjacent) {
			other_block = next;
		}
	}

	*primary = reinterpret_cast<OakEngineClip *>(block_at_time);
	*secondary = reinterpret_cast<OakEngineClip *>(other_block);
	*ptrim_mode = trim_mode;
	*start_point = transition_start_point;

	return true;
}

}
