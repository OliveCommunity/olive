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

#ifndef OAK_PROJECTEXPLORER_H
#define OAK_PROJECTEXPLORER_H

#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeView>

#include "node/project.h"
#include "projectviewmodel.h"
#include "widget/projectexplorer/projectexplorericonview.h"
#include "widget/projectexplorer/projectexplorerlistview.h"
#include "widget/projectexplorer/projectexplorertreeview.h"
#include "widget/projectexplorer/projectexplorernavigation.h"
#include "widget/projecttoolbar/projecttoolbar.h"

namespace olive
{

/**
 * @brief A widget for browsing through a Project structure.
 *
 * ProjectExplorer automatically handles the view<->model system using a ProjectViewModel. Therefore, all that needs to
 * be provided is the Project structure itself.
 *
 * This widget contains three views, tree view, list view, and icon view. These can be switched at any time.
 */
class ProjectExplorer : public QWidget {
	Q_OBJECT
public:
	ProjectExplorer(QWidget *parent);

	const ProjectToolbar::ViewType &view_type() const;

	Project *project() const;
	void set_project(Project *p);

	Folder *get_root() const;
	void set_root(Folder *item);

	QVector<Node *> selected_items() const;

	/**
   * @brief Use a heuristic to determine which (if any) folder is selected
   *
   * Generally for some import/adding processes, we assume that if a folder is selected, the user probably wants to
   * create the new object in it rather than in the root. If, however, more than one folder is selected, we can't
   * truly determine any folder from this and just return the root instead.
   *
   * @return
   *
   * A folder that's heuristically been determined as "selected", or the root directory if none, or nullptr if no
   * project is open.
   */
	Folder *get_selected_folder() const;

	/**
   * @brief Access the ViewModel model of the project
   */
	ProjectViewModel *model();

	void select_all();

	void deselect_all();

	void delete_selected();

	bool select_item(Node *n, bool deselect_all_first = true);

public slots:
	void set_view_type(ProjectToolbar::ViewType type);

	void edit(Node *item);

	void rename_selected_item();

	void set_search_filter(const QString &s);

signals:
	/**
   * @brief Emitted when an Item is double clicked
   *
   * @param item
   *
   * The Item that was double clicked, or nullptr if empty area was double clicked
   */
	void double_clicked_item(Node *item);

	void selection_changed(const QVector<Node *> &selected);

private:
	/**
   * @brief Get all the blocks that solely rely on an input node
   *
   * Ignores blocks that depend on multiple inputs
   */
	QList<Block *> get_footage_blocks(QList<Node *> nodes);

	/**
   * @brief Simple convenience function for adding a view to this stacked widget
   *
   * Mainly for use in the constructor. Adds the view, connects its signals/slots, and sets the model.
   *
   * @param view
   *
   * View to add to the stack
   */
	void add_view(QAbstractItemView *view);

	/**
   * @brief Browse to a specific folder index in the model
   *
   * Only affects list_view_ and icon_view_.
   *
   * @param index
   *
   * Either an invalid index to return to the project root, or an index to a valid Folder object.
   */
	void browse_to_folder(const QModelIndex &index);

	int confirm_item_deletion(Node *item);

	bool delete_items_internal(const QVector<Node *> &selected,
							 bool &check_if_item_is_in_use,
							 MultiUndoCommand *command);

	static QString get_human_readable_node_name(Node *node);

	void update_nav_bar_text();

	/**
   * @brief Get the currently active QAbstractItemView
   */
	QAbstractItemView *current_view() const;

	QStackedWidget *stacked_widget_;

	ProjectExplorerNavigation *nav_bar_;

	ProjectExplorerIconView *icon_view_;
	ProjectExplorerListView *list_view_;
	ProjectExplorerTreeView *tree_view_;

	ProjectToolbar::ViewType view_type_;

	QSortFilterProxyModel sort_model_;
	ProjectViewModel model_;

	QVector<Node *> context_menu_items_;

private slots:
	void view_empty_area_double_clicked_slot();

	void item_double_clicked_slot(const QModelIndex &index);

	void size_changed_slot(int s);

	void dir_up_slot();

	void show_context_menu();

	void show_item_properties_dialog();

	void reveal_selected_footage();

	void replace_selected_footage();

	void open_context_menu_item_in_new_tab();

	void open_context_menu_item_in_new_window();

	void generate_proxies_for_selected_footage();

	void set_selected_footage_proxy_enabled(bool enabled);

	void reveal_proxy_for_selected_footage();

	void delete_proxies_for_selected_footage();

	void show_proxy_dialog_for_selected_footage();

	void view_selection_changed();
};

}

#endif // OAK_PROJECTEXPLORER_H
