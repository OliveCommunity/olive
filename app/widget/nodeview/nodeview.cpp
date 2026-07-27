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

#include "nodeview.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QToolTip>

#include "node/audio/volume/volume.h"
#include "node/distort/transform/transformdistortnode.h"
#include "oakengine/node.h"
#include "oakengine/project.h"
#include "oakengine/serializer.h"
#include "oakengine/undo.h"
#include "panel/panelmanager.h"
#include "node/traverser.h"
#include "ui/icons/icons.h"
#include "widget/menu/factorymenu.h"
#include "widget/menu/menushared.h"
#include "widget/timebased/timebasedview.h"

#define super HandMovableView

namespace olive
{

namespace
{

QVector<OakEngineNode *> node_vector_to_engine(const QVector<Node *> &nodes)
{
	QVector<OakEngineNode *> result;
	result.reserve(nodes.size());
	foreach (Node *n, nodes) {
		result.append(reinterpret_cast<OakEngineNode *>(n));
	}
	return result;
}

QVector<QPair<OakEngineNode *, OakEngineNode *>>
context_pair_vector_to_engine(const QVector<Node::ContextPair> &pairs)
{
	QVector<QPair<OakEngineNode *, OakEngineNode *>> result;
	result.reserve(pairs.size());
	foreach (const Node::ContextPair &p, pairs) {
		result.append(qMakePair(reinterpret_cast<OakEngineNode *>(p.node),
							reinterpret_cast<OakEngineNode *>(p.context)));
	}
	return result;
}

} // namespace

const double NodeView::k_minimum_scale = 0.1;
const int NodeView::k_maximum_contexts = 10;

NodeView::NodeView(QWidget *parent)
	: HandMovableView(parent)
	, drop_edge_(nullptr)
	, create_edge_(nullptr)
	, create_edge_output_item_(nullptr)
	, create_edge_input_item_(nullptr)
	, overlay_view_(nullptr)
	, scale_(1.0)
	, dont_emit_selection_signals_(false)
	, show_in_param_editor_action_(nullptr)
	, bridge_(new EngineEventBridge(this))
{
	setScene(&scene_);
	set_default_drag_mode(RubberBandDrag);
	setContextMenuPolicy(Qt::CustomContextMenu);
	setMouseTracking(true);
	setRenderHint(QPainter::Antialiasing);
	setViewportUpdateMode(FullViewportUpdate);

	connect(this, &NodeView::customContextMenuRequested, this,
			&NodeView::show_context_menu);

	connect(bridge_, &EngineEventBridge::node_removed_from_graph, this,
			&NodeView::node_removed_from_graph);

	connect_selection_changed_signal();

	set_flow_direction(NodeViewCommon::k_left_to_right);

	show_in_param_editor_action_ =
		new QAction(tr("Show in Parameter Editor"), this);
	Menu::conform_item(show_in_param_editor_action_,
					  QStringLiteral("shownodeparams"),
					  QKeySequence(tr("Shift+P")));
	show_in_param_editor_action_->setShortcutContext(Qt::WindowShortcut);
	addAction(show_in_param_editor_action_);
	connect(show_in_param_editor_action_, &QAction::triggered, this,
			&NodeView::show_selected_node_in_param_editor);

	update_scene_bounding_rect();
	connect(&scene_, &QGraphicsScene::changed, this,
			&NodeView::update_scene_bounding_rect);

	minimap_ = new NodeViewMiniMap(&scene_, this);
	minimap_->show();
	connect(minimap_, &NodeViewMiniMap::resized, this,
			&NodeView::reposition_mini_map);
	connect(minimap_, &NodeViewMiniMap::move_to_scene_point, this,
			&NodeView::move_to_scene_point);
	connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
			&NodeView::update_viewport_on_mini_map);
	connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
			&NodeView::update_viewport_on_mini_map);

	viewport()->installEventFilter(this);
}

NodeView::~NodeView()
{
	// Unset the current graph
	clear_graph();
}

void NodeView::set_contexts(const QVector<Node *> &nodes)
{
	if (overlay_view_) {
		close_overlay();
	}

	// Remove contexts that are no longer in the list
	foreach (Node *n, contexts_) {
		if (!nodes.contains(n)) {
			remove_context(n);
		}
	}

	// Add contexts that are now in the list
	foreach (Node *n, nodes) {
		if (scene_.context_map().size() >= k_maximum_contexts) {
			break;
		}

		if (!contexts_.contains(n)) {
			add_context(n);
		}
	}

	contexts_ = nodes;

	center_on_items_bounding_rect();
}

void NodeView::close_contexts_belonging_to_project(Project *project)
{
	QVector<Node *> new_contexts = contexts_;

	for (auto it = new_contexts.begin(); it != new_contexts.end();) {
		if ((*it)->project() == project) {
			it = new_contexts.erase(it);
		} else {
			it++;
		}
	}

	set_contexts(new_contexts);
}

void NodeView::clear_graph()
{
	set_contexts(QVector<Node *>());
}

void NodeView::delete_selected()
{
	QVector<Node *> nodes;
	QVector<Node *> contexts;
	QVector<NodeViewEdge *> edges;

	foreach (NodeViewContext *ctx, scene_.context_map()) {
		ctx->get_selected_for_deletion(nodes, contexts, edges);
	}

	if (nodes.isEmpty() && edges.isEmpty()) {
		return;
	}

	// Build C arrays for the facade
	QVector<OakEngineNode *> oak_nodes(nodes.size());
	QVector<OakEngineNode *> oak_contexts(nodes.size());
	for (int i = 0; i < nodes.size(); i++) {
		oak_nodes[i] = reinterpret_cast<OakEngineNode *>(nodes[i]);
		oak_contexts[i] = reinterpret_cast<OakEngineNode *>(contexts[i]);
	}

	QVector<OakEngineNode *> edge_outputs(edges.size());
	QVector<OakEngineNode *> edge_input_nodes(edges.size());
	QVector<QByteArray> edge_input_ids_storage(edges.size());
	QVector<const char *> edge_input_ids(edges.size());
	QVector<int> edge_input_elements(edges.size());
	for (int i = 0; i < edges.size(); i++) {
		edge_outputs[i] = reinterpret_cast<OakEngineNode *>(edges[i]->output());
		edge_input_nodes[i] = reinterpret_cast<OakEngineNode *>(edges[i]->input().node());
		edge_input_ids_storage[i] = edges[i]->input().input().toUtf8();
		edge_input_ids[i] = edge_input_ids_storage[i].constData();
		edge_input_elements[i] = edges[i]->input().element();
	}

	// ONE undoable command whether this deletes nodes, edges, or both
	oakengine_nodes_delete_many(
		oak_nodes.isEmpty() ? nullptr : oak_nodes.constData(),
		oak_contexts.isEmpty() ? nullptr : oak_contexts.constData(),
		nodes.size(),
		edge_outputs.isEmpty() ? nullptr : edge_outputs.constData(),
		edge_input_nodes.isEmpty() ? nullptr : edge_input_nodes.constData(),
		edge_input_ids.isEmpty() ? nullptr : edge_input_ids.constData(),
		edge_input_elements.isEmpty() ? nullptr : edge_input_elements.constData(),
		edges.size());
}

void NodeView::select_all()
{
	// Optimization: rather than respond to every single item being selected, ignore the signal and
	//               then handle them all at the end.
	disconnect_selection_changed_signal();

	scene_.select_all();

	connect_selection_changed_signal();

	update_selection_cache();
}

void NodeView::deselect_all()
{
	if (selected_nodes_.isEmpty()) {
		return;
	}

	// Optimization: rather than respond to every single item being selected, ignore the signal and
	//               then handle them all at the end.
	disconnect_selection_changed_signal();

	scene_.deselect_all();

	connect_selection_changed_signal();

	// Just emit all the nodes that are currently selected as no longer selected
	emit nodes_deselected(node_vector_to_engine(selected_nodes_));
	selected_nodes_.clear();
	emit node_selection_changed(node_vector_to_engine(selected_nodes_));
	emit node_selection_changed_with_contexts(
		QVector<QPair<OakEngineNode *, OakEngineNode *>>());
}

