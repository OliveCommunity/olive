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

#include "timebasedwidget.h"

#include <QInputDialog>

#include "oakutil/autoscroll.h"
#include "oakutil/range.h"
#include "common/configwrapper.h"
#include "core.h"
#include "engineeventbridge.h"
#include "common/current.h"
#include "dialog/markerproperties/markerpropertiesdialog.h"
#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "oakengine/viewer.h"
#include "oakengine/timeline.h"
#include "oakengine/undo.h"
#include "widget/keyframeview/keyframehandle.h"
#include "widget/timeruler/markerhandle.h"
#include "widget/timeruler/timeruler.h"
#include "widget/timelinewidget/cliphandle.h"

#include "widget/viewer/vieweroutpututils.h"
namespace olive
{

TimeBasedWidget::TimeBasedWidget(bool ruler_text_visible,
								 bool ruler_cache_status_visible,
								 QWidget *parent)
	: TimelineScaledWidget(parent)
	, viewer_node_(nullptr)
	, bridge_(new EngineEventBridge(this))
	, auto_max_scrollbar_(false)
	, toggle_show_all_(false)
	, auto_set_timebase_(true)
	, workarea_(nullptr)
	, markers_(nullptr)
{
	scrollbar_ = new ResizableTimelineScrollBar(Qt::Horizontal, this);
	connect(scrollbar_, &ResizableScrollBar::resize_began, this,
			&TimeBasedWidget::scroll_bar_resize_began);
	connect(scrollbar_, &ResizableScrollBar::resize_moved, this,
			&TimeBasedWidget::scroll_bar_resize_moved);

	ruler_ =
		new TimeRuler(ruler_text_visible, ruler_cache_status_visible, this);
	connect_timeline_view(ruler_);
	ruler()->set_snap_service(this);
	connect(ruler(), &TimeRuler::drag_moved, this,
			static_cast<void (TimeBasedWidget::*)(int)>(
				&TimeBasedWidget::set_catch_up_scroll_value));
	connect(ruler(), &TimeRuler::drag_released, this,
			static_cast<void (TimeBasedWidget::*)()>(
				&TimeBasedWidget::stop_catch_up_scroll_timer));

	catchup_scroll_timer_ = new QTimer(this);
	catchup_scroll_timer_->setInterval(250); // Hardcoded 1/4 scroll limit value
	connect(catchup_scroll_timer_, &QTimer::timeout, this,
			&TimeBasedWidget::catch_up_timer_timeout);
}

void TimeBasedWidget::set_scale_and_center_on_playhead(const double &scale)
{
	SetScale(scale);

	// Zoom towards the playhead
	// (using a hacky singleShot so the scroll occurs after the scene and its scrollbars have updated)
	QTimer::singleShot(0, this, &TimeBasedWidget::center_scroll_on_playhead);
}

OakEngineNode *TimeBasedWidget::get_connected_node() const
{
	// The handle is the same object the QPointer tracks (the documented
	// QPointer<ViewerOutput> exception in timebasedwidget.h); bridge once
	// here so the rest of the app only ever sees the C ABI handle.
	return reinterpret_cast<OakEngineNode *>(viewer_node_.data());
}

void TimeBasedWidget::connect_viewer_node(OakEngineNode *node)
{
	// viewer_node_ is the documented QPointer<ViewerOutput> exception (see
	// timebasedwidget.h): the incoming handle is the same engine object,
	// bridged once here for QPointer's null-on-destroy semantics.
	ViewerOutput *viewer = reinterpret_cast<ViewerOutput *>(node);

	// Ignore no-op
	if (viewer_node_ == viewer) {
		return;
	}

	// Set viewer node
	OakEngineNode *old = get_connected_node();
	viewer_node_ = viewer;

	// Disconnect old bridge subscriptions and connections
	disconnect(bridge_, nullptr, this, nullptr);
	bridge_->unsubscribe_all();

	if (viewer_node_) {
		oak_video_params vp;
		oakengine_viewer_get_video_params(node, 0, &vp);
		// We still need Current class - keep using it for now. It stores an
		// engine-side olive::VideoParams, so bridge through the C ABI
		// create/free pair (setCurrentVideoParams copies the value, the
		// engine object is released immediately).
		void *engine_vp =
			viewer_output_video_params(viewer_node_).create_engine_params();
		Current::getInstance().setCurrentVideoParams(
			*static_cast<olive::VideoParams *>(engine_vp));
		oakengine_video_params_free(engine_vp);
		Current::getInstance().setCurrentAudioParams(
			viewer_output_audio_params(viewer_node_));
	} else {
		void *engine_vp = empty_video_params().create_engine_params();
		Current::getInstance().setCurrentVideoParams(
			*static_cast<olive::VideoParams *>(engine_vp));
		oakengine_video_params_free(engine_vp);
		Current::getInstance().setCurrentAudioParams(AudioParams());
	}
	if (old) {
		// Call potential derivative functions for disconnecting the viewer node
		DisconnectNodeEvent(old);

		// Reset timebase to null
		SetTimebase(Rational());

		// Disconnect ruler and scrollbar from timeline points
		connect_work_area(nullptr);
		connect_markers(nullptr);
	}

	// Call derivatives
	for (TimeBasedView *view : timeline_views_) {
		view->set_viewer_node(reinterpret_cast<OakEngineNode *>(viewer_node_.data()));
	}
	ConnectedNodeChangeEvent(node);

	if (viewer_node_) {
		// Subscribe to viewer events via bridge
		bridge_->subscribe(node, OAKENGINE_EVENT_VIEWER_LENGTH_CHANGED);
		bridge_->subscribe(node, OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED);
		bridge_->subscribe(node, OAKENGINE_EVENT_NODE_REMOVED_FROM_GRAPH);

		connect(bridge_, &EngineEventBridge::viewer_length_changed, this,
				[this](OakEngineNode *, qint64, qint64) {
					update_maximum_scroll();
				});
		connect(bridge_, &EngineEventBridge::viewer_playhead_changed, this,
				[this](OakEngineNode *, qint64 num, qint64 den) {
					playhead_time_changed(Rational(num, den));
				});

		// Node removed from graph - use the bridge signal
		connect(bridge_, &EngineEventBridge::node_removed_from_graph, this,
				[this](OakEngineNode *, OakEngineNode *) {
					connected_node_removed_from_graph();
				});

		// Connect ruler and scrollbar to timeline points
		connect_work_area(oakengine_viewer_get_workarea_handle(node));
		connect_markers(oakengine_viewer_get_marker_list(node));

		// If we're setting the timebase, set it automatically based on the video and audio parameters
		if (auto_set_timebase_) {
			auto_update_timebase();
			bridge_->subscribe(node, OAKENGINE_EVENT_VIEWER_FRAME_RATE_CHANGED);
			bridge_->subscribe(node, OAKENGINE_EVENT_VIEWER_SAMPLE_RATE_CHANGED);
			connect(bridge_, &EngineEventBridge::viewer_frame_rate_changed, this,
					[this](OakEngineNode *, qint64, qint64) {
						auto_update_timebase();
					});
			connect(bridge_, &EngineEventBridge::viewer_sample_rate_changed, this,
					[this](OakEngineNode *, int) {
						auto_update_timebase();
					});
		}

		// Call derivatives
		ConnectNodeEvent(node);
	}

	update_maximum_scroll();

	emit connected_node_changed(old, node);
}

void TimeBasedWidget::connect_work_area(OakEngineWorkarea *workarea)
{
	workarea_ = workarea;
	ruler()->set_work_area(workarea);
	scrollbar_->connect_work_area(workarea);
}

void TimeBasedWidget::connect_markers(OakEngineMarkerList *markers)
{
	markers_ = markers;
	ruler()->set_markers(markers);
	scrollbar_->connect_markers(markers);
}

void TimeBasedWidget::update_maximum_scroll()
{
	Rational length = (viewer_node_) ? viewer_output_length(viewer_node_.data()) : 0;

	if (auto_max_scrollbar_) {
		scrollbar_->setMaximum(
			std::max(0, int(std::ceil(time_to_scene(length)) - width())));
	}

	foreach (TimeBasedView *base, timeline_views_) {
		base->set_end_time(length);
	}
}

void TimeBasedWidget::scroll_bar_resize_began(int current_bar_width,
										   bool top_handle)
{
	QScrollBar *bar = static_cast<QScrollBar *>(sender());

	scrollbar_start_width_ = current_bar_width;
	scrollbar_start_value_ = bar->value();
	scrollbar_start_scale_ = get_scale();
	scrollbar_top_handle_ = top_handle;
}

void TimeBasedWidget::scroll_bar_resize_moved(int movement)
{
	ResizableScrollBar *bar = static_cast<ResizableScrollBar *>(sender());

	// Negate movement for the top handle
	if (scrollbar_top_handle_) {
		movement = -movement;
	}

	// The user wants the bar to be this size
	int proposed_size = scrollbar_start_width_ + movement;

	double ratio = double(scrollbar_start_width_) / double(proposed_size);

	if (ratio > 0) {
		SetScale(scrollbar_start_scale_ * ratio);

		if (scrollbar_top_handle_) {
			int viewable_area;

			if (timeline_views_.isEmpty()) {
				viewable_area = width();
			} else {
				viewable_area = timeline_views_.first()->width();
			}

			bar->setValue((scrollbar_start_value_ + viewable_area) * ratio -
						  viewable_area);
		} else {
			bar->setValue(scrollbar_start_value_ * ratio);
		}
	}
}

void TimeBasedWidget::page_scroll_to_playhead()
{
	if (get_connected_node()) {
		page_scroll_internal(
			qRound(time_to_scene(viewer_output_playhead(get_connected_node()))), true);
	}
}

void TimeBasedWidget::catch_up_scroll_to_playhead()
{
	if (get_connected_node()) {
		catch_up_scroll_to_point(
			qRound(time_to_scene(viewer_output_playhead(get_connected_node()))));
	}
}

void TimeBasedWidget::catch_up_scroll_to_point(int point)
{
	page_scroll_internal(point, false);
}

void TimeBasedWidget::catch_up_timer_timeout()
{
	for (auto it = catchup_scroll_values_.cbegin();
		 it != catchup_scroll_values_.cend(); it++) {
		QScrollBar *sb = it.key();
		const CatchUpScrollData &d = it.value();
		page_scroll_internal(sb, d.maximum, sb->value() + d.value, false);
	}

	SendCatchUpScrollEvent();
}

void TimeBasedWidget::SendCatchUpScrollEvent()
{
	for (auto v : this->timeline_views_) {
		v->CatchUpScrollEvent();
	}
}

void TimeBasedWidget::auto_update_timebase()
{
	if (!viewer_node_) {
		SetTimebase(Rational());
		return;
	}
	Rational video_tb =
		viewer_output_video_params(viewer_node_).frame_rate_as_time_base();

	if (!video_tb.isNull()) {
		SetTimebase(video_tb);
	} else {
		Rational audio_tb =
			viewer_output_audio_params(viewer_node_).sample_rate_as_time_base();

		if (!audio_tb.isNull()) {
			SetTimebase(audio_tb);
		} else {
			SetTimebase(Rational());
		}
	}
}

void TimeBasedWidget::connected_node_removed_from_graph()
{
	connect_viewer_node(nullptr);
}

TimeRuler *TimeBasedWidget::ruler() const
{
	return ruler_;
}

ResizableTimelineScrollBar *TimeBasedWidget::scrollbar() const
{
	return scrollbar_;
}

void TimeBasedWidget::TimebaseChangedEvent(const Rational &timebase)
{
	TimelineScaledWidget::TimebaseChangedEvent(timebase);

	ruler_->set_timebase(timebase);
	scrollbar_->set_timebase(timebase);

	emit timebase_changed(timebase);
}

void TimeBasedWidget::ScaleChangedEvent(const double &scale)
{
	TimelineScaledWidget::ScaleChangedEvent(scale);

	ruler_->set_scale(scale);
	scrollbar_->SetScale(scale);

	update_maximum_scroll();

	QMetaObject::invokeMethod(this, &TimeBasedWidget::SendCatchUpScrollEvent,
							  Qt::QueuedConnection);

	toggle_show_all_ = false;
}

void TimeBasedWidget::set_auto_max_scroll_bar(bool e)
{
	auto_max_scrollbar_ = e;
}

void TimeBasedWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);

	// Update horizontal scrollbar's page step to the width of the panel
	scrollbar()->setPageStep(scrollbar()->width());

	update_maximum_scroll();
}

