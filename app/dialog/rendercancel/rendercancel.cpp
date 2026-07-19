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

#include "rendercancel.h"

namespace olive
{

RenderCancelDialog::RenderCancelDialog(QWidget *parent)
	: ProgressDialog(tr("Waiting for workers to finish..."), tr("Renderer"),
					 parent)
	, busy_workers_(0)
	, total_workers_(0)
{
}

void RenderCancelDialog::run_if_workers_are_busy()
{
	if (busy_workers_ > 0) {
		waiting_workers_ = busy_workers_;

		exec();
	}
}

void RenderCancelDialog::set_worker_count(int count)
{
	total_workers_ = count;

	update_progress();
}

void RenderCancelDialog::worker_started()
{
	busy_workers_++;

	update_progress();
}

void RenderCancelDialog::worker_done()
{
	busy_workers_--;

	update_progress();
}

void RenderCancelDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);

	update_progress();
}

void RenderCancelDialog::update_progress()
{
	if (!total_workers_ || !isVisible()) {
		return;
	}

	set_progress(
		qRound(100.0 * static_cast<double>(waiting_workers_ - busy_workers_) /
			   static_cast<double>(waiting_workers_)));

	if (busy_workers_ == 0) {
		accept();
	}
}

}
