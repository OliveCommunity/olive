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

#include "task/task.h"

#include "../src/taskmanager.h"
#include "taskhandle.h"

using namespace oaktask_capi;

void oaktask_task_free(OakTaskTask *t)
{
	if (!t) {
		return;
	}
	TaskHandle *h = impl(t);
	if (h->owns_task) {
		delete h->task;
	}
	delete h;
	alive()--;
}

int oaktask_task_start_sync(OakTaskTask *t)
{
	if (!t) {
		return OAKTASK_E_INVALID;
	}
	try {
		return impl(t)->task->start() ? 1 : 0;
	} catch (...) {
		return OAKTASK_E_FAILED;
	}
}

int oaktask_task_start(OakTaskTask *t)
{
	if (!t) {
		return OAKTASK_E_INVALID;
	}
	if (!olive::TaskManager::instance()) {
		return OAKTASK_E_STATE;
	}

	TaskHandle *h = impl(t);
	if (h->running_on_manager) {
		return OAKTASK_E_STATE;
	}

	olive::TaskManager::instance()->add_task(h->task);
	h->running_on_manager = true;
	// Ownership transfers to the manager (deleted by delete_finished() /
	// shutdown); freeing this handle later only releases the wrapper.
	h->owns_task = false;
	return OAKTASK_OK;
}

int oaktask_task_cancel(OakTaskTask *t)
{
	if (!t) {
		return OAKTASK_E_INVALID;
	}
	impl(t)->task->cancel();
	return OAKTASK_OK;
}

int oaktask_task_wait(OakTaskTask *t)
{
	if (!t) {
		return OAKTASK_E_INVALID;
	}
	TaskHandle *h = impl(t);
	if (h->running_on_manager && olive::TaskManager::instance()) {
		olive::TaskManager::instance()->cancel_task_and_wait(h->task);
		h->finished = true;
	}
	return OAKTASK_OK;
}

int oaktask_task_is_finished(const OakTaskTask *t)
{
	if (!t) {
		return OAKTASK_E_INVALID;
	}
	return impl(t)->finished.load() ? 1 : 0;
}

int oaktask_task_succeeded(const OakTaskTask *t)
{
	if (!t) {
		return OAKTASK_E_INVALID;
	}
	return impl(t)->succeeded.load() ? 1 : 0;
}

int oaktask_task_title(OakTaskTask *t, char *buf, int buf_size)
{
	if (!t) {
		return OAKTASK_E_INVALID;
	}
	return copy_string(impl(t)->task->get_title(), buf, buf_size);
}

int oaktask_task_error(OakTaskTask *t, char *buf, int buf_size)
{
	if (!t) {
		return OAKTASK_E_INVALID;
	}
	return copy_string(impl(t)->task->get_error(), buf, buf_size);
}

int64_t oaktask_task_subscribe(OakTaskTask *t, oaktask_event_fn fn,
							   void *userdata)
{
	if (!t || !fn) {
		return OAKTASK_E_INVALID;
	}

	try {
		impl(t)->task->add_event_listener(
			[fn, userdata](olive::Task::EventType type, double value) {
				int event_id = OAKTASK_EVENT_PROGRESS;
				switch (type) {
				case olive::Task::k_event_started:
					event_id = OAKTASK_EVENT_STARTED;
					break;
				case olive::Task::k_event_finished:
					event_id = OAKTASK_EVENT_FINISHED;
					break;
				default:
					break;
				}
				fn(event_id, value, userdata);
			});
		return 0;
	} catch (...) {
		return OAKTASK_E_FAILED;
	}
}

int oaktask_debug_alive_count(void)
{
	return alive().load();
}
