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

#ifndef OAK_TIMELINE_PANEL_H
#define OAK_TIMELINE_PANEL_H

#include "panel/timebased/timebased.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

/**
 * @brief Panel container for a TimelineWidget
 */
class TimelinePanel : public TimeBasedPanel {
	Q_OBJECT
public:
	TimelinePanel(const QString &name);

	inline TimelineWidget *timeline_widget() const
	{
		return static_cast<TimelineWidget *>(get_time_based_widget());
	}

	void split_at_playhead();

	virtual void load_data(const Info &info) override;
	virtual Info save_data() const override;

	virtual void select_all() override;

	virtual void deselect_all() override;

	virtual void ripple_to_in() override;

	virtual void ripple_to_out() override;

	virtual void edit_to_in() override;

	virtual void edit_to_out() override;

	virtual void delete_selected() override;

	virtual void ripple_delete() override;

	virtual void increase_track_height() override;

	virtual void decrease_track_height() override;

	virtual void toggle_links() override;

	virtual void paste_insert() override;

	virtual void delete_in_to_out() override;

	virtual void ripple_delete_in_to_out() override;

	virtual void toggle_selected_enabled() override;

	virtual void set_color_label(int index) override;

	virtual void nudge_left() override;

	virtual void nudge_right() override;

	virtual void move_in_to_playhead() override;

	virtual void move_out_to_playhead() override;

	virtual void rename_selected() override;

	void add_default_transitions_to_selected()
	{
		timeline_widget()->add_default_transitions_to_selected();
	}

	void show_speed_duration_dialog_for_selected_clips()
	{
		timeline_widget()->show_speed_duration_dialog_for_selected_clips();
	}

	void nest_selected_clips()
	{
		timeline_widget()->nest_selected_clips();
	}

	void insert_footage_at_playhead(const QVector<ViewerOutput *> &footage);

	void overwrite_footage_at_playhead(const QVector<ViewerOutput *> &footage);

	const QVector<Block *> &get_selected_blocks() const
	{
		return timeline_widget()->get_selected_blocks();
	}

	Sequence *get_sequence() const
	{
		return dynamic_cast<Sequence *>(get_connected_viewer());
	}

protected:
	virtual void retranslate() override;

signals:
	void block_selection_changed(const QVector<Block *> &selected_blocks);

	void request_capture_start(const TimeRange &time,
							 const Track::Reference &track);

	void reveal_viewer_in_project(ViewerOutput *r);
	void reveal_viewer_in_footage_viewer(ViewerOutput *r, const TimeRange &range);
};

}

#endif // OAK_TIMELINE_PANEL_H
