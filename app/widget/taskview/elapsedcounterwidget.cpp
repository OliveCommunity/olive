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

#include "elapsedcounterwidget.h"

#include <olive/core/core.h>
#include <QDateTime>
#include <QHBoxLayout>
#include <cmath>

namespace olive
{

using namespace core;

ElapsedCounterWidget::ElapsedCounterWidget(QWidget *parent)
	: QWidget(parent)
	, last_progress_(0)
	, start_time_(0)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setSpacing(layout->spacing() * 8);
	layout->setContentsMargins(0, 0, 0, 0);

	elapsed_lbl_ = new QLabel();
	layout->addWidget(elapsed_lbl_);

	remaining_lbl_ = new QLabel();
	layout->addWidget(remaining_lbl_);

	elapsed_timer_.setInterval(500);
	connect(&elapsed_timer_, &QTimer::timeout, this,
			&ElapsedCounterWidget::update_timers);
	update_timers();
}

void ElapsedCounterWidget::set_progress(double d)
{
	last_progress_ = d;
	update_timers();
}

void ElapsedCounterWidget::start()
{
	start(QDateTime::currentMSecsSinceEpoch());
}

void ElapsedCounterWidget::start(qint64 start_time)
{
	start_time_ = start_time;
	elapsed_timer_.start();
	update_timers();
}

void ElapsedCounterWidget::stop()
{
	elapsed_timer_.stop();
}

void ElapsedCounterWidget::update_timers()
{
	int64_t elapsed_ms, remaining_ms;

	if (last_progress_ > 0) {
		elapsed_ms = QDateTime::currentMSecsSinceEpoch() - start_time_;

		double ms_per_progress_unit = elapsed_ms / last_progress_;
		double remaining_progress = 1.0 - last_progress_;

		remaining_ms = std::ceil(ms_per_progress_unit * remaining_progress);
	} else {
		elapsed_ms = 0;
		remaining_ms = 0;
	}

	elapsed_lbl_->setText(
		tr("Elapsed: %1")
			.arg(QString::fromStdString(Timecode::time_to_string(elapsed_ms))));
	remaining_lbl_->setText(tr("Remaining: %1")
								.arg(QString::fromStdString(
									Timecode::time_to_string(remaining_ms))));
}

}
