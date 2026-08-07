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

#include "oakengine/timeline.h"
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

	void set_multicam_node(OakEngineNode *viewer, OakEngineNode *n,
						 OakEngineBlock *clip, const Rational &time);

protected:
	virtual void ConnectNodeEvent(OakEngineNode *n) override;
	virtual void DisconnectNodeEvent(OakEngineNode *n) override;
	virtual void TimeChangedEvent(const Rational &t) override;

signals:
	void switched();

private:
	void set_multicam_node_internal(OakEngineNode *viewer, OakEngineNode *n,
								 OakEngineBlock *clip);

	void Switch(int source, bool split_clip);

	void update_viewer_sizer();

	ViewerSizer *sizer_;

	int64_t viewer_sub_ = 0;
	int64_t viewer_sub2_ = 0;

	MulticamDisplay *display_;

	OakEngineNode *node_;

	OakEngineBlock *clip_;

	struct MulticamNodeQueue {
		Rational time;
		OakEngineNode *viewer;
		OakEngineNode *node;
		OakEngineBlock *clip;
	};

	std::list<MulticamNodeQueue> play_queue_;

private slots:
	void display_clicked(const QPoint &p);
};

}

#endif // OAK_MULTICAMWIDGET_H
