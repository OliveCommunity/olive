/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "nodeviewcontext.h"

#include <QBrush>

#include "oakengine/node.h"
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include "core.h"
#include "node/block/block.h"
#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "nodeviewitem.h"
#include "ui/colorcoding.h"

#include "widget/viewer/vieweroutpututils.h"
namespace olive
{

#define super QGraphicsRectItem

NodeViewContext::NodeViewContext(Node *context, QGraphicsItem *item)
	: super(item)
	, context_(context)
	, bridge_(new EngineEventBridge(this))
{
	Block *block = dynamic_cast<Block *>(context_);
	if (block && block->track() && block->track()->sequence()) {
		Rational timebase = viewer_output_video_params(block->track()->sequence())
								.frame_rate_as_time_base();
		QString type_label;
		switch (block->track()->type()) {
		case Track::k_video:
			type_label = QCoreApplication::translate("NodeViewContext", "V");
			break;
		case Track::k_audio:
			type_label = QCoreApplication::translate("NodeViewContext", "A");
			break;
		case Track::k_subtitle:
			type_label = QCoreApplication::translate("NodeViewContext", "S");
			break;
		default:
			break;
		}

		lbl_ =
			QCoreApplication::translate("NodeViewContext", "%1 [%2] :: %3 - %4")
				.arg(block->get_label_and_name(),
					 type_label,
					 QString::fromStdString(Timecode::time_to_timecode(
						 block->in(), timebase,
						 Core::instance()->get_timecode_display())),
					 QString::fromStdString(Timecode::time_to_timecode(
						 block->out(), timebase,
						 Core::instance()->get_timecode_display())));
	} else {
		lbl_ = context_->get_label_and_name();
	}

	connect(bridge_, &EngineEventBridge::node_node_added_to_context, this,
			[this](OakEngineNode *source, OakEngineNode *node) {
				Node *src = reinterpret_cast<Node *>(source);
				if (src == context_) {
					add_child(reinterpret_cast<Node *>(node));
				} else {
					group_added_node(reinterpret_cast<Node *>(node), src);
				}
			});
	connect(bridge_, &EngineEventBridge::node_node_removed_from_context, this,
			[this](OakEngineNode *source, OakEngineNode *node) {
				Node *src = reinterpret_cast<Node *>(source);
				if (src == context_) {
					remove_child(reinterpret_cast<Node *>(node));
				} else {
					group_removed_node(reinterpret_cast<Node *>(node), src);
				}
			});
	connect(bridge_, &EngineEventBridge::node_context_position_changed, this,
			[this](OakEngineNode *, OakEngineNode *node, double x, double y) {
				set_child_position(reinterpret_cast<Node *>(node),
								   QPointF(x, y));
			});
	connect(bridge_, &EngineEventBridge::node_input_connected, this,
			[this](OakEngineNode *source, OakEngineNode *output,
				   const QString &input, int element) {
				child_input_connected(reinterpret_cast<Node *>(output),
					NodeInput(reinterpret_cast<Node *>(source), input,
							  element));
			});
	connect(bridge_, &EngineEventBridge::node_input_disconnected, this,
			[this](OakEngineNode *source, OakEngineNode *output,
				   const QString &input, int element) {
				child_input_disconnected(reinterpret_cast<Node *>(output),
					NodeInput(reinterpret_cast<Node *>(source), input,
							  element));
			});

	node_subs_[context_].append(bridge_->subscribe(
		reinterpret_cast<void *>(context_),
		OAKENGINE_EVENT_NODE_NODE_ADDED_TO_CONTEXT));
	node_subs_[context_].append(bridge_->subscribe(
		reinterpret_cast<void *>(context_),
		OAKENGINE_EVENT_NODE_CONTEXT_POSITION_CHANGED));
	node_subs_[context_].append(bridge_->subscribe(
		reinterpret_cast<void *>(context_),
		OAKENGINE_EVENT_NODE_NODE_REMOVED_FROM_CONTEXT));

	const Node::PositionMap &map = context_->get_context_positions();
	for (auto it = map.cbegin(); it != map.cend(); it++) {
		add_child(it.key());
	}
}

NodeViewContext::~NodeViewContext()
{
	// Delete edges before items, because the edge constructor references the items
	qDeleteAll(edges_);
	edges_.clear();
}

void NodeViewContext::add_child(Node *node)
{
	if (!context_) {
		return;
	}

	NodeViewItem *item = new NodeViewItem(node, context_, this);
	item->set_flow_direction(flow_dir_);

	add_node_internal(node, item);

	if (oakengine_node_is_group(reinterpret_cast<OakEngineNode *>(node))) {
		for (auto it = node->get_context_positions().cbegin();
			 it != node->get_context_positions().cend(); it++) {
			// Use this item as the representative for all of these nodes too
			add_node_internal(it.key(), item);
		}

		node_subs_[node].append(bridge_->subscribe(
			reinterpret_cast<void *>(node),
			OAKENGINE_EVENT_NODE_NODE_ADDED_TO_CONTEXT));
		node_subs_[node].append(bridge_->subscribe(
			reinterpret_cast<void *>(node),
			OAKENGINE_EVENT_NODE_NODE_REMOVED_FROM_CONTEXT));
	}

	update_rect();
}

void NodeViewContext::set_child_position(Node *node, const QPointF &pos)
{
	item_map_.value(node)->set_node_position(pos);
}

void NodeViewContext::remove_child(Node *node)
{
	foreach (int64_t id, node_subs_.take(node)) {
		bridge_->unsubscribe(id);
	}

	NodeViewItem *item = item_map_.take(node);

	// Remove from scene before emitting signal so that any drag functions that might be happening
	// now can be handled before the item is destroyed
	scene()->removeItem(item);

	emit item_about_to_be_deleted(item);

	// Delete edges first because the edge destructor will try to reference item (maybe that should
	// be changed...)
	QVector<NodeViewEdge *> edges_to_remove = item->get_all_edges_recursively();
	foreach (NodeViewEdge *edge, edges_to_remove) {
		if (node == item->get_node() || edge->output() == node ||
			edge->input().node() == node) {
			child_input_disconnected(edge->output(), edge->input());
		}
	}

	// Check if this item is specifically for this node and the node is a group. If so, remove it for
	// all other entries in the map.
	if (item->get_node() == node) {
		if (oakengine_node_is_group(reinterpret_cast<OakEngineNode *>(
				item->get_node()))) {
			for (auto it = item_map_.begin(); it != item_map_.end();) {
				if (it.value() == item) {
					it = item_map_.erase(it);
				} else {
					it++;
				}
			}
		}

		delete item;
	}

	update_rect();
}

void NodeViewContext::child_input_connected(Node *output, const NodeInput &input)
{
	// Add edge
	if (!input.is_hidden()) {
		if (NodeViewItem *output_item = item_map_.value(output)) {
			add_edge_internal(
				output, input, output_item,
				item_map_.value(input.node())->get_item_for_input(input));
		}
	}
}

bool NodeViewContext::child_input_disconnected(Node *output,
											 const NodeInput &input)
{
	// Remove edge
	for (int i = 0; i < edges_.size(); i++) {
		NodeViewEdge *e = edges_.at(i);
		if (e->output() == output && e->input() == input) {
			delete e;
			edges_.removeAt(i);
			return true;
		}
	}

	return false;
}

qreal get_text_offset(const QFontMetricsF &fm)
{
	return fm.height() / 2;
}

void NodeViewContext::update_rect()
{
	QFont f;
	QFontMetricsF fm(f);
	qreal lbl_offset = get_text_offset(fm);

	QRectF cbr = childrenBoundingRect();
	QRectF rect = cbr;
	int pad = NodeViewItem::default_item_height();
	rect.adjust(-pad, -lbl_offset * 2 - fm.height() - pad, pad, pad);
	setRect(rect);

	last_titlebar_height_ = rect.y() + (cbr.y() - rect.y()) - pad;
}

void NodeViewContext::set_flow_direction(NodeViewCommon::FlowDirection dir)
{
	flow_dir_ = dir;

	foreach (NodeViewItem *item, item_map_) {
		item->set_flow_direction(dir);
	}
}

void NodeViewContext::set_curved_edges(bool e)
{
	curved_edges_ = e;

	foreach (NodeViewEdge *edge, edges_) {
		edge->set_curved(e);
	}
}

void NodeViewContext::get_selected_for_deletion(QVector<Node *> &nodes,
											   QVector<Node *> &contexts,
											   QVector<NodeViewEdge *> &edges) const
{
	// Collect any selected edges
	foreach (NodeViewEdge *edge, edges_) {
		if (edge->isSelected()) {
			edges.append(edge);
		}
	}

	// Collect any selected nodes
	foreach (NodeViewItem *node, item_map_) {
		if (node->isSelected()) {
			nodes.append(node->get_node());
			contexts.append(context_);
		}
	}
}

void NodeViewContext::select(const QVector<Node *> &nodes)
{
	foreach (Node *n, nodes) {
		if (NodeViewItem *item = item_map_.value(n)) {
			item->setSelected(true);
		}
	}
}

QVector<NodeViewItem *> NodeViewContext::get_selected_items() const
{
	QVector<NodeViewItem *> items;

	for (auto it = item_map_.cbegin(); it != item_map_.cend(); it++) {
		if (it.value()->isSelected()) {
			if (!items.contains(it.value())) {
				items.append(it.value());
			}
		}
	}

	return items;
}

QPointF NodeViewContext::map_scene_pos_to_node_pos_in_context(const QPointF &pos) const
{
	for (auto it = item_map_.cbegin(); it != item_map_.cend(); it++) {
		QPointF pos_inside_parent =
			it.value()->mapToParent(it.value()->mapFromScene(pos));
		return NodeViewItem::screen_to_node_point(pos_inside_parent, flow_dir_);
	}
	return QPointF(0, 0);
}

void NodeViewContext::paint(QPainter *painter,
							const QStyleOptionGraphicsItem *option,
							QWidget *widget)
{
	// Set pen and brush
	Color color = context_->color();
	QColor c = QtUtils::to_q_color(color);
	QPen pen(c, 2);
	if (option->state & QStyle::State_Selected) {
		pen.setStyle(Qt::DotLine);
	}
	painter->setPen(pen);

	QColor bg = c;
	bg.setAlpha(128);
	painter->setBrush(bg);

	// Draw semi-transparent rect for whole item
	int rounded = painter->fontMetrics().height();
	painter->drawRoundedRect(rect(), rounded, rounded);

	// Draw solid background for titlebar
	QRectF titlebar_rect = rect();
	titlebar_rect.setHeight(last_titlebar_height_ - rect().top());
	painter->setClipRect(titlebar_rect);
	painter->setBrush(c);
	painter->drawRoundedRect(rect(), rounded, rounded);
	painter->setClipping(false);

	// Draw titlebar text
	painter->setPen(ColorCoding::get_ui_selector_color(color));

	int offset = get_text_offset(painter->fontMetrics());

	QRectF text_rect = rect();
	text_rect.adjust(offset, offset, -offset, -offset);
	painter->drawText(text_rect, lbl_);
}

QVariant NodeViewContext::itemChange(GraphicsItemChange change,
									 const QVariant &value)
{
	return super::itemChange(change, value);
}

void NodeViewContext::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
	bool clicked_inside_titlebar = (event->pos().y() < last_titlebar_height_);