void TimeBasedWidget::connect_timeline_view(TimeBasedView *base)
{
	// Connect scale
	connect(base, &TimeBasedView::scale_changed, this,
			&TimeBasedWidget::SetScale);

	// Main scrollbar to view scrollbar and vice versa
	connect(scrollbar(), &QScrollBar::valueChanged, base->horizontalScrollBar(),
			&QScrollBar::setValue);
	connect(base->horizontalScrollBar(), &QScrollBar::valueChanged, scrollbar(),
			&QScrollBar::setValue);

	// Connect scrollbar to other scrollbars
	for (TimeBasedView *other : qAsConst(timeline_views_)) {
		connect(other->horizontalScrollBar(), &QScrollBar::valueChanged,
				base->horizontalScrollBar(), &QScrollBar::setValue);
		connect(base->horizontalScrollBar(), &QScrollBar::valueChanged,
				other->horizontalScrollBar(), &QScrollBar::setValue);
	}

	timeline_views_.append(base);
}

void TimeBasedWidget::set_catch_up_scroll_value(QScrollBar *b, int v, int maximum)
{
	CatchUpScrollData &cudata = catchup_scroll_values_[b];
	cudata.value = v;
	cudata.maximum = maximum;

	static const qint64 min_cooldown = 100; // Hardcoded 1/10 sec cooldown
	if (QDateTime::currentMSecsSinceEpoch() - cudata.last_forced >=
		min_cooldown) {
		QMetaObject::invokeMethod(this, &TimeBasedWidget::catch_up_timer_timeout,
								  Qt::QueuedConnection);
		cudata.last_forced = QDateTime::currentMSecsSinceEpoch();
	}

	if (!catchup_scroll_timer_->isActive()) {
		catchup_scroll_timer_->start();
	}
}

