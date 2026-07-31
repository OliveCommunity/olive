/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_SERIALIZEDLAYOUTINFOAPP_H
#define OAK_SERIALIZEDLAYOUTINFOAPP_H

#include <map>
#include <vector>

#include <QByteArray>
#include <QString>

#include "oakengine/node.h"

namespace olive
{

/**
 * @brief App-side mirror of the engine's olive::SerializedLayoutInfo
 * (engine/node/project/serializer/serializedlayoutinfo.h).
 *
 * SYNC OBLIGATION: the data member layout (types AND order) must stay
 * identical to the engine type. Instances of this struct cross the engine
 * boundary as `void *` (Core::save_project_internal() passes one to
 * oakengine_task_create_project_save(), and the engine hands one back
 * through the load_layout callback); the engine side reinterprets the
 * pointer as its own olive::SerializedLayoutInfo and copies the members,
 * so any layout divergence is silent memory corruption.
 *
 * The engine's std::vector<Folder*> / std::vector<Sequence*> /
 * std::vector<ViewerOutput*> members are mirrored as
 * std::vector<OakEngineNode*> (same pointer size and semantics: borrowed
 * node handles). panel_data mirrors
 * std::map<QString, PanelLayoutInfo> where PanelLayoutInfo is
 * std::map<QString, QString> (identical to PanelWidget::Info).
 *
 * Only the data members are mirrored; the engine type's XML
 * (de)serialization methods (to_xml/from_xml) stay engine-side.
 */
class SerializedLayoutInfo {
public:
	SerializedLayoutInfo() = default;

	QByteArray state;

	std::vector<OakEngineNode *> open_folders;

	std::vector<OakEngineNode *> open_sequences;

	std::vector<OakEngineNode *> open_viewers;

	std::map<QString, std::map<QString, QString>> panel_data;
};

}

Q_DECLARE_METATYPE(olive::SerializedLayoutInfo)

#endif // OAK_SERIALIZEDLAYOUTINFOAPP_H
