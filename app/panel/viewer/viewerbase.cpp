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

#include "viewerbase.h"

#include "window/mainwindow/mainwindow.h"

namespace olive
{

#define super TimeBasedPanel

ViewerPanelBase::ViewerPanelBase(const QString &object_name)
	: super(object_name)
{
	connect(PanelManager::instance(), &PanelManager::focused_panel_changed, this,
			&ViewerPanelBase::focused_panel_changed);
}

void ViewerPanelBase::play_pause()
{
	get_viewer_widget()->toggle_play_pause();
}

void ViewerPanelBase::play_in_to_out()
{
	get_viewer_widget()->play(true);
}

void ViewerPanelBase::shuttle_left()
{
	get_viewer_widget()->shuttle_left();
}

void ViewerPanelBase::shuttle_stop()
{
	get_viewer_widget()->shuttle_stop();
}

void ViewerPanelBase::shuttle_right()
{
	get_viewer_widget()->shuttle_right();
}

void ViewerPanelBase::connect_time_based_panel(TimeBasedPanel *panel)
{
	connect(panel, &TimeBasedPanel::play_pause_requested, this,
			&ViewerPanelBase::play_pause);
	connect(panel, &TimeBasedPanel::play_in_to_out_requested, this,
			&ViewerPanelBase::play_in_to_out);
	connect(panel, &TimeBasedPanel::shuttle_left_requested, this,
			&ViewerPanelBase::shuttle_left);
	connect(panel, &TimeBasedPanel::shuttle_stop_requested, this,
			&ViewerPanelBase::shuttle_stop);
	connect(panel, &TimeBasedPanel::shuttle_right_requested, this,
			&ViewerPanelBase::shuttle_right);
}

void ViewerPanelBase::disconnect_time_based_panel(TimeBasedPanel *panel)
{
	disconnect(panel, &TimeBasedPanel::play_pause_requested, this,
			   &ViewerPanelBase::play_pause);
	disconnect(panel, &TimeBasedPanel::play_in_to_out_requested, this,
			   &ViewerPanelBase::play_in_to_out);
	disconnect(panel, &TimeBasedPanel::shuttle_left_requested, this,
			   &ViewerPanelBase::shuttle_left);
	disconnect(panel, &TimeBasedPanel::shuttle_stop_requested, this,
			   &ViewerPanelBase::shuttle_stop);
	disconnect(panel, &TimeBasedPanel::shuttle_right_requested, this,
			   &ViewerPanelBase::shuttle_right);
}

void ViewerPanelBase::set_full_screen(QScreen *screen)
{
	get_viewer_widget()->set_full_screen(screen);
}

void ViewerPanelBase::set_gizmos(OakEngineNode *node)
{
	get_viewer_widget()->set_gizmos(node);
}

void ViewerPanelBase::cache_entire_sequence()
{
	get_viewer_widget()->cache_entire_sequence();
}

void ViewerPanelBase::cache_sequence_in_out()
{
	get_viewer_widget()->cache_sequence_in_out();
}

void ViewerPanelBase::set_viewer_widget(ViewerWidget *vw)
{
	connect(vw, &ViewerWidget::texture_changed, this,
			&ViewerPanelBase::texture_changed);
	connect(vw, &ViewerWidget::color_processor_changed, this,
			&ViewerPanelBase::color_processor_changed);
	connect(vw, &ViewerWidget::color_manager_changed, this,
			&ViewerPanelBase::color_manager_changed);

	set_time_based_widget(vw);
}

void ViewerPanelBase::focused_panel_changed(PanelWidget *panel)
{
	if (dynamic_cast<ViewerPanelBase *>(panel)) {
		auto vw = get_viewer_widget();
		if (vw->is_playing() && panel != this) {
			vw->pause();
		}
	}
}

}