	setFlag(ItemIsMovable, clicked_inside_titlebar);
	setFlag(ItemIsSelectable, clicked_inside_titlebar);

	super::mousePressEvent(event);
}

void NodeViewContext::add_node_internal(Node *node, NodeViewItem *item)
{
	node_subs_[node].append(bridge_->subscribe(
		reinterpret_cast<void *>(node),
		OAKENGINE_EVENT_NODE_INPUT_CONNECTED));
	node_subs_[node].append(bridge_->subscribe(
		reinterpret_cast<void *>(node),
		OAKENGINE_EVENT_NODE_INPUT_DISCONNECTED));

	item_map_.insert(node, item);

	if (node == context_) {
		item->set_label_as_output(true);
	}

	for (auto it = node->output_connections().cbegin();
		 it != node->output_connections().cend(); it++) {
		if (!it->second.is_hidden()) {
			if (NodeViewItem *other_item = item_map_.value(it->second.node())) {
				add_edge_internal(node, it->second, item,
								other_item->get_item_for_input(it->second));
			}
		}
	}

	for (auto it = node->input_connections().cbegin();
		 it != node->input_connections().cend(); it++) {
		if (!it->first.is_hidden()) {
			if (NodeViewItem *other_item = item_map_.value(it->second)) {
				add_edge_internal(it->second, it->first, other_item,
								item->get_item_for_input(it->first));
			}
		}
	}
}

void NodeViewContext::add_edge_internal(Node *output, const NodeInput &input,
									  NodeViewItem *from, NodeViewItem *to)
{
	if (from == to) {
		return;
	}

	NodeViewEdge *edge_ui = new NodeViewEdge(output, input, from, to, this);

	edge_ui->adjust();
	edge_ui->set_curved(curved_edges_);

	edges_.append(edge_ui);
}

void NodeViewContext::group_added_node(Node *node, Node *group)
{
	add_node_internal(node, item_map_.value(group));
}

void NodeViewContext::group_removed_node(Node *node, Node *group)
{
	if (item_map_.value(node) == item_map_.value(group)) {
		item_map_.remove(node);
	}
}

}
