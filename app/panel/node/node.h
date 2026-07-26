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

#ifndef OAK_NODEPANEL_H
#define OAK_NODEPANEL_H

#include "panel/panel.h"
#include "widget/nodeview/nodewidget.h"

struct OakEngineNode;

namespace olive
{

/**
 * @brief A PanelWidget wrapper around a NodeView
 */
class NodePanel : public PanelWidget {
	Q_OBJECT
public:
	NodePanel();

	NodeWidget *get_node_widget() const
	{
		return node_widget_;
	}

	const QVector<Node *> &get_contexts() const
	{
		return node_widget_->view()->get_contexts();
	}

	bool is_group_overlay() const
	{
		return node_widget_->view()->is_group_overlay();
	}

	void set_contexts(const QVector<Node *> &nodes)
	{
		node_widget_->set_contexts(nodes);
	}

	void close_contexts_belonging_to_project(Project *project)
	{
		node_widget_->view()->close_contexts_belonging_to_project(project);
	}

	virtual void select_all() override
	{
		node_widget_->view()->select_all();
	}

	virtual void deselect_all() override
	{
		node_widget_->view()->deselect_all();
	}

	virtual void delete_selected() override
	{
		node_widget_->view()->delete_selected();
	}

	virtual void cut_selected() override
	{
		node_widget_->view()->copy_selected(true);
	}

	virtual void copy_selected() override
	{
		node_widget_->view()->copy_selected(false);
	}

	virtual void paste() override
	{
		node_widget_->view()->paste();
	}

	virtual void duplicate() override
	{
		node_widget_->view()->duplicate();
	}

	virtual void set_color_label(int index) override
	{
		node_widget_->view()->set_color_label(index);
	}

	virtual void zoom_in() override
	{
		node_widget_->view()->zoom_in();
	}

	virtual void zoom_out() override
	{
		node_widget_->view()->zoom_out();
	}

	virtual void rename_selected() override
	{
		node_widget_->view()->label_selected_nodes();
	}

public slots:
	void select(
		const QVector<QPair<OakEngineNode *, OakEngineNode *>> &p)
	{
		node_widget_->view()->select(p, true);
	}

signals:
	void nodes_selected(const QVector<OakEngineNode *> &nodes);

	void nodes_deselected(const QVector<OakEngineNode *> &nodes);

	void node_selection_changed(const QVector<OakEngineNode *> &nodes);
	void node_selection_changed_with_contexts(
		const QVector<QPair<OakEngineNode *, OakEngineNode *>> &nodes);

	void node_group_opened(OakEngineNode *group);

	void node_group_closed();

private:
	virtual void retranslate() override
	{
		set_title(tr("Node Editor"));
	}

	NodeWidget *node_widget_;
};

}

#endif // OAK_NODEPANEL_H
