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

#ifndef OAK_NODEPARAMVIEWWIDGETBRIDGE_H
#define OAK_NODEPARAMVIEWWIDGETBRIDGE_H

#include <QObject>
#include <cstdint>

#include "engineeventbridge.h"
#include "common/nodevaluehandle.h"
#include "oakengine/node.h"
#include "oakutil/oaknode.h"
#include "widget/slider/base/numericsliderbase.h"
#include "widget/timetarget/timetarget.h"

namespace olive
{

class NodeParamViewScrollBlocker : public QObject {
	Q_OBJECT
public:
	virtual bool eventFilter(QObject *watched, QEvent *event) override;
};

class NodeParamViewWidgetBridge : public QObject, public TimeTargetObject {
	Q_OBJECT
public:
	NodeParamViewWidgetBridge(const oak::Input &input, QObject *parent);
	~NodeParamViewWidgetBridge() override;

	const QVector<QWidget *> &widgets() const
	{
		return widgets_;
	}

	// Set the timebase of certain Timebased widgets
	void set_timebase(const Rational &timebase);

signals:
	void array_widget_double_clicked();

	void widgets_recreated(const oak::Input &input);

	void request_edit_text_in_viewer();

protected:
	virtual void TimeTargetDisconnectEvent(OakEngineNode *v) override;
	virtual void TimeTargetConnectEvent(OakEngineNode *v) override;

private:
	void create_widgets();

	void set_input_value(const QVariant &value, int track);

	void set_string_value(const QString &value);

	void set_input_value_internal(const QVariant &value, int track,
							   void *command,
							   bool insert_on_all_tracks_if_no_key);

	void process_slider(NumericSliderBase *slider, int slider_track,
					   const QVariant &value);
	void process_slider(NumericSliderBase *slider, const QVariant &value)
	{
		process_slider(slider, widgets_.indexOf(slider), value);
	}

	void set_property(const QString &key, const QVariant &value);

	template <typename T> void create_sliders(int count, QWidget *parent);

	void update_widget_values();

	Rational get_current_time_as_node_time() const;

	const oak::Input &get_outer_input() const
	{
		return input_hierarchy_.first();
	}

	const oak::Input &get_inner_input() const
	{
		return input_hierarchy_.last();
	}

	QString get_command_name() const;

	NodeValueType::Type get_data_type() const
	{
		return static_cast<NodeValueType::Type>(get_outer_input().data_type());
	}

	void update_properties();

	QVector<oak::Input> input_hierarchy_;

	QVector<QWidget *> widgets_;

	OakEngineNodeDragger *dragger_ = nullptr;

	NodeParamViewScrollBlocker scroll_filter_;

	EngineEventBridge *bridge_ = nullptr;

	int64_t viewer_sub_ = 0;

private slots:
	void widget_callback();

	void input_value_changed(OakEngineNode *source, const QString &input,
							 int element, qint64 in_ts, qint64 out_ts);

	void input_data_type_changed(OakEngineNode *source,
								 const QString &input);

	void property_changed(const QString &input);
};

}

#endif // OAK_NODEPARAMVIEWWIDGETBRIDGE_H
