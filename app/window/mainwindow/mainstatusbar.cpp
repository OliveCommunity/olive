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
	, connected_task_(nullptr)
{
	setSizeGripEnabled(false);

	bar_ = new QProgressBar();
	addPermanentWidget(bar_);

	bar_->setMinimum(0);
	bar_->setMaximum(100);
	bar_->setVisible(false);

	// Permanent right-hand sequence info chips (resolution + frame rate)
	resolution_label_ = new QLabel();
	fps_label_ = new QLabel();
	resolution_label_->setContentsMargins(6, 0, 6, 0);
	fps_label_->setContentsMargins(6, 0, 6, 0);
	addPermanentWidget(resolution_label_);
	addPermanentWidget(fps_label_);
	resolution_label_->setVisible(false);
	fps_label_->setVisible(false);

	showMessage(tr("Welcome to %1 %2")
					.arg(QCoreApplication::applicationName(),
						 QCoreApplication::applicationVersion()),
				10000);

	// Connect to the single async event dispatcher (issue 0b).
	if (AsyncEngineEvents *async = AsyncEngineEvents::instance()) {
		connect(async, &AsyncEngineEvents::task_manager_list_changed, this,
				&MainStatusBar::update_status);
		connect(async, &AsyncEngineEvents::task_progress, this,
				[this](OakEngineTask *task, double d) {
					if (task == connected_task_) {
						set_progress_bar_value(d);
					}
				});
		connect(async, &AsyncEngineEvents::task_finished, this,
				[this](OakEngineTask *task, bool) {
					if (task == connected_task_) {
						connected_task_finished();
					}
				});
	}
}

void MainStatusBar::set_sequence_info(int width, int height, double fps)
{
	if (width <= 0 || height <= 0) {
		resolution_label_->setVisible(false);
		fps_label_->setVisible(false);
		return;
	}

	resolution_label_->setText(QStringLiteral("%1\u00d7%2").arg(width).arg(height));

	// Show the frame rate as an integer when it is whole, otherwise keep one
	// decimal place (e.g. 23.976 FPS).
	const double rounded = qRound(fps);
	QString fps_str = (qFuzzyCompare(fps, rounded))
						  ? QString::number(static_cast<int>(rounded))
						  : QString::number(fps, 'f', 2);
	fps_label_->setText(tr("%1 FPS").arg(fps_str));

	resolution_label_->setVisible(true);
	fps_label_->setVisible(true);
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

	connected_task_ = t;
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