void NodeView::select(
	const QVector<QPair<OakEngineNode *, OakEngineNode *>> &nodes,
	bool center_view_on_item)
{
	// Optimization: rather than respond to every single item being selected, ignore the signal and
	//               then handle them all at the end.
	disconnect_selection_changed_signal();

	QVector<Node *> deselections = selected_nodes_;
	QVector<Node *> new_selections;

	scene_.deselect_all();

	foreach (const auto &p, nodes) {
		Node *node = reinterpret_cast<Node *>(p.first);
		Node *context = reinterpret_cast<Node *>(p.second);
		NodeViewContext *ctx = scene_.context_map().value(context);
		if (ctx) {
			NodeViewItem *item = ctx->get_item_from_map(node);
			if (item) {
				item->setSelected(true);
			}
		}
	}

	// Center on something
	if (center_view_on_item && !nodes.isEmpty()) {
		QMetaObject::invokeMethod(this, "center_on_node", Qt::QueuedConnection,
								  Q_ARG(OakEngineNode *, nodes.first().first));
	}

	connect_selection_changed_signal();

	// Don't signal when this function was likely triggered from another widget's signal anyway
	dont_emit_selection_signals_ = true;
	update_selection_cache();
	dont_emit_selection_signals_ = false;
}

void NodeView::copy_selected(bool cut)
{
	if (selected_nodes_.isEmpty()) {
		return;
	}

	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_NODES, nullptr, nullptr);

	oakengine_clipboard_set_nodes(
		cb,
		reinterpret_cast<const OakEngineNode *const *>(
			selected_nodes_.constData()),
		selected_nodes_.size());

	for (Node *n : selected_nodes_) {
		NodeViewItem *item = get_assumed_item_for_selected_node(n);

		if (item) {
			Node::Position pos = item->get_node_position_data();
			oakengine_clipboard_set_property(
				cb, reinterpret_cast<OakEngineNode *>(n), "x",
				QByteArray::number(pos.position.x()).constData());
			oakengine_clipboard_set_property(
				cb, reinterpret_cast<OakEngineNode *>(n), "y",
				QByteArray::number(pos.position.y()).constData());
			oakengine_clipboard_set_property(
				cb, reinterpret_cast<OakEngineNode *>(n), "expanded",
				QByteArray::number(pos.expanded).constData());
		}
	}

	// Two-phase buf/size: query the needed length first — serialized node
	// graphs have no size bound, a fixed buffer would truncate large copies.
	const int needed = oakengine_clipboard_save_to_xml(cb, nullptr, 0);
	if (needed >= 0) {
		QByteArray buf(needed + 1, '\0');
		oakengine_clipboard_save_to_xml(cb, buf.data(), buf.size());
		Core::copy_string_to_clipboard(QString::fromUtf8(buf.constData()));
	}

	oakengine_clipboard_free(cb);

	if (cut) {
		delete_selected();
	}
}

void NodeView::paste()
{
	if (contexts_.isEmpty()) {
		return;
	}

	OakEngineClipboard *cb = oakengine_clipboard_create(
		OAKENGINE_CLIPBOARD_NODES, nullptr, nullptr);
	int result_code = OAKENGINE_SERIALIZER_NO_DATA;
	oakengine_clipboard_paste(cb, OAKENGINE_CLIPBOARD_NODES, nullptr,
							&result_code, nullptr, 0);

	if (result_code != OAKENGINE_SERIALIZER_OK) {
		oakengine_clipboard_free(cb);
		return;
	}

	QVector<Node *> nodes;
	const int node_count = oakengine_clipboard_get_loaded_node_count(cb);
	nodes.reserve(node_count);
	for (int i = 0; i < node_count; i++) {
		nodes.append(reinterpret_cast<Node *>(
			oakengine_clipboard_get_loaded_node_at(cb, i)));
	}

	Node::PositionMap map;

	oakengine_clipboard_foreach_property(
		cb,
		[](OakEngineNode *node, const char *key, const char *value,
		   void *userdata) -> int {
			auto *m = static_cast<Node::PositionMap *>(userdata);
			Node *n = reinterpret_cast<Node *>(node);
			Node::Position &pos = (*m)[n];
			if (std::strcmp(key, "x") == 0) {
				pos.position.setX(QString::fromUtf8(value).toDouble());
			} else if (std::strcmp(key, "y") == 0) {
				pos.position.setY(QString::fromUtf8(value).toDouble());
			} else if (std::strcmp(key, "expanded") == 0) {
				pos.expanded = QString::fromUtf8(value).toDouble();
			}
			return 0;
		},
		&map);

	oakengine_clipboard_free(cb);

	post_paste(nodes, map);
}

void NodeView::duplicate()
{
	if (!selected_nodes_.isEmpty()) {
		QVector<Node *> selected = selected_nodes_;
		QVector<Node *> new_nodes;
		Node::PositionMap map;

		new_nodes.resize(selected.size());

		// Create copies of each selected node, checking for groups and adding children if necessary
		for (int i = 0; i < selected.size(); i++) {
			new_nodes[i] = selected.at(i)->copy();

			if (oakengine_node_is_group(
					reinterpret_cast<OakEngineNode *>(selected.at(i)))) {
				for (auto it = selected.at(i)->get_context_positions().cbegin();
					 it != selected.at(i)->get_context_positions().cend(); it++) {
					if (!selected.contains(it.key())) {
						// This should automatically recurse if this is a group inside a group
						selected.append(it.key());
					}
				}
				new_nodes.resize(selected.size());
			}
		}

		// Get positions in contexts, add input passthroughs, and copy input values/keyframes
		for (int i = 0; i < new_nodes.size(); i++) {
			Node *og = selected.at(i);
			Node *copy = new_nodes.at(i);

			Node::Position pos;
			if (get_assumed_position_for_selected_node(og, &pos)) {
				map.insert(copy, pos);
			}

			for (auto it = og->get_context_positions().cbegin();
				 it != og->get_context_positions().cend(); it++) {
				Node *child_og = it.key();
				int child_index = selected.indexOf(child_og);

				if (child_index != -1) {
					Node *child_copy = new_nodes.at(child_index);

					oakengine_node_set_context_position(
						reinterpret_cast<OakEngineNode*>(copy),
						reinterpret_cast<OakEngineNode*>(child_copy),
						it.value().position.x(), it.value().position.y());
				}
			}

			if (oakengine_node_is_group(reinterpret_cast<OakEngineNode *>(og))) {
				const int pt_count = oakengine_group_input_passthrough_count(
					reinterpret_cast<OakEngineNode *>(og));
				for (int pt = 0; pt < pt_count; pt++) {
					OakEngineNode *inner_node = nullptr;
					char inner_input[256];
					int inner_element = 0;
					char id[256];
					if (oakengine_group_input_passthrough_at(
							reinterpret_cast<OakEngineNode *>(og), pt,
							id, sizeof(id), &inner_node, inner_input,
							sizeof(inner_input), &inner_element) !=
						OAKENGINE_OK) {
						continue;
					}
					Node *inner = reinterpret_cast<Node *>(inner_node);
					int src_index = selected.indexOf(inner);
					if (src_index == -1) {
						continue;
					}
					Node *copy_inner = new_nodes.at(src_index);
					char out_id[256];
					oakengine_group_add_input_passthrough(
						reinterpret_cast<OakEngineNode *>(copy),
						reinterpret_cast<OakEngineNode *>(copy_inner),
						inner_input, inner_element, id,
						out_id, sizeof(out_id));
				}

				if (OakEngineNode *output = oakengine_group_get_output_passthrough(
						reinterpret_cast<OakEngineNode *>(og))) {
					int idx = selected.indexOf(reinterpret_cast<Node *>(output));
					if (idx >= 0 && idx < new_nodes.size()) {
						oakengine_group_set_output_passthrough(
							reinterpret_cast<OakEngineNode *>(copy),
							reinterpret_cast<OakEngineNode *>(
								new_nodes.at(idx)));
					}
				}
			}

			oakengine_node_copy_inputs(
				reinterpret_cast<OakEngineNode*>(new_nodes.at(i)),
				reinterpret_cast<const OakEngineNode*>(selected.at(i)));
		}

		// Copy connections
		{
			QVector<OakEngineNode*> sel_arr, new_arr;
			for (Node *n : selected) sel_arr.append(reinterpret_cast<OakEngineNode*>(n));
			for (Node *n : new_nodes) new_arr.append(reinterpret_cast<OakEngineNode*>(n));
			oakengine_node_copy_dependency_graph(
				sel_arr.data(), new_arr.data(), sel_arr.size(), nullptr);
		}

		// Set root level context positions and attach to
		post_paste(new_nodes, map);
	}
}

