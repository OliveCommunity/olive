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

#include "engineeventbridge.h"
#include "oakengine/task.h"

namespace olive
{

TaskManagerPanel::TaskManagerPanel()
	: PanelWidget(QStringLiteral("TaskManagerPanel"))
	, bridge_(new EngineEventBridge(this))
{
	// Create task view
	view_ = new TaskView(this);

	// Set it as the main widget
	setWidget(view_);

	// Connect task view to the task manager via EngineEventBridge
	bridge_->subscribe(oakengine_task_manager_handle(),
					   OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED);
	bridge_->subscribe(oakengine_task_manager_handle(),
					   OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED);
	bridge_->subscribe(oakengine_task_manager_handle(),
					   OAKENGINE_EVENT_TASK_MANAGER_TASK_FAILED);

	connect(bridge_, &EngineEventBridge::task_manager_task_added, this,
			[this](OakEngineTask *task, const QString &) {
				view_->add_task(task);
			});
	connect(bridge_, &EngineEventBridge::task_manager_task_removed, this,
			[this](OakEngineTask *task) {
				view_->remove_task(task);
			});
	connect(bridge_, &EngineEventBridge::task_manager_task_failed, this,
			[this](OakEngineTask *task) {
				view_->task_failed(task);
			});
	connect(view_, &TaskView::task_cancelled, this,
			[](OakEngineTask *t) {
				oakengine_task_manager_cancel(t);
			});

	// Set strings
	retranslate();
}

void TaskManagerPanel::retranslate()
{
	set_title(tr("Task Manager"));
}

}
