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

#ifndef OAK_IMPORTTIMELINETOOL_H
#define OAK_IMPORTTIMELINETOOL_H

#include "common/dropworkflowbehavior.h"
#include "tool.h"

namespace olive
{

class ImportTool : public TimelineTool {
public:
	ImportTool(TimelineWidget *parent);

	virtual void drag_enter(TimelineViewMouseEvent *event) override;
	virtual void drag_move(TimelineViewMouseEvent *event) override;
	virtual void drag_leave(QDragLeaveEvent *event) override;
	virtual void drag_drop(TimelineViewMouseEvent *event) override;

	using DraggedFootageData =
		QVector<QPair<ViewerOutput *, QVector<Track::Reference>>>;

	void place_at(const QVector<ViewerOutput *> &footage, const Rational &start,
				 bool insert, MultiUndoCommand *command, int track_offset = 0,
				 bool jump_to_end = false);
	void place_at(const DraggedFootageData &footage, const Rational &start,
				 bool insert, MultiUndoCommand *command, int track_offset = 0,
				 bool jump_to_end = false);

	// The canonical definition lives in the engine layer
	// (common/dropworkflowbehavior.h); this alias keeps existing call
	// sites source-compatible. Use olive::k_dws_ask etc. for the
	// enumerators (visible unqualified inside namespace olive).
	using DropWithoutSequenceBehavior =
		olive::DropWithoutSequenceBehavior;

private:
	void footage_to_ghosts(Rational ghost_start,
						 const DraggedFootageData &footage,
						 const Rational &dest_tb, const int &track_start);

	void prep_ghosts(const Rational &frame, const int &track_index);

	void drop_ghosts(bool insert, MultiUndoCommand *parent_command);

	TimelineViewGhostItem *create_ghost(const TimeRange &range,
									   const Rational &media_in,
									   const Track::Reference &track);

	DraggedFootageData dragged_footage_;

	int import_pre_buffer_;

	Rational ghost_offset_;
};

}

#endif // OAK_IMPORTTIMELINETOOL_H
