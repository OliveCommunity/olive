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

#ifndef OAK_TASK_CAPI_TASKHANDLE_H
#define OAK_TASK_CAPI_TASKHANDLE_H

#include <atomic>
#include <cstring>
#include <new>
#include <string>

#include "../src/task.h"

/**
 * @brief Internal handle layout shared by oaktask's c_api translation
 *        units
 */
namespace oaktask_capi
{

struct TaskHandle {
	olive::Task *task;
	bool owns_task; /**< false for borrowed wrappers (oaktask_manager_at). */
	bool running_on_manager;
	std::atomic<bool> finished;
	std::atomic<bool> succeeded;
};

inline std::atomic<int> &alive()
{
	static std::atomic<int> g_alive{ 0 };
	return g_alive;
}

inline TaskHandle *impl(OakTaskTask *t)
{
	return reinterpret_cast<TaskHandle *>(t);
}

inline const TaskHandle *impl(const OakTaskTask *t)
{
	return reinterpret_cast<const TaskHandle *>(t);
}

inline OakTaskTask *wrap(olive::Task *task)
{
	if (!task) {
		return NULL;
	}
	TaskHandle *h = new (std::nothrow) TaskHandle{ task, true, false,
												   false, false };
	if (!h) {
		delete task;
		return NULL;
	}
	alive()++;

	std::atomic<bool> *finished = &h->finished;
	std::atomic<bool> *succeeded = &h->succeeded;
	task->add_event_listener(
		[finished, succeeded](olive::Task::EventType type, double value) {
			if (type == olive::Task::k_event_finished) {
				succeeded->store(value != 0.0);
				finished->store(true);
			}
		});

	return reinterpret_cast<OakTaskTask *>(h);
}

/**
 * @brief Wrap a manager-owned (borrowed) task. Freeing the handle does
 *        NOT delete the task.
 */
inline OakTaskTask *wrap_borrowed(olive::Task *task)
{
	if (!task) {
		return NULL;
	}
	TaskHandle *h = new (std::nothrow) TaskHandle{ task, false, true,
												   task->get_start_time() != 0,
												   false };
	if (!h) {
		return NULL;
	}
	alive()++;
	return reinterpret_cast<OakTaskTask *>(h);
}

inline int copy_string(const std::string &value, char *buf, int buf_size)
{
	int needed = int(value.size()) + 1;
	if (buf && buf_size >= needed) {
		memcpy(buf, value.c_str(), needed);
	}
	return needed;
}

} // namespace oaktask_capi

#endif // OAK_TASK_CAPI_TASKHANDLE_H
