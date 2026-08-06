/*
 * Oak Video Editor - Non‑Linear Video Editor
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
 * along with this program.  If not, see <http://www.gnu.org/licenses />.
 */
#include "nodeviewcontext.h"
#include <QBrush>
#include "oakengine/node.h"
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include "core.h"
#include "common/trackreferencehandle.h"
#include "nodeviewitem.h"
#include "common/colorcodingapp.h"
#include "oakengine/timeline.h"
#include "oakutil/qtutils.h"
#include "widget/timelinewidget/cliphandle.h"
#include "widget/viewer/vieweroutpututils.h"

namespace olive
{

#define super QGraphicsRectItem

NodeViewContext::NodeViewContext(oak::Node context, QGraphicsItem *item)
	: super(item)
	, context_(context)
{
	OakEngineNode *track_node =
		oakengine_node_is_block(context_.handle()) ?
			block_track_handle(
				reinterpret_cast<OakEngineBlock *>(context_.handle())) :
			nullptr;
	OakEngineNode *track_seq =
		track_node ? oakengine_track_get_sequence(track_node) : nullptr;

	if (track_seq) {
		Rational timebase = viewer_output_video_params(track_seq)
								.frame_rate_as_time_base();
		QString type_label;
		switch (oakengine_track_get_type(track_node)) {
		case TrackReference::k_video:
			type_label = QCoreApplication::translate("NodeViewContext", "V");
			break;
		case TrackReference::k_audio:
			type_label = QCoreApplication::translate("NodeViewContext", "A");
			break;
		case TrackReference::k_subtitle:
			type_label = QCoreApplication::translate("NodeViewContext", "S");
			break;
		default:
			break;
		}

		int in_num = 0, in_den = 1, out_num = 0, out_den = 1;
		oakengine_block_get_in_rational(context_.handle(), &in_num, &in_den);
		oakengine_block_get_out_rational(context_.handle(), &out_num, &out_den);

		lbl_ =
			QCoreApplication::translate("NodeViewContext", "%1 [%2] :: %3 - %4")
				.arg(context_.label_and_name(),
					 type_label,
					 QString::fromStdString(Timecode::time_to_timecode(
						 Rational(in_num, in_den), timebase,
						 Core::instance()->get_timecode_display())),
					 QString::fromStdString(Timecode::time_to_timecode(
						 Rational(out_num, out_den), timebase,
						 Core::instance()->get_timecode_display())));
	} else {
		lbl_ = context_.label_and_name();
	}

	const int ctx_count = context_.context_node_count();
	for (int i = 0; i < ctx_count; i++) {
		oak::Node child = context_.context_node_at(i).node;
		if (!child.is_null()) {
			add_child(child.handle());
		}
	}

	connect(Core::instance(),
			&Core::project_load_finished,
			this,
			[this](){ update_rect(); });
}

NodeViewContext::~NodeViewContext()
{
	qDeleteAll(edges_);
	edges_.clear();
}

void NodeViewContext::add_child(OakEngineNode *node)
{
	if (context_.is_null()) {
		return;
	}
	NodeViewItem *item = new NodeViewItem(oak::Node(node), context_, this);
	item->set_flow_direction(flow_dir_);
	add_node_internal(node, item);

	oak::Node group_node(node);
	if (group_node.is_group()) {
		const int grp_count = group_node.context_node_count();
		for (int i = 0; i < grp_count; i++) {
			oak::Node grp_child = group_node.context_node_at(i).node;
			if (!grp_child.is_null()) {
				add_node_internal(grp_child.handle(), item);
			}
		}
	}
	update_rect();
}

void NodeViewContext::set_child_position(OakEngineNode *node, const QPointF &pos)
{
	item_map_.value(node)->set_node_position(pos);
}

void NodeViewContext::remove_child(OakEngineNode *node)
{
	NodeViewItem *item = item_map_.take(node);
	if (!item) return;

	scene()->removeItem(item);
	emit item_about_to_be_deleted(item);

	QVector<NodeViewEdge *> edges_to_remove = item->get_all_edges_recursively();
	for (NodeViewEdge *edge : edges_to_remove) {
		if (item->get_node() == oak::Node(node)
			|| edge->output() == oak::Node(node)
			|| edge->input().node_handle() == node) {
			child_input_disconnected(edge->output().handle(), edge->input());
		}
	}

	if (item->get_node() == oak::Node(node)) {
		if (item->get_node().is_group()) {
			for (auto it = item_map_.begin(); it != item_map_.end(); ) {
				if (it.value() == item) {
					it = item_map_.erase(it);
				} else {
					++it;
				}
			}
		}
		delete item;
	}
	update_rect();
}

void NodeViewContext::child_input_connected(OakEngineNode *output, const oak::Input &input)
{
	if (!input.is_hidden()) {
		if (NodeViewItem *output_item = item_map_.value(output)) {
			add_edge_internal(
				output, input, output_item,
				item_map_.value(input.node_handle())->get_item_for_input(input));
		}
	}
}

bool NodeViewContext::child_input_disconnected(OakEngineNode *output, const oak::Input &input)
{
	for (int i = 0; i < edges_.size(); i++) {
		NodeViewEdge *e = edges_.at(i);
		if (e->output() == oak::Node(output) && e->input() == input) {
			if (qEnvironmentVariableIsSet("OAK_DEBUG_EDGES")) {
				qWarning("EDGE‑DEBUG: edge removed: %p -> %p (%s,%d) ctx=%p",
						 (void *)output, (void *)input.node_handle(),
						 qPrintable(input.input_id()), input.element(),
						 (void *)this);
			}
			delete e;
			edges_.removeAt(i);
			return true;
		}
	}
	return false;
}

static qreal get_text_offset(const QFontMetricsF &fm)
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
	for (NodeViewItem *item : item_map_) {
		item->set_flow_direction(dir);
	}
}

