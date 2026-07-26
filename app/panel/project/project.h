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

#ifndef OAK_PROJECT_PANEL_H
#define OAK_PROJECT_PANEL_H

#include "footagemanagementpanel.h"
#include "node/project.h"
#include "panel/panel.h"
#include "widget/projectexplorer/projectexplorer.h"

struct OakEngineNode;

namespace olive
{

/**
 * @brief A PanelWidget wrapper around a ProjectExplorer and a ProjectToolbar
 */
class ProjectPanel : public PanelWidget, public FootageManagementPanel {
	Q_OBJECT
public:
	ProjectPanel(const QString &unique_name);
	~ProjectPanel() override;

	Project *project() const;
	void set_project(Project *p);

	Folder *get_root() const;

	void set_root(Folder *item);

	QVector<Node *> selected_items() const;

	Folder *get_selected_folder() const;

	virtual QVector<OakEngineNode *> get_selected_footage() const override;

	ProjectViewModel *model() const;

	bool select_item(Node *n, bool deselect_all_first = true)
	{
		return explorer_->select_item(n, deselect_all_first);
	}

	virtual void select_all() override;
	virtual void deselect_all() override;

	virtual void delete_selected() override;

	virtual void rename_selected() override;

public slots:
	void edit(OakEngineNode *item);

signals:
	void project_name_changed();

	void selection_changed(const QVector<OakEngineNode *> &selected);

private:
	virtual void retranslate() override;

	ProjectExplorer *explorer_;

	// Event subscription IDs (replaces connect to Project signals)
	int64_t project_name_sub_ = 0;

private slots:
	void item_double_click_slot(OakEngineNode *item);

	void show_new_menu();

	void update_subtitle();

	void save_connected_project();
};

}

#endif // OAK_PROJECT_PANEL_H
