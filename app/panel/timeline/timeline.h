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

#include "common/trackreferencehandle.h"
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

	void insert_footage_at_playhead(const QVector<OakEngineNode *> &footage);

	void overwrite_footage_at_playhead(const QVector<OakEngineNode *> &footage);

	QVector<OakEngineBlock *> get_selected_blocks() const
	{
		// TimelineWidget still exposes engine Block* (its own wave converts
		// it); re-wrap into the handle family here so callers never see the
		// engine type.
		const auto &selected = timeline_widget()->get_selected_blocks();
		QVector<OakEngineBlock *> handles;
		handles.reserve(selected.size());
		for (auto *b : selected) {
			handles.append(reinterpret_cast<OakEngineBlock *>(b));
		}
		return handles;
	}

	OakEngineSequence *get_sequence() const
	{
		// R8: type check via the C ABI facade predicate (replaces
		// dynamic_cast<Sequence*>, which needed the complete engine type);
		// the handle is shared, so the cast is a reinterpret.
		OakEngineNode *connected = get_connected_viewer();
		return (connected && oakengine_node_is_sequence(connected))
				   ? reinterpret_cast<OakEngineSequence *>(connected)
				   : nullptr;
	}

protected:
	virtual void retranslate() override;

signals:
	void block_selection_changed(const QVector<OakEngineBlock *> &selected_blocks);

	void request_capture_start(const TimeRange &time,
							 const TrackReference &track);

	void reveal_viewer_in_project(OakEngineNode *r);
	void reveal_viewer_in_footage_viewer(OakEngineNode *r, const TimeRange &range);
};

}

#endif // OAK_TIMELINE_PANEL_H
