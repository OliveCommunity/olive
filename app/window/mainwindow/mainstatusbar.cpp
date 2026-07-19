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

namespace olive
{

MainStatusBar::MainStatusBar(QWidget *parent)
	: QStatusBar(parent)
	, manager_(nullptr)
	, connected_task_(nullptr)
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

void MainStatusBar::connect_task_manager(TaskManager *manager)
{
	if (manager_) {
		disconnect(manager_, &TaskManager::task_list_changed, this,
				   &MainStatusBar::update_status);
	}

	manager_ = manager;

	if (manager_) {
		connect(manager_, &TaskManager::task_list_changed, this,
				&MainStatusBar::update_status);
	}
}

void MainStatusBar::update_status()
{
	if (!manager_) {
		return;
	}

	if (manager_->get_task_count() == 0) {
		clearMessage();
		bar_->setVisible(false);
		bar_->setValue(0);
	} else {
		Task *t = manager_->get_first_task();

		if (manager_->get_task_count() == 1) {
			showMessage(t->get_title());
		} else {
			showMessage(tr("Running %n background task(s)", nullptr,
						   manager_->get_task_count()));
		}

		bar_->setVisible(true);

		if (connected_task_) {
			disconnect(connected_task_, &Task::progress_changed, this,
					   &MainStatusBar::set_progress_bar_value);
			disconnect(connected_task_, &Task::destroyed, this,
					   &MainStatusBar::connected_task_deleted);
		}

		connected_task_ = t;
		connect(connected_task_, &Task::progress_changed, this,
				&MainStatusBar::set_progress_bar_value);
		connect(connected_task_, &Task::destroyed, this,
				&MainStatusBar::connected_task_deleted);
	}
}

void MainStatusBar::set_progress_bar_value(double d)
{
	bar_->setValue(qRound(100.0 * d));
}

void MainStatusBar::connected_task_deleted()
{
	connected_task_ = nullptr;
}

void MainStatusBar::mouseDoubleClickEvent(QMouseEvent *e)
{
	QStatusBar::mouseDoubleClickEvent(e);

	emit double_clicked();
}

}