void NodeView::set_color_label(int index)
{
	void *command = oakengine_undo_command_create_multi();

	for (Node *node : qAsConst(selected_nodes_)) {
		oakengine_undo_command_multi_add_child(
			command,
			oakengine_node_set_color_label_command(
				reinterpret_cast<OakEngineNode *>(node), index));
	}

	oakengine_undo_push(
		command, tr("Set Color of %1 Node(s)").arg(selected_nodes_.size()).toUtf8().constData());
}

void NodeView::zoom_in()
{
	zoom_from_keyboard(1.25);
}

void NodeView::zoom_out()
{
	zoom_from_keyboard(0.8);
}

void NodeView::keyPressEvent(QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down: {
		void *pos_command = oakengine_undo_command_create_multi();
		for (Node *n : qAsConst(selected_nodes_)) {
			for (Node *context : qAsConst(contexts_)) {
				if (context->context_contains_node(n)) {
					Node::Position old_pos =
						context->get_node_position_in_context(n);

					// Determine one pixel in scene units
					double movement_amt = 1.0 / scale_;

					// Translate to 2D movement
					QPointF node_movement;
					switch (event->key()) {
					case Qt::Key_Left:
						node_movement.setX(-movement_amt);
						break;
					case Qt::Key_Right:
						node_movement.setX(movement_amt);
						break;
					case Qt::Key_Up:
						node_movement.setY(-movement_amt);
						break;
					case Qt::Key_Down:
						node_movement.setY(movement_amt);
						break;
					}

					// Translate from screen units into node units
					node_movement = NodeViewItem::screen_to_node_point(
						node_movement, scene_.get_flow_direction());

					// Move command
					Node::Position new_pos = old_pos;
					new_pos.position += node_movement;
					oakengine_undo_command_multi_add_child(
						pos_command,
						oakengine_node_set_position_command(
							reinterpret_cast<void *>(n),
							reinterpret_cast<void *>(context),
							new_pos.position.x(), new_pos.position.y(),
							new_pos.expanded ? 1 : 0));
				}
			}
		}
		oakengine_undo_push(
			pos_command, tr("Moved %1 Node(s)").arg(selected_nodes_.size()).toUtf8().constData());
		break;
	}
	case Qt::Key_Escape:
		if (!attached_items_.isEmpty()) {
			detach_items_from_cursor();
			break;
		}

		emit esc_pressed();

		/* fall through */
	default:
		super::keyPressEvent(event);
		break;
	}
}

void NodeView::mousePressEvent(QMouseEvent *event)
{
	// Handle mouse press event
	if (hand_press(event))
		return;

	// Get the item that the user clicked on, if any
	QGraphicsItem *item = itemAt(event->pos());

	if (event->button() == Qt::LeftButton) {
		// Sane defaults
		create_edge_already_exists_ = false;
		create_edge_from_output_ = true;
		create_edge_input_.reset();

		if (event->modifiers() & Qt::ControlModifier) {
			NodeViewItem *mouse_item = dynamic_cast<NodeViewItem *>(item);

			if (mouse_item) {
				if (mouse_item->is_output_item()) {
					create_edge_output_item_ = mouse_item;
				} else {
					create_edge_input_item_ = mouse_item;
					create_edge_input_ = mouse_item->get_input();
					create_edge_from_output_ = false;
				}

				// Highlight start item for better user experience
				mouse_item->set_highlighted(true);
			}
		}

		if (!create_edge_output_item_ && !create_edge_input_item_) {
			// Determine if user clicked on a connector
			if (NodeViewItemConnector *connector =
					dynamic_cast<NodeViewItemConnector *>(item)) {
				NodeViewItem *attached =
					static_cast<NodeViewItem *>(connector->parentItem());

				if (connector->is_output()) {
					create_edge_output_item_ = attached;
				} else {
					create_edge_input_item_ = attached;

					if (!create_edge_input_item_->edges().isEmpty()) {
						// Drag existing edge instead
						create_edge_ = create_edge_input_item_->edges().first();
						create_edge_input_item_ = nullptr;
						create_edge_output_item_ = create_edge_->from_item();
						create_edge_already_exists_ = true;
					} else {
						create_edge_from_output_ = false;
						create_edge_input_ =
							create_edge_input_item_->get_input();
					}
				}
			}
		}

		if ((create_edge_output_item_ || create_edge_input_item_) &&
			!create_edge_already_exists_) {
			// Create a new edge from this output
			create_edge_ = new NodeViewEdge();
			create_edge_->set_curved(scene_.get_edges_are_curved());

			// Add edge to scene
			scene_.addItem(create_edge_);

			// Position edge to mouse cursor
			position_new_edge(event->pos());
			return;
		}
	}

	// Handle selections with the right mouse button
	if (event->button() == Qt::RightButton) {
		if (!item || !item->isSelected()) {
			// Qt doesn't do this by default for some reason
			if (!(event->modifiers() & Qt::ShiftModifier)) {
				scene_.clearSelection();
			}

			// If there's an item here, select it
			if (item) {
				item->setSelected(true);
			}
		}
	}

	if (attached_items_.isEmpty()) {
		// Default QGraphicsView functionality (selecting, dragging, etc.)
		super::mousePressEvent(event);
	}

	// For any selected item, store its position in case the user is dragging it somewhere else
	auto selected_items = scene_.get_selected_items();
	foreach (NodeViewItem *i, selected_items) {
		// Ignore items attached to the cursor
		if (!is_item_attached_to_cursor(i)) {
			dragging_items_.insert(i, i->get_node_position());
		}
	}
}

void NodeView::mouseMoveEvent(QMouseEvent *event)
{
	if (hand_move(event))
		return;

	if (create_edge_) {
		position_new_edge(event->pos());
		return;
	}

	if (attached_items_.isEmpty()) {
		super::mouseMoveEvent(event);
	}

	// See if there are any items attached
	if (!attached_items_.isEmpty()) {
		process_moving_attached_nodes(event->pos());
	}
}

void NodeView::mouseReleaseEvent(QMouseEvent *event)
{
	if (hand_release(event))
		return;

	if (create_edge_) {
		end_edge_drag();
	}

	void *command = oakengine_undo_command_create_multi();

	Node *select_context = nullptr;
	QVector<Node *> select_nodes;

	bool had_attached_items = !attached_items_.isEmpty();

	if (!attached_items_.isEmpty()) {
		select_context = get_context_at_mouse_pos(event->pos());

		if (select_context) {
			select_nodes = process_dropping_attached_nodes(command, select_context,
														event->pos());
		} else {
			QToolTip::showText(QCursor::pos(),
							   tr("Nodes must be placed inside a context."));
		}
	}

	QList<QPointer<NodeViewItem>> dragged_items;
	for (auto it = dragging_items_.cbegin(); it != dragging_items_.cend();
		 it++) {
		dragged_items.append(it.key());
	}

	foreach (NodeViewItem *i, dragged_items) {
		if (!i) {
			continue;
		}
		QPointF current_pos = i->get_node_position();
		if (dragging_items_.value(i) != current_pos) {
			oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(
				reinterpret_cast<void *>(i->get_node()),
				reinterpret_cast<void *>(i->get_context()), current_pos.x(),
				current_pos.y(), 0));
		}
	}

	oakengine_undo_push(
		command, tr("Moved %1 Node(s)").arg(dragging_items_.size()).toUtf8().constData());

	dragging_items_.clear();

	if (!had_attached_items) {
		super::mouseReleaseEvent(event);
	}

	if (select_context) {
		deselect_all();
		scene_.context_map().value(select_context)->select(select_nodes);
	}
}

void NodeView::mouseDoubleClickEvent(QMouseEvent *event)
{
	super::mouseDoubleClickEvent(event);

	if (!(event->modifiers() & Qt::ControlModifier)) {
		NodeViewItem *item_at_cursor =
			dynamic_cast<NodeViewItem *>(itemAt(event->pos()));
		if (item_at_cursor) {
			item_at_cursor->toggle_expanded();
		}
	}
}

