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

#include "nodeviewscene.h"

#include "core.h"
#include "node/project/sequence/sequence.h"
#include "nodeviewedge.h"
#include "nodeviewitem.h"

namespace olive
{

NodeViewScene::NodeViewScene(QObject *parent)
	: QGraphicsScene(parent)
	, direction_(NodeViewCommon::k_left_to_right)
	, curved_edges_(true)
{
}

void NodeViewScene::set_flow_direction(NodeViewCommon::FlowDirection direction)
{
	direction_ = direction;

	foreach (NodeViewContext *ctx, context_map_) {
		ctx->set_flow_direction(direction_);
	}
}

void NodeViewScene::select_all()
{
	foreach (QGraphicsItem *i, items()) {
		i->setSelected(true);
	}
}

void NodeViewScene::deselect_all()
{
	foreach (QGraphicsItem *i, items()) {
		i->setSelected(false);
	}
}

QVector<NodeViewItem *> NodeViewScene::get_selected_items() const
{
	QVector<NodeViewItem *> items;

	foreach (NodeViewContext *ctx, context_map_) {
		items.append(ctx->get_selected_items());
	}

	return items;
}

NodeViewContext *NodeViewScene::add_context(Node *node)
{
	NodeViewContext *context_item = context_map_.value(node);

	if (!context_item) {
		context_item = new NodeViewContext(node);

		context_item->set_flow_direction(get_flow_direction());
		context_item->set_curved_edges(get_edges_are_curved());

		QPointF pos(0, 0);
		QRectF item_rect = context_item->rect();
		while (!items(item_rect).isEmpty()) {
			pos.setY(pos.y() + item_rect.height());
			item_rect = context_item->rect().translated(pos);
		}
		context_item->setPos(pos);

		addItem(context_item);

		context_map_.insert(node, context_item);
	}

	return context_item;
}

void NodeViewScene::remove_context(Node *node)
{
	delete context_map_.take(node);
}

Qt::Orientation NodeViewScene::get_flow_orientation() const
{
	return NodeViewCommon::get_flow_orientation(direction_);
}

void NodeViewScene::set_edges_are_curved(bool curved)
{
	if (curved_edges_ != curved) {
		curved_edges_ = curved;

		foreach (NodeViewContext *ctx, context_map_) {
			ctx->set_curved_edges(curved_edges_);
		}
	}
}

}
