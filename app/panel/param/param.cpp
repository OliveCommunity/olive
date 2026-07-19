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

#include "param.h"

#include "window/mainwindow/mainwindow.h"

namespace olive
{

ParamPanel::ParamPanel()
	: TimeBasedPanel(QStringLiteral("ParamPanel"))
{
	NodeParamView *view = new NodeParamView(this);
	connect(view, &NodeParamView::focused_node_changed, this,
			&ParamPanel::focused_node_changed);
	connect(view, &NodeParamView::selected_nodes_changed, this,
			&ParamPanel::selected_nodes_changed);
	connect(view, &NodeParamView::request_viewer_to_start_editing_text, this,
			&ParamPanel::request_viewer_to_start_editing_text);
	connect(this, &ParamPanel::shown, view, &NodeParamView::update_element_y);
	set_time_based_widget(view);

	retranslate();
}

void ParamPanel::delete_selected()
{
	static_cast<NodeParamView *>(get_time_based_widget())->DeleteSelected();
}

void ParamPanel::select_all()
{
	static_cast<NodeParamView *>(get_time_based_widget())->select_all();
}

void ParamPanel::deselect_all()
{
	static_cast<NodeParamView *>(get_time_based_widget())->deselect_all();
}

void ParamPanel::set_contexts(const QVector<Node *> &contexts)
{
	static_cast<NodeParamView *>(get_time_based_widget())->set_contexts(contexts);
}

void ParamPanel::retranslate()
{
	set_title(tr("Parameter Editor"));
}

}
