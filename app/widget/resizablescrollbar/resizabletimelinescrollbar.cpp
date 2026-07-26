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

#include "resizabletimelinescrollbar.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QtMath>

#include "ui/colorcoding.h"

namespace olive
{

ResizableTimelineScrollBar::ResizableTimelineScrollBar(QWidget *parent)
	: ResizableScrollBar(parent)
	, markers_(nullptr)
	, workarea_(nullptr)
	, scale_(1.0)
	, bridge_(new EngineEventBridge(this))
{
}

ResizableTimelineScrollBar::ResizableTimelineScrollBar(
	Qt::Orientation orientation, QWidget *parent)
	: ResizableScrollBar(orientation, parent)
	, markers_(nullptr)
	, workarea_(nullptr)
	, scale_(1.0)
	, bridge_(new EngineEventBridge(this))
{
}

ResizableTimelineScrollBar::~ResizableTimelineScrollBar()
{
	// Raw workarea subscriptions carry `this` as userdata; unlike the
	// bridge (which dies with us), they must be cancelled explicitly or the
	// engine would call back into a dead widget.
	if (workarea_range_sub_ > 0) {
		oakengine_event_unsubscribe(workarea_range_sub_);
	}
	if (workarea_enabled_sub_ > 0) {
		oakengine_event_unsubscribe(workarea_enabled_sub_);
	}
}

void ResizableTimelineScrollBar::connect_markers(TimelineMarkerList *markers)
{
	if (markers_) {
		if (marker_sub_add_)
			bridge_->unsubscribe(marker_sub_add_);
		if (marker_sub_rem_)
			bridge_->unsubscribe(marker_sub_rem_);
		if (marker_sub_mod_)
			bridge_->unsubscribe(marker_sub_mod_);
	}

	markers_ = markers;

	if (markers_) {
		marker_sub_add_ = bridge_->subscribe(
			reinterpret_cast<OakEngineMarkerList *>(markers_),
			OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED);
		marker_sub_rem_ = bridge_->subscribe(
			reinterpret_cast<OakEngineMarkerList *>(markers_),
			OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED);
		marker_sub_mod_ = bridge_->subscribe(
			reinterpret_cast<OakEngineMarkerList *>(markers_),
			OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED);

		connect(bridge_, &EngineEventBridge::marker_list_marker_added, this,
				[this](OakEngineMarkerList *, OakEngineMarker *) {
					update();
				});
		connect(bridge_, &EngineEventBridge::marker_list_marker_removed, this,
				[this](OakEngineMarkerList *, OakEngineMarker *) {
					update();
				});
		connect(bridge_, &EngineEventBridge::marker_list_marker_modified, this,
				[this](OakEngineMarkerList *, OakEngineMarker *) {
					update();
				});
	}

	update();
}

void ResizableTimelineScrollBar::connect_work_area(TimelineWorkArea *workarea)
{
	if (workarea_) {
		if (workarea_range_sub_ > 0) {
			oakengine_event_unsubscribe(workarea_range_sub_);
			workarea_range_sub_ = 0;
		}
		if (workarea_enabled_sub_ > 0) {
			oakengine_event_unsubscribe(workarea_enabled_sub_);
			workarea_enabled_sub_ = 0;
		}
	}

	workarea_ = workarea;

	if (workarea_) {
		void *handle = reinterpret_cast<void *>(workarea_);
		workarea_range_sub_ = oakengine_event_subscribe(
			handle, OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED,
			[](const oakengine_event *, void *userdata) {
				static_cast<ResizableTimelineScrollBar *>(userdata)->update();
			}, this);
		workarea_enabled_sub_ = oakengine_event_subscribe(
			handle, OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED,
			[](const oakengine_event *, void *userdata) {
				static_cast<ResizableTimelineScrollBar *>(userdata)->update();
			}, this);
	}

	update();
}

void ResizableTimelineScrollBar::SetScale(double d)
{
	scale_ = d;

	update();
}

void ResizableTimelineScrollBar::paintEvent(QPaintEvent *event)
{
	ResizableScrollBar::paintEvent(event);

	if (!timebase().isNull() && ((workarea_ && workarea_->enabled()) ||
								 (markers_ && !markers_->empty()))) {
		// Draw workarea
		QStyleOptionSlider opt;
		initStyleOption(&opt);

		QRect gr = style()->subControlRect(QStyle::CC_ScrollBar, &opt,
										   QStyle::SC_ScrollBarGroove, this);

		double ratio =
			scale_ * double(gr.width()) / double(this->maximum() + gr.width());
		QPainter p(this);

		if (workarea_ && workarea_->enabled()) {
			QColor workarea_color(this->palette().highlight().color());
			workarea_color.setAlpha(128);

			qint64 in =
				qMax(qint64(0), qRound64(ratio * time_to_scene(workarea_->in())));

			qint64 out;
			if (workarea_->out() == RATIONAL_MAX) {
				out = gr.width();
			} else {
				out = qMin(qint64(gr.width()),
						   qRound64(ratio * time_to_scene(workarea_->out())));
			}

			qint64 length = qMax(qint64(1), out - in);

			p.fillRect(gr.x() + in, 0, length, height(), workarea_color);
		}

		// Draw markers
		if (markers_ && !markers_->empty()) {
			for (auto it = markers_->cbegin(); it != markers_->cend(); it++) {
				TimelineMarker *marker = *it;

				QColor marker_color =
					QtUtils::to_q_color(ColorCoding::get_color(marker->color()));
				int64_t in = qRound64(ratio * time_to_scene(marker->time().in()));
				int64_t out =
					qRound64(ratio * time_to_scene(marker->time().out()));
				int64_t length = qMax(int64_t(1), out - in);

				p.fillRect(gr.x() + in, 0, length, height(), marker_color);
			}
		}
	}
}

}