void TimeBasedWidget::set_catch_up_scroll_value(int v)
{
	set_catch_up_scroll_value(scrollbar_, v, ruler()->width());
}

void TimeBasedWidget::stop_catch_up_scroll_timer(QScrollBar *b)
{
	catchup_scroll_values_.remove(b);
	if (catchup_scroll_values_.empty()) {
		catchup_scroll_timer_->stop();
	}
}

void TimeBasedWidget::playhead_time_changed(const Rational &time)
{
	if (user_is_dragging_playhead()) {
		// If the user is dragging the playhead, we will simply nudge over and not use autoscroll rules.
		set_catch_up_scroll_value(qRound(time_to_scene(time)) - scrollbar_->value());
	} else {
		// Otherwise, assume we jumped to this out of nowhere and must now autoscroll
		switch (static_cast<AutoScroll::Method>(
			OAK_CONFIG("Autoscroll").toInt())) {
		case AutoScroll::k_none:
			// Do nothing
			break;
		case AutoScroll::k_page:
			QMetaObject::invokeMethod(this, "page_scroll_to_playhead",
									  Qt::QueuedConnection);
			break;
		case AutoScroll::k_smooth:
			QMetaObject::invokeMethod(this, "center_scroll_on_playhead",
									  Qt::QueuedConnection);
			break;
		}
	}

	TimeChangedEvent(time);
}