void NodeViewContext::set_curved_edges(bool e)
{
	curved_edges_ = e;
	for (NodeViewEdge *edge : edges_) {
		edge->set_curved(e);
	}
}

void NodeViewContext::get_selected_for_deletion(QVector<OakEngineNode *> &nodes,
											   QVector<OakEngineNode *> &contexts,
											   QVector<NodeViewEdge *> &edges) const
{
	for (NodeViewEdge *edge : edges_) {
		if (edge->isSelected()) {
			edges.append(edge);
		}
	}
	for (NodeViewItem *node : item_map_) {
		if (node->isSelected()) {
			nodes.append(node->get_node().handle());
			contexts.append(context_.handle());
		}
	}
}

void NodeViewContext::select(const QVector<OakEngineNode *> &nodes)
{
	for (OakEngineNode *n : nodes) {
		if (NodeViewItem *item = item_map_.value(n)) {
			item->setSelected(true);
		}
	}
}

QVector<NodeViewItem *> NodeViewContext::get_selected_items() const
{
	QVector<NodeViewItem *> items;
	for (auto it = item_map_.cbegin(); it != item_map_.cend(); ++it) {
		if (it.value()->isSelected() && !items.contains(it.value())) {
			items.append(it.value());
		}
	}
	return items;
}

QPointF NodeViewContext::map_scene_pos_to_node_pos_in_context(const QPointF &pos) const
{
	for (auto it = item_map_.cbegin(); it != item_map_.cend(); ++it) {
		QPointF pos_inside_parent = it.value()->mapToParent(it.value()->mapFromScene(pos));
		return NodeViewItem::screen_to_node_point(pos_inside_parent, flow_dir_);
	}
	return QPointF(0, 0);
}

