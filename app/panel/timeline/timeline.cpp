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

#include "timeline.h"

#include "panel/panelmanager.h"
#include "panel/project/footagemanagementpanel.h"

namespace olive
{

TimelinePanel::TimelinePanel(const QString &name)
	: TimeBasedPanel(name)
{
	TimelineWidget *tw = new TimelineWidget(this);
	set_time_based_widget(tw);

	retranslate();

	connect(tw, &TimelineWidget::block_selection_changed, this,
			&TimelinePanel::block_selection_changed);
	connect(tw, &TimelineWidget::request_capture_start, this,
			&TimelinePanel::request_capture_start);
	connect(tw, &TimelineWidget::reveal_viewer_in_project, this,
			&TimelinePanel::reveal_viewer_in_project);
	connect(tw, &TimelineWidget::reveal_viewer_in_footage_viewer, this,
			&TimelinePanel::reveal_viewer_in_footage_viewer);
}

void TimelinePanel::split_at_playhead()
{
	timeline_widget()->split_at_playhead();
}

void TimelinePanel::load_data(const Info &info)
{
	timeline_widget()->restore_splitter_state(
		QByteArray::fromBase64(info.at("splitter").toUtf8()));
}

PanelWidget::Info TimelinePanel::save_data() const
{
	Info i;

	i["splitter"] = timeline_widget()->save_splitter_state().toBase64();

	return i;
}

void TimelinePanel::select_all()
{
	timeline_widget()->select_all();
}

void TimelinePanel::deselect_all()
{
	timeline_widget()->deselect_all();
}

void TimelinePanel::ripple_to_in()
{
	timeline_widget()->ripple_to_in();
}

void TimelinePanel::ripple_to_out()
{
	timeline_widget()->ripple_to_out();
}

void TimelinePanel::edit_to_in()
{
	timeline_widget()->edit_to_in();
}

void TimelinePanel::edit_to_out()
{
	timeline_widget()->edit_to_out();
}

void TimelinePanel::delete_selected()
{
	timeline_widget()->DeleteSelected(false);
}

void TimelinePanel::ripple_delete()
{
	timeline_widget()->DeleteSelected(true);
}

void TimelinePanel::increase_track_height()
{
	timeline_widget()->increase_track_height();
}

void TimelinePanel::decrease_track_height()
{
	timeline_widget()->decrease_track_height();
}

void TimelinePanel::toggle_links()
{
	timeline_widget()->toggle_links_on_selected();
}

void TimelinePanel::paste_insert()
{
	timeline_widget()->paste_insert();
}

void TimelinePanel::delete_in_to_out()
{
	timeline_widget()->delete_in_to_out(false);
}

void TimelinePanel::ripple_delete_in_to_out()
{
	timeline_widget()->delete_in_to_out(true);
}

void TimelinePanel::toggle_selected_enabled()
{
	timeline_widget()->toggle_selected_enabled();
}

void TimelinePanel::set_color_label(int index)
{
	timeline_widget()->set_color_label(index);
}

void TimelinePanel::nudge_left()
{
	timeline_widget()->nudge_left();
}

void TimelinePanel::nudge_right()
{
	timeline_widget()->nudge_right();
}

void TimelinePanel::move_in_to_playhead()
{
	timeline_widget()->move_in_to_playhead();
}

void TimelinePanel::move_out_to_playhead()
{
	timeline_widget()->move_out_to_playhead();
}

void TimelinePanel::rename_selected()
{
	timeline_widget()->rename_selected_blocks();
}

void TimelinePanel::insert_footage_at_playhead(
	const QVector<ViewerOutput *> &footage)
{
	timeline_widget()->insert_footage_at_playhead(footage);
}

void TimelinePanel::overwrite_footage_at_playhead(
	const QVector<ViewerOutput *> &footage)
{
	timeline_widget()->overwrite_footage_at_playhead(footage);
}

void TimelinePanel::retranslate()
{
	TimeBasedPanel::retranslate();

	set_title(tr("Timeline"));
}

}
