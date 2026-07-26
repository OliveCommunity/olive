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

#include "nodewidget.h"

#include <QVBoxLayout>

namespace olive
{

NodeWidget::NodeWidget(QWidget *parent)
	: QWidget(parent)
{
	QVBoxLayout *outer_layout = new QVBoxLayout(this);
	outer_layout->setContentsMargins(0, 0, 0, 0);

	toolbar_ = new NodeViewToolBar();
	outer_layout->addWidget(toolbar_);

	// Create NodeView widget
	node_view_ = new NodeView(this);
	outer_layout->addWidget(node_view_);

	// Connect toolbar to NodeView
	connect(toolbar_, &NodeViewToolBar::mini_map_enabled_toggled, node_view_,
			&NodeView::set_mini_map_enabled);
	connect(toolbar_, &NodeViewToolBar::add_node_clicked, node_view_,
			&NodeView::show_add_menu);
	connect(toolbar_, &NodeViewToolBar::zoom_in_clicked, node_view_,
			&NodeView::zoom_in);
	connect(toolbar_, &NodeViewToolBar::zoom_out_clicked, node_view_,
			&NodeView::zoom_out);
	connect(toolbar_, &NodeViewToolBar::fit_clicked, node_view_,
			&NodeView::center_on_items_bounding_rect);

	// Set defaults
	toolbar_->set_mini_map_enabled(true);
	node_view_->set_mini_map_enabled(true);

	setSizePolicy(node_view_->sizePolicy());
}

}
