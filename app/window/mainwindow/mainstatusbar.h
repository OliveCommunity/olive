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

#ifndef OAK_MAINSTATUSBAR_H
#define OAK_MAINSTATUSBAR_H

#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>

#include "oakengine/task.h"

namespace olive
{

class EngineEventBridge;

/**
 * @brief Shows abbreviated information from the global TaskManager
 *
 * Per the Oak UI design reference the status bar is a global information
 * strip. The transient task message is shown on the left while the active
 * sequence's resolution and frame rate are shown permanently on the right.
 */
class MainStatusBar : public QStatusBar {
	Q_OBJECT
public:
	MainStatusBar(QWidget *parent = nullptr);

	void connect_task_manager(EngineEventBridge *bridge);

	/**
	 * @brief Update the permanent right-hand sequence info chips.
	 *
	 * Plain (non-slot) method taking primitives so no engine C++ type leaks
	 * into the MOC-processed header. Pass width <= 0 to clear the chips.
	 */
	void set_sequence_info(int width, int height, double fps);

signals:
	void double_clicked();

protected:
	virtual void mouseDoubleClickEvent(QMouseEvent *e) override;

private slots:
	void update_status();

	void set_progress_bar_value(double d);

	void connected_task_finished();

private:
	EngineEventBridge *bridge_;

	QProgressBar *bar_;

	QLabel *resolution_label_;
	QLabel *fps_label_;

	OakEngineTask *connected_task_;

	int64_t task_progress_sub_;

	int64_t task_finished_sub_;
};

}

#endif // OAK_MAINSTATUSBAR_H
