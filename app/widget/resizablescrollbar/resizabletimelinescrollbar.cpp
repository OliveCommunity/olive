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

#include "common/colorcodingapp.h"
#include "oakengine/timeline.h"
#include "oakutil/qtutils.h"
#include "widget/timeruler/markerhandle.h"

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

void ResizableTimelineScrollBar::connect_markers(OakEngineMarkerList *markers)
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
			markers_, OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED);
		marker_sub_rem_ = bridge_->subscribe(
			markers_, OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED);
		marker_sub_mod_ = bridge_->subscribe(
			markers_, OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED);

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

void ResizableTimelineScrollBar::connect_work_area(OakEngineWorkarea *workarea)
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
		void *handle = workarea_;
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

	// Fetch workarea/marker state through the C ABI (the engine types are
	// opaque identity pointers on this side).
	int64_t wa_in_num = 0, wa_in_den = 1, wa_out_num = 0, wa_out_den = 1;
	int wa_enabled_flag = 0;
	const bool wa_enabled =
		workarea_ &&
		oakengine_workarea_get(workarea_, &wa_in_num,
			&wa_in_den, &wa_out_num, &wa_out_den,
			&wa_enabled_flag) == OAKENGINE_OK &&
		wa_enabled_flag;
	const int marker_count =
		markers_ ? oakengine_marker_list_count(markers_) : 0;

	if (!timebase().isNull() && (wa_enabled || marker_count > 0)) {
		// Draw workarea
		QStyleOptionSlider opt;
		initStyleOption(&opt);

		QRect gr = style()->subControlRect(QStyle::CC_ScrollBar, &opt,
										   QStyle::SC_ScrollBarGroove, this);

		double ratio =
			scale_ * double(gr.width()) / double(this->maximum() + gr.width());
		QPainter p(this);

		if (wa_enabled) {
			const Rational wa_in{int(wa_in_num), int(wa_in_den)};
			const Rational wa_out{int(wa_out_num), int(wa_out_den)};

			QColor workarea_color(this->palette().highlight().color());
			workarea_color.setAlpha(128);

			qint64 in =
				qMax(qint64(0), qRound64(ratio * time_to_scene(wa_in)));

			qint64 out;
			if (wa_out == RATIONAL_MAX) {
				out = gr.width();
			} else {
				out = qMin(qint64(gr.width()),
						   qRound64(ratio * time_to_scene(wa_out)));
			}

			qint64 length = qMax(qint64(1), out - in);

			p.fillRect(gr.x() + in, 0, length, height(), workarea_color);
		}

		// Draw markers
		if (marker_count > 0) {
			for (int i = 0; i < marker_count; i++) {
				OakEngineMarker *marker =
					oakengine_marker_list_at(markers_, i);
				const TimeRange range = marker_time(marker);

				QColor marker_qcolor = QtUtils::to_q_color(
					AppColorCoding::get_color(marker_color(marker)));
				int64_t in = qRound64(ratio * time_to_scene(range.in()));
				int64_t out = qRound64(ratio * time_to_scene(range.out()));
				int64_t length = qMax(int64_t(1), out - in);

				p.fillRect(gr.x() + in, 0, length, height(), marker_qcolor);
			}
		}
	}
}

}
