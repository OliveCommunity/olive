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

#ifndef OAK_TASKDIALOG_H
#define OAK_TASKDIALOG_H

#include "dialog/progress/progress.h"
#include "engineeventbridge.h"
#include "oakengine/task.h"

namespace olive
{

class TaskDialog : public ProgressDialog {
	Q_OBJECT
public:
	TaskDialog(OakEngineTask *task, const QString &title, QWidget *parent = nullptr);

	~TaskDialog() override;

	void set_destroy_on_close(bool e)
	{
		destroy_on_close_ = e;
	}

	OakEngineTask *get_task() const
	{
		return task_;
	}

protected:
	virtual void showEvent(QShowEvent *e) override;

	virtual void closeEvent(QCloseEvent *e) override;

signals:
	void task_succeeded(OakEngineTask *task);

	void task_failed(OakEngineTask *task);

private:
	OakEngineTask *task_;

	EngineEventBridge *bridge_ = nullptr;

	bool destroy_on_close_;

	bool already_shown_;

	bool task_finished_;

private slots:
	void task_finished();
};

}

#endif // OAK_TASKDIALOG_H