void NodeViewContext::paint(QPainter *painter,
							const QStyleOptionGraphicsItem *option,
							QWidget *widget)
{
	Color color = AppColorCoding::get_color(context_.effective_color_label());
	QColor c = QtUtils::to_q_color(color);
	QPen pen(c, 2);
	if (option->state & QStyle::State_Selected) {
		pen.setStyle(Qt::DotLine);
	}
	painter->setPen(pen);

	QColor bg = c;
	bg.setAlpha(128);
	painter->setBrush(bg);

	int rounded = painter->fontMetrics().height();
	painter->drawRoundedRect(rect(), rounded, rounded);

	QRectF titlebar_rect = rect();
	titlebar_rect.setHeight(last_titlebar_height_ - rect().top());
	painter->setClipRect(titlebar_rect);
	painter->setBrush(c);
	painter->drawRoundedRect(rect(), rounded, rounded);
	painter->setClipping(false);

	painter->setPen(AppColorCoding::get_ui_selector_color(color));
	int offset = get_text_offset(painter->fontMetrics());
	QRectF text_rect = rect();
	text_rect.adjust(offset, offset, -offset, -offset);
	painter->drawText(text_rect, lbl_);
}

QVariant NodeViewContext::itemChange(GraphicsItemChange change, const QVariant &value)
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

void NodeViewContext::add_node_internal(OakEngineNode *node, NodeViewItem *item)
{
	if (qEnvironmentVariableIsSet("OAK_DEBUG_EDGES")) {
		qWarning("EDGE‑DEBUG: node added to ctx %p: %p", (void *)this, (void *)node);
	}

	item_map_.insert(node, item);
	if (node == context_.handle()) {
		item->set_label_as_output(true);
	}

	oak::Node wrapped(node);
	const int out_count = wrapped.output_connection_count();
	for (int i = 0; i < out_count; i++) {
		oak::NodeConnection conn = wrapped.output_connection_at_ex(i);
		if (!conn.hidden) {
			if (NodeViewItem *other_item = item_map_.value(conn.node.handle())) {
				oak::Input ai(conn.node.handle(), conn.input_id, conn.element);
				add_edge_internal(node, ai, item, other_item->get_item_for_input(ai));
			} else if (qEnvironmentVariableIsSet("OAK_DEBUG_EDGES")) {
				qWarning("EDGE‑DEBUG: out‑edge skipped (no item for %p): %p -> %p (%s) ctx=%p",
						 (void *)conn.node.handle(), (void *)node,
						 (void *)conn.node.handle(), qPrintable(conn.input_id), (void *)this);
			}
		}
	}

	const int in_count = wrapped.input_connection_count_all();
	for (int i = 0; i < in_count; i++) {
		oak::NodeConnection conn = wrapped.input_connection_at_all(i);
		if (!conn.hidden) {
			if (NodeViewItem *other_item = item_map_.value(conn.source_node.handle())) {
				oak::Input ai(conn.node.handle(), conn.input_id, conn.element);
				add_edge_internal(conn.source_node.handle(), ai, other_item, item->get_item_for_input(ai));
			} else if (qEnvironmentVariableIsSet("OAK_DEBUG_EDGES")) {
				qWarning("EDGE‑DEBUG: in‑edge skipped (no item for %p): %p -> %p (%s) ctx=%p",
						 (void *)conn.source_node.handle(), (void *)conn.source_node.handle(),
						 (void *)node, qPrintable(conn.input_id), (void *)this);
			}
		}
	}
}

void NodeViewContext::add_edge_internal(OakEngineNode *output, const oak::Input &input,
									  NodeViewItem *from, NodeViewItem *to)
{
	if (from == to) return;

	if (qEnvironmentVariableIsSet("OAK_DEBUG_EDGES")) {
		qWarning("EDGE‑DEBUG: edge added: %p -> %p (%s,%d) ctx=%p", (void *)output,
				 (void *)input.node_handle(), qPrintable(input.input_id()),
				 input.element(), (void *)this);
	}

	NodeViewEdge *edge_ui = new NodeViewEdge(oak::Node(output), input, from, to, this);
	edge_ui->adjust();
	edge_ui->set_curved(curved_edges_);
	edges_.append(edge_ui);
}

void NodeViewContext::group_added_node(OakEngineNode *node, OakEngineNode *group)
{
	add_node_internal(node, item_map_.value(group));
}

void NodeViewContext::group_removed_node(OakEngineNode *node, OakEngineNode *group)
{
	if (item_map_.value(node) == item_map_.value(group)) {
		item_map_.remove(node);
	}
}

}