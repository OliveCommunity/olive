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

#include "oakengine/node.h"
#include "oakengine/undo.h"
#include "widget/menu/factorymenu.h"

namespace olive
{

#define super NodeParamViewItemBase

NodeParamViewContext::NodeParamViewContext(QWidget *parent)
	: super(parent)
	, type_(TrackReference::k_none)
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

NodeParamViewItem *NodeParamViewContext::get_item(oak::Node node, oak::Node ctx)
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

void NodeParamViewContext::remove_node(oak::Node node, oak::Node ctx)
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

void NodeParamViewContext::remove_nodes_with_context(oak::Node ctx)
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

void NodeParamViewContext::set_input_checked(const oak::Input &input, bool e)
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

void NodeParamViewContext::set_time_target(OakEngineNode *n)
{
	foreach (NodeParamViewItem *item, items_) {
		item->set_time_target(n);
	}
}

void NodeParamViewContext::set_effect_type(TrackReference::Type type)
{
	type_ = type;
}

void NodeParamViewContext::retranslate()
{
}

void NodeParamViewContext::add_effect_button_clicked()
{
	uint64_t flag = (type_ == TrackReference::k_video)
						? oakengine_node_flag_video_effect()
						: oakengine_node_flag_audio_effect();

	if (flag == 0) {
		return;
	}

	Menu *m =
		create_node_menu(this, false, oak::k_category_unknown, flag);
	connect(m, &Menu::triggered, this,
			&NodeParamViewContext::add_effect_menu_item_triggered);
	m->exec(QCursor::pos());
	delete m;
}

void NodeParamViewContext::add_effect_menu_item_triggered(QAction *a)
{
	// Owned handle: handed to the add-to-project undo command below
	oak::Node n = create_node_from_menu_action(a);

	if (!n.is_null()) {
		oak::Input new_node_input = n.effect_input();
		// WRAPPER-GAP: oakengine_undo_* / oakengine_node_*_command (undo
		// command assembly has no oak:: wrapper)
		void *command = oakengine_undo_command_create_multi();

		QVector<OakEngineProject *> graphs_added_to;

		foreach (oak::Node ctx, contexts_) {
			oak::Input ctx_input = ctx.effect_input();

			OakEngineProject *ctx_project = ctx.project().handle();
			if (!graphs_added_to.contains(ctx_project)) {
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_node_add_to_project_command(
						ctx_project, n.handle()));
				graphs_added_to.append(ctx_project);
			}

			QPointF ctx_pos;
			ctx.context_position_of(ctx, &ctx_pos);
			oakengine_undo_command_multi_add_child(
				command, oakengine_node_set_position_command(n.handle(), ctx.handle(), ctx_pos.x(), ctx_pos.y(), 0));
			oakengine_undo_command_multi_add_child(
				command, oakengine_node_set_position_command(
					ctx.handle(), ctx.handle(),
					ctx_pos.x() + 1,
					ctx_pos.y(), 0));

			if (ctx_input.is_connected()) {
				oak::Node prev_output = ctx_input.connected_node();

				oakengine_undo_command_multi_add_child(
					command,
					oakengine_node_disconnect_command(
						ctx_input.node_handle(),
						ctx_input.input_id().toUtf8().constData(),
						ctx_input.element()));
				oakengine_undo_command_multi_add_child(
					command,
					oakengine_node_connect_command(
						prev_output.handle(),
						new_node_input.node_handle(),
						new_node_input.input_id().toUtf8().constData(),
						new_node_input.element()));
			}

			oakengine_undo_command_multi_add_child(
				command,
				oakengine_node_connect_command(
					n.handle(),
					ctx_input.node_handle(),
					ctx_input.input_id().toUtf8().constData(),
					ctx_input.element()));
		}

		oakengine_undo_push(
			command, tr("Added %1 to Node Chain").arg(n.name()).toUtf8().constData());
	}
}

}
