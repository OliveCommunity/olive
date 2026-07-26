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

#include "taskviewitem.h"

#include <QDateTime>
#include <QVBoxLayout>

#include "ui/icons/icons.h"

namespace olive
{

TaskViewItem::TaskViewItem(OakEngineTask *task, QWidget *parent)
	: QFrame(parent)
	, task_(task)
{
	// Draw border around this item
	setFrameShape(QFrame::StyledPanel);

	// Create layout
	QVBoxLayout *layout = new QVBoxLayout(this);

	// Create header label
	task_name_lbl_ = new QLabel(this);
	char title_buf[512];
	title_buf[0] = '\0';
	oakengine_task_title(task_, title_buf, sizeof(title_buf));
	task_name_lbl_->setText(QString::fromUtf8(title_buf));
	layout->addWidget(task_name_lbl_);

	// Create center layout (combines progress bar and a cancel button)
	QHBoxLayout *middle_layout = new QHBoxLayout();
	layout->addLayout(middle_layout);

	// Create progress bar
	progress_bar_ = new QProgressBar(this);
	progress_bar_->setRange(0, 100);
	middle_layout->addWidget(progress_bar_);

	// Create cancel button
	cancel_btn_ = new QPushButton(this);
	cancel_btn_->setIcon(icon::error);
	middle_layout->addWidget(cancel_btn_);

	// Create stack with error label and elapsed/remaining time
	status_stack_ = new QStackedWidget();
	status_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
	layout->addWidget(status_stack_);

	// Create elapsed timer
	elapsed_timer_lbl_ = new ElapsedCounterWidget();
	status_stack_->addWidget(elapsed_timer_lbl_);

	// Create error label
	task_error_lbl_ = new QLabel(this);
	status_stack_->addWidget(task_error_lbl_);

	// Set up elapsed timer
	status_stack_->setCurrentWidget(elapsed_timer_lbl_);

	// Connect to the task via EngineEventBridge
	bridge_ = new EngineEventBridge(this);
	bridge_->subscribe(task_, OAKENGINE_EVENT_TASK_STARTED);
	bridge_->subscribe(task_, OAKENGINE_EVENT_TASK_PROGRESS);
	connect(bridge_, &EngineEventBridge::task_started, this,
			[this](OakEngineTask *, qint64 start_time) {
				elapsed_timer_lbl_->start(start_time);
			});
	connect(bridge_, &EngineEventBridge::task_progress, this,
			[this](OakEngineTask *, double progress) {
				update_progress(progress);
			});
	connect(cancel_btn_, &QPushButton::clicked, this,
			[this] { emit task_cancelled(task_); });
}

void TaskViewItem::failed()
{
	status_stack_->setCurrentWidget(task_error_lbl_);
	task_error_lbl_->setStyleSheet("color: red");
	char err[512];
	err[0] = '\0';
	oakengine_task_error(task_, err, sizeof(err));
	task_error_lbl_->setText(tr("Error: %1").arg(QString::fromUtf8(err)));
}

void TaskViewItem::update_progress(double d)
{
	progress_bar_->setValue(qRound(100.0 * d));
	elapsed_timer_lbl_->set_progress(d);
}

}
