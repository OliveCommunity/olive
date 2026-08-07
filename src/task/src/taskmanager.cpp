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

namespace olive
{

TaskManager *TaskManager::instance_ = nullptr;

TaskManager::~TaskManager()
{
	std::vector<std::unique_ptr<Entry>> tasks;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		tasks = std::move(tasks_);
	}

	for (auto &entry : tasks) {
		entry->task->cancel();
	}
	for (auto &entry : tasks) {
		if (entry->thread.joinable()) {
			entry->thread.join();
		}
	}
}

void TaskManager::create_instance()
{
	instance_ = new TaskManager();
}

void TaskManager::destroy_instance()
{
	delete instance_;
	instance_ = nullptr;
}

TaskManager *TaskManager::instance()
{
	return instance_;
}

int TaskManager::get_task_count() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
	return int(tasks_.size());
}

Task *TaskManager::get_task_at(int index) const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mutex_));
	if (index < 0 || index >= int(tasks_.size())) {
		return nullptr;
	}
	return tasks_[index]->task;
}

void TaskManager::add_task(Task *t)
{
	auto entry = std::make_unique<Entry>();
	entry->task = t;
	entry->finished = std::make_shared<std::atomic<bool>>(false);
	entry->succeeded = std::make_shared<std::atomic<bool>>(false);

	std::shared_ptr<std::atomic<bool>> finished = entry->finished;
	std::shared_ptr<std::atomic<bool>> succeeded = entry->succeeded;

	entry->thread = std::thread([t, finished, succeeded]() {
		bool ret = t->start();
		succeeded->store(ret);
		finished->store(true);
	});

	std::lock_guard<std::mutex> lock(mutex_);
	tasks_.push_back(std::move(entry));
}

void TaskManager::cancel_task(Task *t)
{
	t->cancel();
}

void TaskManager::cancel_task_and_wait(Task *t)
{
	t->cancel();

	std::thread tmp;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (auto &entry : tasks_) {
			if (entry->task == t && entry->thread.joinable()) {
				tmp = std::move(entry->thread);
				break;
			}
		}
	}
	if (tmp.joinable()) {
		tmp.join();
	}
}

void TaskManager::delete_finished()
{
	std::vector<std::unique_ptr<Entry>> finished_entries;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = tasks_.begin();
		while (it != tasks_.end()) {
			if ((*it)->finished->load()) {
				finished_entries.push_back(std::move(*it));
				it = tasks_.erase(it);
			} else {
				it++;
			}
		}
	}

	for (auto &entry : finished_entries) {
		if (entry->thread.joinable()) {
			entry->thread.join();
		}
	}
}

}