void NodeView::dragEnterEvent(QDragEnterEvent *event)
{
	if (contexts_.empty()) {
		event->ignore();
		return;
	}

	QStringList mime_fmts = event->mimeData()->formats();

	if (mime_fmts.contains(QString::fromUtf8(oakengine_project_item_mime_type()))) {
		QByteArray model_data = event->mimeData()->data(QString::fromUtf8(oakengine_project_item_mime_type()));
		QDataStream stream(&model_data, QIODevice::ReadOnly);

		// Variables to deserialize into
		quintptr item_ptr;
		QVector<Track::Reference> enabled_streams;
		QVector<AttachedItem> new_attached;

		int y = 0;

		while (!stream.atEnd()) {
			stream >> enabled_streams >> item_ptr;

			// Get Item object
			Node *item = reinterpret_cast<Node *>(item_ptr);

			if (ViewerOutput *f = dynamic_cast<ViewerOutput *>(item)) {
				NodeViewItem *new_item;

				new_item = new NodeViewItem(f, nullptr);
				new_item->set_flow_direction(scene_.get_flow_direction());
				new_item->set_node_position(QPointF(0, y));
				y++;
				scene_.addItem(new_item);

				new_attached.append({ new_item, f, new_item->pos() });
			}
		}

		if (new_attached.empty()) {
			event->ignore();
		} else {
			set_attached_items(new_attached);

			event->accept();
		}
	}
}

void NodeView::dragMoveEvent(QDragMoveEvent *event)
{
	if (attached_items_.empty()) {
		event->ignore();
	} else {
		process_moving_attached_nodes(event->pos());

		if (get_context_at_mouse_pos(event->pos())) {
			event->accept();
		} else {
			event->ignore();
		}
	}
}

void NodeView::dropEvent(QDropEvent *event)
{
	if (Node *drop_ctx = get_context_at_mouse_pos(event->pos())) {
		void *command = oakengine_undo_command_create_multi();
		QVector<Node *> select_nodes =
			process_dropping_attached_nodes(command, drop_ctx, event->pos());
		oakengine_undo_push(
			command, tr("Dropped %1 Node(s)").arg(select_nodes.size()).toUtf8().constData());

		deselect_all();
		scene_.context_map().value(drop_ctx)->select(select_nodes);

		event->accept();
	} else {
		detach_items_from_cursor(false);
		event->ignore();
	}
}

void NodeView::dragLeaveEvent(QDragLeaveEvent *event)
{
	if (attached_items_.empty()) {
		event->ignore();
	} else {
		detach_items_from_cursor(false);

		event->accept();
	}
}

void NodeView::resizeEvent(QResizeEvent *event)
{
	super::resizeEvent(event);

	reposition_mini_map();

	if (overlay_view_) {
		resize_overlay();
	}
}

void NodeView::update_selection_cache()
{
	QVector<NodeViewItem *> current_selection = scene_.get_selected_items();

	QVector<Node *> selected;
	QVector<Node *> deselected;

	QVector<Node::ContextPair> sel_with_ctx(current_selection.size());

	// Determine which nodes are newly selected
	for (int j = 0; j < current_selection.size(); j++) {
		NodeViewItem *i = current_selection.at(j);
		Node *n = i->get_node();
		if (!selected_nodes_.contains(n)) {
			selected.append(n);
			selected_nodes_.append(n);
		}

		sel_with_ctx[j] = { n, i->get_context() };
	}

	// Determine which nodes are newly deselected
	if (current_selection.isEmpty()) {
		// All nodes that were selected have been deselected, so we'll just set them all to `deselected`
		deselected = selected_nodes_;
		selected_nodes_.clear();
	} else {
		foreach (Node *n, selected_nodes_) {
			bool still_selected = false;

			foreach (NodeViewItem *i, current_selection) {
				if (i->get_node() == n) {
					still_selected = true;
					break;
				}
			}

			if (!still_selected) {
				deselected.append(n);
				selected_nodes_.removeOne(n);
			}
		}
	}

	if (!deselected.isEmpty()) {
		emit nodes_deselected(node_vector_to_engine(deselected));
	}

	if (!selected.isEmpty()) {
		emit nodes_selected(node_vector_to_engine(selected));
	}

	if (!dont_emit_selection_signals_) {
		emit node_selection_changed(node_vector_to_engine(selected_nodes_));
		emit node_selection_changed_with_contexts(
			context_pair_vector_to_engine(sel_with_ctx));
	}
}

void NodeView::show_context_menu(const QPoint &pos)
{
	if (contexts_.isEmpty()) {
		return;
	}

	Menu m;

	MenuShared::instance()->add_items_for_edit_menu(&m, false);

	m.addSeparator();

	QVector<NodeViewItem *> selected = scene_.get_selected_items();

	NodeViewItem *item_under_cursor = dynamic_cast<NodeViewItem *>(itemAt(pos));

	if (item_under_cursor && !selected.contains(item_under_cursor)) {
		// Right-clicked a node that isn't part of the current selection,
		// make the clicked node the sole selection so context-menu actions
		// operate on it.
		scene_.clearSelection();
		item_under_cursor->setSelected(true);
		selected = scene_.get_selected_items();
	}

	if (item_under_cursor && !selected.isEmpty()) {
		// Grouping
		if (selected.size() == 1 &&
			oakengine_node_is_group(reinterpret_cast<OakEngineNode *>(
				selected.first()->get_node()))) {
			QAction *ungroup_action = m.addAction(tr("Ungroup"));
			connect(ungroup_action, &QAction::triggered, this,
					&NodeView::ungroup_nodes);
		} else {
			QAction *group_action = m.addAction(tr("Group"));
			connect(group_action, &QAction::triggered, this,
					&NodeView::group_nodes);
		}

		// Color menu
		MenuShared::instance()->add_color_coding_menu(&m);

		// Show in Viewer option for nodes based on Viewer
		if (ViewerOutput *viewer =
				dynamic_cast<ViewerOutput *>(selected.first()->get_node())) {
			Q_UNUSED(viewer)
			m.addSeparator();
			QAction *open_in_viewer_action = m.addAction(tr("Open in Viewer"));
			connect(open_in_viewer_action, &QAction::triggered, this,
					&NodeView::open_selected_node_in_viewer);
		}

		m.addSeparator();

		// Show in Parameter Editor
		QAction *show_in_param_editor_action =
			m.addAction(tr("Show in Parameter Editor"));
		show_in_param_editor_action->setShortcut(
			show_in_param_editor_action_->shortcut());
		connect(show_in_param_editor_action, &QAction::triggered, this,
				&NodeView::show_selected_node_in_param_editor);

		// Properties
		QAction *properties_action = m.addAction(tr("P&roperties"));
		connect(properties_action, &QAction::triggered, this,
				&NodeView::show_node_properties);

	} else {
		QAction *curved_action = m.addAction(tr("Smooth Edges"));
		curved_action->setCheckable(true);
		curved_action->setChecked(scene_.get_edges_are_curved());
		connect(curved_action, &QAction::triggered, &scene_,
				&NodeViewScene::set_edges_are_curved);

		m.addSeparator();

		Menu *direction_menu = new Menu(tr("Direction"), &m);
		m.addMenu(direction_menu);

		direction_menu->add_action_with_data(tr("Top to Bottom"),
										  NodeViewCommon::k_top_to_bottom,
										  scene_.get_flow_direction());

		direction_menu->add_action_with_data(tr("Bottom to Top"),
										  NodeViewCommon::k_bottom_to_top,
										  scene_.get_flow_direction());

		direction_menu->add_action_with_data(tr("Left to Right"),
										  NodeViewCommon::k_left_to_right,
										  scene_.get_flow_direction());

		direction_menu->add_action_with_data(tr("Right to Left"),
										  NodeViewCommon::k_right_to_left,
										  scene_.get_flow_direction());

		connect(direction_menu, &Menu::triggered, this,
				&NodeView::context_menu_set_direction);

		m.addSeparator();

		Menu *add_menu = create_add_menu(&m);
		m.addMenu(add_menu);
	}

	m.exec(mapToGlobal(pos));
}

void NodeView::create_node_slot(QAction *action)
{
	Node *new_node = create_node_from_menu_action(action);

	if (new_node) {
		NodeViewItem *new_item = new NodeViewItem(new_node, nullptr);
		new_item->set_flow_direction(scene_.get_flow_direction());
		scene_.addItem(new_item);

		QVector<AttachedItem> new_attached;

		new_attached.append({ new_item, new_node, QPointF(0, 0) });

		if (oakengine_node_is_group(
				reinterpret_cast<OakEngineNode *>(new_node))) {
			for (auto it = new_node->get_context_positions().cbegin();
				 it != new_node->get_context_positions().cend(); it++) {
				new_attached.append({ nullptr, it.key(), QPointF(0, 0) });
			}
		}

		set_attached_items(new_attached);
	}
}

void NodeView::context_menu_set_direction(QAction *action)
{
	set_flow_direction(
		static_cast<NodeViewCommon::FlowDirection>(action->data().toInt()));
}

