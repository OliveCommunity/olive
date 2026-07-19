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

#include "add.h"
#include "core.h"
#include "node/block/subtitle/subtitle.h"
#include "node/factory.h"
#include "node/generator/shape/shapenode.h"
#include "node/generator/solid/solid.h"
#include "node/generator/text/textv3.h"
#include "node/nodeundo.h"
#include "timeline/timelineundopointer.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

AddTool::AddTool(TimelineWidget *parent)
	: BeamTool(parent)
	, ghost_(nullptr)
{
}

void AddTool::mouse_press(TimelineViewMouseEvent *event)
{
	const Track::Reference &track = event->get_track();

	// Check if track is locked
	Track *t = parent()->get_track_from_reference(track);
	if (t && t->is_locked()) {
		return;
	}

	Track::Type add_type = Track::k_none;

	switch (Core::instance()->get_selected_addable_object()) {
	case Tool::k_addable_bars:
	case Tool::k_addable_solid:
	case Tool::k_addable_title:
	case Tool::k_addable_shape:
		add_type = Track::k_video;
		break;
	case Tool::k_addable_tone:
		add_type = Track::k_audio;
		break;
	case Tool::k_addable_subtitle:
		add_type = Track::k_subtitle;
		break;
	case Tool::k_addable_empty:
		// Leave as "none", which means this block can be placed on any track
		break;
	case Tool::k_addable_count:
		// Return so we do nothing
		return;
	}

	if (add_type == Track::k_none || add_type == track.type()) {
		drag_start_point_ =
			validated_coordinate(event->get_coordinates(true)).get_frame();

		ghost_ = new TimelineViewGhostItem();
		ghost_->set_in(drag_start_point_);
		ghost_->set_out(drag_start_point_);
		ghost_->set_track(track);
		parent()->add_ghost(ghost_);

		snap_points_.push_back(drag_start_point_);
	}
}

void AddTool::mouse_move(TimelineViewMouseEvent *event)
{
	if (!ghost_) {
		return;
	}

	mouse_move_internal(event->get_frame(),
					  event->get_modifiers() & Qt::AltModifier);
}

void AddTool::mouse_release(TimelineViewMouseEvent *event)
{
	if (ghost_) {
		if (!ghost_->get_adjusted_length().isNull()) {
			MultiUndoCommand *command = new MultiUndoCommand();

			if (MultiUndoCommand *subtitle_section_command =
					parent()->take_subtitle_section_command()) {
				command->add_child(subtitle_section_command);
			}

			Sequence *s = parent()->sequence();

			QRectF r;
			if (Core::instance()->get_selected_addable_object() ==
				Tool::k_addable_title) {
				VideoParams svp = s->get_video_params();
				r = QRectF(0, 0, svp.width(), svp.height());
				r.adjust(svp.width() / 10, svp.height() / 10, -svp.width() / 10,
						 -svp.height() / 10);
			}

			create_addable_clip(command, s, ghost_->get_track(),
							  ghost_->get_adjusted_in(),
							  ghost_->get_adjusted_length(), r);

			Core::instance()->undo_stack()->push(
				command, qApp->translate("AddTool", "Added Clip"));
		}

		parent()->clear_ghosts();
		snap_points_.clear();
		ghost_ = nullptr;
	}
}

Node *AddTool::create_addable_clip(MultiUndoCommand *command, Sequence *sequence,
								 const Track::Reference &track,
								 const Rational &in, const Rational &length,
								 const QRectF &rect)
{
	ClipBlock *clip;
	if (Core::instance()->get_selected_addable_object() ==
		Tool::k_addable_subtitle) {
		clip = new SubtitleBlock();
	} else {
		clip = new ClipBlock();
		clip->set_label(olive::Tool::get_addable_object_name(
			Core::instance()->get_selected_addable_object()));
	}
	clip->set_length_and_media_out(length);

	Project *graph = sequence->parent();

	command->add_child(new NodeAddCommand(graph, clip));
	command->add_child(new NodeSetPositionCommand(clip, clip, QPointF(0, 0)));
	command->add_child(new TrackPlaceBlockCommand(
		sequence->track_list(track.type()), track.index(), clip, in));

	Node *node_to_add = nullptr;

	switch (Core::instance()->get_selected_addable_object()) {
	case Tool::k_addable_empty:
		// Empty, nothing to be done
		break;
	case Tool::k_addable_solid:
		node_to_add = new SolidGenerator();
		break;
	case Tool::k_addable_shape:
		node_to_add = new ShapeNode();
		break;
	case Tool::k_addable_title:
		node_to_add = new TextGeneratorV3();
		break;
	case Tool::k_addable_bars:
	case Tool::k_addable_tone:
		// Not implemented yet
		qWarning() << "Unimplemented add object:"
				   << Core::instance()->get_selected_addable_object();
		break;
	case Tool::k_addable_subtitle:
		// The block itself is the node we want
		break;
	case Tool::k_addable_count:
		// Invalid value, do nothing
		break;
	}

	if (node_to_add) {
		QPointF extra_node_offset(k_default_distance_from_output, 0);
		command->add_child(new NodeAddCommand(graph, node_to_add));
		command->add_child(new NodeEdgeAddCommand(
			node_to_add, NodeInput(clip, ClipBlock::k_buffer_in)));
		command->add_child(
			new NodeSetPositionCommand(node_to_add, clip, extra_node_offset));

		if (!rect.isNull()) {
			if (ShapeNodeBase *shape =
					dynamic_cast<ShapeNodeBase *>(node_to_add)) {
				shape->set_rect(rect, sequence->get_video_params(), command);
			}
		}
	}

	return node_to_add;
}

void AddTool::mouse_move_internal(const Rational &cursor_frame, bool outwards)
{
	// Calculate movement
	Rational movement = cursor_frame - drag_start_point_;

	// Validation: Ensure in point never goes below 0
	if (movement < -ghost_->get_in() ||
		(outwards && -movement < -ghost_->get_in())) {
		movement = -ghost_->get_in();
	}

	// Snap movement
	bool snapped;

	if (Core::instance()->snapping()) {
		snapped = parent()->snap_point(snap_points_, &movement);
	} else {
		snapped = false;
	}

	// If alt is held, our movement goes both ways (outwards)
	if (!snapped && outwards) {
		// Snap backwards too
		movement = -movement;
		parent()->snap_point(snap_points_, &movement);
		// We don't need to un-neg here because outwards means all future processing will be done both pos and neg
	}

	// Make adjustment
	if (!movement) {
		ghost_->set_in_adjustment(0);
		ghost_->set_out_adjustment(0);
	} else if (movement > 0) {
		ghost_->set_in_adjustment(outwards ? -movement : 0);
		ghost_->set_out_adjustment(movement);
	} else if (movement < 0) {
		ghost_->set_in_adjustment(movement);
		ghost_->set_out_adjustment(outwards ? -movement : 0);
	}
}

}
