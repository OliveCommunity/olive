/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "record.h"

#include "widget/timelinewidget/timelinewidget.h"
#include "widget/timelinewidget/trackhandle.h"

namespace olive
{

RecordTool::RecordTool(TimelineWidget *parent)
	: BeamTool(parent)
	, ghost_(nullptr)
{
}

void RecordTool::mouse_press(TimelineViewMouseEvent *event)
{
	const TrackReference &track = event->get_track();

	// Check if track is locked
	// TimelineWidget::get_track_from_reference() takes the app
	// TrackReference mirror (ordinals static_assert-pinned to the engine
	// Track::Type values, see common/trackreferencehandle.h).
	OakEngineTrack *t = parent()->get_track_from_reference(track);
	if (track_is_locked(t)) {
		return;
	}

	if (t && oakengine_track_type(t) != TrackReference::k_audio) {
		// We only support audio tracks here
		return;
	}

	drag_start_point_ =
		validated_coordinate(event->get_coordinates(true)).get_frame();

	ghost_ = new TimelineViewGhostItem();
	ghost_->set_in(drag_start_point_);
	ghost_->set_out(drag_start_point_);
	ghost_->set_track(track);
	parent()->add_ghost(ghost_);

	snap_points_.push_back(drag_start_point_);
}

void RecordTool::mouse_move(TimelineViewMouseEvent *event)
{
	if (!ghost_) {
		return;
	}

	// Calculate movement
	Rational movement = event->get_frame() - drag_start_point_;

	// Validation: Ensure in point never goes below 0
	if (movement < -ghost_->get_in()) {
		movement = -ghost_->get_in();
	}

	// Snap movement
	bool snapped;

	if (Core::instance()->snapping()) {
		snapped = parent()->snap_point(snap_points_, &movement);
	} else {
		snapped = false;
	}

	// Make adjustment
	if (!movement) {
		ghost_->set_in_adjustment(0);
		ghost_->set_out_adjustment(0);
	} else if (movement > 0) {
		ghost_->set_in_adjustment(0);
		ghost_->set_out_adjustment(movement);
	} else if (movement < 0) {
		ghost_->set_in_adjustment(movement);
		ghost_->set_out_adjustment(0);
	}

	Q_UNUSED(snapped)
}

void RecordTool::mouse_release(TimelineViewMouseEvent *event)
{
	if (ghost_) {
		emit parent() -> request_capture_start(
			TimeRange(ghost_->get_adjusted_in(), ghost_->get_adjusted_out()),
			ghost_->get_track());
		parent()->clear_ghosts();
		snap_points_.clear();
		ghost_ = nullptr;
	}
}

}
