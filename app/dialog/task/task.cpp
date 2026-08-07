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

#include "task.h"

#include <QFutureWatcher>
#include <QtConcurrent>

#include "oakengine/task.h"

namespace olive
{

#define super ProgressDialog

TaskDialog::TaskDialog(OakEngineTask *task, const QString &title, QWidget *parent)
	: super([&]() {
			char buf[512];
			buf[0] = '\0';
			oakengine_task_title(task, buf, sizeof(buf));
			return QString::fromUtf8(buf);
		}(), title, parent)
	, task_(task)
	, destroy_on_close_(true)
	, already_shown_(false)
	, task_finished_(false)
{
	// Connect to the single async event dispatcher (issue 0b).
	if (AsyncEngineEvents *async = AsyncEngineEvents::instance()) {
		connect(async, &AsyncEngineEvents::task_progress, this,
				[this](OakEngineTask *t, double progress) {
					if (t == task_) {
						set_progress(progress);
					}
				}, Qt::QueuedConnection);
	}

	connect(this, &TaskDialog::cancelled, this, [this]() {
		oakengine_task_cancel(task_);
	}, Qt::DirectConnection);
}

TaskDialog::~TaskDialog()
{
	if (task_) {
		oakengine_task_free(task_);
	}
}

void TaskDialog::showEvent(QShowEvent *e)
{
	super::showEvent(e);

	if (!already_shown_) {
		QFutureWatcher<bool> *task_watcher = new QFutureWatcher<bool>();

		connect(task_watcher, &QFutureWatcher<bool>::finished, this,
				&TaskDialog::task_finished, Qt::QueuedConnection);

		task_watcher->setFuture(
			QtConcurrent::run([this]() -> bool {
				return oakengine_task_start_sync(task_) == 1;
			})
		);

		already_shown_ = true;
	}
}

void TaskDialog::closeEvent(QCloseEvent *e)
{
	oakengine_task_cancel(task_);

	super::closeEvent(e);

	already_shown_ = false;

	if (destroy_on_close_ && task_finished_) {
		deleteLater();
	}
}

void TaskDialog::task_finished()
{
	QFutureWatcher<bool> *task_watcher =
		static_cast<QFutureWatcher<bool> *>(sender());

	task_finished_ = true;

	if (task_watcher->result()) {
		emit task_succeeded(task_);
	} else {
		char err[512];
		err[0] = '\0';
		oakengine_task_error(task_, err, sizeof(err));
		show_error_message(tr("Task Failed"), QString::fromUtf8(err));
		emit task_failed(task_);
	}

	task_watcher->deleteLater();

	close();
}

}
