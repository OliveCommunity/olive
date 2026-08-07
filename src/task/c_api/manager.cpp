/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "task/manager.h"

#include "../src/codecbridge.h"
#include "../src/taskmanager.h"
#include "taskhandle.h"

int oaktask_manager_init(void)
{
	if (olive::TaskManager::instance()) {
		return OAKTASK_E_STATE;
	}

	olive::TaskManager::create_instance();
	olive::register_codec_task_submitter();
	return OAKTASK_OK;
}

void oaktask_manager_shutdown(void)
{
	olive::unregister_codec_task_submitter();
	olive::TaskManager::destroy_instance();
}

int oaktask_register_codec_submitter(void)
{
	olive::register_codec_task_submitter();
	return OAKTASK_OK;
}

int oaktask_manager_count(void)
{
	if (!olive::TaskManager::instance()) {
		return OAKTASK_E_STATE;
	}
	return olive::TaskManager::instance()->get_task_count();
}

OakTaskTask oaktask_manager_at(int i)
{
	if (!olive::TaskManager::instance()) {
		return OakTaskTask{};
	}
	olive::Task *task = olive::TaskManager::instance()->get_task_at(i);
	// Borrowed wrapper: releasing it does not delete the task.
	return oaktask_capi::wrap_borrowed(task);
}

void oaktask_manager_delete_finished(void)
{
	if (olive::TaskManager::instance()) {
		olive::TaskManager::instance()->delete_finished();
	}
}