void NodeView::open_selected_node_in_viewer()
{
	// Find first viewer in list of selected nodes and open it
	foreach (Node *n, selected_nodes_) {
		if (ViewerOutput *viewer = dynamic_cast<ViewerOutput *>(n)) {
			Core::instance()->open_node_in_viewer(
				reinterpret_cast<OakEngineNode *>(viewer));
			break;
		}
	}
}

void NodeView::update_scene_bounding_rect()
{
	// Get current items bounding rect
	QRectF r = scene_.itemsBoundingRect();

	// Adjust so that it fills the view
	r.adjust(-width(), -height(), width(), height());

	// Set it
	scene_.setSceneRect(r);
}

void NodeView::center_on_items_bounding_rect()
{
	centerOn(scene_.itemsBoundingRect().center());
}

void NodeView::center_on_node(OakEngineNode *n)
{
	foreach (NodeViewContext *ctx, scene_.context_map()) {
		if (NodeViewItem *item = ctx->get_item_from_map(
				reinterpret_cast<Node *>(n))) {
			centerOn(item);
			break;
		}
	}
}

void NodeView::reposition_mini_map()
{
	if (minimap_->isVisible()) {
		int margin = fontMetrics().height();

		int w = width() - minimap_->width() - margin;
		int h = height() - minimap_->height() - margin;

		if (verticalScrollBar()->isVisible()) {
			w -= verticalScrollBar()->width();
		}

		if (horizontalScrollBar()->isVisible()) {
			h -= horizontalScrollBar()->height();
		}

		minimap_->move(w, h);

		update_viewport_on_mini_map();
	}
}

void NodeView::update_viewport_on_mini_map()
{
	if (minimap_->isVisible()) {
		minimap_->set_viewport_rect(mapToScene(viewport()->rect()));
	}
}

void NodeView::move_to_scene_point(const QPointF &pos)
{
	centerOn(pos);
}

void NodeView::node_removed_from_graph(OakEngineNode *source)
{
	Node *context = reinterpret_cast<Node *>(source);

	remove_context(context);

	contexts_.removeOne(context);
}

void NodeView::detach_items_from_cursor(bool delete_nodes_too)
{
	foreach (const AttachedItem &ai, attached_items_) {
		delete ai.item;

		if (delete_nodes_too) {
			qDebug() << "deleting" << ai.node;
			delete ai.node;
		}
	}

	attached_items_.clear();
}

void NodeView::set_flow_direction(NodeViewCommon::FlowDirection dir)
{
	scene_.set_flow_direction(dir);
}

void NodeView::move_attached_nodes_to_cursor(const QPoint &p)
{
	QPointF item_pos = mapToScene(p);

	for (const AttachedItem &i : qAsConst(attached_items_)) {
		if (i.item) {
			i.item->setPos(item_pos + i.original_pos);
		}
	}
}

void NodeView::process_moving_attached_nodes(const QPoint &pos)
{
	// Move those items to the cursor
	move_attached_nodes_to_cursor(pos);

	// See if the user clicked on an edge (only when dropping single nodes)
	if (attached_items_.size() == 1) {
		Node *attached_node = attached_items_.first().item->get_node();

		QRect edge_detect_rect(pos, pos);

		int edge_detect_radius = fontMetrics().height();
		edge_detect_rect.adjust(-edge_detect_radius, -edge_detect_radius,
								edge_detect_radius, edge_detect_radius);

		QList<QGraphicsItem *> items = this->items(edge_detect_rect);

		NodeViewEdge *new_drop_edge = nullptr;

		// See if there is an edge here
		for (QGraphicsItem *item : qAsConst(items)) {
			new_drop_edge = dynamic_cast<NodeViewEdge *>(item);

			if (new_drop_edge) {
				drop_input_.reset();

				NodeValue::Type drop_edge_data_type =
					new_drop_edge->input().get_data_type();

				// Determine best input to connect to our new node
				if (attached_node->get_effect_input().is_valid()) {
					// If node specifies an effect input, use that immediately
					drop_input_ = attached_node->get_effect_input();
				} else {
					// Otherwise, we may have to iterate to find a valid one
					for (const QString &input : attached_node->inputs()) {
						if (input == QLatin1String(oakengine_node_enabled_input_id())) {
							// Ignore enabled input
							continue;
						}

						NodeInput i(attached_node, input);

						if (attached_node->is_input_connectable(input)) {
							if (attached_node->get_input_data_type(input) ==
								drop_edge_data_type) {
								// Found exactly the type we're looking for, set and break this loop
								drop_input_ = i;
								break;
							} else if (!drop_input_.is_valid()) {
								// Default to first connectable input
								drop_input_ = i;
							}
						}
					}
				}

				if (attached_node->inputs_from(new_drop_edge->input().node(),
											  true)) {
					drop_input_.reset();
				}

				if (drop_input_.is_valid()) {
					break;
				} else {
					new_drop_edge = nullptr;
				}
			}
		}

		if (drop_edge_ != new_drop_edge) {
			if (drop_edge_) {
				drop_edge_->set_highlighted(false);
			}

			drop_edge_ = new_drop_edge;

			if (drop_edge_) {
				drop_edge_->set_highlighted(true);
			}
		}
	}
}

QVector<Node *>
NodeView::process_dropping_attached_nodes(void *command,
									   Node *select_context, const QPoint &pos)
{
	QVector<Node *> select_nodes;

	// Make a copy
	QVector<AttachedItem> attached = attached_items_;

	for (int i = 0; i < attached.size(); i++) {
		const AttachedItem &ai = attached.at(i);

		if (ai.node->inputs_from(select_context, true)) {
			attached.removeAt(i);
		} else if (select_context->context_contains_node(ai.node)) {
			select_nodes.append(ai.node);
			attached.removeAt(i);
		}
	}

	{
		void *add_command = oakengine_undo_command_create_multi();

		foreach (const AttachedItem &ai, attached) {
			// Add node to the same graph that the context is in
			if (ai.node->parent() != select_context->parent()) {
				oakengine_undo_command_multi_add_child(add_command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(select_context->parent()),
			reinterpret_cast<OakEngineNode *>(ai.node)));
				if (ai.node->is_item() && !ai.node->folder()) {
					oakengine_folder_add_child(
						reinterpret_cast<OakEngineNode *>(select_context->parent()->root()),
						reinterpret_cast<OakEngineNode *>(ai.node));
				}
			}

			// Add node to the context
			if (ai.item) {
				select_nodes.append(ai.node);
				oakengine_undo_command_multi_add_child(add_command, oakengine_node_set_position_command(reinterpret_cast<void *>(ai.node), reinterpret_cast<void *>(select_context), scene_.context_map()
						.value(select_context)
						->map_scene_pos_to_node_pos_in_context(ai.item->pos()).x(), scene_.context_map()
						.value(select_context)
						->map_scene_pos_to_node_pos_in_context(ai.item->pos()).y(), 0));
			}
		}

		if (oakengine_undo_command_multi_child_count(add_command) > 0) {
			oakengine_undo_command_redo_now(add_command);
			oakengine_undo_command_multi_add_child(command, add_command);
		} else {
			oakengine_undo_command_free(add_command);
		}
	}

	{
		// Dropped attached item onto an edge, connect it between them as one
		// undoable child of the parent command.
		if (attached.size() == 1) {
			Node *dropping_node = nullptr;

			foreach (const AttachedItem &ai, attached) {
				if (ai.item && !ai.node->inputs_from(select_context, true)) {
					dropping_node = ai.node;
					break;
				}
			}

			if (dropping_node && drop_edge_) {
				void *edge_command = oakengine_undo_command_create_multi();

				oakengine_undo_command_multi_add_child(
					edge_command,
					oakengine_node_disconnect_command(
						reinterpret_cast<OakEngineNode *>(drop_edge_->input().node()),
						drop_edge_->input().input().toUtf8().constData(),
						drop_edge_->input().element()));
				oakengine_undo_command_multi_add_child(
					edge_command,
					oakengine_node_connect_command(
						reinterpret_cast<OakEngineNode *>(drop_edge_->output()),
						reinterpret_cast<OakEngineNode *>(drop_input_.node()),
						drop_input_.input().toUtf8().constData(),
						drop_input_.element()));
				oakengine_undo_command_multi_add_child(
					edge_command,
					oakengine_node_connect_command(
						reinterpret_cast<OakEngineNode *>(dropping_node),
						reinterpret_cast<OakEngineNode *>(drop_edge_->input().node()),
						drop_edge_->input().input().toUtf8().constData(),
						drop_edge_->input().element()));

				oakengine_undo_command_redo_now(edge_command);
				oakengine_undo_command_multi_add_child(command, edge_command);
			}

			drop_edge_ = nullptr;
		}
	}

	detach_items_from_cursor(false);

	return select_nodes;
}

