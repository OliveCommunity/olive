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

#ifndef OAK_NODEPARAMVIEWKEYFRAMECONTROL_H
#define OAK_NODEPARAMVIEWKEYFRAMECONTROL_H

#include <QPushButton>
#include <QWidget>
#include <cstdint>

#include "engineeventbridge.h"
#include "oakutil/oaknode.h"
#include "widget/timetarget/timetarget.h"

namespace olive
{

class NodeParamViewKeyframeControl : public QWidget, public TimeTargetObject {
	Q_OBJECT
public:
	NodeParamViewKeyframeControl(bool right_align, QWidget *parent = nullptr);
	NodeParamViewKeyframeControl(QWidget *parent = nullptr)
		: NodeParamViewKeyframeControl(true, parent)
	{
	}
	~NodeParamViewKeyframeControl() override;

	const oak::Input &get_connected_input() const
	{
		return input_;
	}

	void set_input(const oak::Input &input);

protected:
	virtual void TimeTargetDisconnectEvent(OakEngineNode *v) override;
	virtual void TimeTargetConnectEvent(OakEngineNode *v) override;

private:
	QPushButton *create_new_tool_button(const QIcon &icon) const;

	void set_buttons_enabled(bool e);

	Rational get_current_time_as_node_time() const;

	Rational convert_to_viewer_time(const Rational &r) const;

	QPushButton *prev_key_btn_;
	QPushButton *toggle_key_btn_;
	QPushButton *next_key_btn_;
	QPushButton *enable_key_btn_;

	oak::Input input_;

	EngineEventBridge *bridge_ = nullptr;

	int64_t keyframe_enable_sub_ = 0;
	int64_t keyframe_added_sub_ = 0;
	int64_t keyframe_removed_sub_ = 0;
	int64_t keyframe_time_sub_ = 0;

	QMetaObject::Connection viewer_conn_;

private slots:
	void show_buttons_from_keyframe_enable(bool e);

	void toggle_keyframe(bool e);

	void update_state();

	void go_to_previous_key();

	void go_to_next_key();

	void keyframe_enable_btn_clicked(bool e);

	void keyframe_enable_changed(const oak::Input &input, bool e);
};

}

#endif // OAK_NODEPARAMVIEWKEYFRAMECONTROL_H
