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

#ifndef OAK_NODEPARAMVIEWITEM_H
#define OAK_NODEPARAMVIEWITEM_H

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "node/node.h"
#include "nodeparamviewarraywidget.h"
#include "nodeparamviewconnectedlabel.h"
#include "nodeparamviewkeyframecontrol.h"
#include "nodeparamviewitembase.h"
#include "nodeparamviewwidgetbridge.h"
#include "widget/clickablelabel/clickablelabel.h"
#include "widget/collapsebutton/collapsebutton.h"
#include "widget/keyframeview/keyframeview.h"

namespace olive
{

enum NodeParamViewCheckBoxBehavior {
	k_no_check_boxes,
	k_check_boxes_on,
	k_check_boxes_on_non_connected
};

class NodeParamViewItemBody : public QWidget {
	Q_OBJECT
public:
	NodeParamViewItemBody(Node *node,
						  NodeParamViewCheckBoxBehavior create_checkboxes,
						  QWidget *parent = nullptr);

	void set_time_target(ViewerOutput *target);

	void retranslate();

	int get_element_y(NodeInput c) const;

	// Set the timebase of any timebased widgets contained here
	void set_timebase(const Rational &timebase);

	void set_input_checked(const NodeInput &input, bool e);

signals:
	void request_select_node(Node *node);

	void array_expanded_changed(bool e);

	void input_checked_changed(const NodeInput &input, bool e);

	void request_edit_text_in_viewer();

private:
	void create_widgets(QGridLayout *layout, Node *node, const QString &input,
					   int element, int row_index);

	void update_ui_for_edge_connection(const NodeInput &input);

	void place_widgets_from_bridge(QGridLayout *layout,
								NodeParamViewWidgetBridge *bridge, int row);

	void input_array_size_changed_internal(Node *node, const QString &input,
									   int size);

	struct InputUI {
		InputUI();

		QLabel *main_label;
		NodeParamViewWidgetBridge *widget_bridge;
		NodeParamViewConnectedLabel *connected_label;
		NodeParamViewKeyframeControl *key_control;
		QGridLayout *layout;
		int row;
		QPushButton *extra_btn;
		QCheckBox *optional_checkbox;

		NodeParamViewArrayButton *array_insert_btn;
		NodeParamViewArrayButton *array_remove_btn;
	};

	QHash<NodeInput, InputUI> input_ui_map_;

	struct ArrayUI {
		QWidget *widget;
		int count;
		NodeParamViewArrayButton *append_btn;
	};

	void set_time_target_on_input_ui(const InputUI &ui);
	void set_timebase_on_input_ui(const InputUI &ui);

	Node *node_;

	QHash<NodeInputPair, ArrayUI> array_ui_;

	QHash<NodeInputPair, CollapseButton *> array_collapse_buttons_;

	Rational timebase_;

	ViewerOutput *time_target_;

	NodeParamViewCheckBoxBehavior create_checkboxes_;

	QHash<NodeInputPair, NodeInputPair> input_group_lookup_;

	/**
   * @brief The column to place the keyframe controls in
   *
   * Serves as an effective "maximum column" index because the keyframe button is always aligned
   * to the right edge.
   */
	static const int k_key_control_column;

	static const int k_array_insert_column;
	static const int k_array_remove_column;
	static const int k_extra_button_column;

	static const int k_optional_check_box;
	static const int k_array_collapse_btn_column;
	static const int k_label_column;
	static const int k_widget_start_column;
	static const int k_max_widget_column;

private slots:
	void edge_changed(Node *output, const NodeInput &input);

	void array_collapse_btn_pressed(bool checked);

	void input_array_size_changed(const QString &input, int old_sz, int size);

	void array_append_clicked();

	void array_insert_clicked();

	void array_remove_clicked();

	void toggle_array_expanded();

	void replace_widgets(const NodeInput &input);

	void show_speed_duration_dialog_for_node();

	void optional_check_box_clicked(bool e);
};

class NodeParamViewItem : public NodeParamViewItemBase {
	Q_OBJECT
public:
	NodeParamViewItem(Node *node,
					  NodeParamViewCheckBoxBehavior create_checkboxes,
					  QWidget *parent = nullptr);

	void set_time_target(ViewerOutput *target)
	{
		time_target_ = target;

		body_->set_time_target(target);
	}

	void set_timebase(const Rational &timebase)
	{
		timebase_ = timebase;

		body_->set_timebase(timebase);
	}

	Node *get_context() const
	{
		return ctx_;
	}

	void set_context(Node *ctx)
	{
		ctx_ = ctx;
	}

	Node *get_node() const
	{
		return node_;
	}

	int get_element_y(const NodeInput &c) const;

	void set_input_checked(const NodeInput &input, bool e);

	KeyframeView::NodeConnections &get_keyframe_connections()
	{
		return keyframe_connections_;
	}

	void set_keyframe_connections(const KeyframeView::NodeConnections &c)
	{
		keyframe_connections_ = c;
	}

signals:
	void request_select_node(Node *node);

	void array_expanded_changed(bool e);

	void input_checked_changed(const NodeInput &input, bool e);

	void request_edit_text_in_viewer();

	void input_array_size_changed(const QString &input, int old_size,
							   int new_size);

protected slots:
	virtual void retranslate() override;

private:
	NodeParamViewItemBody *body_;
	QLabel *message_label_;
	QPushButton *message_clear_button_;
	QWidget *message_container_;

	Node *node_;

	NodeParamViewCheckBoxBehavior create_checkboxes_;

	Node *ctx_;

	ViewerOutput *time_target_;

	Rational timebase_;

	KeyframeView::NodeConnections keyframe_connections_;

private slots:
	void recreate_body();
	void update_message_panel();
	void clear_messages();
};

}

#endif // OAK_NODEPARAMVIEWITEM_H
