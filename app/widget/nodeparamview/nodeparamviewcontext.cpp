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

#include "nodeparamviewcontext.h"

#include <QMessageBox>

#include "node/block/clip/clip.h"
#include "node/factory.h"
#include "node/nodeundo.h"

namespace olive
{

#define super NodeParamViewItemBase

NodeParamViewContext::NodeParamViewContext(QWidget *parent)
	: super(parent)
	, type_(Track::k_none)
{
	QWidget *body = new QWidget();
	QHBoxLayout *body_layout = new QHBoxLayout(body);
	set_body(body);

	dock_area_ = new NodeParamViewDockArea();
	body_layout->addWidget(dock_area_);

	setBackgroundRole(QPalette::Base);

	retranslate();

	connect(title_bar(), &NodeParamViewItemTitleBar::add_effect_button_clicked,
			this, &NodeParamViewContext::add_effect_button_clicked);
}

NodeParamViewItem *NodeParamViewContext::get_item(Node *node, Node *ctx)
{
	for (auto it = items_.begin(); it != items_.end(); it++) {
		NodeParamViewItem *item = *it;

		if (item->get_node() == node && item->get_context() == ctx) {
			return item;
		}
	}

	return nullptr;
}

void NodeParamViewContext::add_node(NodeParamViewItem *item)
{
	items_.append(item);
	dock_area_->add_item(item);
}

void NodeParamViewContext::remove_node(Node *node, Node *ctx)
{
	for (auto it = items_.begin(); it != items_.end();) {
		NodeParamViewItem *item = *it;

		if (item->get_node() == node && item->get_context() == ctx) {
			emit about_to_delete_item(item);
			dock_area_->remove_item(item);
			it = items_.erase(it);
		} else {
			it++;
		}
	}
}

void NodeParamViewContext::remove_nodes_with_context(Node *ctx)
{
	for (auto it = items_.begin(); it != items_.end();) {
		NodeParamViewItem *item = *it;

		if (item->get_context() == ctx) {
			emit about_to_delete_item(item);
			dock_area_->remove_item(item);
			it = items_.erase(it);
		} else {
			it++;
		}
	}
}

void NodeParamViewContext::set_input_checked(const NodeInput &input, bool e)
{
	foreach (NodeParamViewItem *item, items_) {
		if (item->get_node() == input.node()) {
			item->set_input_checked(input, e);
		}
	}
}

void NodeParamViewContext::set_timebase(const Rational &timebase)
{
	foreach (NodeParamViewItem *item, items_) {
		item->set_timebase(timebase);
	}
}

void NodeParamViewContext::set_time_target(ViewerOutput *n)
{
	foreach (NodeParamViewItem *item, items_) {
		item->set_time_target(n);
	}
}

void NodeParamViewContext::set_effect_type(Track::Type type)
{
	type_ = type;
}

void NodeParamViewContext::retranslate()
{
}

void NodeParamViewContext::add_effect_button_clicked()
{
	Node::Flag flag = Node::k_none;

	if (type_ == Track::k_video) {
		flag = Node::k_video_effect;
	} else {
		flag = Node::k_audio_effect;
	}

	if (flag == Node::k_none) {
		return;
	}

	Menu *m =
		NodeFactory::create_menu(this, false, Node::k_category_unknown, flag);
	connect(m, &Menu::triggered, this,
			&NodeParamViewContext::add_effect_menu_item_triggered);
	m->exec(QCursor::pos());
	delete m;
}

void NodeParamViewContext::add_effect_menu_item_triggered(QAction *a)
{
	Node *n = NodeFactory::CreateFromMenuAction(a);

	if (n) {
		NodeInput new_node_input = n->get_effect_input();
		MultiUndoCommand *command = new MultiUndoCommand();

		QVector<Project *> graphs_added_to;

		foreach (Node *ctx, contexts_) {
			NodeInput ctx_input = ctx->get_effect_input();

			if (!graphs_added_to.contains(ctx->parent())) {
				command->add_child(new NodeAddCommand(ctx->parent(), n));
				graphs_added_to.append(ctx->parent());
			}

			command->add_child(new NodeSetPositionCommand(
				n, ctx, ctx->get_node_position_in_context(ctx)));
			command->add_child(new NodeSetPositionCommand(
				ctx, ctx, ctx->get_node_position_in_context(ctx) + QPointF(1, 0)));

			if (ctx_input.is_connected()) {
				Node *prev_output = ctx_input.get_connected_output();

				command->add_child(
					new NodeEdgeRemoveCommand(prev_output, ctx_input));
				command->add_child(
					new NodeEdgeAddCommand(prev_output, new_node_input));
			}

			command->add_child(new NodeEdgeAddCommand(n, ctx_input));
		}

		Core::instance()->undo_stack()->push(
			command, tr("Added %1 to Node Chain").arg(n->name()));
	}
}

}
