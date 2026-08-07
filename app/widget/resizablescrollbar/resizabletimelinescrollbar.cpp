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
#include "core.h"
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
	init_connections();
}

ResizableTimelineScrollBar::ResizableTimelineScrollBar(
	Qt::Orientation orientation, QWidget *parent)
	: ResizableScrollBar(orientation, parent)
	, markers_(nullptr)
	, workarea_(nullptr)
	, scale_(1.0)
	, bridge_(new EngineEventBridge(this))
{
	init_connections();
}

ResizableTimelineScrollBar::~ResizableTimelineScrollBar()
{
	// bridge_ unsubscribes all subscriptions on destruction.
}

void ResizableTimelineScrollBar::init_connections()
{
	// Issue 10: refresh the scrollbar whenever markers or the workarea
	// change. The bridge_ signals carry engine object pointers that Qt
	// ignores when connecting to the no-arg QWidget::update() slot.
	connect(bridge_, &EngineEventBridge::marker_list_marker_added, this,
			static_cast<void (QWidget::*)()>(&QWidget::update));
	connect(bridge_, &EngineEventBridge::marker_list_marker_removed, this,
			static_cast<void (QWidget::*)()>(&QWidget::update));
	connect(bridge_, &EngineEventBridge::marker_list_marker_modified, this,
			static_cast<void (QWidget::*)()>(&QWidget::update));
	connect(bridge_, &EngineEventBridge::workarea_range_changed, this,
			static_cast<void (QWidget::*)()>(&QWidget::update));
	connect(bridge_, &EngineEventBridge::workarea_enabled_changed, this,
			static_cast<void (QWidget::*)()>(&QWidget::update));

	// Reuse issue 7 signal: refresh after undo/redo so marker/workarea
	// changes replayed from the undo stack are visible on the scrollbar.
	connect(Core::instance(), &Core::undo_index_changed, this,
			static_cast<void (QWidget::*)()>(&QWidget::update));
}

void ResizableTimelineScrollBar::connect_markers(OakEngineMarkerList *markers)
{
	if (markers_) {
		if (marker_sub_add_) {
			bridge_->unsubscribe(marker_sub_add_);
			marker_sub_add_ = 0;
		}
		if (marker_sub_rem_) {
			bridge_->unsubscribe(marker_sub_rem_);
			marker_sub_rem_ = 0;
		}
		if (marker_sub_mod_) {
			bridge_->unsubscribe(marker_sub_mod_);
			marker_sub_mod_ = 0;
		}
	}

	markers_ = markers;

	if (markers_) {
		// Subscribe through the bridge. Corresponding Qt signal connections
		// live in init_connections() so they are not duplicated when the
		// observed marker list changes.
		marker_sub_add_ = bridge_->subscribe(
			markers_, OAKENGINE_EVENT_MARKER_LIST_MARKER_ADDED);
		marker_sub_rem_ = bridge_->subscribe(
			markers_, OAKENGINE_EVENT_MARKER_LIST_MARKER_REMOVED);
		marker_sub_mod_ = bridge_->subscribe(
			markers_, OAKENGINE_EVENT_MARKER_LIST_MARKER_MODIFIED);
	}

	update();
}

void ResizableTimelineScrollBar::connect_work_area(OakEngineWorkarea *workarea)
{
	if (workarea_) {
		if (workarea_range_sub_ > 0) {
			bridge_->unsubscribe(workarea_range_sub_);
			workarea_range_sub_ = 0;
		}
		if (workarea_enabled_sub_ > 0) {
			bridge_->unsubscribe(workarea_enabled_sub_);
			workarea_enabled_sub_ = 0;
		}
	}

	workarea_ = workarea;

	if (workarea_) {
		// Subscribe through the bridge. Corresponding Qt signal connections
		// live in init_connections() so they are not duplicated when the
		// observed workarea changes.
		workarea_range_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(workarea_),
			OAKENGINE_EVENT_WORKAREA_RANGE_CHANGED);
		workarea_enabled_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(workarea_),
			OAKENGINE_EVENT_WORKAREA_ENABLED_CHANGED);
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