Node *NodeView::get_context_at_mouse_pos(const QPoint &p)
{
	QList<QGraphicsItem *> items_at_cursor = this->items(p);
	foreach (QGraphicsItem *i, items_at_cursor) {
		if (NodeViewContext *context_item =
				dynamic_cast<NodeViewContext *>(i)) {
			return context_item->get_context();
		}
	}

	return nullptr;
}

void NodeView::connect_selection_changed_signal()
{
	connect(&scene_, &QGraphicsScene::selectionChanged, this,
			&NodeView::update_selection_cache);
}

void NodeView::disconnect_selection_changed_signal()
{
	disconnect(&scene_, &QGraphicsScene::selectionChanged, this,
			   &NodeView::update_selection_cache);
}

void NodeView::zoom_into_cursor_position(QWheelEvent *event, double multiplier,
									  const QPointF &cursor_pos)
{
	Q_UNUSED(event)

	double test_scale = scale_ * multiplier;

	if (test_scale > k_minimum_scale) {
		int anchor_x =
			qRound(double(cursor_pos.x() + horizontalScrollBar()->value()) /
					   scale_ * test_scale -
				   cursor_pos.x());
		int anchor_y =
			qRound(double(cursor_pos.y() + verticalScrollBar()->value()) /
					   scale_ * test_scale -
				   cursor_pos.y());

		scale(multiplier, multiplier);

		this->horizontalScrollBar()->setValue(anchor_x);
		this->verticalScrollBar()->setValue(anchor_y);

		scale_ = test_scale;
	}
}

bool NodeView::event(QEvent *event)
{
	if (event->type() == QEvent::ShortcutOverride) {
		QKeyEvent *se = static_cast<QKeyEvent *>(event);
		if (se->key() == Qt::Key_Left || se->key() == Qt::Key_Right ||
			se->key() == Qt::Key_Up || se->key() == Qt::Key_Down) {
			se->accept();
			return true;
		}
	}

	return super::event(event);
}

bool NodeView::eventFilter(QObject *object, QEvent *event)
{
	return super::eventFilter(object, event);
}

void NodeView::changeEvent(QEvent *e)
{
	// Add translation code

	super::changeEvent(e);
}

void NodeView::zoom_from_keyboard(double multiplier)
{
	QPoint cursor_pos = mapFromGlobal(QCursor::pos());

	// If the cursor is not currently within the widget, zoom into the center
	if (!rect().contains(cursor_pos)) {
		cursor_pos = QPoint(width() / 2, height() / 2);
	}

	zoom_into_cursor_position(nullptr, multiplier, cursor_pos);
}

void NodeView::clear_create_edge_input_if_necessary()
{
	if (create_edge_from_output_ && create_edge_input_.is_valid()) {
		create_edge_input_.reset();
	}
}

QPointF NodeView::get_estimated_position_for_context(NodeViewItem *item,
												 Node *context) const
{
	return item->get_node_position() - context_offsets_.value(context);
}

NodeViewItem *NodeView::get_assumed_item_for_selected_node(Node *node)
{
	// Try to find corresponding selected item
	foreach (NodeViewContext *ctx, scene_.context_map()) {
		NodeViewItem *item = ctx->get_item_from_map(node);
		if (item && item->get_node() == node && item->isSelected()) {
			// Good enough
			return item;
		}
	}

	return nullptr;
}

bool NodeView::get_assumed_position_for_selected_node(Node *node,
												 Node::Position *pos)
{
	if (NodeViewItem *item = get_assumed_item_for_selected_node(node)) {
		*pos = item->get_node_position_data();
		return true;
	} else {
		return false;
	}
}

Menu *NodeView::create_add_menu(Menu *parent)
{
	Menu *add_menu = create_node_menu(parent);
	add_menu->setTitle(tr("Add"));
	connect(add_menu, &Menu::triggered, this, &NodeView::create_node_slot);
	return add_menu;
}

void NodeView::position_new_edge(const QPoint &pos)
{
	// Determine scene coordinate
	QPointF scene_pt = mapToScene(pos);

	// Find if the cursor is currently inside an item
	NodeViewItem *item_at_cursor = dynamic_cast<NodeViewItem *>(itemAt(pos));

	NodeViewItem *source_item = create_edge_from_output_ ?
									create_edge_output_item_ :
									create_edge_input_item_;
	NodeViewItem *&opposing_item = create_edge_from_output_ ?
									   create_edge_input_item_ :
									   create_edge_output_item_;

	// Filter out connecting to self
	if (item_at_cursor && item_at_cursor->get_node() == source_item->get_node()) {
		item_at_cursor = nullptr;
	}

	// Collapse any items that the cursor is no longer inside
	int i = create_edge_expanded_items_.size() - 1;
	for (; i >= 0; i--) {
		NodeViewItem *nvi = create_edge_expanded_items_.at(i);
		QPointF local_pt = nvi->mapFromScene(scene_pt);

		if (nvi->scene() == &scene_ &&
			(nvi->contains(local_pt) ||
			 (!nvi->is_output_item() &&
			  nvi->parentItem()->contains(
				  nvi->parentItem()->mapFromScene(scene_pt)) &&
			  local_pt.y() > nvi->rect().bottom()))) {
			break;
		} else {
			// Collapsing an item will destroy its children, so if the cursor item happens to be a child
			// of the item we're about to collapse, set it to null
			if (item_at_cursor && item_at_cursor->parentItem() == nvi) {
				item_at_cursor = nullptr;
			}

			if (opposing_item && opposing_item->parentItem() == nvi) {
				opposing_item = nullptr;
				clear_create_edge_input_if_necessary();
			}

			collapse_item(nvi);
		}
	}
	create_edge_expanded_items_.resize(i + 1);

	// Expand item if possible
	if (item_at_cursor && item_at_cursor->can_be_expanded() &&
		!item_at_cursor->is_expanded() && create_edge_from_output_) {
		expand_item(item_at_cursor);
		create_edge_expanded_items_.append(item_at_cursor);
	}

	// Filter out connecting to a node that connects to us or an item of the same type
	if (item_at_cursor &&
		((create_edge_from_output_ && source_item->get_node()->inputs_from(
										  item_at_cursor->get_node(), true)) ||
		 (!create_edge_from_output_ && item_at_cursor->get_node()->inputs_from(
										   source_item->get_node(), true)) ||
		 (create_edge_from_output_ == item_at_cursor->is_output_item()))) {
		item_at_cursor = nullptr;
	}

	// Filter out "output node" of the context, we assume users won't want to fetch the output of this
	if (item_at_cursor && !create_edge_from_output_ &&
		item_at_cursor->is_labelled_as_output_of_context()) {
		item_at_cursor = nullptr;
	}

	// If the item has changed
	if (item_at_cursor != opposing_item) {
		// If we had a destination active, disconnect from it since the item has changed
		if (opposing_item) {
			opposing_item->set_highlighted(false);
			opposing_item = nullptr;
		}

		// Clear cached input
		clear_create_edge_input_if_necessary();

		// If this is an input and we're
		opposing_item = item_at_cursor;

		if (opposing_item) {
			opposing_item->set_highlighted(true);
			if (!opposing_item->is_output_item()) {
				create_edge_input_ = opposing_item->get_input();
			}
		}
	}

	QPointF output_point = create_edge_output_item_ ?
							   create_edge_output_item_->get_output_point() :
							   scene_pt;
	QPointF input_point = create_edge_input_.is_valid() ?
							  create_edge_input_item_->get_input_point() :
							  scene_pt;

	create_edge_->set_points(output_point, input_point);
	create_edge_->set_connected(create_edge_output_item_ &&
							   create_edge_input_.is_valid());
}

