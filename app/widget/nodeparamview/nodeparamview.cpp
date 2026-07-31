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
#include <QSet>
#include <QSplitter>

#include "oakengine/footage.h"
#include "oakengine/node.h"
#include "oakengine/undo.h"
#include "oakutil/oaknode.h"
#include "widget/timelinewidget/cliphandle.h"
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
	context_items_.resize(TrackReference::k_count + 1);
	for (int i = 0; i < context_items_.size(); i++) {
		NodeParamViewContext *c = new NodeParamViewContext(param_widget_area_);
		c->setVisible(false);
		connect(c, &NodeParamViewContext::about_to_delete_item, this,
				&NodeParamView::item_about_to_be_removed, Qt::DirectConnection);

		NodeParamViewItemTitleBar *title_bar =
			static_cast<NodeParamViewItemTitleBar *>(c->titleBarWidget());

		if (i == TrackReference::k_video || i == TrackReference::k_audio) {
			c->set_effect_type(static_cast<TrackReference::Type>(i));
			title_bar->set_add_effect_button_visible(true);
			title_bar->set_text(tr("%1 Nodes")
								   .arg(QString::fromUtf8(
									   [i]() -> QByteArray {
										   char buf[64];
										   buf[0] = '\0';
										   oakengine_footage_stream_type_name(
											   static_cast<int>(i), buf,
											   sizeof(buf));
										   return QByteArray(buf);
									   }())));
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

	// Connect bridge signals for group input passthrough events
	connect(bridge_, &EngineEventBridge::group_input_passthrough_added, this,
			[this](OakEngineNode *source, OakEngineNode *node,
				   const QString &input, int element) {
				group_input_passthrough_added(source,
					oak::Input(node, input, element));
			});
	connect(bridge_, &EngineEventBridge::group_input_passthrough_removed, this,
			[this](OakEngineNode *source, OakEngineNode *node,
				   const QString &input, int element) {
				group_input_passthrough_removed(source,
					oak::Input(node, input, element));
			});
	connect(bridge_, &EngineEventBridge::node_node_added_to_context, this,
			[this](OakEngineNode *source, OakEngineNode *node) {
				node_added_to_context(node, source);
			}, Qt::QueuedConnection);
	connect(bridge_, &EngineEventBridge::node_node_removed_from_context, this,
			[this](OakEngineNode *source, OakEngineNode *node) {
				node_removed_from_context(node, source);
			}, Qt::QueuedConnection);
}

NodeParamView::~NodeParamView()
{
	qDeleteAll(context_items_);
}

void NodeParamView::close_contexts_belonging_to_project(oak::Project p)
{
	QVector<oak::Node> new_contexts = contexts_;

	for (int i = 0; i < new_contexts.size(); i++) {
		if (new_contexts.at(i).project().handle() == p.handle()) {
			new_contexts.removeAt(i);
			i--;
		}
	}

	set_contexts(new_contexts);
}

/*void NodeParamView::SelectNodes(const QVector<OakEngineNode *> &nodes)
{
  return;
  int original_node_count = items_.size();

  foreach (OakEngineNode* n, nodes) {
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

void NodeParamView::DeselectNodes(const QVector<OakEngineNode *> &nodes)
{
  return;
  // Remove item from map and delete the widget
  int original_node_count = items_.size();

  foreach (OakEngineNode* n, nodes) {
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

	foreach (const oak::Node &ctx, current_contexts_) {
		if (!contexts_.contains(ctx)) {
			// Context is being removed
			remove_context(ctx);
			changes_made = true;
		}
	}

	foreach (const oak::Node &ctx, contexts_) {
		if (!current_contexts_.contains(ctx)) {
			// Context is being added
			add_context(ctx);
			changes_made = true;
		}
	}

	if (changes_made) {
		current_contexts_ = contexts_;

		// Tear down any previous group input passthrough subscriptions before
		// (re-)subscribing to the current group, avoiding duplicates across
		// repeated calls and stale callbacks after leaving group mode.
		if (group_passthrough_added_sub_ > 0) {
			bridge_->unsubscribe(group_passthrough_added_sub_);
			group_passthrough_added_sub_ = 0;
		}
		if (group_passthrough_removed_sub_ > 0) {
			bridge_->unsubscribe(group_passthrough_removed_sub_);
			group_passthrough_removed_sub_ = 0;
		}

		if (is_group_mode()) {
			// Check inputs that have been passed through
			oak::Node group = contexts_.first();
			// WRAPPER-GAP: oakengine_group_input_passthrough_* (group API
			// has no oak:: wrapper)
			const int pt_count =
				oakengine_group_input_passthrough_count(group.handle());
			for (int i = 0; i < pt_count; i++) {
				OakEngineNode *inner_node = nullptr;
				char inner_input[256];
				int inner_element = 0;
				char id[256];
				if (oakengine_group_input_passthrough_at(
						group.handle(), i,
						id, sizeof(id), &inner_node, inner_input,
						sizeof(inner_input), &inner_element) ==
					OAKENGINE_OK) {
					group_input_passthrough_added(
						group.handle(),
						oak::Input(inner_node,
								  QString::fromUtf8(inner_input),
								  inner_element));
				}
			}

			OakEngineNode *group_handle = group.handle();
			group_passthrough_added_sub_ = bridge_->subscribe(
				group_handle, OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_ADDED);
			group_passthrough_removed_sub_ = bridge_->subscribe(
				group_handle, OAKENGINE_EVENT_GROUP_INPUT_PASSTHROUGH_REMOVED);
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

void NodeParamView::select_node_from_connected_link(OakEngineNode *node)
{
	NodeParamViewItem *item = static_cast<NodeParamViewItem *>(sender());

	QPair<OakEngineNode *, OakEngineNode *> p = qMakePair(
		node, item->get_context().handle());
	set_selected_nodes({ p });
}

void NodeParamView::request_edit_text_in_viewer()
{
	NodeParamViewItem *item = static_cast<NodeParamViewItem *>(sender());

	set_selected_nodes({ item });
	emit request_viewer_to_start_editing_text();
}

void NodeParamView::set_contexts(const QVector<oak::Node> &contexts)
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

void NodeParamView::ConnectedNodeChangeEvent(OakEngineNode *n)
{
	if (keyframe_view_) {
		// Set viewer as a time target
		keyframe_view_->set_time_target(n);
	}

	foreach (NodeParamViewContext *item, context_items_) {
		item->set_time_target(n);
	}
}

// Collects the bypass rewiring edges for a node about to be deleted: every
// consumer of `deleting` gets rewired to `output` (the first surviving node
// upstream). The edges are handed to oakengine_nodes_delete_many_ex(), which
// applies them AFTER the deletion inside the same undoable command — the
// target inputs are still occupied until then, so connecting here directly
// would fail with OAKENGINE_E_STATE.
struct ReconnectEdgeList {
	QVector<OakEngineNode *> outputs;
	QVector<OakEngineNode *> input_nodes;
	QVector<QByteArray> ids_storage;
	QVector<const char *> ids;
	QVector<int> elements;

	void finalize_ids()
	{
		ids.clear();
		ids.reserve(ids_storage.size());
		for (const QByteArray &b : ids_storage) {
			ids.append(b.constData());
		}
	}
};

void collect_reconnect_edges(QSet<OakEngineNode *> &deleted_nodes,
							 OakEngineNode *output,
							 OakEngineNode *deleting, ReconnectEdgeList &edges)
{
	// Output-connection enumeration goes through the oak:: wrapper (C ABI);
	// replaces the engine Node::output_connections() map iteration.
	const oak::Node deleting_handle(deleting);
	const int connection_count = deleting_handle.output_connection_count();
	for (int i = 0; i < connection_count; i++) {
		const oak::NodeConnection proposed_reconnect =
			deleting_handle.output_connection_at_ex(i);
		OakEngineNode *proposed_node = proposed_reconnect.node.handle();

		if (deleted_nodes.contains(proposed_node)) {
			// Uh-oh we're deleting this node too, instead connect to its outputs
			collect_reconnect_edges(deleted_nodes, output, proposed_node, edges);
		} else {
			edges.outputs.append(output);
			edges.input_nodes.append(proposed_reconnect.node.handle());
			edges.ids_storage.append(proposed_reconnect.input_id.toUtf8());
			edges.elements.append(proposed_reconnect.element);
		}
	}
}

void NodeParamView::DeleteSelected()
{
	if (keyframe_view_ && keyframe_view_->hasFocus()) {
		keyframe_view_->delete_selected();
	} else if (!selected_nodes_.isEmpty()) {
		QVector<OakEngineNode *> nodes;
		QVector<OakEngineNode *> contexts;

		QSet<OakEngineNode *> deleted_nodes_set;

		// Collect all nodes to delete
		foreach (NodeParamViewItem *item, selected_nodes_) {
			OakEngineNode *n = item->get_node().handle();
			nodes.append(n);
			contexts.append(item->get_context().handle());
			deleted_nodes_set.insert(n);
		}

		// Collect bypass rewiring edges (applied after the deletion by the
		// facade, inside the same undoable command)
		ReconnectEdgeList edges;
		foreach (NodeParamViewItem *item, selected_nodes_) {
			OakEngineNode *n = item->get_node().handle();

			OakEngineNode *node_being_deleted = n;
			OakEngineNode *connected_to_effect_input = nullptr;

			while (true) {
				oak::Input effect_input =
					oak::Node(node_being_deleted).effect_input();
				if (effect_input.is_valid()) {
					oak::Node connected = effect_input.connected_node();
					if (!connected.is_null()) {
						connected_to_effect_input = connected.handle();
						if (deleted_nodes_set.contains(connected_to_effect_input)) {
							// Node's getting deleted, recurse
							node_being_deleted = connected_to_effect_input;
							continue;
						}
					}
				}

				break;
			}

			if (connected_to_effect_input) {
				collect_reconnect_edges(deleted_nodes_set,
										connected_to_effect_input, n, edges);
			}
		}
		edges.finalize_ids();

		// Delete the nodes and rewire around them in ONE undoable command
		oakengine_nodes_delete_many_ex(
			nodes.constData(), contexts.constData(), nodes.size(),
			nullptr, nullptr, nullptr, nullptr, 0,
			edges.outputs.isEmpty() ? nullptr : edges.outputs.constData(),
			edges.input_nodes.isEmpty() ? nullptr : edges.input_nodes.constData(),
			edges.ids.isEmpty() ? nullptr : edges.ids.constData(),
			edges.elements.isEmpty() ? nullptr : edges.elements.constData(),
			edges.outputs.size());
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

	QVector<QPair<OakEngineNode *, OakEngineNode *>> p;
	if (emit_signal) {
		p.resize(selected_nodes_.size());
	}

	for (int i = 0; i < selected_nodes_.size(); i++) {
		NodeParamViewItem *n = selected_nodes_.at(i);
		n->set_highlighted(true);

		if (emit_signal) {
			p[i] = qMakePair(n->get_node().handle(),
							 n->get_context().handle());
		}
	}

	if (handle_focused_node) {
		focused_node_ = nullptr;

		foreach (NodeParamViewItem *n, selected_nodes_) {
			if (oakengine_node_has_gizmos(n->get_node().handle())) {
				focused_node_ = n;
				break;
			}
		}

		OakEngineNode *n = focused_node_ ? focused_node_->get_node().handle() :
										nullptr;
		emit focused_node_changed(n);
	}

	if (emit_signal) {
		emit selected_nodes_changed(p);
	}
}

void NodeParamView::set_selected_nodes(
	const QVector<QPair<OakEngineNode *, OakEngineNode *>> &nodes,
	bool emit_signal)
{
	QVector<NodeParamViewItem *> items;
	NodeParamViewContext *scrolled_ctx = nullptr;

	foreach (const auto &n, nodes) {
		for (auto it = context_items_.cbegin(); it != context_items_.cend();
			 it++) {
			NodeParamViewContext *ctx = *it;

			NodeParamViewItem *item =
				ctx->get_item(oak::Node(n.first), oak::Node(n.second));

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

OakEngineNode *NodeParamView::get_node_with_id(const QString &id)
{
	return get_node_with_id_and_ignore_list(id, QVector<OakEngineNode *>());
}

OakEngineNode *NodeParamView::get_node_with_id_and_ignore_list(const QString &id,
												const QVector<OakEngineNode *> &ignore)
{
	for (NodeParamViewItem *item : selected_nodes_) {
		OakEngineNode *n = item->get_node().handle();
		if (item->get_node().id() == id && !ignore.contains(n)) {
			return n;
		}
	}

	for (NodeParamViewContext *ctx : context_items_) {
		for (NodeParamViewItem *item : ctx->get_items()) {
			OakEngineNode *n = item->get_node().handle();
			if (item->get_node().id() == id && !ignore.contains(n)) {
				return n;
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

	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_NODES, nullptr, nullptr);
	QVector<OakEngineNode *> nodes;

	for (NodeParamViewItem *item : selected_nodes_) {
		OakEngineNode *n = item->get_node().handle();

		if (!nodes.contains(n)) {
			nodes.append(n);

			QPointF pos;
			bool expanded = false;
			item->get_context().context_position_of(
				item->get_node(), &pos, &expanded);

			oakengine_clipboard_set_property(
				cb, n, "x",
				QByteArray::number(pos.x()).constData());
			oakengine_clipboard_set_property(
				cb, n, "y",
				QByteArray::number(pos.y()).constData());
			oakengine_clipboard_set_property(
				cb, n, "expanded",
				QByteArray::number(expanded).constData());
		}
	}

	oakengine_clipboard_set_nodes(
		cb,
		nodes.constData(),
		nodes.size());

	oakengine_clipboard_copy(cb);
	oakengine_clipboard_free(cb);

	if (cut) {
		DeleteSelected();
	}

	return false;
}

bool NodeParamView::paste()
{
	if (keyframe_view_) {
		if (keyframe_view_->paste([this](const QString &id) {
				return oak::Node(get_node_with_id(id));
			})) {
			return true;
		}
	}

	return paste(this, std::bind(&NodeParamView::generate_existing_paste_map, this,
								 std::placeholders::_1));
}

bool NodeParamView::paste(
	QWidget *parent,
	std::function<QHash<OakEngineNode *, OakEngineNode *>(void *)>
		get_existing_map_function)
{
	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_NODES, nullptr, nullptr);
	int result_code = OAKENGINE_SERIALIZER_NO_DATA;
	oakengine_clipboard_paste(cb, OAKENGINE_CLIPBOARD_NODES, nullptr,
							&result_code, nullptr, 0);

	if (result_code != OAKENGINE_SERIALIZER_OK) {
		oakengine_clipboard_free(cb);
		return false;
	}

	// Collect pasted nodes
	QVector<OakEngineNode *> pasted_nodes;
	const int node_count = oakengine_clipboard_get_loaded_node_count(cb);
	pasted_nodes.reserve(node_count);
	for (int i = 0; i < node_count; i++) {
		pasted_nodes.append(oakengine_clipboard_get_loaded_node_at(cb, i));
	}

	if (pasted_nodes.isEmpty()) {
		oakengine_clipboard_free(cb);
		return false;
	}

	// Determine if any nodes of this type are already in the editor
	QHash<OakEngineNode *, OakEngineNode *> existing_nodes = get_existing_map_function(cb);

	QVector<OakEngineNode *> nodes_to_paste_as_new = pasted_nodes;
	void *command = oakengine_undo_command_create_multi();

	if (!existing_nodes.empty()) {
		QMessageBox b(parent);
		b.setWindowTitle(tr("Paste Nodes"));

		QStringList node_names;
		for (auto it = existing_nodes.cbegin(); it != existing_nodes.cend();
			 it++) {
			node_names.append(oak::Node(it.key()).label_and_name());
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
			// Caller-owned pasted nodes that will not be inserted: free them
			// synchronously through the facade (replaces qDeleteAll on engine
			// Node*).
			for (OakEngineNode *n : nodes_to_paste_as_new) {
				oakengine_node_free(n);
			}
			nodes_to_paste_as_new.clear();

		} else if (b.clickedButton() == as_vals) {
			for (auto it = existing_nodes.cbegin(); it != existing_nodes.cend();
				 it++) {
				// NOTE: the C ABI oakengine_node_copy_inputs pushes its own
				// undo entry rather than becoming a child of `command`.
				oakengine_node_copy_inputs(
					it.key(),
					it.value());
				nodes_to_paste_as_new.removeOne(it.value());
			}
		}
	}

	// NOTE: upstream Olive built a Node::PositionMap from the clipboard
	// properties here but never applied it (dead code). Dropped during the
	// facade migration instead of carrying a placeholder; positions fall
	// back to the context default, same as before.

	oakengine_undo_push(
		command, tr("Pasted %1 Node(s)").arg(nodes_to_paste_as_new.size()).toUtf8().constData());

	oakengine_clipboard_free(cb);
	return true;
}

void NodeParamView::queue_keyframe_position_update()
{
	QMetaObject::invokeMethod(this, &NodeParamView::update_element_y,
							  Qt::QueuedConnection);
}

void NodeParamView::add_context(oak::Node ctx)
{
	NodeParamViewContext *item =
		get_context_item_from_context(ctx.handle());

	// TEMP: Creating many NPV items is EXTREMELY slow so limit to one item per context for now.
	//       I have a better solution in the works to use one UI for several nodes, but I haven't
	//       done it yet, and this can severely affect productivity.
	if (item->get_contexts().size() == 1) {
		return;
	}

	// Queued so that if any further work is done in connecting this node to the context, it'll be
	// done before our sorting function is called
	context_subs_[ctx].first = bridge_->subscribe(
		reinterpret_cast<void *>(ctx.handle()),
		OAKENGINE_EVENT_NODE_NODE_ADDED_TO_CONTEXT);
	context_subs_[ctx].second = bridge_->subscribe(
		reinterpret_cast<void *>(ctx.handle()),
		OAKENGINE_EVENT_NODE_NODE_REMOVED_FROM_CONTEXT);

	item->add_context(ctx);
	item->setVisible(true);

	const int context_node_count = ctx.context_node_count();
	for (int i = 0; i < context_node_count; i++) {
		add_node(ctx.context_node_at(i).node.handle(), ctx.handle(), item);
	}
}

void NodeParamView::remove_context(oak::Node ctx)
{
	auto subs = context_subs_.take(ctx);
	bridge_->unsubscribe(subs.first);
	bridge_->unsubscribe(subs.second);

	foreach (NodeParamViewContext *item, context_items_) {
		item->remove_context(ctx);
		item->remove_nodes_with_context(ctx);

		if (item->get_contexts().isEmpty()) {
			item->setVisible(false);
		}
	}
}

void NodeParamView::add_node(OakEngineNode *n, OakEngineNode *ctx, NodeParamViewContext *context)
{
	if ((oak::Node(n).flags() & oakengine_node_flag_dont_show_in_param_view()) &&
		!is_group_mode() && !show_all_nodes_) {
		return;
	}

	NodeParamViewItem *item = new NodeParamViewItem(
		oak::Node(n),
		is_group_mode() ? k_check_boxes_on_non_connected : k_no_check_boxes,
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

	item->set_context(oak::Node(ctx));
	item->set_time_target(get_connected_node());
	item->set_timebase(timebase());

	context->add_node(item);

	if (!focused_node_ && oakengine_node_has_gizmos(n)) {
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

		item->set_keyframe_connections(keyframe_view_->add_keyframes_of_node(
			oak::Node(n)));
	}
}

int get_distance_between_nodes(OakEngineNode *start, OakEngineNode *end)
{
	if (start == end) {
		return 0;
	}

	const oak::Node start_node(start);
	const int connection_count = start_node.input_connection_count_all();
	for (int i = 0; i < connection_count; i++) {
		OakEngineNode *upstream =
			start_node.input_connection_at_all(i).node.handle();
		int this_node_dist = get_distance_between_nodes(upstream, end);
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
		foreach (const oak::Node &ctx, context_item->get_contexts()) {
			distance =
				qMax(distance, get_distance_between_nodes(
					ctx.handle(),
					item->get_node().handle()));
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

NodeParamViewContext *NodeParamView::get_context_item_from_context(OakEngineNode *ctx)
{
	TrackReference::Type ctx_type = TrackReference::k_count;

	if (oakengine_node_is_clip(ctx)) {
		OakEngineNode *track = block_track_handle(
			reinterpret_cast<OakEngineBlock *>(ctx));
		if (track) {
			int type = oakengine_track_get_type(track);
			if (type != TrackReference::k_none) {
				ctx_type = static_cast<TrackReference::Type>(type);
			}
		}
	} else if (oakengine_node_is_track(ctx)) {
		int type = oakengine_track_get_type(ctx);
		if (type != TrackReference::k_none) {
			ctx_type = static_cast<TrackReference::Type>(type);
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

			emit focused_node_changed(focused_node_ ? focused_node_->get_node().handle() :
													nullptr);
		}
	}
}

QHash<OakEngineNode *, OakEngineNode *>
NodeParamView::generate_existing_paste_map(void *clipboard)
{
	QVector<OakEngineNode *> ignore_nodes;
	QHash<OakEngineNode *, OakEngineNode *> existing_nodes;
	OakEngineClipboard *cb = static_cast<OakEngineClipboard *>(clipboard);
	const int node_count = oakengine_clipboard_get_loaded_node_count(cb);
	for (int i = 0; i < node_count; i++) {
		OakEngineNode *n = oakengine_clipboard_get_loaded_node_at(cb, i);
		if (OakEngineNode *existing =
				get_node_with_id_and_ignore_list(oak::Node(n).id(), ignore_nodes)) {
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
	OakEngineNode *node = item->get_node().handle();

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
			oak::Node node = item->get_node();
			const KeyframeView::NodeConnections &connections =
				item->get_keyframe_connections();

			if (!connections.isEmpty()) {
				const int node_input_count = node.input_count();
				for (int input_index = 0; input_index < node_input_count;
					 input_index++) {
					const QString input = node.input_id(input_index);
					if (!oak::Input(node.handle(), input).is_hidden()) {
						OakEngineNode *out_node = nullptr;
						char out_input[256];
						int out_element = 0;
						int arr_sz = 0;
						// WRAPPER-GAP: oakengine_group_resolve_input (group
						// API has no oak:: wrapper)
						if (oakengine_group_resolve_input(
								contexts_.first().handle(),
							input.toUtf8().constData(), -1, &out_node,
							out_input, sizeof(out_input),
							&out_element) == OAKENGINE_OK && out_node) {
							arr_sz = oak::Input(out_node,
												QString::fromUtf8(out_input))
										 .array_size();
						}
						for (int i = -1; i < arr_sz; i++) {
							oak::Input ic(node.handle(), input, i);

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

void NodeParamView::node_added_to_context(OakEngineNode *n, OakEngineNode *ctx)
{
	NodeParamViewContext *item = get_context_item_from_context(ctx);

	add_node(n, ctx, item);

	sort_items_in_context(item);

	if (keyframe_view_) {
		queue_keyframe_position_update();
	}
}

void NodeParamView::node_removed_from_context(OakEngineNode *n, OakEngineNode *ctx)
{
	foreach (NodeParamViewContext *ctx_item, context_items_) {
		ctx_item->remove_node(oak::Node(n), oak::Node(ctx));
	}

	if (keyframe_view_) {
		queue_keyframe_position_update();
	}
}

void NodeParamView::input_check_box_changed(const oak::Input &input, bool e)
{
	oak::Node group = contexts_.first();

	if (e) {
		char out_id[256];
		// WRAPPER-GAP: oakengine_group_add_input_passthrough (group API has
		// no oak:: wrapper)
		oakengine_group_add_input_passthrough(
			group.handle(),
			nullptr, input.input_id().toUtf8().constData(), input.element(),
			nullptr, out_id, sizeof(out_id));
	} else {
		// WRAPPER-GAP: oakengine_group_remove_input_passthrough (group API
		// has no oak:: wrapper)
		oakengine_group_remove_input_passthrough(
			group.handle(),
			nullptr, input.input_id().toUtf8().constData(), input.element());
	}
}

void NodeParamView::group_input_passthrough_added(OakEngineNode *group,
											   const oak::Input &input)
{
	foreach (NodeParamViewContext *pvctx, context_items_) {
		pvctx->set_input_checked(input, true);
	}
}

void NodeParamView::group_input_passthrough_removed(OakEngineNode *group,
												 const oak::Input &input)
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
					oak::Input(sender->get_node().handle(), input, i - 1));
			}
		}
	}

	queue_keyframe_position_update();
}

}
