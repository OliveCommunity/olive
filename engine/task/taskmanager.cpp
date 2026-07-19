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

#include <QDebug>
#include <QThread>

namespace olive
{

TaskManager *TaskManager::instance_ = nullptr;

TaskManager::TaskManager()
{
	thread_pool_.setMaxThreadCount(1);
}

TaskManager::~TaskManager()
{
	thread_pool_.clear();

	foreach (Task *t, tasks_) {
		t->Cancel();
	}

	thread_pool_.waitForDone();

	foreach (Task *t, tasks_) {
		t->deleteLater();
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
	return tasks_.size();
}

Task *TaskManager::get_first_task() const
{
	return tasks_.begin().value();
}

void TaskManager::cancel_task_and_wait(Task *t)
{
	t->Cancel();

	QFutureWatcher<bool> *w = tasks_.key(t);

	if (w) {
		w->waitForFinished();
	}
}

void TaskManager::add_task(Task *t)
{
	// Create a watcher for signalling
	QFutureWatcher<bool> *watcher = new QFutureWatcher<bool>();
	connect(watcher, &QFutureWatcher<bool>::finished, this,
			&TaskManager::task_finished);

	// Add the Task to the queue
	tasks_.insert(watcher, t);

	// Run task concurrently
	watcher->setFuture(
#if QT_VERSION_MAJOR >= 6
		QtConcurrent::run(&thread_pool_, &Task::start, t)
#else
		QtConcurrent::run(&thread_pool_, t, &Task::Start)
#endif
	);

	// Emit signal that a Task was added
	emit task_added(t);
	emit task_list_changed();
}

void TaskManager::cancel_task(Task *t)
{
	if (std::find(failed_tasks_.begin(), failed_tasks_.end(), t) !=
		failed_tasks_.end()) {
		failed_tasks_.remove(t);
		emit task_removed(t);
		t->deleteLater();
	} else {
		t->Cancel();
	}
}

void TaskManager::task_finished()
{
	QFutureWatcher<bool> *watcher =
		static_cast<QFutureWatcher<bool> *>(sender());
	Task *t = tasks_.value(watcher);

	tasks_.remove(watcher);

	if (watcher->result()) {
		// Task completed successfully
		emit task_removed(t);
		t->deleteLater();
	} else {
		// Task failed, keep it so the user can see the error message
		emit task_failed(t);
		failed_tasks_.push_back(t);
	}

	watcher->deleteLater();

	emit task_list_changed();
}

}