void NodeView::group_nodes()
{
	// Get items
	QVector<NodeViewItem *> items = scene_.get_selected_items();
	if (items.isEmpty()) {
		return;
	}

	// Get node context
	Node *context = items.first()->get_context();
	QPointF avg_pos = items.first()->get_node_position();
	for (int i = 1; i < items.size(); i++) {
		if (items.at(i)->get_context() != context) {
			QMessageBox::critical(
				this, tr("Failed to group nodes"),
				tr("Nodes can only be grouped if they're in the same context."));
			return;
		}

		avg_pos += items.at(i)->get_node_position();
	}
	avg_pos /= items.size();

	// Create group
	Node *group = reinterpret_cast<Node *>(oakengine_node_group_create());

	// Add group to graph and context
	void *command = oakengine_undo_command_create_multi();

	// Add nodes to group
	Node *output_passthrough = nullptr;
	QVector<Node *> nodes_to_group = selected_nodes_;
	deselect_all();
	foreach (Node *n, nodes_to_group) {
		oakengine_undo_command_multi_add_child(command, oakengine_node_remove_position_command(reinterpret_cast<void *>(n), reinterpret_cast<void *>(context)));
		oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(n), reinterpret_cast<void *>(group), context->get_node_position_data_in_context(n).position.x(), context->get_node_position_data_in_context(n).position.y(), context->get_node_position_data_in_context(n).expanded ? 1 : 0));

		for (auto it = n->inputs().cbegin(); it != n->inputs().cend(); it++) {
			NodeInput input(n, *it, -1);

			if (!input.is_connected() ||
				!nodes_to_group.contains(input.get_connected_output())) {
				oakengine_undo_command_multi_add_child(command, (void *)(oakengine_group_add_input_passthrough_command(
						reinterpret_cast<OakEngineNode *>(group),
						reinterpret_cast<OakEngineNode *>(input.node()),
						input.input().toUtf8().constData(), input.element(),
						nullptr)));
			}
		}

		if (!output_passthrough) {
			// Default to the first node we find that doesn't output to a node inside the group
			output_passthrough = nodes_to_group.first();
			foreach (Node *potential_in, nodes_to_group) {
				if (potential_in != n && !potential_in->inputs_from(n, false)) {
					output_passthrough = n;
					break;
				}
			}
		}
	}

	// Set output passthrough
	oakengine_undo_command_multi_add_child(command, (void *)(oakengine_group_set_output_passthrough_command(
			reinterpret_cast<OakEngineNode *>(group),
			reinterpret_cast<OakEngineNode *>(output_passthrough))));

	// Add group to graph
	oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(context->parent()),
			reinterpret_cast<OakEngineNode *>(group)));
	oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(group), reinterpret_cast<void *>(context), avg_pos.x(), avg_pos.y(), 0));

	// Do command
	Core::instance()->label_nodes(
		QVector<OakEngineNode *>{ reinterpret_cast<OakEngineNode *>(group) },
		command);

	oakengine_undo_push(command, tr("Grouped Nodes").toUtf8().constData());
}

void NodeView::ungroup_nodes()
{
	NodeViewItem *group_item = nullptr;
	QVector<NodeViewItem *> items = scene_.get_selected_items();
	if (items.isEmpty()) {
		return;
	}

	Node *group = nullptr;
	foreach (NodeViewItem *i, items) {
		if (oakengine_node_is_group(reinterpret_cast<OakEngineNode *>(
				i->get_node()))) {
			group = i->get_node();
			group_item = i;
			break;
		}
	}

	if (!group_item) {
		return;
	}

	void *command = oakengine_undo_command_create_multi();

	Node *context = group_item->get_context();

	oakengine_undo_command_multi_add_child(command, oakengine_node_remove_position_command(reinterpret_cast<void *>(group), reinterpret_cast<void *>(context)));
	oakengine_undo_command_multi_add_child(command, oakengine_node_remove_and_disconnect_command(reinterpret_cast<void *>(group)));

	for (auto it = group->get_context_positions().cbegin();
		 it != group->get_context_positions().cend(); it++) {
		oakengine_undo_command_multi_add_child(command, oakengine_node_remove_position_command(reinterpret_cast<void *>(it.key()), reinterpret_cast<void *>(group)));
		oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(it.key()), reinterpret_cast<void *>(context), group->get_node_position_data_in_context(it.key()).position.x(), group->get_node_position_data_in_context(it.key()).position.y(), group->get_node_position_data_in_context(it.key()).expanded ? 1 : 0));
	}

	oakengine_undo_push(command, tr("Ungrouped Nodes").toUtf8().constData());
}

void NodeView::show_node_properties()
{
	Node *first_node = selected_nodes_.first();

	if (oakengine_node_is_group(reinterpret_cast<OakEngineNode *>(first_node))) {
		if (!overlay_view_) {
			overlay_view_ = new NodeView(this);
			overlay_view_->show();

			QPushButton *overlay_close_btn = new QPushButton(overlay_view_);
			overlay_close_btn->setIcon(icon::error);
			int offset = overlay_close_btn->sizeHint().width() / 2;
			overlay_close_btn->move(offset, offset);
			overlay_close_btn->show();

			connect(overlay_view_, &NodeView::nodes_selected, this,
					&NodeView::nodes_selected);
			connect(overlay_view_, &NodeView::nodes_deselected, this,
					&NodeView::nodes_deselected);
			connect(overlay_view_, &NodeView::node_group_opened, this,
					&NodeView::node_group_opened);
			connect(overlay_view_, &NodeView::node_group_closed, this,
					&NodeView::node_group_closed);
			connect(overlay_view_, &NodeView::esc_pressed, this,
					&NodeView::close_overlay);
			connect(overlay_close_btn, &QPushButton::clicked, this,
					&NodeView::close_overlay);

			const QColor &bgcol = overlay_view_->palette().base().color();
			overlay_view_->setStyleSheet(
				QStringLiteral(
					"QGraphicsView { background: rgba(%1, %2, %3, 0.8); }")
					.arg(QString::number(bgcol.red()),
						 QString::number(bgcol.green()),
						 QString::number(bgcol.blue())));

			overlay_close_btn->setStyleSheet(
				QStringLiteral("background: transparent; border: none;"));
		}
		overlay_view_->set_contexts({ first_node });
		resize_overlay();
		QMetaObject::invokeMethod(overlay_view_,
								  &NodeView::center_on_items_bounding_rect,
								  Qt::QueuedConnection);
		overlay_view_->setFocus();

		emit nodes_deselected(node_vector_to_engine(selected_nodes_));
		emit node_selection_changed(QVector<OakEngineNode *>());
		emit node_selection_changed_with_contexts(
			QVector<QPair<OakEngineNode *, OakEngineNode *>>());
		overlay_view_->select_all();

		emit node_group_opened(reinterpret_cast<OakEngineNode *>(first_node));
	} else {
		label_selected_nodes();
	}
}

void NodeView::show_selected_node_in_param_editor()
{
	QVector<NodeViewItem *> selected = scene_.get_selected_items();
	if (selected.isEmpty()) {
		return;
	}

	QVector<Node::ContextPair> selection_with_contexts;
	selection_with_contexts.reserve(selected.size());
	foreach (NodeViewItem *item, selected) {
		if (item && item->get_node()) {
			selection_with_contexts.append(
				Node::ContextPair{ item->get_node(), item->get_context() });
		}
	}

	if (selection_with_contexts.isEmpty()) {
		return;
	}

	if (PanelManager::instance()) {
		if (PanelWidget *panel = PanelManager::instance()->get_panel_with_name(
				QStringLiteral("ParamPanel"))) {
			panel->show();
			QMetaObject::invokeMethod(panel, &PanelWidget::raise,
									  Qt::QueuedConnection);
			QMetaObject::invokeMethod(
				panel,
				[panel]() {
					panel->activateWindow();
					panel->setFocus(Qt::OtherFocusReason);
				},
				Qt::QueuedConnection);
		}
	}

	emit node_selection_changed_with_contexts(
		context_pair_vector_to_engine(selection_with_contexts));
}

void NodeView::label_selected_nodes()
{
	Core::instance()->label_nodes(
		reinterpret_cast<const QVector<OakEngineNode *> &>(selected_nodes_));
}

void NodeView::item_about_to_be_deleted(NodeViewItem *item)
{
	dragging_items_.remove(item);

	if (create_edge_) {
		// Item should be removed from scene, but not yet deleted, allowing a safe PositionNewEdge call
		// to disconnect
		position_new_edge(mapFromGlobal(QCursor::pos()));

		QGraphicsItem *test = item;
		do {
			if (test == item) {
				break;
			}

			test = test->parentItem();
		} while (test);

		if (test == item) {
			// Cancel edge function
			end_edge_drag(true);
		}
	}
}

