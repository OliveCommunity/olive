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

#ifndef OAK_PARAM_H
#define OAK_PARAM_H

#include "panel/curve/curve.h"
#include "panel/timebased/timebased.h"
#include "widget/nodeparamview/nodeparamview.h"

namespace olive
{

class ParamPanel : public TimeBasedPanel {
	Q_OBJECT
public:
	ParamPanel();

	NodeParamView *get_param_view() const
	{
		return static_cast<NodeParamView *>(get_time_based_widget());
	}

	const QVector<oak::Node> &get_contexts() const
	{
		return get_param_view()->get_contexts();
	}

	void close_contexts_belonging_to_project(oak::Project p)
	{
		get_param_view()->close_contexts_belonging_to_project(p);
	}

public slots:
	void set_selected_nodes(
		const QVector<QPair<OakEngineNode *, OakEngineNode *>> &nodes)
	{
		get_param_view()->set_selected_nodes(nodes, false);
	}

	virtual void delete_selected() override;

	virtual void select_all() override;

	virtual void deselect_all() override;

public:
	// Not a slot: mirrors NodePanel::set_contexts(), which is also a plain
	// member function. All connections use new-style member-function syntax.
	void set_contexts(const QVector<oak::Node> &contexts);

signals:
	void focused_node_changed(OakEngineNode *n);

	void selected_nodes_changed(
		const QVector<QPair<OakEngineNode *, OakEngineNode *>> &nodes);

	void request_viewer_to_start_editing_text();

protected:
	virtual void retranslate() override;
};

}

#endif // OAK_PARAM_H
