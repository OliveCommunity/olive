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

#ifndef OAK_RESIZABLETIMELINESCROLLBAR_H
#define OAK_RESIZABLETIMELINESCROLLBAR_H

#include "resizablescrollbar.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineworkarea.h"
#include "widget/timebased/timescaledobject.h"
#include "engineeventbridge.h"
#include "oakengine/events.h"

namespace olive
{

class ResizableTimelineScrollBar : public ResizableScrollBar,
								   public TimeScaledObject {
	Q_OBJECT
public:
	ResizableTimelineScrollBar(QWidget *parent = nullptr);
	ResizableTimelineScrollBar(Qt::Orientation orientation,
							   QWidget *parent = nullptr);
	~ResizableTimelineScrollBar() override;

	void connect_markers(TimelineMarkerList *markers);
	void connect_work_area(TimelineWorkArea *workarea);

	void SetScale(double d);

protected:
	virtual void paintEvent(QPaintEvent *event) override;

private:
	TimelineMarkerList *markers_;

	TimelineWorkArea *workarea_;

	// Workarea signal subscriptions (event 141/142-style, but workarea is a
	// timeline-level concept tracked via OakEngineEvents).
	int64_t workarea_range_sub_ = 0;
	int64_t workarea_enabled_sub_ = 0;

	double scale_;

	EngineEventBridge *bridge_ = nullptr;

	int64_t marker_sub_add_ = 0;

	int64_t marker_sub_rem_ = 0;

	int64_t marker_sub_mod_ = 0;
};

}

#endif // OAK_RESIZABLETIMELINESCROLLBAR_H
