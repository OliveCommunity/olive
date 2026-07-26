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

#ifndef OAK_TIMELINEVIEW_H
#define OAK_TIMELINEVIEW_H

#include <QGraphicsView>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>

#include "engineeventbridge.h"
#include "node/block/clip/clip.h"
#include "timelineviewmouseevent.h"
#include "timelineviewghostitem.h"
#include "widget/timebased/timebasedview.h"

namespace olive
{

/**
 * @brief A widget for viewing and interacting Sequences
 *
 * This widget primarily exposes users to viewing and modifying Block nodes, usually through a TimelineOutput node.
 */
class TimelineView : public TimeBasedView {
	Q_OBJECT
public:
	TimelineView(Qt::Alignment vertical_alignment = Qt::AlignTop,
				 QWidget *parent = nullptr);

	int get_track_y(int track_index) const;
	int get_track_height(int track_index) const;

	QPoint get_scroll_coordinates() const;
	void set_scroll_coordinates(const QPoint &pt);

	void connect_track_list(TrackList *list);

	void track_list_changed();

	void set_beam_cursor(const TimelineCoordinate &coord);
	void set_transition_overlay(ClipBlock *out, ClipBlock *in);
	void enable_recording_overlay(const TimelineCoordinate &coord);
	void disable_recording_overlay();

	void set_selection_list(QHash<Track::Reference, TimeRangeList> *s)
	{
		selections_ = s;
	}

	void set_ghost_list(QVector<TimelineViewGhostItem *> *ghosts)
	{
		ghosts_ = ghosts;
	}

	int scene_to_track(double y);

	Block *get_item_at_scene_pos(const Rational &time, int track_index) const;

	QVector<Block *> get_items_at_scene_rect(const QRectF &rect) const;

signals:
	void mouse_pressed(TimelineViewMouseEvent *event);
	void mouse_moved(TimelineViewMouseEvent *event);
	void mouse_released(TimelineViewMouseEvent *event);
	void mouse_double_clicked(TimelineViewMouseEvent *event);

	void drag_entered(TimelineViewMouseEvent *event);
	void drag_moved(TimelineViewMouseEvent *event);
	void drag_left(QDragLeaveEvent *event);
	void drag_dropped(TimelineViewMouseEvent *event);

protected:
	virtual void mousePressEvent(QMouseEvent *event) override;
	virtual void mouseMoveEvent(QMouseEvent *event) override;
	virtual void mouseReleaseEvent(QMouseEvent *event) override;
	virtual void mouseDoubleClickEvent(QMouseEvent *event) override;

	virtual void dragEnterEvent(QDragEnterEvent *event) override;
	virtual void dragMoveEvent(QDragMoveEvent *event) override;
	virtual void dragLeaveEvent(QDragLeaveEvent *event) override;
	virtual void dropEvent(QDropEvent *event) override;

	virtual void drawBackground(QPainter *painter, const QRectF &rect) override;
	virtual void drawForeground(QPainter *painter, const QRectF &rect) override;

	virtual void ToolChangedEvent(Tool::Item tool) override;

	virtual void SceneRectUpdateEvent(QRectF &rect) override;

private:
	Track::Type connected_track_type();

	TimelineCoordinate screen_to_coordinate(const QPoint &pt);
	TimelineCoordinate scene_to_coordinate(const QPointF &pt);

	TimelineViewMouseEvent CreateMouseEvent(QMouseEvent *event);
	TimelineViewMouseEvent CreateMouseEvent(const QPoint &pos,
											Qt::MouseButton button,
											Qt::KeyboardModifiers modifiers);

	void draw_blocks(QPainter *painter, bool foreground);

	void draw_block(QPainter *painter, bool foreground, Block *block, qreal top,
				   qreal height, const Rational &in, const Rational &out,
				   const Rational &media_in);
	void draw_block(QPainter *painter, bool foreground, Block *block, qreal top,
				   qreal height);

	void draw_zebra_stripes(QPainter *painter, const QRectF &r);

	int get_height_of_all_tracks() const;

	void update_playhead_rect();

	qreal get_timeline_left_bound() const;

	qreal get_timeline_right_bound() const;

	void draw_thumbnail(QPainter *painter, const FrameHashCache *thumbs,
					   const Rational &time, int x, const QRect &preview_rect,
					   QRect *thumb_rect) const;

	QHash<Track::Reference, TimeRangeList> *selections_;

	QVector<TimelineViewGhostItem *> *ghosts_;

	bool show_beam_cursor_;

	TimelineCoordinate cursor_coord_;

	TrackList *connected_track_list_;

	ClipBlock *transition_overlay_out_;
	ClipBlock *transition_overlay_in_;

	QMap<TimelineMarker *, QRectF> clip_marker_rects_;

	bool recording_overlay_;
	TimelineCoordinate recording_coord_;
};

}

#endif // OAK_TIMELINEVIEW_H
