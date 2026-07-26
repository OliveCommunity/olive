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

#ifndef OAK_TIMEBASEDWIDGET_H
#define OAK_TIMEBASEDWIDGET_H

#include <QPointer>
#include <QWidget>

#include "node/output/viewer/viewer.h"
#include "timeline/timelinecommon.h"
#include "widget/keyframeview/keyframeviewinputconnection.h"
#include "widget/resizablescrollbar/resizabletimelinescrollbar.h"
#include "widget/timebased/timescaledobject.h"
#include "widget/timelinewidget/view/timelineview.h"
#include "widget/timetarget/timetarget.h"

namespace olive
{

class EngineEventBridge;
class TimeRuler;

class TimeBasedWidget : public TimelineScaledWidget {
	Q_OBJECT
public:
	TimeBasedWidget(bool ruler_text_visible = true,
					bool ruler_cache_status_visible = false,
					QWidget *parent = nullptr);

	void zoom_in();

	void zoom_out();

	ViewerOutput *get_connected_node() const;

	void connect_viewer_node(ViewerOutput *node);

	TimelineWorkArea *get_connected_work_area() const
	{
		return workarea_;
	}
	TimelineMarkerList *get_connected_markers() const
	{
		return markers_;
	}
	void connect_work_area(TimelineWorkArea *workarea);
	void connect_markers(TimelineMarkerList *markers);

	void set_scale_and_center_on_playhead(const double &scale);

	TimeRuler *ruler() const;

	using SnapMask = uint32_t;
	enum SnapPoints {
		k_snap_to_clips = 0x1,
		k_snap_to_playhead = 0x2,
		k_snap_to_markers = 0x4,
		k_snap_to_keyframes = 0x8,
		k_snap_to_workarea = 0x10,
		k_snap_all = UINT32_MAX
	};

	/**
   * @brief Snaps point `start_point` that is moving by `movement` to currently existing clips
   */
	bool snap_point(const std::vector<Rational> &start_times, Rational *movement,
				   SnapMask snap_points = k_snap_all);
	void show_snaps(const std::vector<Rational> &times);
	void hide_snaps();

	virtual bool copy_selected(bool cut);

	virtual bool paste();

public slots:
	void SetTimebase(const Rational &timebase);

	void SetScale(const double &scale);

	void go_to_start();

	void prev_frame();

	void next_frame();

	void go_to_end();

	void go_to_prev_cut();

	void go_to_next_cut();

	void set_in_at_playhead();

	void set_out_at_playhead();

	void reset_in();

	void reset_out();

	void clear_in_out_points();

	void set_marker();

	void toggle_show_all();

	void go_to_in();

	void go_to_out();

	void delete_selected();

protected:
	ResizableTimelineScrollBar *scrollbar() const;

	virtual void TimebaseChangedEvent(const Rational &) override;

	virtual void TimeChangedEvent(const Rational &)
	{
	}

	virtual void ScaleChangedEvent(const double &) override;

	virtual void ConnectedNodeChangeEvent(ViewerOutput *)
	{
	}

	virtual void ConnectedWorkAreaChangeEvent(TimelineWorkArea *)
	{
	}
	virtual void ConnectedMarkersChangeEvent(TimelineMarkerList *)
	{
	}

	EngineEventBridge *bridge_ = nullptr;

	virtual void ConnectNodeEvent(ViewerOutput *)
	{
	}

	virtual void DisconnectNodeEvent(ViewerOutput *)
	{
	}

	void set_auto_max_scroll_bar(bool e);

	virtual void resizeEvent(QResizeEvent *event) override;

	void connect_timeline_view(TimeBasedView *base);

	void set_catch_up_scroll_value(QScrollBar *b, int v, int maximum);
	void stop_catch_up_scroll_timer(QScrollBar *b);

	virtual const QVector<Block *> *get_snap_blocks() const
	{
		return nullptr;
	}
	virtual const QVector<KeyframeViewInputConnection *> *
	get_snap_keyframes() const
	{
		return nullptr;
	}
	virtual const TimeTargetObject *get_keyframe_time_target() const
	{
		return nullptr;
	}
	virtual const std::vector<NodeKeyframe *> *get_snap_ignore_keyframes() const
	{
		return nullptr;
	}
	virtual const std::vector<TimelineMarker *> *get_snap_ignore_markers() const
	{
		return nullptr;
	}

protected slots:
	/**
   * @brief Slot to center the horizontal scroll bar on the playhead's current position
   */
	void center_scroll_on_playhead();

	/**
   * @brief By default, TimeBasedWidget will set the timebase to the viewer node's video timebase.
   * Set this to false if you want to set your own timebase.
   */
	void set_auto_set_timebase(bool e);

	static void page_scroll_internal(QScrollBar *bar, int maximum,
								   int screen_position, bool whole_page_scroll);

	void stop_catch_up_scroll_timer()
	{
		stop_catch_up_scroll_timer(scrollbar_);
	}

	void set_catch_up_scroll_value(int v);

signals:
	void timebase_changed(const Rational &);

	void connected_node_changed(OakEngineNode *old, OakEngineNode *now);

protected slots:
	virtual void SendCatchUpScrollEvent();

private:
	/**
   * @brief Set either in or out point to the current playhead
   *
   * @param m
   *
   * Set to kTrimIn or kTrimOut for setting the in point or out point respectively.
   */
	void set_point(Timeline::MovementMode m, const Rational &time);

	/**
   * @brief Reset either the in or out point
   *
   * Sets either the in point to 0 or the out point to `RATIONAL_MAX`.
   *
   * @param m
   *
   * Set to kTrimIn or kTrimOut for setting the in point or out point respectively.
   */
	void reset_point(Timeline::MovementMode m);

	void page_scroll_internal(int screen_position, bool whole_page_scroll);

	bool user_is_dragging_playhead() const;

	QPointer<ViewerOutput> viewer_node_;

	TimeRuler *ruler_;

	ResizableTimelineScrollBar *scrollbar_;

	bool auto_max_scrollbar_;

	QList<TimeBasedView *> timeline_views_;

	bool toggle_show_all_;

	double toggle_show_all_old_scale_;
	int toggle_show_all_old_scroll_;

	bool auto_set_timebase_;

	int scrollbar_start_width_;
	double scrollbar_start_value_;
	double scrollbar_start_scale_;
	bool scrollbar_top_handle_;

	TimelineWorkArea *workarea_;
	TimelineMarkerList *markers_;

	QTimer *catchup_scroll_timer_;
	struct CatchUpScrollData {
		qint64 last_forced = 0;
		int maximum;
		int value;
	};
	QMap<QScrollBar *, CatchUpScrollData> catchup_scroll_values_;

private slots:
	void update_maximum_scroll();

	void scroll_bar_resize_began(int current_bar_width, bool top_handle);

	void scroll_bar_resize_moved(int new_bar_width);

	/**
   * @brief Slot to handle page scrolling of the playhead
   *
   * If the playhead is outside the current scroll bounds, this function will scroll to where it is. Otherwise it will
   * do nothing.
   */
	void page_scroll_to_playhead();

	void catch_up_scroll_to_playhead();

	void catch_up_scroll_to_point(int point);

	void catch_up_timer_timeout();

	void auto_update_timebase();

	void connected_node_removed_from_graph();

	void playhead_time_changed(const Rational &time);
};

}

#endif // OAK_TIMEBASEDWIDGET_H