void NodeView::close_overlay()
{
	if (overlay_view_->overlay_view_) {
		overlay_view_->close_overlay();
	}

	overlay_view_->deleteLater();
	overlay_view_ = nullptr;
	emit node_group_closed();
}

void NodeView::add_context(Node *n)
{
	NodeViewContext *ctx = scene_.add_context(n);

	connect(ctx, &NodeViewContext::item_about_to_be_deleted, this,
			&NodeView::item_about_to_be_deleted);

	removed_from_graph_subs_[n] = bridge_->subscribe(
		reinterpret_cast<void *>(n), OAKENGINE_EVENT_NODE_REMOVED_FROM_GRAPH);
}

void NodeView::remove_context(Node *n)
{
	scene_.remove_context(n);
	bridge_->unsubscribe(removed_from_graph_subs_.take(n));
}

bool NodeView::is_item_attached_to_cursor(NodeViewItem *item) const
{
	foreach (const AttachedItem &ai, attached_items_) {
		if (ai.item == item) {
			return true;
		}
	}

	return false;
}

void NodeView::expand_item(NodeViewItem *item)
{
	item->set_expanded(true);
	item->setZValue(100);
}

void NodeView::collapse_item(NodeViewItem *item)
{
	item->set_expanded(false);
	item->setZValue(0);
}

void NodeView::end_edge_drag(bool cancel)
{
	// Check if the edge was reconnected to the same place as before
	void *command = oakengine_undo_command_create_multi();

	bool reconnected_to_itself = false;

	if (create_edge_already_exists_) {
		if (!cancel) {
			if (create_edge_output_item_ == create_edge_->from_item() &&
				create_edge_->input() == create_edge_input_) {
				reconnected_to_itself = true;
			} else {
				// We are moving (or removing) an existing edge
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_node_disconnect_command(
						reinterpret_cast<OakEngineNode *>(create_edge_->input().node()),
						create_edge_->input().input().toUtf8().constData(),
						create_edge_->input().element()));
			}
		}
	} else {
		// We're creating a new edge, which means this UI object is only temporary
		delete create_edge_;
	}

	create_edge_ = nullptr;

	// Clear highlight if we set one
	if (create_edge_output_item_) {
		create_edge_output_item_->set_highlighted(false);
	}
	if (create_edge_input_item_) {
		create_edge_input_item_->set_highlighted(false);
	}

	QString command_name;

	NodeInput &creating_input = create_edge_input_;
	if (create_edge_output_item_ && create_edge_input_item_ && !cancel) {
		if (creating_input.is_valid()) {
			// Make connection
			if (!reconnected_to_itself) {
				Node *creating_output = create_edge_output_item_->get_node();

				while (oakengine_node_is_group(
					   reinterpret_cast<OakEngineNode *>(creating_output))) {
					OakEngineNode *out = oakengine_group_get_output_passthrough(
						reinterpret_cast<OakEngineNode *>(creating_output));
					if (!out) {
						break;
					}
					creating_output = reinterpret_cast<Node *>(out);
				}

				while (oakengine_node_is_group(reinterpret_cast<OakEngineNode *>(
						creating_input.node()))) {
					OakEngineNode *inner_node = nullptr;
					char inner_input[256];
					int inner_element = 0;
					if (oakengine_group_get_passthrough_from_id(
							reinterpret_cast<OakEngineNode *>(
								creating_input.node()),
							creating_input.input().toUtf8().constData(),
							&inner_node, inner_input, sizeof(inner_input),
							&inner_element) != OAKENGINE_OK) {
						break;
					}
					creating_input = NodeInput(
						reinterpret_cast<Node *>(inner_node),
						QString::fromUtf8(inner_input), inner_element);
				}

				if (creating_input.is_connected()) {
					Node::OutputConnection existing_edge_to_remove = {
						creating_input.get_connected_output(), creating_input
					};

					Node *already_connected_output =
						creating_input.get_connected_output();
					NodeViewContext *ctx =
						get_context_item_from_node_item(create_edge_input_item_);
					if (ctx && !ctx->get_item_from_map(already_connected_output)) {
						if (QMessageBox::warning(
								this, QString(),
								tr("Input \"%1\" is currently connected to node \"%2\", which is not visible in this context. "
								   "By connecting this, that connection will be removed. Do you wish to continue?")
									.arg(creating_input.name(),
										 already_connected_output
											 ->get_label_and_name()),
								QMessageBox::Yes | QMessageBox::No) ==
							QMessageBox::No) {
							cancel = true;
						}
					}

					if (!cancel) {
						oakengine_undo_command_multi_add_child(
							command,
							oakengine_node_disconnect_command(
								reinterpret_cast<OakEngineNode *>(existing_edge_to_remove.second.node()),
								existing_edge_to_remove.second.input().toUtf8().constData(),
								existing_edge_to_remove.second.element()));
					}
				}

				if (!cancel) {
					oakengine_undo_command_multi_add_child(
						command,
						oakengine_node_connect_command(
							reinterpret_cast<OakEngineNode *>(creating_output),
							reinterpret_cast<OakEngineNode *>(creating_input.node()),
							creating_input.input().toUtf8().constData(),
							creating_input.element()));

					{
						char cmd_buf[256];
						oakengine_node_connect_command_string(
							reinterpret_cast<OakEngineNode*>(creating_output),
							reinterpret_cast<OakEngineNode*>(creating_input.node()),
							creating_input.input().toUtf8().constData(),
							creating_input.element(), cmd_buf, sizeof(cmd_buf));
						command_name = QString::fromUtf8(cmd_buf);
					}

					// If the output is not in the input's context, add it now. We check the item rather than
					// the node itself, because sometimes a node may not be in the context but another node
					// representing it will be (e.g. groups)
						if (!scene_.context_map()
								 .value(create_edge_input_item_->get_context())
								 ->get_item_from_map(creating_output)) {
							QPointF new_pos = scene_.context_map()
											.value(create_edge_input_item_->get_context())
											->map_scene_pos_to_node_pos_in_context(
													create_edge_output_item_->scenePos());
							oakengine_undo_command_multi_add_child(
								command,
								oakengine_node_set_position_command(
									reinterpret_cast<void *>(creating_output),
									reinterpret_cast<void *>(
										create_edge_input_item_->get_context()),
									new_pos.x(), new_pos.y(), 0));
						}
				}
			}
		}
	}

	creating_input.reset();
	create_edge_output_item_ = nullptr;
	create_edge_input_item_ = nullptr;

	// Collapse any items we expanded
	for (auto it = create_edge_expanded_items_.crbegin();
		 it != create_edge_expanded_items_.crend(); it++) {
		collapse_item(*it);
	}
	create_edge_expanded_items_.clear();

	oakengine_undo_push(command, command_name.toUtf8().constData());
}


void NodeView::post_paste(const QVector<Node *> &new_nodes,
						 const Node::PositionMap &map)
{
	QVector<AttachedItem> new_attached;

	NodeViewItem *first_item = nullptr;

	for (int i = 0; i < new_nodes.size(); i++) {
		Node *node = new_nodes.at(i);

		// Determine if item had a position, if not don't create an item for it
		NodeViewItem *new_item;

		if (map.contains(node)) {
			new_item = new NodeViewItem(node, nullptr);
			new_item->set_flow_direction(scene_.get_flow_direction());
			new_item->set_node_position(map.value(node));
			scene_.addItem(new_item);

			if (!first_item) {
				first_item = new_item;
			}
		} else {
			new_item = nullptr;
		}

		new_attached.append({ new_item, node, QPointF(0, 0) });
	}

	// Correct positions
	if (first_item) {
		for (int i = 0; i < new_attached.size(); i++) {
			AttachedItem &ai = new_attached[i];

			if (ai.item) {
				ai.original_pos = ai.item->pos() - first_item->pos();
			}
		}
	}

	set_attached_items(new_attached);
}

void NodeView::resize_overlay()
{
	overlay_view_->resize(this->size());
}

NodeViewContext *NodeView::get_context_item_from_node_item(NodeViewItem *item)
{
	QGraphicsItem *i = item;
	while ((i = i->parentItem())) {
		if (NodeViewContext *nvc = dynamic_cast<NodeViewContext *>(i)) {
			return nvc;
		}
	}
	return nullptr;
}

void NodeView::set_attached_items(const QVector<AttachedItem> &items)
{
	// Detach anything currently attached
	detach_items_from_cursor();

	attached_items_ = items;

	// Move to cursor
	move_attached_nodes_to_cursor(mapFromGlobal(QCursor::pos()));
}

}