void TimeBasedWidget::SetTimebase(const Rational &timebase)
{
	TimelineScaledWidget::set_timebase(timebase);
}

void TimeBasedWidget::SetScale(const double &scale)
{
	// Simple QObject slot wrapper around TimelineScaledWidget::SetScale()
	TimelineScaledWidget::set_scale(scale);
}

void TimeBasedWidget::zoom_in()
{
	set_scale_and_center_on_playhead(get_scale() * 2);
}

void TimeBasedWidget::zoom_out()
{
	set_scale_and_center_on_playhead(get_scale() * 0.5);
}

void TimeBasedWidget::go_to_prev_cut()
{
	// Cuts are only possible in sequences
	OakEngineNode *sequence_node = get_connected_node();

	if (!oakengine_node_is_sequence(sequence_node)) {
		return;
	}

	const Rational playhead = viewer_output_playhead(sequence_node);
	if (playhead.isNull()) {
		return;
	}

	OakEngineSequence *sequence =
		reinterpret_cast<OakEngineSequence *>(sequence_node);
	Rational closest_cut = 0;

	// Iterate all track lists (video, audio, subtitle), mirroring
	// Sequence::get_tracks(). The per-type counts line up with the
	// OAKENGINE_TRACK_TYPE_* ordinals (0..2).
	int track_counts[3] = { 0, 0, 0 };
	oakengine_sequence_track_count(sequence, &track_counts[0],
								   &track_counts[1], &track_counts[2]);

	for (int type = 0; type < 3; type++) {
		for (int ti = 0; ti < track_counts[type]; ti++) {
			OakEngineTrack *track =
				oakengine_sequence_track_at(sequence, type, ti);
			if (!track) {
				continue;
			}

			Rational this_track_closest_cut = 0;

			const int block_count = oakengine_track_block_count(track);
			for (int bi = 0; bi < block_count; bi++) {
				OakEngineBlock *block = oakengine_track_block_at(track, bi);
				int out_num = 0, out_den = 1;
				oakengine_block_get_out_rational(
					reinterpret_cast<const OakEngineNode *>(block), &out_num,
					&out_den);
				const Rational block_out(out_num, out_den);
				if (block_out < playhead) {
					this_track_closest_cut = block_out;
				} else {
					break;
				}
			}

			closest_cut = qMax(closest_cut, this_track_closest_cut);
		}
	}

	oakengine_viewer_set_playhead(sequence_node,
		closest_cut.numerator(), closest_cut.denominator());
}

void TimeBasedWidget::go_to_next_cut()
{
	// Cuts are only possible in sequences
	OakEngineNode *sequence_node = get_connected_node();

	if (!oakengine_node_is_sequence(sequence_node)) {
		return;
	}

	OakEngineSequence *sequence =
		reinterpret_cast<OakEngineSequence *>(sequence_node);
	const Rational playhead = viewer_output_playhead(sequence_node);
	Rational closest_cut = RATIONAL_MAX;

	// See go_to_prev_cut() for the track iteration convention.
	int track_counts[3] = { 0, 0, 0 };
	oakengine_sequence_track_count(sequence, &track_counts[0],
								   &track_counts[1], &track_counts[2]);

	for (int type = 0; type < 3; type++) {
		for (int ti = 0; ti < track_counts[type]; ti++) {
			OakEngineTrack *track =
				oakengine_sequence_track_at(sequence, type, ti);
			if (!track) {
				continue;
			}

			const int block_count = oakengine_track_block_count(track);

			// Track::track_length(): out of the last block (0 when empty)
			Rational this_track_closest_cut = 0;
			if (block_count > 0) {
				OakEngineBlock *last =
					oakengine_track_block_at(track, block_count - 1);
				int out_num = 0, out_den = 1;
				oakengine_block_get_out_rational(
					reinterpret_cast<const OakEngineNode *>(last), &out_num,
					&out_den);
				this_track_closest_cut = Rational(out_num, out_den);
			}

			if (this_track_closest_cut <= playhead) {
				this_track_closest_cut = RATIONAL_MAX;
			}

			for (int bi = 0; bi < block_count; bi++) {
				OakEngineBlock *block = oakengine_track_block_at(track, bi);
				int in_num = 0, in_den = 1;
				oakengine_block_get_in_rational(
					reinterpret_cast<const OakEngineNode *>(block), &in_num,
					&in_den);
				const Rational block_in(in_num, in_den);
				if (block_in > playhead) {
					this_track_closest_cut = block_in;
					break;
				}
			}

			closest_cut = qMin(closest_cut, this_track_closest_cut);
		}
	}

	if (closest_cut < RATIONAL_MAX) {
		oakengine_viewer_set_playhead(sequence_node,
		closest_cut.numerator(), closest_cut.denominator());
	}
}

