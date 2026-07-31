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

#include "project.h"

#include <QMenu>
#include <QVBoxLayout>

#include "core.h"
#include "oakengine/events.h"
#include "oakengine/project.h"
#include "panel/footageviewer/footageviewer.h"
#include "panel/timeline/timeline.h"
#include "panel/panelmanager.h"
#include "widget/menu/menushared.h"
#include "widget/projecttoolbar/projecttoolbar.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

ProjectPanel::~ProjectPanel()
{
	if (project_name_sub_ > 0) {
		oakengine_event_unsubscribe(project_name_sub_);
		project_name_sub_ = 0;
	}
}

ProjectPanel::ProjectPanel(const QString &unique_name)
	: PanelWidget(unique_name)
{
	// Create main widget and its layout
	QWidget *central_widget = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(central_widget);
	layout->setContentsMargins(0, 0, 0, 0);

	set_widget_with_padding(central_widget);

	// Set up project toolbar
	ProjectToolbar *toolbar = new ProjectToolbar(this);
	layout->addWidget(toolbar);

	// Make toolbar connections
	connect(toolbar, &ProjectToolbar::new_clicked, this,
			&ProjectPanel::show_new_menu);
	connect(toolbar, &ProjectToolbar::open_clicked, Core::instance(),
			&Core::open_project);
	connect(toolbar, &ProjectToolbar::save_clicked, this,
			&ProjectPanel::save_connected_project);

	// Set up main explorer object
	explorer_ = new ProjectExplorer(this);
	layout->addWidget(explorer_);
	connect(explorer_, &ProjectExplorer::double_clicked_item, this,
			&ProjectPanel::item_double_click_slot);
	connect(explorer_, &ProjectExplorer::selection_changed, this,
			&ProjectPanel::selection_changed);
	connect(toolbar, &ProjectToolbar::search_changed, explorer_,
			&ProjectExplorer::set_search_filter);

	// Set toolbar's view to the explorer's view
	toolbar->set_view(explorer_->view_type());

	// Connect toolbar's view change signal to the explorer's view change slot
	connect(toolbar, &ProjectToolbar::view_changed, explorer_,
			&ProjectExplorer::set_view_type);

	// Set strings
	retranslate();
}

oak::Project ProjectPanel::project() const
{
	return explorer_->project();
}

void ProjectPanel::set_project(oak::Project p)
{
	if (project()) {
		if (project_name_sub_ > 0) {
			oakengine_event_unsubscribe(project_name_sub_);
			project_name_sub_ = 0;
		}
	}

	explorer_->set_project(p);

	if (project()) {
		project_name_sub_ = oakengine_event_subscribe(
			project().handle(), OAKENGINE_EVENT_PROJECT_NAME_CHANGED,
			[](const oakengine_event *, void *userdata) {
				auto *s = static_cast<ProjectPanel *>(userdata);
				s->update_subtitle();
				s->project_name_changed();
			},
			this);
	}

	update_subtitle();

	emit project_name_changed();
}

oak::Node ProjectPanel::get_root() const
{
	return explorer_->get_root();
}

void ProjectPanel::set_root(oak::Node item)
{
	explorer_->set_root(item);

	retranslate();
}

QVector<oak::Node> ProjectPanel::selected_items() const
{
	return explorer_->selected_items();
}

oak::Node ProjectPanel::get_selected_folder() const
{
	return explorer_->get_selected_folder();
}

ProjectViewModel *ProjectPanel::model() const
{
	return explorer_->model();
}

void ProjectPanel::select_all()
{
	explorer_->select_all();
}

void ProjectPanel::deselect_all()
{
	explorer_->deselect_all();
}

void ProjectPanel::delete_selected()
{
	explorer_->delete_selected();
}

void ProjectPanel::rename_selected()
{
	explorer_->rename_selected_item();
}

void ProjectPanel::edit(OakEngineNode *item)
{
	explorer_->edit(item);
}

void ProjectPanel::retranslate()
{
	if (project() && explorer_->get_root() != project().root()) {
		set_title(tr("Folder"));
	} else {
		set_title(tr("Project"));
	}

	update_subtitle();
}

void ProjectPanel::item_double_click_slot(OakEngineNode *item_handle)
{
	oak::Node item(item_handle);
	if (item == nullptr) {
		// If the user double clicks on empty space, show the import dialog
		Core::instance()->dialog_import_show();
	} else if (item.is_footage()) {
		// Open this footage in a FootageViewer
		auto panel =
			PanelManager::instance()->most_recently_focused<FootageViewerPanel>();
		panel->connect_viewer_node(item.handle());
		panel->raise();
		panel->setFocus(Qt::FocusReason::MouseFocusReason);
	} else if (item.is_sequence()) {
		// Open this sequence in the Timeline
		Core::instance()->main_window()->open_sequence(item.handle());
	}
}

void ProjectPanel::show_new_menu()
{
	Menu new_menu(this);

	MenuShared::instance()->add_items_for_new_menu(&new_menu);

	new_menu.exec(QCursor::pos());
}

void ProjectPanel::update_subtitle()
{
	if (project()) {
		QString project_title = project().name();

		if (explorer_->get_root() != project().root()) {
			QString folder_path;

			oak::Node item = explorer_->get_root();

			do {
				folder_path.prepend(
					QStringLiteral("/%1").arg(item.get_label()));

				item = item.folder();
			} while (item != project().root());

			project_title.append(folder_path);
		}

		set_subtitle(project_title);
	} else {
		set_subtitle(tr("(none)"));
	}
}

void ProjectPanel::save_connected_project()
{
	Core::instance()->save_project();
}

QVector<OakEngineNode *> ProjectPanel::get_selected_footage() const
{
	QVector<oak::Node> items = selected_items();
	QVector<OakEngineNode *> footage;

	foreach (const oak::Node &i, items) {
		if (i.is_viewer_output()) {
			footage.append(i.handle());
		}
	}

	return footage;
}

}
