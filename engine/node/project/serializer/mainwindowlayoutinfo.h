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

#ifndef OAK_MAINWINDOWLAYOUTINFO_H
#define OAK_MAINWINDOWLAYOUTINFO_H

#include <map>

#include "node/project/folder/folder.h"
#include "node/project/sequence/sequence.h"

namespace olive
{

/**
 * @brief Per-panel layout data (key/value pairs)
 *
 * Identical to PanelWidget::Info in the UI layer; defined here so the
 * engine-side layout data structure does not depend on widget headers.
 */
using PanelLayoutInfo = std::map<QString, QString>;

class MainWindowLayoutInfo {
public:
	MainWindowLayoutInfo() = default;

	void to_xml(QXmlStreamWriter *writer) const;

	static MainWindowLayoutInfo
	from_xml(QXmlStreamReader *reader, const QHash<quintptr, Node *> &node_map);

	void add_folder(Folder *f);

	void add_sequence(Sequence *seq);

	void add_viewer(ViewerOutput *viewer);

	void set_panel_data(const QString &id, const PanelLayoutInfo &data);

	void move_panel_data(const QString &old, const QString &now);

	void set_state(const QByteArray &layout);

	const std::vector<Folder *> &open_folders() const
	{
		return open_folders_;
	}

	const std::vector<Sequence *> &open_sequences() const
	{
		return open_sequences_;
	}

	const std::vector<ViewerOutput *> &open_viewers() const
	{
		return open_viewers_;
	}

	const std::map<QString, PanelLayoutInfo> &panel_data() const
	{
		return panel_data_;
	}

	const QByteArray &state() const
	{
		return state_;
	}

private:
	QByteArray state_;

	std::vector<Folder *> open_folders_;

	std::vector<Sequence *> open_sequences_;

	std::vector<ViewerOutput *> open_viewers_;

	std::map<QString, PanelLayoutInfo> panel_data_;

	static const unsigned int k_version = 1;
};

}

Q_DECLARE_METATYPE(olive::MainWindowLayoutInfo)

#endif // OAK_MAINWINDOWLAYOUTINFO_H
