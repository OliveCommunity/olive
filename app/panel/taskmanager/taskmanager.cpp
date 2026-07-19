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

#include "taskmanager.h"

#include "task/taskmanager.h"

namespace olive
{

TaskManagerPanel::TaskManagerPanel()
	: PanelWidget(QStringLiteral("TaskManagerPanel"))
{
	// Create task view
	view_ = new TaskView(this);

	// Set it as the main widget
	setWidget(view_);

	// Connect task view to the task manager
	connect(TaskManager::instance(), &TaskManager::task_added, view_,
			&TaskView::add_task);
	connect(TaskManager::instance(), &TaskManager::task_removed, view_,
			&TaskView::remove_task);
	connect(TaskManager::instance(), &TaskManager::task_failed, view_,
			&TaskView::task_failed);
	connect(view_, &TaskView::task_cancelled, TaskManager::instance(),
			&TaskManager::cancel_task);

	// Set strings
	retranslate();
}

void TaskManagerPanel::retranslate()
{
	set_title(tr("Task Manager"));
}

}
