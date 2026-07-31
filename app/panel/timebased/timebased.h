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

#ifndef OAK_TIMEBASEDPANEL_H
#define OAK_TIMEBASEDPANEL_H

#include "panel/panel.h"
#include "widget/timebased/timebasedwidget.h"
#include "engineeventbridge.h"

namespace olive
{

class TimeBasedPanel : public PanelWidget {
	Q_OBJECT
public:
	TimeBasedPanel(const QString &object_name);

	virtual ~TimeBasedPanel() override;

	void connect_viewer_node(OakEngineNode *node);

	void disconnect_viewer_node()
	{
		connect_viewer_node(nullptr);
	}

	// Get the timebase of this panels widget
	const Rational &timebase();

	OakEngineNode *get_connected_viewer() const
	{
		return widget_->get_connected_node();
	}

	TimeRuler *ruler() const
	{
		return widget_->ruler();
	}

	virtual void zoom_in() override;

	virtual void zoom_out() override;

	virtual void go_to_start() override;

	virtual void prev_frame() override;

	virtual void next_frame() override;

	virtual void go_to_end() override;

	virtual void go_to_prev_cut() override;

	virtual void go_to_next_cut() override;

	virtual void play_pause() override;

	virtual void play_in_to_out() override;

	virtual void shuttle_left() override;

	virtual void shuttle_stop() override;

	virtual void shuttle_right() override;

	virtual void set_in() override;

	virtual void set_out() override;

	virtual void reset_in() override;

	virtual void reset_out() override;

	virtual void clear_in_out() override;

	virtual void set_marker() override;

	virtual void toggle_show_all() override;

	virtual void go_to_in() override;

	virtual void go_to_out() override;

	virtual void delete_selected() override;

	virtual void cut_selected() override;

	virtual void copy_selected() override;

	virtual void paste() override;

	TimeBasedWidget *get_time_based_widget() const
	{
		return widget_;
	}

public slots:
	void set_timebase(const Rational &timebase);

signals:
	void play_pause_requested();

	void play_in_to_out_requested();

	void shuttle_left_requested();

	void shuttle_stop_requested();

	void shuttle_right_requested();

protected:
	void set_time_based_widget(TimeBasedWidget *widget);

	virtual void retranslate() override;

	void set_show_and_raise_on_connect()
	{
		show_and_raise_on_connect_ = true;
	}

private:
	TimeBasedWidget *widget_;

	bool show_and_raise_on_connect_;

	EngineEventBridge *bridge_ = nullptr;
	int64_t label_sub_ = 0;

private slots:
	void connected_node_changed(OakEngineNode *old, OakEngineNode *now);
};

}

#endif // OAK_TIMEBASEDPANEL_H