void TimeBasedWidget::go_to_start()
{
	if (viewer_node_) {
		oakengine_viewer_set_playhead(get_connected_node(), 0, 1);
	}
}

void TimeBasedWidget::prev_frame()
{
	if (viewer_node_) {
		Rational proposed_time = Timecode::snap_time_to_timebase(
			viewer_output_playhead(viewer_node_.data()) - timebase(), timebase(),
			Timecode::k_ceil);
		if (proposed_time == viewer_output_playhead(viewer_node_.data())) {
			// Catch rounding error, assume this time is snapped and just subtract a timebase
			proposed_time -= timebase();
		}
		{
			Rational _pt = qMax(Rational(0), proposed_time);
			oakengine_viewer_set_playhead(get_connected_node(),
				_pt.numerator(), _pt.denominator());
		}
	}
}

void TimeBasedWidget::next_frame()
{
	if (viewer_node_) {
		Rational proposed_time = Timecode::snap_time_to_timebase(
			viewer_output_playhead(viewer_node_.data()) + timebase(), timebase(),
			Timecode::k_floor);
		if (proposed_time == viewer_output_playhead(viewer_node_.data())) {
			// Catch rounding error, assume this time is snapped and just add a timebase
			proposed_time += timebase();
		}
		oakengine_viewer_set_playhead(get_connected_node(),
			proposed_time.numerator(), proposed_time.denominator());
	}
}

void TimeBasedWidget::go_to_end()
{
	if (viewer_node_) {
		const Rational length = viewer_output_length(viewer_node_.data());
		oakengine_viewer_set_playhead(get_connected_node(),
			length.numerator(), length.denominator());
	}
}

void TimeBasedWidget::center_scroll_on_playhead()
{
	if (get_connected_node()) {
		scrollbar_->setValue(
			qRound(time_to_scene(viewer_output_playhead(get_connected_node()))) -
			scrollbar_->width() / 2);
	}
}

void TimeBasedWidget::set_auto_set_timebase(bool e)
{
	auto_set_timebase_ = e;
}

void TimeBasedWidget::set_point(TimelineApp::MovementMode m, const Rational &time)
{
	if (!viewer_node_) {
		return;
	}

	void *command = oakengine_undo_command_create_multi();
	OakEngineWorkarea *points =
		oakengine_viewer_get_workarea_handle(get_connected_node());

	int64_t wa_in_num = 0, wa_in_den = 1, wa_out_num = 0, wa_out_den = 1;
	int wa_enabled = 0;
	oakengine_workarea_get(points, &wa_in_num, &wa_in_den, &wa_out_num,
						   &wa_out_den, &wa_enabled);

	// Enable workarea if it isn't already enabled. Note the enable is only
	// queued on `command` (applied on push below), so the in/out logic keeps
	// using this pre-enable snapshot, mirroring the original undo semantics.
	if (!wa_enabled) {
		oakengine_workarea_set_enabled_undoable(points, 1, command);
	}

	// Determine our new range
	Rational in_point, out_point;

	if (m == TimelineApp::k_trim_in) {
		in_point = time;

		if (!wa_enabled ||
			Rational(int(wa_out_num), int(wa_out_den)) < in_point) {
			out_point = RATIONAL_MAX;
		} else {
			out_point = Rational(int(wa_out_num), int(wa_out_den));
		}
	} else {
		out_point = time;

		if (!wa_enabled ||
			Rational(int(wa_in_num), int(wa_in_den)) > out_point) {
			in_point = Rational(0, 1);
		} else {
			in_point = Rational(int(wa_in_num), int(wa_in_den));
		}
	}

	// Set workarea
	{
		int64_t old_in_num, old_in_den, old_out_num, old_out_den;
		int old_enabled;
		oakengine_workarea_get(points,
			&old_in_num, &old_in_den, &old_out_num, &old_out_den,
			&old_enabled);
		oakengine_workarea_set_range_undoable(
			points,
			in_point.numerator(), in_point.denominator(),
			out_point.numerator(), out_point.denominator(),
			old_in_num, old_in_den, old_out_num, old_out_den, command);
	}

	oakengine_undo_push(command, tr("Set In/Out Point").toUtf8().constData());
}

void TimeBasedWidget::reset_point(TimelineApp::MovementMode m)
{
	if (!get_connected_node()) {
		return;
	}

	OakEngineWorkarea *points =
		oakengine_viewer_get_workarea_handle(get_connected_node());

	int64_t wa_in_num = 0, wa_in_den = 1, wa_out_num = 0, wa_out_den = 1;
	int wa_enabled = 0;
	oakengine_workarea_get(points, &wa_in_num, &wa_in_den, &wa_out_num,
						   &wa_out_den, &wa_enabled);

	if (!wa_enabled) {
		return;
	}

	TimeRange r{Rational(int(wa_in_num), int(wa_in_den)),
				Rational(int(wa_out_num), int(wa_out_den))};

	if (m == TimelineApp::k_trim_in) {
		r.set_in(Rational(0, 1));
	} else {
		r.set_out(RATIONAL_MAX);
	}

	{
		auto reset_cmd = oakengine_undo_command_create_multi();
		int64_t old_in_num, old_in_den, old_out_num, old_out_den;
		int old_enabled;
		oakengine_workarea_get(points,
			&old_in_num, &old_in_den, &old_out_num, &old_out_den,
			&old_enabled);
		oakengine_workarea_set_range_undoable(
			points,
			r.in().numerator(), r.in().denominator(),
			r.out().numerator(), r.out().denominator(),
			old_in_num, old_in_den, old_out_num, old_out_den, reset_cmd);
		oakengine_undo_push(reset_cmd,
									 tr("Reset In/Out Points").toUtf8().constData());
	}
}

