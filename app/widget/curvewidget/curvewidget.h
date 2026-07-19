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

#ifndef OAK_CURVEWIDGET_H
#define OAK_CURVEWIDGET_H

#include <QCheckBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>

#include "curveview.h"
#include "widget/nodeparamview/nodeparamviewkeyframecontrol.h"
#include "widget/nodeparamview/nodeparamviewwidgetbridge.h"
#include "widget/nodetreeview/nodetreeview.h"
#include "widget/timebased/timebasedwidget.h"

namespace olive
{

class CurveWidget : public TimeBasedWidget, public TimeTargetObject {
	Q_OBJECT
public:
	CurveWidget(QWidget *parent = nullptr);

	const double &get_vertical_scale();
	void set_vertical_scale(const double &vscale);

	void DeleteSelected();

	void select_all()
	{
		view_->select_all();
	}

	void deselect_all()
	{
		view_->deselect_all();
	}

	Node *get_selected_node_with_id(const QString &id);

	virtual bool copy_selected(bool cut) override;

	virtual bool paste() override;

public slots:
	void set_nodes(const QVector<Node *> &nodes);

protected:
	virtual void TimebaseChangedEvent(const Rational &) override;
	virtual void ScaleChangedEvent(const double &) override;

	virtual void TimeTargetChangedEvent(ViewerOutput *target) override;

	virtual void ConnectedNodeChangeEvent(ViewerOutput *n) override;

	virtual const QVector<KeyframeViewInputConnection *> *
	get_snap_keyframes() const override
	{
		return &view_->get_keyframe_tracks();
	}

	virtual const TimeTargetObject *get_keyframe_time_target() const override
	{
		return view_;
	}

	virtual const std::vector<NodeKeyframe *> *
	get_snap_ignore_keyframes() const override
	{
		return &view_->get_selected_keyframes();
	}

private:
	void set_keyframe_button_enabled(bool enable);

	void set_keyframe_button_checked(bool checked);

	void set_keyframe_button_checked_from_type(NodeKeyframe::Type type);

	void connect_input(Node *node, const QString &input, int element);

	void connect_input_internal(Node *node, const QString &input, int element);

	QHash<NodeKeyframeTrackReference, QColor> keyframe_colors_;

	NodeTreeView *tree_view_;

	QPushButton *linear_button_;

	QPushButton *bezier_button_;

	QPushButton *hold_button_;

	CurveView *view_;

	NodeParamViewKeyframeControl *key_control_;

	QVector<Node *> nodes_;

	QVector<NodeKeyframeTrackReference> selected_tracks_;

private slots:
	void selection_changed();

	void keyframe_type_button_triggered(bool checked);

	void input_selection_changed(const NodeKeyframeTrackReference &ref);

	void keyframe_view_dragged(int x, int y);
	void keyframe_view_released();
};

}

#endif // OAK_CURVEWIDGET_H
