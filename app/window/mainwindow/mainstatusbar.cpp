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

#include "mainstatusbar.h"

#include <QCoreApplication>

#include "engineeventbridge.h"

namespace olive
{

MainStatusBar::MainStatusBar(QWidget *parent)
	: QStatusBar(parent)
	, bridge_(nullptr)
	, connected_task_(nullptr)
	, task_progress_sub_(0)
	, task_finished_sub_(0)
{
	setSizeGripEnabled(false);

	bar_ = new QProgressBar();
	addPermanentWidget(bar_);

	bar_->setMinimum(0);
	bar_->setMaximum(100);
	bar_->setVisible(false);

	showMessage(tr("Welcome to %1 %2")
					.arg(QCoreApplication::applicationName(),
						 QCoreApplication::applicationVersion()),
				10000);
}

void MainStatusBar::connect_task_manager(EngineEventBridge *bridge)
{
	bridge_ = bridge;
	bridge_->subscribe(oakengine_task_manager_handle(),
					   OAKENGINE_EVENT_TASK_MANAGER_LIST_CHANGED);
	connect(bridge_, &EngineEventBridge::task_manager_list_changed,
			this, &MainStatusBar::update_status);

	// Route progress/finished events for any task we track
	connect(bridge_, &EngineEventBridge::task_progress,
			this, [this](OakEngineTask *task, double d) {
				if (task == connected_task_) {
					set_progress_bar_value(d);
				}
			});
	connect(bridge_, &EngineEventBridge::task_finished,
			this, [this](OakEngineTask *task, bool) {
				if (task == connected_task_) {
					connected_task_finished();
				}
			});
}

void MainStatusBar::update_status()
{
	const int count = oakengine_task_manager_count();
	if (count <= 0) {
		clearMessage();
		bar_->setVisible(false);
		bar_->setValue(0);
		connected_task_ = nullptr;
		return;
	}

	OakEngineTask *t = oakengine_task_manager_first();

	if (count == 1) {
		char title[256];
		oakengine_task_title(t, title, sizeof(title));
		showMessage(QString::fromUtf8(title));
	} else {
		showMessage(tr("Running %n background task(s)", nullptr, count));
	}

	bar_->setVisible(true);

	// Unsubscribe from previous task events
	if (task_progress_sub_ > 0) {
		bridge_->unsubscribe(task_progress_sub_);
		task_progress_sub_ = 0;
	}
	if (task_finished_sub_ > 0) {
		bridge_->unsubscribe(task_finished_sub_);
		task_finished_sub_ = 0;
	}

	connected_task_ = t;

	// Subscribe to progress and finished events on this task
	task_progress_sub_ = bridge_->subscribe(
		t, OAKENGINE_EVENT_TASK_PROGRESS);
	task_finished_sub_ = bridge_->subscribe(
		t, OAKENGINE_EVENT_TASK_FINISHED);
}

void MainStatusBar::set_progress_bar_value(double d)
{
	bar_->setValue(qRound(100.0 * d));
}

void MainStatusBar::connected_task_finished()
{
	connected_task_ = nullptr;
}

void MainStatusBar::mouseDoubleClickEvent(QMouseEvent *e)
{
	QStatusBar::mouseDoubleClickEvent(e);

	emit double_clicked();
}

}
