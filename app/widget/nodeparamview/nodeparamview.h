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

#ifndef OAK_NODEPARAMVIEW_H
#define OAK_NODEPARAMVIEW_H

#include <QHash>
#include <QVBoxLayout>
#include <QWidget>

#include "oakengine/serializer.h"
#include "oakutil/oaknode.h"
#include "nodeparamviewcontext.h"
#include "nodeparamviewdockarea.h"
#include "nodeparamviewitem.h"
#include "widget/keyframeview/keyframeview.h"
#include "widget/timebased/timebasedwidget.h"

namespace olive
{

class NodeParamView : public TimeBasedWidget {
	Q_OBJECT
public:
	NodeParamView(bool create_keyframe_view, QWidget *parent = nullptr);
	NodeParamView(QWidget *parent = nullptr)
		: NodeParamView(true, parent)
	{
	}

	virtual ~NodeParamView() override;

	void close_contexts_belonging_to_project(oak::Project p);

	void DeleteSelected();

	void select_all()
	{
		keyframe_view_->select_all();
	}

	void deselect_all()
	{
		keyframe_view_->deselect_all();
	}

	void set_selected_nodes(const QVector<NodeParamViewItem *> &nodes,
						  bool handle_focused_node = true,
						  bool emit_signal = true);
	void set_selected_nodes(
		const QVector<QPair<OakEngineNode *, OakEngineNode *>> &nodes,
		bool emit_signal = true);

	OakEngineNode *get_node_with_id(const QString &id);
	OakEngineNode *get_node_with_id_and_ignore_list(const QString &id,
											 const QVector<OakEngineNode *> &ignore);

	const QVector<oak::Node> &get_contexts() const
	{
		return contexts_;
	}

	virtual bool copy_selected(bool cut) override;

	virtual bool paste() override;
	static bool paste(
		QWidget *parent,
		std::function<QHash<OakEngineNode *, OakEngineNode *>(void *)>
			get_existing_map_function);

public:
	// Not a slot: mirrors NodeView::set_contexts(), which is also a plain
	// member function. All connections use new-style member-function syntax.
	void set_contexts(const QVector<oak::Node> &contexts);

public slots:
	void update_element_y();

signals:
	void focused_node_changed(OakEngineNode *n);

	void selected_nodes_changed(
		const QVector<QPair<OakEngineNode *, OakEngineNode *>> &nodes);

	void request_viewer_to_start_editing_text();

protected:
	virtual void resizeEvent(QResizeEvent *event) override;

	virtual void ScaleChangedEvent(const double &) override;
	virtual void TimebaseChangedEvent(const Rational &) override;

	virtual void ConnectedNodeChangeEvent(OakEngineNode *n) override;

	virtual const QVector<KeyframeViewInputConnection *> *
	get_snap_keyframes() const override
	{
		return keyframe_view_ ? &keyframe_view_->get_keyframe_tracks() : nullptr;
	}

	virtual const std::vector<OakEngineKeyframe *> *
	get_snap_ignore_keyframes() const override
	{
		return keyframe_view_ ? &keyframe_view_->get_selected_keyframes() :
								nullptr;
	}

	virtual const TimeTargetObject *get_keyframe_time_target() const override
	{
		return keyframe_view_;
	}

private:
	void queue_keyframe_position_update();

	void add_context(oak::Node context);

	void remove_context(oak::Node context);

	// Ordinary member functions (NOT slots): they are only invoked from
	// lambdas inside this class, never used as connect() targets.
	void node_added_to_context(OakEngineNode *n, OakEngineNode *ctx);

	void node_removed_from_context(OakEngineNode *n, OakEngineNode *ctx);

	void add_node(OakEngineNode *n, OakEngineNode *ctx, NodeParamViewContext *context);

	void sort_items_in_context(NodeParamViewContext *context);

	NodeParamViewContext *get_context_item_from_context(OakEngineNode *context);

	bool is_group_mode() const
	{
		return contexts_.size() == 1 && contexts_.first().is_group();
	}

	void toggle_select(NodeParamViewItem *item);

	QHash<OakEngineNode *, OakEngineNode *>
	generate_existing_paste_map(void *clipboard);

	KeyframeView *keyframe_view_;

	QVector<NodeParamViewContext *> context_items_;

	QScrollBar *vertical_scrollbar_;

	int last_scroll_val_;

	QScrollArea *param_scroll_area_;

	QWidget *param_widget_container_;

	NodeParamViewDockArea *param_widget_area_;

	QVector<OakEngineNode *> pinned_nodes_;

	QVector<OakEngineNode *> active_nodes_;

	NodeParamViewItem *focused_node_;
	QVector<NodeParamViewItem *> selected_nodes_;

	QVector<oak::Node> contexts_;
	QVector<oak::Node> current_contexts_;

	bool show_all_nodes_;

	// Group input passthrough subscription IDs (guarded against duplicate
	// subscriptions when update_contexts() is called repeatedly).
	int64_t group_passthrough_added_sub_ = 0;
	int64_t group_passthrough_removed_sub_ = 0;

	QHash<oak::Node, QPair<int64_t, int64_t>> context_subs_;

private slots:
	void update_global_scroll_bar();

	void pin_node(bool pin);

	//void FocusChanged(QWidget *old, QWidget *now);

	void input_check_box_changed(const oak::Input &input, bool e);

	void group_input_passthrough_added(OakEngineNode *group,
									const oak::Input &input);

	void group_input_passthrough_removed(OakEngineNode *group,
									  const oak::Input &input);

	void update_contexts();

	void item_about_to_be_removed(NodeParamViewItem *item);

	void item_clicked();

	void select_node_from_connected_link(OakEngineNode *node);

	void request_edit_text_in_viewer();

	void input_array_size_changed(const QString &input, int old_size,
							   int new_size);
};

}

#endif // OAK_NODEPARAMVIEW_H
