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

#ifndef OAK_NODEPARAMVIEWCONTEXT_H
#define OAK_NODEPARAMVIEWCONTEXT_H

#include "nodeparamviewdockarea.h"
#include "nodeparamviewitembase.h"
#include "nodeparamviewitem.h"

namespace olive
{

class NodeParamViewContext : public NodeParamViewItemBase {
	Q_OBJECT
public:
	NodeParamViewContext(QWidget *parent = nullptr);

	NodeParamViewDockArea *get_dock_area() const
	{
		return dock_area_;
	}

	const QVector<Node *> &get_contexts() const
	{
		return contexts_;
	}

	const QVector<NodeParamViewItem *> &get_items() const
	{
		return items_;
	}

	NodeParamViewItem *get_item(Node *node, Node *ctx);

	void add_node(NodeParamViewItem *item);

	void remove_node(Node *node, Node *ctx);

	void remove_nodes_with_context(Node *ctx);

	void set_input_checked(const NodeInput &input, bool e);

	void set_timebase(const Rational &timebase);

	void set_time_target(ViewerOutput *n);

	void set_effect_type(Track::Type type);

signals:
	void about_to_delete_item(NodeParamViewItem *item);

public slots:
	void add_context(Node *node)
	{
		contexts_.append(node);
	}

	void remove_context(Node *node)
	{
		contexts_.removeOne(node);
	}

protected slots:
	virtual void retranslate() override;

private:
	NodeParamViewDockArea *dock_area_;

	QVector<Node *> contexts_;

	QVector<NodeParamViewItem *> items_;

	Track::Type type_;

private slots:
	void add_effect_button_clicked();

	void add_effect_menu_item_triggered(QAction *a);
};

}

#endif // OAK_NODEPARAMVIEWCONTEXT_H
