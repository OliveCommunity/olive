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

#include "curve.h"

namespace olive
{

CurvePanel::CurvePanel()
	: TimeBasedPanel(QStringLiteral("CurvePanel"))
{
	// Create main widget and set it
	set_time_based_widget(new CurveWidget(this));

	// Set strings
	retranslate();
}

void CurvePanel::delete_selected()
{
	static_cast<CurveWidget *>(get_time_based_widget())->DeleteSelected();
}

void CurvePanel::select_all()
{
	static_cast<CurveWidget *>(get_time_based_widget())->select_all();
}

void CurvePanel::deselect_all()
{
	static_cast<CurveWidget *>(get_time_based_widget())->deselect_all();
}

void CurvePanel::set_nodes(const QVector<OakEngineNode *> &nodes)
{
	// Convert to oak::Node for the curve widget (C ABI wrapper layer)
	QVector<oak::Node> oak_nodes;
	oak_nodes.reserve(nodes.size());
	foreach (OakEngineNode *n, nodes) {
		oak_nodes.append(oak::Node(n));
	}
	static_cast<CurveWidget *>(get_time_based_widget())->set_nodes(oak_nodes);
}

void CurvePanel::increase_track_height()
{
	CurveWidget *c = static_cast<CurveWidget *>(get_time_based_widget());
	c->set_vertical_scale(c->get_vertical_scale() * 2);
}

void CurvePanel::decrease_track_height()
{
	CurveWidget *c = static_cast<CurveWidget *>(get_time_based_widget());
	c->set_vertical_scale(c->get_vertical_scale() * 0.5);
}

void CurvePanel::retranslate()
{
	TimeBasedPanel::retranslate();

	set_title(tr("Curve Editor"));
}

}
