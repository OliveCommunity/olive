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

#include "timelineview.h"

#include <QByteArray>
#include <QDebug>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QtMath>
#include <QPen>

#include "common/configwrapper.h"
#include "oakutil/qtutils.h"
#include "../../timeruler/markerpainting.h"
#include "oakengine/preview.h"
#include "oakengine/timeline.h"
#include "oakengine/viewer.h"
#include "panel/panelmanager.h"
#include "panel/timeline/timeline.h"
#include "widget/timelinewidget/cliphandle.h"
#include "widget/timelinewidget/trackhandle.h"
#include "common/colorcodingapp.h"
#include "widget/timelinewidget/timelinewidget.h"

#include "widget/viewer/vieweroutpututils.h"
namespace olive
{

#define super TimeBasedView

namespace
{

/// Number of tracks of `type` in `seq` (TrackList::get_track_count()).
int track_count_of(OakEngineSequence *seq, int type)
{
	if (!seq || type < 0 || type >= 3) {
		return 0;
	}
	int counts[3] = { 0, 0, 0 };
	oakengine_sequence_track_count(seq, &counts[0], &counts[1], &counts[2]);
	return counts[type];
}

/// Borrowed track at (type, index) as an opaque track handle.
OakEngineTrack *track_at(OakEngineSequence *seq, int type, int index)
{
	return oakengine_sequence_track_at(seq, type, index);
}

/// All tracks of `type` through the C ABI (count + indexed access).
QVector<OakEngineTrack *> track_list_tracks(OakEngineSequence *seq, int type)
{
	QVector<OakEngineTrack *> tracks;
	const int n = track_count_of(seq, type);
	tracks.reserve(n);
	for (int i = 0; i < n; i++) {
		if (OakEngineTrack *t = track_at(seq, type, i)) {
			tracks.append(t);
		}
	}
	return tracks;
}

/// Track::get_track_height_in_pixels() through the C ABI.
int track_height_in_pixels(OakEngineSequence *seq, int type, int index)
{
	double h = 0;
	if (!seq ||
		oakengine_track_get_height(seq, type, index, &h) != OAKENGINE_OK) {
		return oakengine_track_default_height_in_pixels();
	}
	return oakengine_track_height_internal_to_pixels(h);
}

/// The track's blocks through the C ABI (count + indexed access).
QVector<OakEngineBlock *> track_all_blocks(OakEngineTrack *track)
{
	QVector<OakEngineBlock *> blocks;
	OakEngineTrack *h = trackhandle(track);
	const int n = oakengine_track_block_count(h);
	blocks.reserve(n);
	for (int i = 0; i < n; i++) {
		if (OakEngineBlock *b = oakengine_track_block_at(h, i)) {
			blocks.append(b);
		}
	}
	return blocks;
}

/// Clip-style predicate for block handles (replaces a dynamic_cast to the
/// engine clip class now that its definition is no longer visible here).
OakEngineBlock *block_as_clip(OakEngineBlock *block)
{
	return (block &&
			oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(block)))
			   ? block
			   : nullptr;
}

/// The block's in-point as rational seconds.
Rational block_time_in(OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_in_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// The block's out-point as rational seconds.
Rational block_time_out(OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_out_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// The block's length as rational seconds.
Rational block_length(OakEngineBlock *block)
{
	int num = 0, den = 1;
	oakengine_block_get_length_rational(
		reinterpret_cast<const OakEngineNode *>(block), &num, &den);
	return Rational(num, den);
}

/// Next block on the track (borrowed handle, may be null).
OakEngineBlock *block_next(OakEngineBlock *block)
{
	return oakengine_block_next(block);
}

/// The block's enabled flag through the C ABI.
bool block_is_enabled(OakEngineBlock *block)
{
	return oakengine_block_is_enabled(block) != 0;
}

/// The block's effective color through the C ABI (color label -> app
/// ColorCoding).
core::Color block_color(OakEngineBlock *block)
{
	return AppColorCoding::get_color(oakengine_node_get_effective_color_label(
		reinterpret_cast<const OakEngineNode *>(block)));
}

/// Brush for a block, app-side (gradient or flat, mirroring the engine
/// version).
QBrush block_brush(OakEngineBlock *block, qreal top, qreal bottom)
{
	const QColor c = QtUtils::to_q_color(block_color(block));
	if (OAK_CONFIG("UseGradients").toBool()) {
		QLinearGradient grad;
		grad.setStart(0, top);
		grad.setFinalStop(0, bottom);
		grad.setColorAt(0.0, c.lighter());
		grad.setColorAt(1.0, c);
		return grad;
	}
	return c;
}

/// The block's link presence through the C ABI.
bool block_has_links(OakEngineBlock *block)
{
	return oakengine_block_link_count(block) > 0;
}

/// The block's label-or-name through the C ABI.
QString block_label_or_name(OakEngineBlock *block)
{
	char buf[1024];
	buf[0] = '\0';
	oakengine_node_get_label_and_name(
		reinterpret_cast<const OakEngineNode *>(block), buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

/// The clip's connected viewer as an OakEngineNode handle (borrowed).
OakEngineNode *clip_connected_viewer(OakEngineBlock *clip)
{
	return oakengine_clip_get_connected_viewer(clip);
}

/// The clip's track type through the C ABI (track of the clip).
int clip_track_type(OakEngineBlock *clip)
{
	return oakengine_track_type(reinterpret_cast<OakEngineTrack *>(
		oakengine_clip_get_track(reinterpret_cast<OakEngineNode *>(clip))));
}

/// The viewer's length as rational seconds.
Rational viewer_length(OakEngineNode *viewer)
{
	int64_t num = 0, den = 1;
	oakengine_viewer_get_length(viewer, &num, &den);
	return Rational(int(num), int(den));
}

/// Marker time range as rational seconds (oakengine_marker_get_time).
TimeRange marker_time_range(const OakEngineMarker *marker)
{
	int64_t in_num = 0, in_den = 1, out_num = 0, out_den = 1;
	oakengine_marker_get_time(marker, &in_num, &in_den, &out_num, &out_den);
	return TimeRange(Rational(int(in_num), int(in_den)),
					 Rational(int(out_num), int(out_den)));
}

/// Marker name through the C ABI.
QString marker_name_of(const OakEngineMarker *marker)
{
	const int size = oakengine_marker_get_name(marker, nullptr, 0);
	QByteArray buf(size + 1, '\0');
	oakengine_marker_get_name(marker, buf.data(), int(buf.size()));
	return QString::fromUtf8(buf.constData());
}

} // namespace

TimelineView::TimelineView(Qt::Alignment vertical_alignment, QWidget *parent)
	: super(parent)
	, selections_(nullptr)
	, ghosts_(nullptr)
	, show_beam_cursor_(false)
	, connected_sequence_(nullptr)
	, connected_track_type_(-1)
	, transition_overlay_out_(nullptr)
	, transition_overlay_in_(nullptr)
{
	Q_ASSERT(vertical_alignment == Qt::AlignTop ||
			 vertical_alignment == Qt::AlignBottom);
	setAlignment(Qt::AlignLeft | vertical_alignment);

	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	setBackgroundRole(QPalette::Window);
	setContextMenuPolicy(Qt::CustomContextMenu);
	viewport()->setMouseTracking(true);

	set_is_timeline_axes(true);
}

void TimelineView::mousePressEvent(QMouseEvent *event)
{
	// If we click on marker, jump to that point in the timeline
	QPointF scene_pos = mapToScene(event->pos());
	for (auto it = clip_marker_rects_.cbegin(); it != clip_marker_rects_.cend();
		 it++) {
		if (it.value().contains(scene_pos)) {
			const Rational marker_in = marker_time_range(it.key()).in();
			oakengine_viewer_set_playhead(get_viewer_node(),
			marker_in.numerator(),
			marker_in.denominator());
			break;
		}
	}

	TimelineViewMouseEvent timeline_event = CreateMouseEvent(event);

	if (hand_press(event) ||
		(!get_item_at_scene_pos(timeline_event.get_frame(),
							timeline_event.get_track().index()) &&
		 Core::instance()->tool() != Tool::k_add && playhead_press(event))) {
		// Let the parent handle this
		return;
	}

	if (dragMode() != get_default_drag_mode()) {
		// Use default behavior when hand dragging for instance
		super::mousePressEvent(event);
		return;
	}

	emit mouse_pressed(&timeline_event);
}

void TimelineView::mouseMoveEvent(QMouseEvent *event)
{
	TimelineViewMouseEvent timeline_event = CreateMouseEvent(event);

	if (hand_move(event) || playhead_move(event)) {
		// Let the parent handle this
		return;
	}

	if (dragMode() != get_default_drag_mode()) {
		super::mouseMoveEvent(event);
		return;
	}

	if (event->buttons() == Qt::NoButton) {
		OakEngineBlock *b = get_item_at_scene_pos(
			timeline_event.get_frame(), timeline_event.get_track().index());
		if (b) {
			setToolTip(
				tr("In: %1\nOut: %2\nDuration: %3")
					.arg(QString::fromStdString(Timecode::time_to_timecode(
							 block_time_in(b), timebase(),
							 Core::instance()->get_timecode_display())),
						 QString::fromStdString(Timecode::time_to_timecode(
							 block_time_out(b), timebase(),
							 Core::instance()->get_timecode_display())),
						 QString::fromStdString(Timecode::time_to_timecode(
							 block_length(b), timebase(),
							 Core::instance()->get_timecode_display()))));
		} else {
			setToolTip(QString());
		}
	}

	emit mouse_moved(&timeline_event);
}

void TimelineView::mouseReleaseEvent(QMouseEvent *event)
{
	if (hand_release(event) || playhead_release(event)) {
		// Let the parent handle this
		return;
	}

	if (dragMode() != get_default_drag_mode()) {
		super::mouseReleaseEvent(event);
		return;
	}

	TimelineViewMouseEvent timeline_event = CreateMouseEvent(event);

	emit mouse_released(&timeline_event);
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent *event)
{
	TimelineViewMouseEvent timeline_event = CreateMouseEvent(event);

	emit mouse_double_clicked(&timeline_event);
}

void TimelineView::dragEnterEvent(QDragEnterEvent *event)
{
	TimelineViewMouseEvent timeline_event = CreateMouseEvent(
		event->pos(), Qt::NoButton, event->keyboardModifiers());

	timeline_event.set_mime_data(event->mimeData());
	timeline_event.SetEvent(event);

	emit drag_entered(&timeline_event);
}

void TimelineView::dragMoveEvent(QDragMoveEvent *event)
{
	TimelineViewMouseEvent timeline_event = CreateMouseEvent(
		event->pos(), Qt::NoButton, event->keyboardModifiers());

	timeline_event.set_mime_data(event->mimeData());
	timeline_event.SetEvent(event);

	emit drag_moved(&timeline_event);
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent *event)
{
	emit drag_left(event);
}

void TimelineView::dropEvent(QDropEvent *event)
{
	TimelineViewMouseEvent timeline_event = CreateMouseEvent(
		event->pos(), Qt::NoButton, event->keyboardModifiers());

	timeline_event.set_mime_data(event->mimeData());
	timeline_event.SetEvent(event);

	emit drag_dropped(&timeline_event);
}

void TimelineView::drawBackground(QPainter *painter, const QRectF &rect)
{
	if (!connected_sequence_) {
		return;
	}

	painter->setPen(palette().base().color());

	int line_y = 0;

	const int track_count = track_count_of(connected_sequence_,
										   connected_track_type_);
	for (int i = 0; i < track_count; i++) {
		line_y += track_height_in_pixels(connected_sequence_,
										 connected_track_type_, i);

		// One px gap between tracks
		line_y++;

		int this_line_y;

		if (alignment() & Qt::AlignTop) {
			this_line_y = line_y;
		} else {
			this_line_y = -line_y;
		}

		painter->drawLine(qRound(rect.left()), this_line_y,
						  qRound(rect.right()), this_line_y);
	}
}

void TimelineView::drawForeground(QPainter *painter, const QRectF &rect)
{
	if (!connected_sequence_) {
		return;
	}

	// Draw block backgrounds
	draw_blocks(painter, false);

	// Draw selections
	if (selections_ && !selections_->isEmpty()) {
		painter->setPen(Qt::NoPen);
		painter->setBrush(QColor(0, 0, 0, 64));

		for (auto it = selections_->cbegin(); it != selections_->cend(); it++) {
			// TrackReference mirror ordinals == engine Track::Type ordinals
			// (static_assert-pinned in trackreferencehandle.h)
			if (it.key().type() ==
				static_cast<TrackReference::Type>(connected_track_type_)) {
				int track_index = it.key().index();

				foreach (const TimeRange &range, it.value()) {
					painter->drawRect(time_to_scene(range.in()),
									  get_track_y(track_index),
									  time_to_scene(range.length()),
									  get_track_height(track_index));
				}
			}
		}
	}

	// Draw block foregrounds
	draw_blocks(painter, true);

	// Draw ghosts
	if (ghosts_ && !ghosts_->isEmpty()) {
		foreach (TimelineViewGhostItem *ghost, (*ghosts_)) {
			if (ghost->get_track().type() ==
					static_cast<TrackReference::Type>(connected_track_type_) &&
				!ghost->is_invisible()) {
				int track_index = ghost->get_adjusted_track().index();

				OakEngineBlock *attached = QtUtils::value_to_ptr<OakEngineBlock>(
					ghost->get_data(TimelineViewGhostItem::k_attached_block));

				if (attached &&
					OAK_CONFIG("ShowClipWhileDragging").toBool()) {
					int adj_track = ghost->get_adjusted_track().index();
					qreal track_top = get_track_y(adj_track);
					qreal track_height = get_track_height(adj_track);

					qreal old_opacity = painter->opacity();
					painter->setOpacity(0.5);

					Rational in = ghost->get_adjusted_in(),
							 out = ghost->get_adjusted_out(),
							 media_in = ghost->get_adjusted_media_in();
					draw_block(painter, false, attached, track_top, track_height,
							  in, out, media_in);
					draw_block(painter, true, attached, track_top, track_height,
							  in, out, media_in);

					painter->setOpacity(old_opacity);
				}

				painter->setPen(QPen(Qt::yellow, 2));
				painter->setBrush(Qt::NoBrush);

				painter->drawRect(time_to_scene(ghost->get_adjusted_in()),
								  get_track_y(track_index),
								  time_to_scene(ghost->get_adjusted_length()),
								  get_track_height(track_index));
			}
		}
	}

	// Draw beam cursor
	if (show_beam_cursor_ &&
		cursor_coord_.get_track().type() ==
			static_cast<TrackReference::Type>(connected_track_type_)) {
		painter->setPen(Qt::gray);

		double cursor_x = time_to_scene(cursor_coord_.get_frame());
		int track_index = cursor_coord_.get_track().index();
		int track_y = get_track_y(track_index);

		painter->drawLine(cursor_x, track_y, cursor_x,
						  track_y + get_track_height(track_index));
	}

	// Draw recording overlay
	if (recording_overlay_ &&
		recording_coord_.get_track().type() ==
			static_cast<TrackReference::Type>(connected_track_type_)) {
		painter->setPen(QPen(Qt::red, 2));
		painter->setBrush(QColor(255, 128, 128));

		int x = time_to_scene(recording_coord_.get_frame());
		int64_t ph_num = 0, ph_den = 1;
		oakengine_viewer_get_playhead(get_viewer_node(), &ph_num,
			&ph_den);
		painter->drawRect(x, get_track_y(recording_coord_.get_track().index()),
						  time_to_scene(Rational(int(ph_num), int(ph_den))) - x,
						  get_track_height(recording_coord_.get_track().index()));
	}

	// Draw standard TimelineViewBase things (such as playhead)
	super::drawForeground(painter, rect);
}

void TimelineView::ToolChangedEvent(Tool::Item tool)
{
	switch (tool) {
	case Tool::k_razor:
		setCursor(Qt::SplitHCursor);
		break;
	case Tool::k_edit:
		setCursor(Qt::IBeamCursor);
		break;
	case Tool::k_add:
	case Tool::k_transition:
	case Tool::k_zoom:
	case Tool::k_record:
		setCursor(Qt::CrossCursor);
		break;
	case Tool::k_track_select:
		setCursor(Qt::SizeHorCursor); // FIXME: Not the ideal cursor
		break;
	default:
		unsetCursor();
	}

	// Hide/show cursor if necessary
	if (show_beam_cursor_) {
		show_beam_cursor_ = false;
		viewport()->update();
	}
}

void TimelineView::SceneRectUpdateEvent(QRectF &rect)
{
	if (alignment() & Qt::AlignTop) {
		rect.setTop(0);
		rect.setBottom(get_height_of_all_tracks() + height() / 2);
	} else if (alignment() & Qt::AlignBottom) {
		rect.setBottom(0);
		rect.setTop(get_height_of_all_tracks() - height() / 2);
	}
}

TrackReference::Type TimelineView::connected_track_type() const
{
	return static_cast<TrackReference::Type>(connected_track_type_);
}

TimelineCoordinate TimelineView::screen_to_coordinate(const QPoint &pt)
{
	return scene_to_coordinate(mapToScene(pt));
}

TimelineCoordinate TimelineView::scene_to_coordinate(const QPointF &pt)
{
	return TimelineCoordinate(scene_to_time(pt.x()),
							  TrackReference(connected_track_type(),
											 scene_to_track(pt.y())));
}

TimelineViewMouseEvent TimelineView::CreateMouseEvent(QMouseEvent *event)
{
	return CreateMouseEvent(event->pos(), event->button(), event->modifiers());
}

TimelineViewMouseEvent
TimelineView::CreateMouseEvent(const QPoint &pos, Qt::MouseButton button,
							   Qt::KeyboardModifiers modifiers)
{
	QPointF scene_pt = mapToScene(pos);

	return TimelineViewMouseEvent(scene_pt, pos, get_scale(), timebase(),
								  TrackReference(connected_track_type(),
												 scene_to_track(scene_pt.y())),
								  button, modifiers);
}

void TimelineView::draw_blocks(QPainter *painter, bool foreground)
{
	Rational start_time = scene_to_time(get_timeline_left_bound());
	Rational end_time = scene_to_time(get_timeline_right_bound());
	const int64_t start_ts = core::Timecode::time_to_timestamp(
		start_time, sequence_timebase(connected_sequence_));

	foreach (OakEngineTrack *track,
			 track_list_tracks(connected_sequence_, connected_track_type_)) {
		// Get first visible block in this track
		OakEngineBlock *block = oakengine_track_nearest_block_before_or_at(
			trackhandle(track), start_ts);

		const int track_index = track_index_of(track);
		qreal track_top = get_track_y(track_index);
		qreal track_height = get_track_height(track_index);

		while (block) {
			draw_block(painter, foreground, block, track_top, track_height);

			if (block_time_out(block) >= end_time) {
				// Rest of the clips are offscreen, can break loop now
				break;
			}

			block = block_next(block);
		}
	}
}

void TimelineView::draw_block(QPainter *painter, bool foreground,
							 OakEngineBlock *block, qreal block_top,
							 qreal block_height)
{
	Rational media_in = 0;
	if (OakEngineBlock *cb = block_as_clip(block)) {
		int64_t in_num, in_den;
		if (oakengine_clip_get_media_range_rational(
				cliphandle(cb), &in_num, &in_den,
				nullptr, nullptr) == OAKENGINE_OK) {
			media_in = Rational(in_num, in_den);
		}
	}
	draw_block(painter, foreground, block, block_top, block_height,
			   block_time_in(block), block_time_out(block), media_in);
}

void TimelineView::draw_block(QPainter *painter, bool foreground,
							 OakEngineBlock *block, qreal block_top,
							 qreal block_height, const Rational &in,
							 const Rational &out, const Rational &media_in)
{
	if (block_as_clip(block) ||
		oakengine_node_is_transition(reinterpret_cast<OakEngineNode *>(block))) {
		qreal block_in = time_to_scene(in);

		qreal block_left = qMax(get_timeline_left_bound(), block_in);
		qreal block_right = qMin(get_timeline_right_bound(), time_to_scene(out)) - 1;

		QRectF r(block_left, block_top, block_right - block_left, block_height);

		QColor shadow_color = block_is_enabled(block) ?
								  QtUtils::to_q_color(block_color(block)).darker() :
								  QColor(Qt::darkGray).darker();

		const qreal minimum_rect_width = 2;
		const qreal minimum_detail_width = 8;

		if (r.width() <= minimum_rect_width) {
			if (!foreground) {
				// Just draw a green background
				// Width is likely fractional, so we ceil it and add 1 to ensure the entire width of the
				// rect is painted
				r.setWidth(std::ceil(r.width()) + 1);
				painter->fillRect(r, shadow_color);
			}
		} else {
			QFontMetrics fm = fontMetrics();
			int text_height = fm.height();
			int text_padding =
				text_height /
				4; // This ties into the track minimum height being 1.5
			int text_total_height = text_height + text_padding + text_padding;

			if (foreground) {
				painter->setBrush(Qt::NoBrush);

				if (r.width() > minimum_detail_width) {
					QString using_label = block_label_or_name(block);

					QRectF text_rect = r.adjusted(text_padding, text_padding,
												  -text_padding, -text_padding);
					painter->setPen(
						block_is_enabled(block) ?
							AppColorCoding::get_ui_selector_color(block_color(block)) :
							Qt::lightGray);
					painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignTop,
									  using_label);

					if (block_has_links(block)) {
						int text_width =
							qMin(qRound(text_rect.width()),
								 QtUtils::q_font_metrics_width(fm, using_label));

						int underline_y = text_rect.y() + text_height;

						painter->drawLine(text_rect.x(), underline_y,
										  text_width + text_rect.x(),
										  underline_y);
					}
				}

				qreal line_bottom = block_top + block_height - 1;

				painter->setPen(Qt::white);
				painter->drawLine(block_left, block_top, block_right,
								  block_top);
				painter->drawLine(block_left, block_top, block_left,
								  line_bottom);

				painter->setPen(shadow_color);
				painter->drawLine(block_left, line_bottom, block_right,
								  line_bottom);
				painter->drawLine(block_right, line_bottom, block_right,
								  block_top);
			} else {
				painter->setPen(Qt::NoPen);
				painter->setBrush(
					block_is_enabled(block) ?
						block_brush(block, block_top, block_top + block_height) :
						Qt::gray);
				painter->drawRect(r);

				if (r.width() > minimum_detail_width) {
					if (OakEngineBlock *clip = block_as_clip(block)) {
						QRect preview_rect = r.toRect();

						// Draw clip thumbnails
						if (clip_track_type(clip) == TrackReference::k_video &&
							OAK_CONFIG("TimelineThumbnailMode").toInt() !=
								TimelineApp::k_thumbnail_off) {
							// Start thumbnails underneath clip name
							preview_rect.adjust(0, text_total_height, 0, 0);

							if (preview_rect.height() > r.height() / 3) {
								if (const FrameHashCache *thumbs =
										clip_thumbnails(clip)) {
									QRect thumb_rect;
									painter->setRenderHint(
										QPainter::SmoothPixmapTransform);
									painter->setClipRect(preview_rect);

									if (OAK_CONFIG("TimelineThumbnailMode") ==
										TimelineApp::k_thumbnail_on) {
										OakEngineNode *s = oakengine_track_get_sequence(
											oakengine_clip_get_track(
												reinterpret_cast<OakEngineNode *>(clip)));
										int width = viewer_output_video_params(s).width();
										int height =
											viewer_output_video_params(s).height();
										int start;
										if (height >
											0) { // Prevent divide by zero/invalid params
											double scale =
												double(preview_rect.height()) /
												double(height);
											thumb_rect.setWidth(width * scale);
											start = (((preview_rect.left() -
													   int(qFloor(block_in))) /
													  thumb_rect.width()) *
													 thumb_rect.width()) +
													qFloor(block_in);
										} else {
											start = preview_rect.left();
										}

										for (int i = start;
											 i < preview_rect.right();
											 i += thumb_rect.width() + 1) {
											Rational time_here =
												scene_to_time(
													i - block_in, get_scale(),
													viewer_output_video_params(
														connected_sequence_)
														.frame_rate_as_time_base()) +
												media_in;
											draw_thumbnail(painter, thumbs,
														  time_here, i,
														  preview_rect,
														  &thumb_rect);
										}

									} else {
										Rational time =
											clip_media_range(clip).in();
										time = Timecode::snap_time_to_timebase(
											time, thumbs->get_timebase(),
											Timecode::k_floor);
										draw_thumbnail(painter, thumbs, time,
													  block_left, preview_rect,
													  &thumb_rect);
									}

									painter->setClipping(false);
								}
							}
						}

						// Draw waveform
						if (clip_track_type(clip) == TrackReference::k_audio &&
							OAK_CONFIG("TimelineWaveformMode").toInt() ==
								TimelineApp::k_waveforms_enabled) {
							if (const AudioWaveformCache *wave =
									clip_waveform(clip)) {
								Rational waveform_start =
									scene_to_time(
										block_left - block_in, get_scale(),
									viewer_output_audio_params(
										connected_sequence_)
										.sample_rate_as_time_base()) +
									media_in;
								painter->setPen(shadow_color);

								wave->Draw(painter, preview_rect,
										   this->get_scale(), waveform_start);
							}
						}

						// Draw zebra stripes and markers
						OakEngineNode *connected_viewer =
							clip_connected_viewer(clip);
						if (connected_viewer) {
							const Rational connected_length =
								viewer_length(connected_viewer);
							if (!connected_length.isNull()) {
								painter->setPen(shadow_color);

								if (clip_media_in(clip) < 0) {
									qreal zebra_right = time_to_scene(
										block_time_in(block) -
										clip_media_in(clip));

									switch (clip_loop_mode(clip)) {
									case OAKENGINE_LOOP_MODE_OFF:
										// Draw stripes for sections of clip < 0
										if (zebra_right >
											get_timeline_left_bound()) {
											draw_zebra_stripes(
												painter,
												QRectF(block_left, block_top,
													   zebra_right - block_left,
													   block_height));
										}
										break;
									case OAKENGINE_LOOP_MODE_LOOP:
										for (qreal i = zebra_right;
											 i > block_left;
											 i -= time_to_scene(
												 connected_length)) {
											painter->drawLine(i, block_top, i,
															  block_top +
																  block_height);
										}
										break;
									case OAKENGINE_LOOP_MODE_CLAMP:
										painter->drawLine(
											zebra_right, block_top, zebra_right,
											block_top + block_height);
										break;
									}
								}

								if (block_length(block) + clip_media_in(clip) >
									connected_length) {
									qreal zebra_left = time_to_scene(
										block_time_out(block) -
										(clip_media_in(clip) +
										 block_length(block) -
										 connected_length));
									switch (clip_loop_mode(clip)) {
									case OAKENGINE_LOOP_MODE_OFF:
										// Draw stripes for sections for clip > clip length
										if (zebra_left <
											get_timeline_right_bound()) {
											draw_zebra_stripes(
												painter,
												QRectF(zebra_left, block_top,
													   block_right - zebra_left,
													   block_height));
										}
										break;
									case OAKENGINE_LOOP_MODE_LOOP:
										for (qreal i = zebra_left;
											 i < block_right;
											 i += time_to_scene(
												 connected_length)) {
											painter->drawLine(i, block_top, i,
															  block_top +
																  block_height);
										}
										break;
									case OAKENGINE_LOOP_MODE_CLAMP:
										painter->drawLine(
											zebra_left, block_top, zebra_left,
											block_top + block_height);
										break;
									}
								}
							}

							OakEngineMarkerList *marker_list =
								oakengine_viewer_get_marker_list(
									connected_viewer);
							const int marker_count =
								oakengine_marker_list_count(marker_list);
							if (marker_count > 0) {
								clip_marker_rects_.clear();

								for (int mi = 0; mi < marker_count; mi++) {
									OakEngineMarker *marker =
										oakengine_marker_list_at(marker_list,
																 mi);
									const TimeRange marker_range =
										marker_time_range(marker);
									// Make sure marker is within In/Out points of the clip
									if (marker_range.in() >=
											clip_media_in(clip) &&
										marker_range.out() <=
											clip_media_in(clip) +
												block_length(block)) {
										QPoint marker_pt(
											time_to_scene(
												block_time_in(block) -
												clip_media_in(clip) +
												marker_range.in()),
											block_top + block_height);
										painter->setClipRect(r);
										QRect marker_rect =
											MarkerPainting::draw(
												painter, marker_pt, -1,
												get_scale(), false,
												marker_name_of(marker),
												oakengine_marker_get_color(
													marker),
												marker_range.in(),
												marker_range.out());
										clip_marker_rects_.insert(marker,
																  marker_rect);
										painter->setClipping(false);
									}
								}
							}
						}

						if (const FrameHashCache *cache =
								clip_connected_video_cache(clip)) {
							if (cache->has_validated_ranges()) {
								QRect cache_rect =
									r.adjusted(
										 0,
										 r.height() -
											 oakengine_playback_cache_indicator_height(),
										 0, 0)
										.toRect();
								cache->draw(painter, clip_media_in(clip),
											get_scale(), cache_rect);
							}
						}
					}

					// For transitions, show lines representing a transition
					if (oakengine_node_is_transition(
							reinterpret_cast<OakEngineNode *>(block))) {
						QVector<QLineF> lines;
						OakEngineBlock *tb = block;

						if (oakengine_transition_connected_in_block(tb)) {
							lines.append(QLineF(r.bottomLeft(), r.topRight()));
						}

						if (oakengine_transition_connected_out_block(tb)) {
							lines.append(QLineF(r.topLeft(), r.bottomRight()));
						}

						painter->setPen(shadow_color);
						painter->drawLines(lines);
					}

					OakEngineBlock *overlay_out = transition_overlay_out_;
					OakEngineBlock *overlay_in = transition_overlay_in_;
					if (overlay_out == block || overlay_in == block) {
						QRectF transition_overlay_rect = r;

						qreal transition_overlay_width =
							time_to_scene(block_length(block)) * 0.5;
						if (overlay_out && overlay_in) {
							// This is a dual transition, use the smallest width
							OakEngineBlock *other_block =
								(overlay_out == block) ? overlay_in : overlay_out;

							qreal other_width =
								time_to_scene(block_length(other_block)) * 0.5;

							transition_overlay_width =
								qMin(transition_overlay_width, other_width);
						}

						if (overlay_out == block) {
							transition_overlay_rect.setLeft(
								transition_overlay_rect.right() -
								transition_overlay_width);
						} else {
							transition_overlay_rect.setRight(
								transition_overlay_rect.left() +
								transition_overlay_width);
						}

						painter->setPen(Qt::NoPen);
						painter->setBrush(QColor(0, 0, 0, 64));

						painter->drawRect(transition_overlay_rect);
					}
				}
			}
		}
	}
}

void TimelineView::draw_zebra_stripes(QPainter *painter, const QRectF &r)
{
	int zebra_interval = fontMetrics().height();

	painter->setPen(QPen(QColor(0, 0, 0, 128), zebra_interval / 4));
	painter->setBrush(Qt::NoBrush);

	QVector<QLineF> lines;
	lines.reserve(qCeil(r.width() / zebra_interval));

	qreal left = r.left() - r.height();
	qreal right = r.right() + r.height();

	for (qreal i = left; i < right; i += zebra_interval) {
		lines.append(QLineF(i, r.top(), i - r.height(), r.bottom()));
	}

	painter->setClipRect(r);
	painter->drawLines(lines);
	painter->setClipping(false);
}

int TimelineView::get_height_of_all_tracks() const
{
	if (connected_sequence_) {
		const int count =
			track_count_of(connected_sequence_, connected_track_type_);
		if (alignment() & Qt::AlignTop) {
			return get_track_y(count);
		} else {
			return get_track_y(count - 1);
		}
	} else {
		return 0;
	}
}

qreal TimelineView::get_timeline_left_bound() const
{
	return horizontalScrollBar()->value();
}

qreal TimelineView::get_timeline_right_bound() const
{
	return get_timeline_left_bound() + viewport()->width();
}

void TimelineView::draw_thumbnail(QPainter *painter,
								 const FrameHashCache *thumbs,
								 const Rational &time, int x,
								 const QRect &preview_rect,
								 QRect *thumb_rect) const
{
	QString thumbnail = thumbs->get_valid_cache_filename(time);

	if (!thumbnail.isEmpty()) {
		QImage img;
		if (img.load(thumbnail, "jpg")) {
			double scale = double(preview_rect.height()) / double(img.height());
			*thumb_rect = QRect(x, preview_rect.top(), img.width() * scale,
								preview_rect.height());
			painter->drawImage(*thumb_rect, img);
		}
	}
}

int TimelineView::get_track_y(int track_index) const
{
	if (!connected_sequence_ ||
		!track_count_of(connected_sequence_, connected_track_type_)) {
		return 0;
	}

	int y = 0;

	if (alignment() & Qt::AlignBottom) {
		track_index++;
	}

	for (int i = 0; i < track_index; i++) {
		y += get_track_height(i);

		// One px line between each track
		y++;
	}

	if (alignment() & Qt::AlignBottom) {
		y = -y + 1;
	}

	return y;
}

int TimelineView::get_track_height(int track_index) const
{
	const int count = track_count_of(connected_sequence_, connected_track_type_);
	if (!connected_sequence_ || count == 0) {
		// Handle null or empty track list
		return oakengine_track_default_height_in_pixels();
	}

	if (track_index >= count) {
		// Handle new track at the end of the list
		return track_height_in_pixels(connected_sequence_,
									  connected_track_type_, count - 1);
	}

	if (track_index < 0) {
		// Handle new track at the beginning of the list
		return track_height_in_pixels(connected_sequence_,
									  connected_track_type_, 0);
	}

	// The track definitely exists, return its actual height
	return track_height_in_pixels(connected_sequence_, connected_track_type_,
								  track_index);
}

QPoint TimelineView::get_scroll_coordinates() const
{
	return QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
}

void TimelineView::set_scroll_coordinates(const QPoint &pt)
{
	horizontalScrollBar()->setValue(pt.x());
	verticalScrollBar()->setValue(pt.y());
}

void TimelineView::connect_track_list(OakEngineSequence *sequence,
									  int track_type)
{
	connected_sequence_ = sequence;
	connected_track_type_ = track_type;
}

void TimelineView::set_beam_cursor(const TimelineCoordinate &coord)
{
	if (!connected_sequence_) {
		return;
	}

	bool update_required =
		coord.get_track().type() ==
			static_cast<TrackReference::Type>(connected_track_type_) ||
		cursor_coord_.get_track().type() ==
			static_cast<TrackReference::Type>(connected_track_type_);

	show_beam_cursor_ = true;
	cursor_coord_ = coord;

	if (update_required) {
		viewport()->update();
	}
}

void TimelineView::set_transition_overlay(OakEngineBlock *out,
										 OakEngineBlock *in)
{
	if (transition_overlay_out_ != out || transition_overlay_in_ != in) {
		int type = -1;

		if (out) {
			type = clip_track_type(out);
		} else if (in) {
			type = clip_track_type(in);
		}

		if (type == connected_track_type_) {
			transition_overlay_out_ = out;
			transition_overlay_in_ = in;
		} else {
			transition_overlay_out_ = nullptr;
			transition_overlay_in_ = nullptr;
		}

		viewport()->update();
	}
}

void TimelineView::enable_recording_overlay(const TimelineCoordinate &coord)
{
	recording_overlay_ = true;
	recording_coord_ = coord;
	viewport()->update();
}

void TimelineView::disable_recording_overlay()
{
	recording_overlay_ = false;
	viewport()->update();
}

int TimelineView::scene_to_track(double y)
{
	int track = -1;
	int heights = 0;

	if (alignment() & Qt::AlignBottom) {
		y = -y;
	}

	do {
		track++;
		heights += get_track_height(track);
	} while (y > heights);

	return track;
}

OakEngineBlock *TimelineView::get_item_at_scene_pos(const Rational &time,
												int track_index) const
{
	if (connected_sequence_) {
		OakEngineTrack *track = track_at(connected_sequence_,
										 connected_track_type_, track_index);

		if (track) {
			foreach (OakEngineBlock *b, track_all_blocks(track)) {
				if (block_time_in(b) <= time && block_time_out(b) > time) {
					return b;
				}
			}
		}
	}

	return nullptr;
}

QVector<OakEngineBlock *> TimelineView::get_items_at_scene_rect(
	const QRectF &rect) const
{
	QVector<OakEngineBlock *> list;

	if (connected_sequence_) {
		Rational start = this->scene_to_time(rect.left());
		Rational end = this->scene_to_time(rect.right());
		const int64_t start_ts = core::Timecode::time_to_timestamp(
			start, sequence_timebase(connected_sequence_));

		const int count =
			track_count_of(connected_sequence_, connected_track_type_);
		for (int i = 0; i < count; i++) {
			OakEngineTrack *track = track_at(connected_sequence_,
											 connected_track_type_, i);
			int track_top = get_track_y(i);
			int track_bottom = track_top + get_track_height(i);

			if (track) {
				if (!(track_bottom < rect.top() || track_top > rect.bottom())) {
					OakEngineBlock *b =
						oakengine_track_nearest_block_before_or_at(
							trackhandle(track), start_ts);
					while (b && block_time_in(b) < end) {
						list.append(b);
						b = block_next(b);
					}
				}
			}
		}
	}

	return list;
}

void TimelineView::track_list_changed()
{
	update_scene_rect();
	viewport()->update();
}

}
