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

#ifndef OAK_TASKVIEWITEM_H
#define OAK_TASKVIEWITEM_H

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

#include "asyncengineevents.h"
#include "elapsedcounterwidget.h"
#include "oakengine/task.h"

namespace olive
{

class TaskViewItem : public QFrame {
	Q_OBJECT
public:
	TaskViewItem(OakEngineTask *task, QWidget *parent = nullptr);

	void failed();

signals:
	void task_cancelled(OakEngineTask *t);

private:
	QLabel *task_name_lbl_;
	QProgressBar *progress_bar_;
	QPushButton *cancel_btn_;

	QStackedWidget *status_stack_;
	ElapsedCounterWidget *elapsed_timer_lbl_;
	QLabel *task_error_lbl_;

	OakEngineTask *task_;

private slots:
	void update_progress(double d);
};

}

#endif // OAK_TASKVIEWITEM_H
