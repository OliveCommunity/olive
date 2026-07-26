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

#include "footageviewer.h"

namespace olive
{

#define super ViewerPanelBase

FootageViewerPanel::FootageViewerPanel()
	: super(QStringLiteral("FootageViewerPanel"))
{
	// Set ViewerWidget as the central widget
	FootageViewerWidget *fvw = new FootageViewerWidget(this);
	set_viewer_widget(fvw);

	// Set strings
	retranslate();

	// Show and raise on connect
	set_show_and_raise_on_connect();
}

void FootageViewerPanel::override_work_area(const TimeRange &r)
{
	get_footage_viewer_widget()->override_work_area(r);
}

QVector<OakEngineNode *> FootageViewerPanel::get_selected_footage() const
{
	QVector<OakEngineNode *> list;

	if (get_connected_viewer()) {
		list.append(reinterpret_cast<OakEngineNode *>(get_connected_viewer()));
	}

	return list;
}

void FootageViewerPanel::retranslate()
{
	super::retranslate();

	set_title(tr("Footage Viewer"));
}

}
