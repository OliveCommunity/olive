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

#ifndef OAK_NODEPARAMVIEWCONNECTEDLABEL_H
#define OAK_NODEPARAMVIEWCONNECTEDLABEL_H

#include "engineeventbridge.h"
#include "node/param.h"
#include "widget/clickablelabel/clickablelabel.h"
#include "widget/nodevaluetree/nodevaluetree.h"

#include <cstdint>

namespace olive
{

class NodeParamViewConnectedLabel : public QWidget {
	Q_OBJECT
public:
	NodeParamViewConnectedLabel(const NodeInput &input,
								QWidget *parent = nullptr);

	void set_viewer_node(ViewerOutput *viewer);

signals:
	void request_select_node(OakEngineNode *n);

private slots:
	void input_connected(OakEngineNode *output, const NodeInput &input);

	void input_disconnected(OakEngineNode *output, const NodeInput &input);

	void show_label_context_menu();

	void connection_clicked();

private:
	void update_label();

	void update_value_tree();

	void create_tree();

	ClickableLabel *connected_to_lbl_;

	NodeInput input_;

	Node *connected_node_;

	NodeValueTree *value_tree_;

	ViewerOutput *viewer_;

	EngineEventBridge *bridge_ = nullptr;

	int64_t viewer_sub_ = 0;

private slots:
	void set_value_tree_visible(bool e);
};

}

#endif // OAK_NODEPARAMVIEWCONNECTEDLABEL_H
