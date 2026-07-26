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

#include "timebased.h"

namespace olive
{

TimeBasedPanel::TimeBasedPanel(const QString &object_name)
	: PanelWidget(object_name)
	, widget_(nullptr)
	, show_and_raise_on_connect_(false)
{
	bridge_ = new EngineEventBridge(this);
	connect(bridge_, &EngineEventBridge::node_label_changed, this,
			[this](OakEngineNode *, const QString &label) {
				set_subtitle(label);
			});
}

TimeBasedPanel::~TimeBasedPanel()
{
	delete widget_;
}

const Rational &TimeBasedPanel::timebase()
{
	return widget_->timebase();
}

void TimeBasedPanel::go_to_start()
{
	widget_->go_to_start();
}

void TimeBasedPanel::prev_frame()
{
	widget_->prev_frame();
}

void TimeBasedPanel::next_frame()
{
	widget_->next_frame();
}

void TimeBasedPanel::go_to_end()
{
	widget_->go_to_end();
}

void TimeBasedPanel::zoom_in()
{
	widget_->zoom_in();
}

void TimeBasedPanel::zoom_out()
{
	widget_->zoom_out();
}

void TimeBasedPanel::set_timebase(const Rational &timebase)
{
	widget_->SetTimebase(timebase);
}

void TimeBasedPanel::go_to_prev_cut()
{
	widget_->go_to_prev_cut();
}

void TimeBasedPanel::go_to_next_cut()
{
	widget_->go_to_next_cut();
}

void TimeBasedPanel::play_pause()
{
	emit play_pause_requested();
}

void TimeBasedPanel::play_in_to_out()
{
	emit play_in_to_out_requested();
}

void TimeBasedPanel::shuttle_left()
{
	emit shuttle_left_requested();
}

void TimeBasedPanel::shuttle_stop()
{
	emit shuttle_stop_requested();
}

void TimeBasedPanel::shuttle_right()
{
	emit shuttle_right_requested();
}

void TimeBasedPanel::connect_viewer_node(ViewerOutput *node)
{
	widget_->connect_viewer_node(node);
}

void TimeBasedPanel::set_time_based_widget(TimeBasedWidget *widget)
{
	if (widget_) {
		disconnect(widget_, &TimeBasedWidget::connected_node_changed, this,
				   &TimeBasedPanel::connected_node_changed);
	}

	widget_ = widget;

	if (widget_) {
		connect(widget_, &TimeBasedWidget::connected_node_changed, this,
				&TimeBasedPanel::connected_node_changed);
	}

	set_widget_with_padding(widget_);
}

void TimeBasedPanel::retranslate()
{
	if (get_time_based_widget()->get_connected_node()) {
		set_subtitle(get_time_based_widget()->get_connected_node()->get_label());
	} else {
		set_subtitle(tr("(none)"));
	}
}

void TimeBasedPanel::connected_node_changed(OakEngineNode *old, OakEngineNode *now)
{
	if (old) {
		if (label_sub_) {
			bridge_->unsubscribe(label_sub_);
			label_sub_ = 0;
		}
	}

	if (now) {
		label_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(now),
			OAKENGINE_EVENT_NODE_LABEL_CHANGED);

		if (show_and_raise_on_connect_) {
			this->show();
			this->raise();
		}
	}

	// Update strings
	retranslate();
}

void TimeBasedPanel::set_in()
{
	get_time_based_widget()->set_in_at_playhead();
}

void TimeBasedPanel::set_out()
{
	get_time_based_widget()->set_out_at_playhead();
}

void TimeBasedPanel::reset_in()
{
	get_time_based_widget()->reset_in();
}

void TimeBasedPanel::reset_out()
{
	get_time_based_widget()->reset_out();
}

void TimeBasedPanel::clear_in_out()
{
	get_time_based_widget()->clear_in_out_points();
}

void TimeBasedPanel::set_marker()
{
	get_time_based_widget()->set_marker();
}

void TimeBasedPanel::toggle_show_all()
{
	get_time_based_widget()->toggle_show_all();
}

void TimeBasedPanel::go_to_in()
{
	get_time_based_widget()->go_to_in();
}

void TimeBasedPanel::go_to_out()
{
	get_time_based_widget()->go_to_out();
}

void TimeBasedPanel::delete_selected()
{
	get_time_based_widget()->delete_selected();
}

void TimeBasedPanel::cut_selected()
{
	get_time_based_widget()->copy_selected(true);
}

void TimeBasedPanel::copy_selected()
{
	get_time_based_widget()->copy_selected(false);
}

void TimeBasedPanel::paste()
{
	get_time_based_widget()->paste();
}

}
