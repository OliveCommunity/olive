/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_SERIALIZEDLAYOUTINFO_H
#define OAK_SERIALIZEDLAYOUTINFO_H

#include <map>

#include "project/folder/folder.h"
#include "project/sequence/sequence.h"

namespace olive
{

/**
 * @brief Per-panel layout data (key/value pairs)
 *
 * Identical to PanelWidget::Info in the UI layer; defined here so the
 * engine-side layout data structure does not depend on widget headers.
 */
using PanelLayoutInfo = std::map<std::string, std::string>;

/**
 * @brief Plain data container for a serialized main window layout
 *
 * Pure data structure with no behavior beyond XML (de)serialization, so
 * consumers (app, tests) can use it without pulling in any engine-side
 * C++ symbols.
 */
class SerializedLayoutInfo {
public:
	SerializedLayoutInfo() = default;

	void to_xml(XmlStreamWriter *writer) const;

	static SerializedLayoutInfo
	from_xml(XmlStreamReader *reader,
			 const std::map<uintptr_t, Node *> &node_map);

	ByteArray state;

	std::vector<Folder *> open_folders;

	std::vector<Sequence *> open_sequences;

	std::vector<ViewerOutput *> open_viewers;

	std::map<std::string, PanelLayoutInfo> panel_data;

private:
	static const unsigned int k_version = 1;
};

}

#endif // OAK_SERIALIZEDLAYOUTINFO_H