void TimeBasedWidget::page_scroll_internal(QScrollBar *bar, int maximum,
										 int screen_position,
										 bool whole_page_scroll)
{
	int viewport_padding = maximum / 16;

	if (whole_page_scroll) {
		if (screen_position < bar->value()) {
			// Anchor the playhead to the RIGHT of where we scroll to
			bar->setValue(screen_position - maximum + viewport_padding);
		} else if (screen_position > bar->value() + maximum) {
			// Anchor the playhead to the LEFT of where we scroll to
			bar->setValue(screen_position - viewport_padding);
		}
	} else {
		// Just jump in increments
		if (screen_position < bar->value() + viewport_padding) {
			bar->setValue(bar->value() - viewport_padding);
		} else if (screen_position >
				   bar->value() + maximum - viewport_padding) {
			bar->setValue(bar->value() + viewport_padding);
		}
	}
}

void TimeBasedWidget::page_scroll_internal(int screen_position,
										 bool whole_page_scroll)
{
	page_scroll_internal(scrollbar(), ruler()->width(), screen_position,
					   whole_page_scroll);
}

bool TimeBasedWidget::user_is_dragging_playhead() const
{
	foreach (TimeBasedView *view, timeline_views_) {
		if (view->is_dragging_playhead()) {
			return true;
		}
	}

	return false;
}

void TimeBasedWidget::set_in_at_playhead()
{
	set_point(TimelineApp::k_trim_in,
			  viewer_output_playhead(get_connected_node()));
}

void TimeBasedWidget::set_out_at_playhead()
{
	set_point(TimelineApp::k_trim_out,
			  viewer_output_playhead(get_connected_node()));
}

void TimeBasedWidget::reset_in()
{
	reset_point(TimelineApp::k_trim_in);
}

void TimeBasedWidget::reset_out()
{
	reset_point(TimelineApp::k_trim_out);
}

void TimeBasedWidget::clear_in_out_points()
{
	if (!get_connected_node()) {
		return;
	}

	{
		auto clear_cmd = oakengine_undo_command_create_multi();
		oakengine_workarea_set_enabled_undoable(
			oakengine_viewer_get_workarea_handle(get_connected_node()),
			0, clear_cmd);
		oakengine_undo_push(clear_cmd,
			tr("Cleared In/Out Points").toUtf8().constData());
	}
}

/**
 * @brief TimelineMarkerList::get_closest_marker_to_time() through the C ABI:
 * the list is sorted by time, so the walk stops as soon as the distance
 * starts increasing.
 */
static OakEngineMarker *marker_list_closest_to_time(
	OakEngineMarkerList *markers, const Rational &t)
{
	OakEngineMarker *closest = nullptr;

	const int count = oakengine_marker_list_count(markers);
	for (int i = 0; i < count; i++) {
		OakEngineMarker *m = oakengine_marker_list_at(markers, i);

		Rational this_diff = qAbs(marker_time(m).in() - t);

		if (closest) {
			Rational stored_diff = qAbs(marker_time(closest).in() - t);

			if (this_diff > stored_diff) {
				// Since the list is organized by time, if the diff increases, assume we are only going
				// to move further away from here and there's no need to check
				break;
			}
		}

		closest = m;
	}

	return closest;
}

void TimeBasedWidget::set_marker()
{
	OakEngineNode *viewer = get_connected_node();
	if (!viewer) {
		return;
	}

	OakEngineMarkerList *markers = oakengine_viewer_get_marker_list(viewer);
	const Rational playhead = viewer_output_playhead(viewer);

	if (OakEngineMarker *existing =
			oakengine_marker_list_marker_at_time(
				markers, playhead.numerator(), playhead.denominator())) {
		// We already have a marker here, so pop open the edit dialog
		MarkerPropertiesDialog mpd({ existing }, timebase(), this);
		mpd.exec();
	} else {
		// Create a new marker and place it here
		int color;
		if (OakEngineMarker *closest =
				marker_list_closest_to_time(markers, playhead)) {
			// Copy color of closest marker to this time
			color = marker_color(closest);
		} else {
			// Fallback to default color in preferences
			color = OAK_CONFIG("MarkerColor").toInt();
		}

		OakEngineMarker *marker = oakengine_marker_create(
			color, playhead.numerator(), playhead.denominator(),
			playhead.numerator(), playhead.denominator(), "");

		bool edited_in_dialog = false;
		if (OAK_CONFIG("SetNameWithMarker").toBool()) {
			MarkerPropertiesDialog mpd({ marker }, timebase(), this);
			if (mpd.exec() != QDialog::Accepted) {
				oakengine_marker_free(marker);
				marker = nullptr;
			} else {
				edited_in_dialog = true;
			}
		}

		if (marker) {
			if (edited_in_dialog) {
				// The dialog pushed undo commands referencing this exact
				// marker object, so it must be the one added to the list.
				oakengine_marker_list_add_existing(markers, marker);
			} else {
				// Pristine marker: add through the liboakengine C ABI
				// facade (one undoable command) and drop the temporary.
				oakengine_sequence_marker_add_ex(
					reinterpret_cast<OakEngineSequence *>(viewer),
					Timecode::time_to_timestamp(marker_time(marker).in(),
												timebase(),
												Timecode::k_round),
					"", marker_color(marker));
				oakengine_marker_free(marker);
			}
		}
	}
}

