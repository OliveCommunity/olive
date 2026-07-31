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

#ifndef OAK_POINTERTIMELINETOOL_H
#define OAK_POINTERTIMELINETOOL_H

#include "tool.h"

namespace olive
{

class PointerTool : public TimelineTool {
public:
	PointerTool(TimelineWidget *parent);

	virtual void mouse_press(TimelineViewMouseEvent *event) override;
	virtual void mouse_move(TimelineViewMouseEvent *event) override;
	virtual void mouse_release(TimelineViewMouseEvent *event) override;

	virtual void hover_move(TimelineViewMouseEvent *event) override;

protected:
	virtual void finish_drag(TimelineViewMouseEvent *event);

	virtual void initiate_drag(OakEngineBlock *clicked_item,
							  TimelineApp::MovementMode trim_mode,
							  Qt::KeyboardModifiers modifiers);

	TimelineViewGhostItem *get_existing_ghost_from_block(OakEngineBlock *block);

	TimelineViewGhostItem *add_ghost_from_block(OakEngineBlock *block,
											 TimelineApp::MovementMode mode,
											 bool check_if_exists = false);

	TimelineViewGhostItem *add_ghost_from_null(const Rational &in,
											const Rational &out,
											const TrackReference &track,
											TimelineApp::MovementMode mode);

	/**
   * @brief Validates Ghosts that are getting their in points trimmed
   *
   * Assumes ghost->data() is a Block. Ensures no Ghost's in point becomes a negative timecode. Also ensures no
   * Ghost's length becomes 0 or negative.
   */
	Rational validate_in_trimming(Rational movement);

	/**
   * @brief Validates Ghosts that are getting their out points trimmed
   *
   * Assumes ghost->data() is a Block. Ensures no Ghost's in point becomes a negative timecode. Also ensures no
   * Ghost's length becomes 0 or negative.
   */
	Rational validate_out_trimming(Rational movement);

	virtual void process_drag(const TimelineCoordinate &mouse_pos);

	void initiate_drag_internal(OakEngineBlock *clicked_item,
							  TimelineApp::MovementMode trim_mode,
							  Qt::KeyboardModifiers modifiers,
							  bool dont_roll_trims, bool allow_nongap_rolling,
							  bool slide_instead_of_moving);

	const TimelineApp::MovementMode &drag_movement_mode() const
	{
		return drag_movement_mode_;
	}
	void set_drag_movement_mode(const TimelineApp::MovementMode &d)
	{
		drag_movement_mode_ = d;
	}

	static bool can_transition_move(OakEngineBlock *transit,
								  const QVector<OakEngineBlock *> &clips);

	void set_movement_allowed(bool e)
	{
		movement_allowed_ = e;
	}

	void set_trimming_allowed(bool e)
	{
		trimming_allowed_ = e;
	}

	void set_track_movement_allowed(bool e)
	{
		track_movement_allowed_ = e;
	}

	void set_gap_trimming_allowed(bool e)
	{
		gap_trimming_allowed_ = e;
	}

	void set_clicked_item(OakEngineBlock *b)
	{
		clicked_item_ = b;
	}

private:
	TimelineApp::MovementMode is_cursor_in_trim_handle(OakEngineBlock *block, qreal cursor_x);

	void add_ghost_internal(TimelineViewGhostItem *ghost,
						  TimelineApp::MovementMode mode);

	bool is_clip_trimmable(OakEngineBlock *clip, const QVector<OakEngineBlock *> &items,
						 const TimelineApp::MovementMode &mode);

	void process_ghosts_for_sliding();

	void process_ghosts_for_rolling();

	bool movement_allowed_;
	bool trimming_allowed_;
	bool track_movement_allowed_;
	bool gap_trimming_allowed_;
	bool can_rubberband_select_;
	bool rubberband_selecting_;

	TrackReference::Type drag_track_type_;
	TimelineApp::MovementMode drag_movement_mode_;

	OakEngineBlock *clicked_item_;

	QPoint drag_global_start_;
};

}

#endif // OAK_POINTERTIMELINETOOL_H
