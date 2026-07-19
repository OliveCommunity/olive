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

#ifndef OAK_RENDERCANCELDIALOG_H
#define OAK_RENDERCANCELDIALOG_H

#include "dialog/progress/progress.h"

namespace olive
{

class RenderCancelDialog : public ProgressDialog {
	Q_OBJECT
public:
	RenderCancelDialog(QWidget *parent = nullptr);

	void run_if_workers_are_busy();

	void set_worker_count(int count);

	void worker_started();

public slots:
	void worker_done();

protected:
	virtual void showEvent(QShowEvent *event) override;

private:
	void update_progress();

	int busy_workers_;

	int total_workers_;

	int waiting_workers_;
};

}

#endif // OAK_RENDERCANCELDIALOG_H