void TimeBasedWidget::toggle_show_all()
{
	if (!get_connected_node()) {
		return;
	}

	if (toggle_show_all_) {
		SetScale(toggle_show_all_old_scale_);
		scrollbar_->setValue(toggle_show_all_old_scroll_);

		// Don't have to set toggle_show_all_ because SetScale() will automatically set it to false
	} else {
		int w;

		if (timeline_views_.isEmpty()) {
			w = width();
		} else {
			w = timeline_views_.first()->width();
		}

		toggle_show_all_old_scale_ = get_scale();
		toggle_show_all_old_scroll_ = scrollbar_->value();

		set_scale_from_dimensions(w, viewer_output_length(get_connected_node()).to_double());
		scrollbar_->setValue(0);

		// Must explicitly do this because SetScale() will automatically set this to false
		toggle_show_all_ = true;
	}
}

void TimeBasedWidget::go_to_in()
{
	if (get_connected_node()) {
		oakengine_viewer_workarea wa;
		oakengine_viewer_get_workarea(get_connected_node(), &wa);
		if (wa.enabled) {
			oakengine_viewer_set_playhead(get_connected_node(),
				wa.in_num, wa.in_den);
		} else {
			go_to_start();
		}
	}
}

void TimeBasedWidget::go_to_out()
{
	if (get_connected_node()) {
		oakengine_viewer_workarea wa;
		oakengine_viewer_get_workarea(get_connected_node(), &wa);
		if (wa.enabled) {
			oakengine_viewer_set_playhead(get_connected_node(),
				wa.out_num, wa.out_den);
		} else {
			go_to_end();
		}
	}
}

void TimeBasedWidget::delete_selected()
{
	if (ruler_->has_items_selected()) {
		ruler_->delete_selected();
	}
}

struct SnapData {
	Rational time;
	Rational movement;
};

void attempt_snap(std::vector<SnapData> &snap_data,
				 const std::vector<double> &screen_pt, double compare_pt,
				 const std::vector<Rational> &start_times,
				 const Rational &compare_time)
{
	const qreal k_snap_range = 10; // FIXME: Hardcoded number

	for (size_t i = 0; i < screen_pt.size(); i++) {
		// Attempt snapping to clip out point
		if (in_range(screen_pt.at(i), compare_pt, k_snap_range)) {
			snap_data.push_back(
				{ compare_time, compare_time - start_times.at(i) });
		}
	}
}

