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

#ifndef OAK_TASKMANAGER_H
#define OAK_TASKMANAGER_H

#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "task.h"

namespace olive
{

/**
 * @brief An object that manages background Task objects, handling their start and end
 *
 * TaskManager handles the life of a Task object. After a new Task is created, it should be sent to TaskManager through
 * add_task(). TaskManager will take ownership of the task and run it on a worker thread.
 *
 * De-Qt version: no signals. The caller that creates/deletes tasks knows
 * what changed (01 §4), so no list-changed notification exists here.
 */
class TaskManager {
public:
	TaskManager() = default;

	/**
	 * @brief Ensures all Tasks are cancelled, joined and deleted
	 */
	virtual ~TaskManager();

	static void create_instance();

	static void destroy_instance();

	static TaskManager *instance();

	int get_task_count() const;

	Task *get_task_at(int index) const;

	/**
	 * @brief Add a new Task
	 *
	 * TaskManager takes ownership of this Task and will be responsible for freeing it.
	 * A Task object should only be added once.
	 */
	void add_task(Task *t);

	void cancel_task(Task *t);

	void cancel_task_and_wait(Task *t);

	/**
	 * @brief Delete all finished tasks (successful or failed)
	 */
	void delete_finished();

private:
	struct Entry {
		Task *task;
		std::thread thread;
		std::shared_ptr<std::atomic<bool>> finished;
		std::shared_ptr<std::atomic<bool>> succeeded;
	};

	std::mutex mutex_;
	std::vector<std::unique_ptr<Entry>> tasks_;

	static TaskManager *instance_;
};

}

#endif // OAK_TASKMANAGER_H
