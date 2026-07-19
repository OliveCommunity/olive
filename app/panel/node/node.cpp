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

#include "node.h"

namespace olive
{

NodePanel::NodePanel()
	: PanelWidget(QStringLiteral("NodePanel"))
{
	node_widget_ = new NodeWidget(this);
	connect(this, &NodePanel::shown, node_widget_->view(),
			&NodeView::center_on_items_bounding_rect);

	connect(node_widget_->view(), &NodeView::nodes_selected, this,
			&NodePanel::nodes_selected);
	connect(node_widget_->view(), &NodeView::nodes_deselected, this,
			&NodePanel::nodes_deselected);
	connect(node_widget_->view(), &NodeView::node_selection_changed, this,
			&NodePanel::node_selection_changed);
	connect(node_widget_->view(), &NodeView::node_selection_changed_with_contexts,
			this, &NodePanel::node_selection_changed_with_contexts);
	connect(node_widget_->view(), &NodeView::node_group_opened, this,
			&NodePanel::node_group_opened);
	connect(node_widget_->view(), &NodeView::node_group_closed, this,
			&NodePanel::node_group_closed);

	// Set it as the main widget of this panel
	set_widget_with_padding(node_widget_);

	// Set strings
	retranslate();
}

}