bool TimeBasedWidget::snap_point(const std::vector<Rational> &start_times,
								Rational *movement, SnapMask snap_points)
{
	std::vector<double> screen_pt(start_times.size());

	for (size_t i = 0; i < start_times.size(); i++) {
		screen_pt[i] = time_to_scene(start_times.at(i) + *movement);
	}

	std::vector<SnapData> potential_snaps;

	if (snap_points & k_snap_to_playhead) {
		Rational playhead_abs_time =
			viewer_output_playhead(get_connected_node());
		qreal playhead_pos = time_to_scene(playhead_abs_time);
		attempt_snap(potential_snaps, screen_pt, playhead_pos, start_times,
					playhead_abs_time);
	}

	if ((snap_points & k_snap_to_clips) && get_snap_blocks()) {
		for (auto it = get_snap_blocks()->cbegin(); it != get_snap_blocks()->cend();
			 it++) {
			OakEngineBlock *b = *it;

			int in_num = 0, in_den = 1, out_num = 0, out_den = 1;
			oakengine_block_get_in_rational(
				reinterpret_cast<const OakEngineNode *>(b), &in_num, &in_den);
			oakengine_block_get_out_rational(
				reinterpret_cast<const OakEngineNode *>(b), &out_num, &out_den);
			const Rational block_in(in_num, in_den);
			const Rational block_out(out_num, out_den);

			qreal rect_left = time_to_scene(block_in);
			qreal rect_right = time_to_scene(block_out);

			// Attempt snapping to clip in point
			attempt_snap(potential_snaps, screen_pt, rect_left, start_times,
						block_in);

			// Attempt snapping to clip out point
			attempt_snap(potential_snaps, screen_pt, rect_right, start_times,
						block_out);

			if (snap_points & k_snap_to_markers) {
				// Snap to clip markers too
				if (oakengine_node_is_clip(
						reinterpret_cast<OakEngineNode *>(b))) {
					if (OakEngineNode *clip_viewer =
							oakengine_clip_get_connected_viewer(b)) {
						OakEngineMarkerList *markers =
							oakengine_viewer_get_marker_list(clip_viewer);
						const int marker_count =
							oakengine_marker_list_count(markers);
						for (int mi = 0; mi < marker_count; mi++) {
							OakEngineMarker *marker =
								oakengine_marker_list_at(markers, mi);

							TimeRange marker_range =
								marker_time(marker) + block_in - clip_media_in(b);

							qreal marker_in_screen =
								time_to_scene(marker_range.in());
							qreal marker_out_screen =
								time_to_scene(marker_range.out());

							attempt_snap(potential_snaps, screen_pt,
										marker_in_screen, start_times,
										marker_range.in());
							attempt_snap(potential_snaps, screen_pt,
										marker_out_screen, start_times,
										marker_range.out());
						}
					}
				}
			}
		}
	}

	if ((snap_points & k_snap_to_markers) && ruler()->get_markers()) {
		OakEngineMarkerList *ruler_markers = ruler()->get_markers();
		const int marker_count = oakengine_marker_list_count(ruler_markers);
		for (int mi = 0; mi < marker_count; mi++) {
			OakEngineMarker *m = oakengine_marker_list_at(ruler_markers, mi);

			// Ignore selected markers
			if (std::find(ruler()->get_selected_markers().cbegin(),
						  ruler()->get_selected_markers().cend(),
						  m) != ruler()->get_selected_markers().cend()) {
				continue;
			}

			const TimeRange m_time = marker_time(m);

			qreal marker_pos = time_to_scene(m_time.in());
			attempt_snap(potential_snaps, screen_pt, marker_pos, start_times,
						m_time.in());

			if (m_time.in() != m_time.out()) {
				marker_pos = time_to_scene(m_time.out());
				attempt_snap(potential_snaps, screen_pt, marker_pos, start_times,
							m_time.out());
			}
		}
	}

	if ((snap_points & k_snap_to_workarea) && ruler()->get_work_area()) {
		int64_t wa_in_num = 0, wa_in_den = 1, wa_out_num = 0, wa_out_den = 1;
		int wa_enabled = 0;
		oakengine_workarea_get(ruler()->get_work_area(), &wa_in_num,
							   &wa_in_den, &wa_out_num, &wa_out_den,
							   &wa_enabled);
		if (wa_enabled) {
			const Rational workarea_in{int(wa_in_num), int(wa_in_den)};
			const Rational workarea_out{int(wa_out_num), int(wa_out_den)};

			attempt_snap(potential_snaps, screen_pt, time_to_scene(workarea_in),
						start_times, workarea_in);
			attempt_snap(potential_snaps, screen_pt, time_to_scene(workarea_out),
						start_times, workarea_out);
		}
	}

	if ((snap_points & k_snap_to_keyframes) && get_snap_keyframes()) {
		for (auto it = get_snap_keyframes()->cbegin();
			 it != get_snap_keyframes()->cend(); it++) {
			const QVector<oak::Keyframe> &keys = (*it)->get_keyframes();
			for (auto jt = keys.cbegin(); jt != keys.cend(); jt++) {
				OakEngineKeyframe *key = jt->handle();

				auto ignore = get_snap_ignore_keyframes();
				if (ignore && std::find(ignore->cbegin(), ignore->cend(),
										key) != ignore->cend()) {
					continue;
				}

				Rational time = key_time(key);
				if (const TimeTargetObject *target = get_keyframe_time_target()) {
					if (OakEngineNode *parent = key_node(key)) {
						time = target->get_adjusted_time(
							parent, target->get_time_target(), time,
							k_transform_towards_output);
					}
				}

				qreal key_scene_pt = time_to_scene(time);

				attempt_snap(potential_snaps, screen_pt, key_scene_pt,
							start_times, time);
			}
		}
	}

	if (potential_snaps.empty()) {
		hide_snaps();
		return false;
	}

	int closest_snap = 0;
	Rational closest_diff = qAbs(potential_snaps.at(0).movement - *movement);

	// Determine which snap point was the closest
	for (size_t i = 1; i < potential_snaps.size(); i++) {
		Rational this_diff = qAbs(potential_snaps.at(i).movement - *movement);

		if (this_diff < closest_diff) {
			closest_snap = i;
			closest_diff = this_diff;
		}
	}

	*movement = potential_snaps.at(closest_snap).movement;

	// Find all points at this movement
	std::vector<Rational> snap_times;
	foreach (const SnapData &d, potential_snaps) {
		if (d.movement == *movement) {
			snap_times.push_back(d.time);
		}
	}

	show_snaps(snap_times);

	return true;
}

void TimeBasedWidget::show_snaps(const std::vector<Rational> &times)
{
	foreach (TimeBasedView *view, timeline_views_) {
		view->enable_snap(times);
	}
}

void TimeBasedWidget::hide_snaps()
{
	foreach (TimeBasedView *view, timeline_views_) {
		view->disable_snap();
	}
}

bool TimeBasedWidget::copy_selected(bool cut)
{
	if (ruler()->copy_selected(cut)) {
		return true;
	}

	return false;
}

bool TimeBasedWidget::paste()
{
	if (ruler()->paste_markers()) {
		return true;
	}

	return false;
}

}
