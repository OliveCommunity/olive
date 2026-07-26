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

#ifndef OAK_MULTICAMWIDGET_H
#define OAK_MULTICAMWIDGET_H

#include "multicamdisplay.h"
#include <cstdint>

#include "node/input/multicam/multicamnode.h"
#include "widget/viewer/viewer.h"

namespace olive
{

class MulticamWidget : public TimeBasedWidget {
	Q_OBJECT
public:
	explicit MulticamWidget(QWidget *parent = nullptr);

	MulticamDisplay *get_display_widget() const
	{
		return display_;
	}

	void set_multicam_node(ViewerOutput *viewer, MultiCamNode *n, ClipBlock *clip,
						 const Rational &time);

protected:
	virtual void ConnectNodeEvent(ViewerOutput *n) override;
	virtual void DisconnectNodeEvent(ViewerOutput *n) override;
	virtual void TimeChangedEvent(const Rational &t) override;

signals:
	void switched();

private:
	void set_multicam_node_internal(ViewerOutput *viewer, MultiCamNode *n,
								 ClipBlock *clip);

	void Switch(int source, bool split_clip);

	ViewerSizer *sizer_;

	int64_t viewer_sub_ = 0;
	int64_t viewer_sub2_ = 0;

	MulticamDisplay *display_;

	MultiCamNode *node_;

	ClipBlock *clip_;

	struct MulticamNodeQueue {
		Rational time;
		ViewerOutput *viewer;
		MultiCamNode *node;
		ClipBlock *clip;
	};

	std::list<MulticamNodeQueue> play_queue_;

private slots:
	void display_clicked(const QPoint &p);
};

}

#endif // OAK_MULTICAMWIDGET_H
