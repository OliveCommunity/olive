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
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include "core.h"
#include "node/block/block.h"
#include "node/group/group.h"
#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "nodeviewitem.h"
#include "ui/colorcoding.h"

namespace olive
{

#define super QGraphicsRectItem

NodeViewContext::NodeViewContext(Node *context, QGraphicsItem *item)
	: super(item)
	, context_(context)
{
	Block *block = dynamic_cast<Block *>(context_);
	if (block && block->track() && block->track()->sequence()) {
		Rational timebase = block->track()
								->sequence()
								->get_video_params()
								.frame_rate_as_time_base();
		lbl_ =
			QCoreApplication::translate("NodeViewContext", "%1 [%2] :: %3 - %4")
				.arg(block->get_label_and_name(),
					 Track::Reference::type_to_translated_string(
						 block->track()->type()),
					 QString::fromStdString(Timecode::time_to_timecode(
						 block->in(), timebase,
						 Core::instance()->get_timecode_display())),
					 QString::fromStdString(Timecode::time_to_timecode(
						 block->out(), timebase,
						 Core::instance()->get_timecode_display())));
	} else {
		lbl_ = context_->get_label_and_name();
	}

	const Node::PositionMap &map = context_->get_context_positions();
	for (auto it = map.cbegin(); it != map.cend(); it++) {
		add_child(it.key());
	}

	connect(context_, &Node::node_added_to_context, this,
			&NodeViewContext::add_child, Qt::DirectConnection);
	connect(context_, &Node::node_position_in_context_changed, this,
			&NodeViewContext::set_child_position, Qt::DirectConnection);
	connect(context_, &Node::node_removed_from_context, this,
			&NodeViewContext::remove_child, Qt::DirectConnection);
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

	if (NodeGroup *group = dynamic_cast<NodeGroup *>(node)) {
		for (auto it = group->get_context_positions().cbegin();
			 it != group->get_context_positions().cend(); it++) {
			// Use this item as the representative for all of these nodes too
			add_node_internal(it.key(), item);
		}

		connect(group, &NodeGroup::node_added_to_context, this,
				&NodeViewContext::group_added_node);
		connect(group, &NodeGroup::node_removed_from_context, this,
				&NodeViewContext::group_removed_node);
	}

	update_rect();
}

void NodeViewContext::set_child_position(Node *node, const QPointF &pos)
{
	item_map_.value(node)->set_node_position(pos);
}

void NodeViewContext::remove_child(Node *node)
{
	disconnect(node, &Node::input_connected, this,
			   &NodeViewContext::child_input_connected);
	disconnect(node, &Node::input_disconnected, this,
			   &NodeViewContext::child_input_disconnected);

	if (NodeGroup *group = dynamic_cast<NodeGroup *>(node)) {
		disconnect(group, &NodeGroup::node_added_to_context, this,
				   &NodeViewContext::group_added_node);
		disconnect(group, &NodeGroup::node_removed_from_context, this,
				   &NodeViewContext::group_removed_node);
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
		if (dynamic_cast<NodeGroup *>(item->get_node())) {
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

int NodeViewContext::delete_selected(NodeViewDeleteCommand *command)
{
	int count = 0;

	// Delete any selected edges
	foreach (NodeViewEdge *edge, edges_) {
		if (edge->isSelected()) {
			command->add_edge(edge->output(), edge->input());
		}
	}

	// Delete any selected nodes
	foreach (NodeViewItem *node, item_map_) {
		if (node->isSelected()) {
			command->add_node(node->get_node(), context_);
			count++;
		}
	}

	return count;
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
	connect(node, &Node::input_connected, this,
			&NodeViewContext::child_input_connected);
	connect(node, &Node::input_disconnected, this,
			&NodeViewContext::child_input_disconnected);

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

void NodeViewContext::group_added_node(Node *node)
{
	NodeGroup *group = static_cast<NodeGroup *>(sender());

	add_node_internal(node, item_map_.value(group));
}

void NodeViewContext::group_removed_node(Node *node)
{
	NodeGroup *group = static_cast<NodeGroup *>(sender());

	if (item_map_.value(node) == item_map_.value(group)) {
		item_map_.remove(node);
	}
}

}
