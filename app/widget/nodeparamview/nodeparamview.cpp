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

#include "nodeparamview.h"

#include <QApplication>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>

#include "node/nodeundo.h"
#include "node/output/viewer/viewer.h"
#include "widget/timeruler/timeruler.h"

namespace olive
{

#define super TimeBasedWidget

NodeParamView::NodeParamView(bool create_keyframe_view, QWidget *parent)
	: super(true, false, parent)
	, last_scroll_val_(0)
	, focused_node_(nullptr)
	, show_all_nodes_(false)
{
	// Create horizontal layout to place scroll area in (and keyframe editing eventually)
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

	QSplitter *splitter = new QSplitter(Qt::Horizontal);
	layout->addWidget(splitter);

	// Set up scroll area for params
	param_scroll_area_ = new QScrollArea();
	param_scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	param_scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	param_scroll_area_->setWidgetResizable(true);
	splitter->addWidget(param_scroll_area_);

	// Param widget
	param_widget_container_ = new QWidget();
	param_scroll_area_->setWidget(param_widget_container_);

	param_widget_area_ = new NodeParamViewDockArea();

	QVBoxLayout *param_widget_container_layout =
		new QVBoxLayout(param_widget_container_);
	QMargins param_widget_margin =
		param_widget_container_layout->contentsMargins();
	param_widget_margin.setTop(ruler()->height());
	param_widget_container_layout->setContentsMargins(param_widget_margin);
	param_widget_container_layout->setSpacing(0);
	param_widget_container_layout->addWidget(param_widget_area_);

	param_widget_container_layout->addStretch(INT_MAX);

	// Create contexts for three different types
	context_items_.resize(Track::k_count + 1);
	for (int i = 0; i < context_items_.size(); i++) {
		NodeParamViewContext *c = new NodeParamViewContext(param_widget_area_);
		c->setVisible(false);
		connect(c, &NodeParamViewContext::about_to_delete_item, this,
				&NodeParamView::item_about_to_be_removed, Qt::DirectConnection);

		NodeParamViewItemTitleBar *title_bar =
			static_cast<NodeParamViewItemTitleBar *>(c->titleBarWidget());

		if (i == Track::k_video || i == Track::k_audio) {
			c->set_effect_type(static_cast<Track::Type>(i));
			title_bar->set_add_effect_button_visible(true);
			title_bar->set_text(tr("%1 Nodes")
								   .arg(Footage::get_stream_type_name(
									   static_cast<Track::Type>(i))));
		} else {
			title_bar->set_text(tr("Other"));
		}

		context_items_[i] = c;
		param_widget_area_->add_item(c);
	}

	// Disable collapsing param view (but collapsing keyframe view is permitted)
	splitter->setCollapsible(0, false);

	// Create global vertical scrollbar on the right
	vertical_scrollbar_ = new QScrollBar();
	vertical_scrollbar_->setMaximum(0);
	layout->addWidget(vertical_scrollbar_);

	// Connect scrollbars together
	connect(param_scroll_area_->verticalScrollBar(), &QScrollBar::valueChanged,
			vertical_scrollbar_, &QScrollBar::setValue);
	connect(param_scroll_area_->verticalScrollBar(), &QScrollBar::rangeChanged,
			vertical_scrollbar_, &QScrollBar::setRange);
	connect(param_scroll_area_->verticalScrollBar(), &QScrollBar::rangeChanged,
			this, &NodeParamView::update_global_scroll_bar);
	connect(vertical_scrollbar_, &QScrollBar::valueChanged,
			param_scroll_area_->verticalScrollBar(), &QScrollBar::setValue);

	if (create_keyframe_view) {
		// Set up keyframe view
		QWidget *keyframe_area = new QWidget();
		QVBoxLayout *keyframe_area_layout = new QVBoxLayout(keyframe_area);
		keyframe_area_layout->setSpacing(0);
		keyframe_area_layout->setContentsMargins(0, 0, 0, 0);

		// Create ruler object
		keyframe_area_layout->addWidget(ruler());

		// Create keyframe view
		keyframe_view_ = new KeyframeView();
		keyframe_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		keyframe_view_->set_snap_service(this);
		connect_timeline_view(keyframe_view_);
		keyframe_area_layout->addWidget(keyframe_view_);

		// Connect ruler and keyframe view together
		connect(keyframe_view_, &KeyframeView::dragged, this,
				static_cast<void (NodeParamView::*)(int)>(
					&NodeParamView::set_catch_up_scroll_value));
		connect(keyframe_view_, &KeyframeView::released, this,
				static_cast<void (NodeParamView::*)()>(
					&NodeParamView::stop_catch_up_scroll_timer));

		splitter->addWidget(keyframe_area);

		// Set both widgets to 50/50
		splitter->setSizes({ INT_MAX, INT_MAX });

		connect(keyframe_view_->verticalScrollBar(), &QScrollBar::valueChanged,
				vertical_scrollbar_, &QScrollBar::setValue);
		connect(keyframe_view_->verticalScrollBar(), &QScrollBar::valueChanged,
				param_scroll_area_->verticalScrollBar(), &QScrollBar::setValue);
		connect(param_scroll_area_->verticalScrollBar(),
				&QScrollBar::valueChanged, keyframe_view_->verticalScrollBar(),
				&QScrollBar::setValue);
		connect(vertical_scrollbar_, &QScrollBar::valueChanged,
				keyframe_view_->verticalScrollBar(), &QScrollBar::setValue);

		// TimeBasedWidget's scrollbar has extra functionality that we can take advantage of
		keyframe_view_->setHorizontalScrollBar(scrollbar());
		keyframe_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	} else {
		keyframe_view_ = nullptr;
	}

	// Set a default scale - FIXME: Hardcoded
	SetScale(120);

	// Pickup on widget focus changes
	// DISABLED - we now just handle this with item/titlebar clicking (see ToggleSelect)
	/*connect(qApp,
          &QApplication::focusChanged,
          this,
          &NodeParamView::FocusChanged);*/
}

NodeParamView::~NodeParamView()
{
	qDeleteAll(context_items_);
}

void NodeParamView::close_contexts_belonging_to_project(Project *p)
{
	QVector<Node *> new_contexts = contexts_;

	for (int i = 0; i < new_contexts.size(); i++) {
		if (new_contexts.at(i)->project() == p) {
			new_contexts.removeAt(i);
			i--;
		}
	}

	set_contexts(new_contexts);
}

/*void NodeParamView::SelectNodes(const QVector<Node *> &nodes)
{
  return;
  int original_node_count = items_.size();

  foreach (Node* n, nodes) {
    // If we've already added this node (either a duplicate or a pinned node), don't add another
    if (items_.contains(n)) {
      continue;
    }

    // Add to "active" list to represent currently selected node
    active_nodes_.append(n);

    // Create node UI
    AddNode(n, param_widget_area_);
  }

  if (items_.size() > original_node_count ) {
    UpdateItemTime(GetTime());

    // Re-arrange keyframes
    QueueKeyframePositionUpdate();

    SignalNodeOrder();
  }
}

void NodeParamView::DeselectNodes(const QVector<Node *> &nodes)
{
  return;
  // Remove item from map and delete the widget
  int original_node_count = items_.size();

  foreach (Node* n, nodes) {
    // Filter out duplicates
    if (!items_.contains(n)) {
      continue;
    }

    if (!pinned_nodes_.contains(n)) {
      // Store expanded state
      node_expanded_state_.insert(n, items_.value(n)->IsExpanded());

      // Remove all keyframes from this node
      RemoveNode(n);
    }

    active_nodes_.removeOne(n);
  }

  if (items_.size() < original_node_count) {
    // Re-arrange keyframes
    QueueKeyframePositionUpdate();

    SignalNodeOrder();
  }
}*/

void NodeParamView::update_contexts()
{
	bool changes_made = false;

	foreach (Node *ctx, current_contexts_) {
		if (!contexts_.contains(ctx)) {
			// Context is being removed
			remove_context(ctx);
			changes_made = true;
		}
	}

	foreach (Node *ctx, contexts_) {
		if (!current_contexts_.contains(ctx)) {
			// Context is being added
			add_context(ctx);
			changes_made = true;
		}
	}

	if (changes_made) {
		current_contexts_ = contexts_;

		if (is_group_mode()) {
			// Check inputs that have been passed through
			NodeGroup *group = static_cast<NodeGroup *>(contexts_.first());
			for (auto it = group->get_input_passthroughs().cbegin();
				 it != group->get_input_passthroughs().cend(); it++) {
				group_input_passthrough_added(group, it->second);
			}

			connect(group, &NodeGroup::input_passthrough_added, this,
					&NodeParamView::group_input_passthrough_added);
			connect(group, &NodeGroup::input_passthrough_removed, this,
					&NodeParamView::group_input_passthrough_removed);
		}

		foreach (NodeParamViewContext *ctx, context_items_) {
			sort_items_in_context(ctx);
		}

		if (keyframe_view_) {
			queue_keyframe_position_update();
		}
	}
}

void NodeParamView::item_about_to_be_removed(NodeParamViewItem *item)
{
	if (keyframe_view_) {
		for (auto it = item->get_keyframe_connections().begin();
			 it != item->get_keyframe_connections().end(); it++) {
			for (auto jt = it->begin(); jt != it->end(); jt++) {
				for (auto kt = jt->begin(); kt != jt->end(); kt++) {
					keyframe_view_->remove_keyframes_of_track(*kt);
				}
			}
		}
	}

	QVector<NodeParamViewItem *> copy = selected_nodes_;
	if (copy.removeOne(item)) {
		set_selected_nodes(copy);
	}
}

void NodeParamView::item_clicked()
{
	toggle_select(static_cast<NodeParamViewItem *>(sender()));
}

void NodeParamView::select_node_from_connected_link(Node *node)
{
	NodeParamViewItem *item = static_cast<NodeParamViewItem *>(sender());

	Node::ContextPair p = { node, item->get_context() };
	set_selected_nodes({ p });
}

void NodeParamView::request_edit_text_in_viewer()
{
	NodeParamViewItem *item = static_cast<NodeParamViewItem *>(sender());

	set_selected_nodes({ item });
	emit request_viewer_to_start_editing_text();
}

void NodeParamView::set_contexts(const QVector<Node *> &contexts)
{
	// Setting contexts is expensive, so we queue it here to prevent multiple calls in a short timespan
	contexts_ = contexts;
	update_contexts();
}

void NodeParamView::resizeEvent(QResizeEvent *event)
{
	super::resizeEvent(event);

	vertical_scrollbar_->setPageStep(vertical_scrollbar_->height());
}

void NodeParamView::ScaleChangedEvent(const double &scale)
{
	super::ScaleChangedEvent(scale);

	if (keyframe_view_) {
		keyframe_view_->set_scale(scale);
	}
}

void NodeParamView::TimebaseChangedEvent(const Rational &timebase)
{
	super::TimebaseChangedEvent(timebase);

	if (keyframe_view_) {
		keyframe_view_->set_timebase(timebase);
	}

	foreach (NodeParamViewContext *ctx, context_items_) {
		ctx->set_timebase(timebase);
	}
}

void NodeParamView::ConnectedNodeChangeEvent(ViewerOutput *n)
{
	if (keyframe_view_) {
		// Set viewer as a time target
		keyframe_view_->set_time_target(n);
	}

	foreach (NodeParamViewContext *item, context_items_) {
		item->set_time_target(n);
	}
}

void reconnect_outputs_if_not_deleting_node(MultiUndoCommand *c,
									   NodeViewDeleteCommand *dc, Node *output,
									   Node *deleting, Node *context)
{
	for (auto it = deleting->output_connections().cbegin();
		 it != deleting->output_connections().cend(); it++) {
		const NodeInput &proposed_reconnect = it->second;

		if (dc->contains_node(proposed_reconnect.node(), context)) {
			// Uh-oh we're deleting this node too, instead connect to its outputs
			reconnect_outputs_if_not_deleting_node(
				c, dc, output, proposed_reconnect.node(), context);
		} else {
			c->add_child(new NodeEdgeAddCommand(output, it->second));
		}
	}
}

void NodeParamView::DeleteSelected()
{
	if (keyframe_view_ && keyframe_view_->hasFocus()) {
		keyframe_view_->delete_selected();
	} else if (!selected_nodes_.isEmpty()) {
		MultiUndoCommand *c = new MultiUndoCommand();

		// Create command to delete node from context and/or graph
		NodeViewDeleteCommand *dc = new NodeViewDeleteCommand();
		c->add_child(dc);

		// Add all nodes
		foreach (NodeParamViewItem *item, selected_nodes_) {
			Node *n = item->get_node();
			dc->add_node(n, item->get_context());
		}

		// Make reconnections where possible
		foreach (NodeParamViewItem *item, selected_nodes_) {
			Node *n = item->get_node();

			Node *node_being_deleted = n;
			Node *connected_to_effect_input = nullptr;

			while (true) {
				if (node_being_deleted->get_effect_input().is_valid()) {
					if ((connected_to_effect_input =
							 node_being_deleted->get_effect_input()
								 .get_connected_output())) {
						if (dc->contains_node(connected_to_effect_input,
											 item->get_context())) {
							// Node's getting deleted, recurse
							node_being_deleted = connected_to_effect_input;
							continue;
						}
					}
				}

				break;
			}

			if (connected_to_effect_input) {
				reconnect_outputs_if_not_deleting_node(
					c, dc, connected_to_effect_input, n, item->get_context());
			}
		}

		Core::instance()->undo_stack()->push(
			c, tr("Deleted %1 Node(s)").arg(selected_nodes_.size()));
	}
}

void NodeParamView::set_selected_nodes(const QVector<NodeParamViewItem *> &nodes,
									 bool handle_focused_node, bool emit_signal)
{
	if (handle_focused_node) {
		handle_focused_node = !focused_node_ ||
							  selected_nodes_.contains(focused_node_);
	}

	foreach (NodeParamViewItem *n, selected_nodes_) {
		n->set_highlighted(false);
	}

	selected_nodes_ = nodes;

	QVector<Node::ContextPair> p;
	if (emit_signal) {
		p.resize(selected_nodes_.size());
	}

	for (int i = 0; i < selected_nodes_.size(); i++) {
		NodeParamViewItem *n = selected_nodes_.at(i);
		n->set_highlighted(true);

		if (emit_signal) {
			p[i] = { n->get_node(), n->get_context() };
		}
	}

	if (handle_focused_node) {
		focused_node_ = nullptr;

		foreach (NodeParamViewItem *n, selected_nodes_) {
			if (n->get_node()->has_gizmos()) {
				focused_node_ = n;
				break;
			}
		}

		Node *n = focused_node_ ? focused_node_->get_node() : nullptr;
		emit focused_node_changed(n);
	}

	if (emit_signal) {
		emit selected_nodes_changed(p);
	}
}

void NodeParamView::set_selected_nodes(const QVector<Node::ContextPair> &nodes,
									 bool emit_signal)
{
	QVector<NodeParamViewItem *> items;
	NodeParamViewContext *scrolled_ctx = nullptr;

	foreach (const Node::ContextPair &n, nodes) {
		for (auto it = context_items_.cbegin(); it != context_items_.cend();
			 it++) {
			NodeParamViewContext *ctx = *it;

			NodeParamViewItem *item = ctx->get_item(n.node, n.context);

			if (item) {
				items.append(item);
				if (!scrolled_ctx) {
					scrolled_ctx = ctx;
				}
			}
		}
	}

	set_selected_nodes(items, true, emit_signal);

	if (!selected_nodes_.empty()) {
		NodeParamViewItem *scrolled_to = selected_nodes_.front();
		param_scroll_area_->ensureWidgetVisible(scrolled_to, 0, 0);

		QPoint viewport_pos = scrolled_to->mapTo(
			param_scroll_area_, scrolled_to->geometry().topLeft());

		param_scroll_area_->verticalScrollBar()->setValue(viewport_pos.y());

		// Make sure the dock/tab containing this node is visible
		if (scrolled_ctx) {
			scrolled_ctx->set_expanded(true);
			scrolled_ctx->raise();
		}
	}
}

Node *NodeParamView::get_node_with_id(const QString &id)
{
	return get_node_with_id_and_ignore_list(id, QVector<Node *>());
}

Node *NodeParamView::get_node_with_id_and_ignore_list(const QString &id,
												const QVector<Node *> &ignore)
{
	for (NodeParamViewItem *item : selected_nodes_) {
		if (item->get_node()->id() == id && !ignore.contains(item->get_node())) {
			return item->get_node();
		}
	}

	for (NodeParamViewContext *ctx : context_items_) {
		for (NodeParamViewItem *item : ctx->get_items()) {
			if (item->get_node()->id() == id &&
				!ignore.contains(item->get_node())) {
				return item->get_node();
			}
		}
	}

	return nullptr;
}

bool NodeParamView::copy_selected(bool cut)
{
	if (super::copy_selected(cut)) {
		return true;
	}

	if (keyframe_view_ && keyframe_view_->hasFocus()) {
		if (keyframe_view_->copy_selected(cut)) {
			return true;
		}
	}

	if (contexts_.empty()) {
		return false;
	}

	ProjectSerializer::SaveData sdata(ProjectSerializer::k_only_nodes);
	ProjectSerializer::SerializedProperties properties;
	QVector<Node *> nodes;

	for (NodeParamViewItem *item : selected_nodes_) {
		Node *n = item->get_node();

		if (!nodes.contains(n)) {
			nodes.append(n);

			Node::Position pos =
				item->get_context()->get_node_position_data_in_context(n);

			properties[n][QStringLiteral("x")] =
				QString::number(pos.position.x());
			properties[n][QStringLiteral("y")] =
				QString::number(pos.position.y());
			properties[n][QStringLiteral("expanded")] =
				QString::number(pos.expanded);
		}
	}

	sdata.set_only_serialize_nodes_and_resolve_groups(nodes);
	sdata.set_properties(properties);

	ProjectSerializer::copy(sdata);

	if (cut) {
		DeleteSelected();
	}

	return false;
}

bool NodeParamView::paste()
{
	if (keyframe_view_) {
		if (keyframe_view_->paste(std::bind(&NodeParamView::get_node_with_id, this,
											std::placeholders::_1))) {
			return true;
		}
	}

	return paste(this, std::bind(&NodeParamView::generate_existing_paste_map, this,
								 std::placeholders::_1));
}

bool NodeParamView::paste(
	QWidget *parent,
	std::function<QHash<Node *, Node *>(const ProjectSerializer::Result &)>
		get_existing_map_function)
{
	ProjectSerializer::Result res =
		ProjectSerializer::paste(ProjectSerializer::k_only_nodes);
	if (res.get_load_data().nodes.isEmpty()) {
		return false;
	}

	// Determine if any nodes of this type are already in the editor
	QHash<Node *, Node *> existing_nodes = get_existing_map_function(res);

	QVector<Node *> nodes_to_paste_as_new = res.get_load_data().nodes;
	MultiUndoCommand *command = new MultiUndoCommand();

	if (!existing_nodes.empty()) {
		QMessageBox b(parent);
		b.setWindowTitle(tr("Paste Nodes"));

		QStringList node_names;
		for (auto it = existing_nodes.cbegin(); it != existing_nodes.cend();
			 it++) {
			node_names.append(it.key()->get_label_and_name());
		}

		b.setText(
			tr("The following node types already exist in this context:\n\n"
			   "%1\n\n"
			   "Do you wish to paste values onto the existing nodes or paste new nodes?")
				.arg(node_names.join('\n')));

		auto as_vals = b.addButton(tr("Paste As Values"), QMessageBox::YesRole);
		auto as_nodes = b.addButton(tr("Paste As Nodes"), QMessageBox::NoRole);
		auto cancel_btn = b.addButton(QMessageBox::Cancel);

		Q_UNUSED(as_nodes)

		b.exec();

		if (b.clickedButton() == cancel_btn) {
			// Delete pasted nodes and clear array so no later code runs
			qDeleteAll(nodes_to_paste_as_new);
			nodes_to_paste_as_new.clear();

		} else if (b.clickedButton() == as_vals) {
			// Filter out existing nodes
			for (auto it = existing_nodes.cbegin(); it != existing_nodes.cend();
				 it++) {
				Node::copy_inputs(it.value(), it.key(), false, command);
				nodes_to_paste_as_new.removeOne(it.value());
			}
		}
	}

	if (!nodes_to_paste_as_new.isEmpty()) {
		Node::PositionMap map;

		for (auto it = res.get_load_data().properties.cbegin();
			 it != res.get_load_data().properties.cend(); it++) {
			if (nodes_to_paste_as_new.contains(it.key())) {
				Node::Position pos;

				const QMap<QString, QString> &node_props = it.value();
				pos.position.setX(
					node_props.value(QStringLiteral("x")).toDouble());
				pos.position.setY(
					node_props.value(QStringLiteral("y")).toDouble());
				pos.expanded =
					node_props.value(QStringLiteral("expanded")).toDouble();

				map.insert(it.key(), pos);
			}
		}
	}

	Core::instance()->undo_stack()->push(
		command, tr("Pasted %1 Node(s)").arg(nodes_to_paste_as_new.size()));

	return true;
}

void NodeParamView::queue_keyframe_position_update()
{
	QMetaObject::invokeMethod(this, &NodeParamView::update_element_y,
							  Qt::QueuedConnection);
}

void NodeParamView::add_context(Node *ctx)
{
	NodeParamViewContext *item = get_context_item_from_context(ctx);

	// TEMP: Creating many NPV items is EXTREMELY slow so limit to one item per context for now.
	//       I have a better solution in the works to use one UI for several nodes, but I haven't
	//       done it yet, and this can severely affect productivity.
	if (item->get_contexts().size() == 1) {
		return;
	}

	// Queued so that if any further work is done in connecting this node to the context, it'll be
	// done before our sorting function is called
	connect(ctx, &Node::node_added_to_context, this,
			&NodeParamView::node_added_to_context, Qt::QueuedConnection);
	connect(ctx, &Node::node_removed_from_context, this,
			&NodeParamView::node_removed_from_context, Qt::QueuedConnection);

	item->add_context(ctx);
	item->setVisible(true);

	for (auto it = ctx->get_context_positions().cbegin();
		 it != ctx->get_context_positions().cend(); it++) {
		add_node(it.key(), ctx, item);
	}
}

void NodeParamView::remove_context(Node *ctx)
{
	disconnect(ctx, &Node::node_added_to_context, this,
			   &NodeParamView::node_added_to_context);
	disconnect(ctx, &Node::node_removed_from_context, this,
			   &NodeParamView::node_removed_from_context);

	foreach (NodeParamViewContext *item, context_items_) {
		item->remove_context(ctx);
		item->remove_nodes_with_context(ctx);

		if (item->get_contexts().isEmpty()) {
			item->setVisible(false);
		}
	}
}

void NodeParamView::add_node(Node *n, Node *ctx, NodeParamViewContext *context)
{
	if ((n->get_flags() & Node::k_dont_show_in_param_view) && !is_group_mode() &&
		!show_all_nodes_) {
		return;
	}

	NodeParamViewItem *item = new NodeParamViewItem(
		n, is_group_mode() ? k_check_boxes_on_non_connected : k_no_check_boxes,
		context->get_dock_area());

	connect(item, &NodeParamViewItem::request_select_node, this,
			&NodeParamView::select_node_from_connected_link);
	connect(item, &NodeParamViewItem::pin_toggled, this,
			&NodeParamView::pin_node);
	connect(item, &NodeParamViewItem::input_checked_changed, this,
			&NodeParamView::input_check_box_changed);
	connect(item, &NodeParamViewItem::clicked, this,
			&NodeParamView::item_clicked);
	connect(item, &NodeParamViewItem::request_edit_text_in_viewer, this,
			&NodeParamView::request_edit_text_in_viewer);

	item->set_context(ctx);
	item->set_time_target(get_connected_node());
	item->set_timebase(timebase());

	context->add_node(item);

	if (!focused_node_ && n->has_gizmos()) {
		// We'll focus this node now
		set_selected_nodes({ item });
	}

	if (keyframe_view_) {
		connect(item, &NodeParamViewItem::dockLocationChanged, this,
				&NodeParamView::queue_keyframe_position_update);
		connect(item, &NodeParamViewItem::array_expanded_changed, this,
				&NodeParamView::queue_keyframe_position_update);
		connect(item, &NodeParamViewItem::expanded_changed, this,
				&NodeParamView::queue_keyframe_position_update);
		connect(item, &NodeParamViewItem::moved, this,
				&NodeParamView::queue_keyframe_position_update);
		connect(item, &NodeParamViewItem::input_array_size_changed, this,
				&NodeParamView::input_array_size_changed);

		item->set_keyframe_connections(keyframe_view_->add_keyframes_of_node(n));
	}
}

int get_distance_between_nodes(Node *start, Node *end)
{
	if (start == end) {
		return 0;
	}

	for (auto it = start->input_connections().cbegin();
		 it != start->input_connections().cend(); it++) {
		int this_node_dist = get_distance_between_nodes(it->second, end);
		if (this_node_dist != -1) {
			return 1 + this_node_dist;
		}
	}

	return -1;
}

void NodeParamView::sort_items_in_context(NodeParamViewContext *context_item)
{
	QVector<QPair<NodeParamViewItem *, int>> distances;

	for (auto it = context_item->get_items().cbegin();
		 it != context_item->get_items().cend(); it++) {
		NodeParamViewItem *item = *it;

		int distance = -1;
		foreach (Node *ctx, context_item->get_contexts()) {
			distance =
				qMax(distance, get_distance_between_nodes(ctx, item->get_node()));
		}

		if (distance == -1) {
			distance = INT_MAX;
		}

		bool inserted = false;
		QPair<NodeParamViewItem *, int> dist(item, distance);

		for (int i = 0; i < distances.size(); i++) {
			if (distances.at(i).second < distance) {
				distances.insert(i, dist);
				inserted = true;
				break;
			}
		}

		if (!inserted) {
			distances.append(dist);
		}
	}

	foreach (auto info, distances) {
		context_item->get_dock_area()->add_item(info.first);
	}
}

NodeParamViewContext *NodeParamView::get_context_item_from_context(Node *ctx)
{
	Track::Type ctx_type = Track::k_count;

	if (ClipBlock *clip = dynamic_cast<ClipBlock *>(ctx)) {
		if (clip->track()) {
			if (clip->track()->type() != Track::k_none) {
				ctx_type = clip->track()->type();
			}
		}
	} else if (Track *track = dynamic_cast<Track *>(ctx)) {
		if (track->type() != Track::k_none) {
			ctx_type = track->type();
		}
	}

	return context_items_.at(ctx_type);
}

void NodeParamView::toggle_select(NodeParamViewItem *item)
{
	QVector<NodeParamViewItem *> new_sel;

	if (qApp->keyboardModifiers() & Qt::ShiftModifier) {
		new_sel = selected_nodes_;
	}

	if (selected_nodes_.contains(item)) {
		// De-select this node
		if (qApp->keyboardModifiers() & Qt::ShiftModifier) {
			new_sel.removeOne(item);
			set_selected_nodes(new_sel, true);
		}
	} else {
		new_sel.append(item);
		set_selected_nodes(new_sel, false);

		if (!new_sel.contains(focused_node_)) {
			// This node gets sent to both the curve editor and viewer, so we focus it even if it has
			// no gizmos
			focused_node_ = item;

			emit focused_node_changed(focused_node_ ? focused_node_->get_node() :
													nullptr);
		}
	}
}

QHash<Node *, Node *>
NodeParamView::generate_existing_paste_map(const ProjectSerializer::Result &r)
{
	QVector<Node *> ignore_nodes;
	QHash<Node *, Node *> existing_nodes;
	for (Node *n : r.get_load_data().nodes) {
		if (Node *existing =
				get_node_with_id_and_ignore_list(n->id(), ignore_nodes)) {
			existing_nodes.insert(existing, n);
			ignore_nodes.append(existing);
		}
	}
	return existing_nodes;
}

void NodeParamView::update_global_scroll_bar()
{
	if (keyframe_view_) {
		keyframe_view_->set_max_scroll(param_widget_container_->height() -
									 ruler()->height());
	}
}

void NodeParamView::pin_node(bool pin)
{
	NodeParamViewItem *item = static_cast<NodeParamViewItem *>(sender());
	Node *node = item->get_node();

	if (pin) {
		pinned_nodes_.append(node);
	} else {
		pinned_nodes_.removeOne(node);

		if (!active_nodes_.contains(node)) {
			//RemoveNode(node);
		}
	}
}

/*void NodeParamView::FocusChanged(QWidget* old, QWidget* now)
{
  Q_UNUSED(old)

  QObject* parent = now;

  while (parent) {
    if (NodeParamViewItem* item = dynamic_cast<NodeParamViewItem*>(parent)) {
      // Found a NodeParamViewItem that isn't already focused, see if it belongs to us
      bool ours = false;

      do {
        parent = parent->parent();

        if (parent == this) {
          ours = true;
          break;
        }
      } while (parent);

      if (ours) {
        //ToggleSelect(item);
        Q_UNUSED(item)
      }
      break;
    }

    parent = parent->parent();
  }
}*/

void NodeParamView::update_element_y()
{
	for (NodeParamViewContext *ctx : context_items_) {
		for (auto it = ctx->get_items().cbegin(); it != ctx->get_items().cend();
			 it++) {
			NodeParamViewItem *item = *it;
			Node *node = item->get_node();
			const KeyframeView::NodeConnections &connections =
				item->get_keyframe_connections();

			if (!connections.isEmpty()) {
				for (const QString &input : node->inputs()) {
					if (!(node->get_input_flags(input) & k_input_flag_hidden)) {
						int arr_sz =
							NodeGroup::resolve_input(NodeInput(node, input))
								.get_array_size();

						for (int i = -1; i < arr_sz; i++) {
							NodeInput ic = { node, input, i };

							int y = item->get_element_y(ic);

							// For some reason Qt's mapToGlobal doesn't seem to handle this, so we offset here
							y += vertical_scrollbar_->value();

							const KeyframeView::InputConnections &input_con =
								connections.value(input);
							int use_index = i + 1;
							if (use_index < input_con.size()) {
								const KeyframeView::ElementConnections &ele_con =
									input_con.at(ic.element() + 1);
								for (KeyframeViewInputConnection *track :
									 ele_con) {
									track->set_keyframe_y(y);
								}
							}
						}
					}
				}
			}
		}
	}
}

void NodeParamView::node_added_to_context(Node *n)
{
	Node *ctx = static_cast<Node *>(sender());
	NodeParamViewContext *item = get_context_item_from_context(ctx);

	add_node(n, ctx, item);

	sort_items_in_context(item);

	if (keyframe_view_) {
		queue_keyframe_position_update();
	}
}

void NodeParamView::node_removed_from_context(Node *n)
{
	Node *ctx = static_cast<Node *>(sender());

	foreach (NodeParamViewContext *ctx_item, context_items_) {
		ctx_item->remove_node(n, ctx);
	}

	if (keyframe_view_) {
		queue_keyframe_position_update();
	}
}

void NodeParamView::input_check_box_changed(const NodeInput &input, bool e)
{
	NodeGroup *group = static_cast<NodeGroup *>(contexts_.first());

	if (e) {
		group->add_input_passthrough(input);
	} else {
		group->remove_input_passthrough(input);
	}
}

void NodeParamView::group_input_passthrough_added(NodeGroup *group,
											   const NodeInput &input)
{
	foreach (NodeParamViewContext *pvctx, context_items_) {
		pvctx->set_input_checked(input, true);
	}
}

void NodeParamView::group_input_passthrough_removed(NodeGroup *group,
												 const NodeInput &input)
{
	foreach (NodeParamViewContext *pvctx, context_items_) {
		pvctx->set_input_checked(input, false);
	}
}

void NodeParamView::input_array_size_changed(const QString &input, int,
										  int new_size)
{
	NodeParamViewItem *sender =
		static_cast<NodeParamViewItem *>(this->sender());

	KeyframeView::NodeConnections &connections =
		sender->get_keyframe_connections();
	KeyframeView::InputConnections &inputs = connections[input];

	int adj_new_size = new_size + 1;

	if (adj_new_size != inputs.size()) {
		if (adj_new_size < inputs.size()) {
			// Remove elements from keyframe view
			for (int i = adj_new_size; i < inputs.size(); i++) {
				const KeyframeView::ElementConnections &ec = inputs.at(i);
				for (auto kc : ec) {
					keyframe_view_->remove_keyframes_of_track(kc);
				}
			}

			// Resize vector to match new size
			inputs.resize(adj_new_size);
		} else {
			// Add elements
			int old_size = inputs.size();

			// Resize vector to match
			inputs.resize(adj_new_size);

			// Fill in extra elements
			for (int i = old_size; i < inputs.size(); i++) {
				inputs[i] = keyframe_view_->add_keyframes_of_element(
					NodeInput(sender->get_node(), input, i - 1));
			}
		}
	}

	queue_keyframe_position_update();
}

}
