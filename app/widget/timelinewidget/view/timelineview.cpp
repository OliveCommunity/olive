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

#include <QDebug>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QtMath>
#include <QPen>

#include "config/config.h"
#include "common/qtutils.h"
#include "node/project/footage/footage.h"
#include "panel/panelmanager.h"
#include "panel/timeline/timeline.h"
#include "ui/colorcoding.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

#define super TimeBasedView

TimelineView::TimelineView(Qt::Alignment vertical_alignment, QWidget *parent)
	: super(parent)
	, selections_(nullptr)
	, ghosts_(nullptr)
	, show_beam_cursor_(false)
	, connected_track_list_(nullptr)
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
			get_viewer_node()->set_playhead(it.key()->time().in());
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
		Block *b = get_item_at_scene_pos(timeline_event.get_frame(),
									 timeline_event.get_track().index());
		if (b) {
			setToolTip(
				tr("In: %1\nOut: %2\nDuration: %3")
					.arg(QString::fromStdString(Timecode::time_to_timecode(
							 b->in(), timebase(),
							 Core::instance()->get_timecode_display())),
						 QString::fromStdString(Timecode::time_to_timecode(
							 b->out(), timebase(),
							 Core::instance()->get_timecode_display())),
						 QString::fromStdString(Timecode::time_to_timecode(
							 b->length(), timebase(),
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
	if (!connected_track_list_) {
		return;
	}

	painter->setPen(palette().base().color());

	int line_y = 0;

	foreach (Track *track, connected_track_list_->get_tracks()) {
		line_y += track->get_track_height_in_pixels();

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
	if (!connected_track_list_) {
		return;
	}

	// Draw block backgrounds
	draw_blocks(painter, false);

	// Draw selections
	if (selections_ && !selections_->isEmpty()) {
		painter->setPen(Qt::NoPen);
		painter->setBrush(QColor(0, 0, 0, 64));

		for (auto it = selections_->cbegin(); it != selections_->cend(); it++) {
			if (it.key().type() == connected_track_list_->type()) {
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
			if (ghost->get_track().type() == connected_track_list_->type() &&
				!ghost->is_invisible()) {
				int track_index = ghost->get_adjusted_track().index();

				Block *attached = QtUtils::value_to_ptr<Block>(
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
		cursor_coord_.get_track().type() == connected_track_list_->type()) {
		painter->setPen(Qt::gray);

		double cursor_x = time_to_scene(cursor_coord_.get_frame());
		int track_index = cursor_coord_.get_track().index();
		int track_y = get_track_y(track_index);

		painter->drawLine(cursor_x, track_y, cursor_x,
						  track_y + get_track_height(track_index));
	}

	// Draw recording overlay
	if (recording_overlay_ &&
		recording_coord_.get_track().type() == connected_track_list_->type()) {
		painter->setPen(QPen(Qt::red, 2));
		painter->setBrush(QColor(255, 128, 128));

		int x = time_to_scene(recording_coord_.get_frame());
		painter->drawRect(x, get_track_y(recording_coord_.get_track().index()),
						  time_to_scene(get_viewer_node()->get_playhead()) - x,
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

Track::Type TimelineView::connected_track_type()
{
	if (connected_track_list_) {
		return connected_track_list_->type();
	}

	return Track::k_none;
}

TimelineCoordinate TimelineView::screen_to_coordinate(const QPoint &pt)
{
	return scene_to_coordinate(mapToScene(pt));
}

TimelineCoordinate TimelineView::scene_to_coordinate(const QPointF &pt)
{
	return TimelineCoordinate(scene_to_time(pt.x()),
							  Track::Reference(connected_track_type(),
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
								  Track::Reference(connected_track_type(),
												   scene_to_track(scene_pt.y())),
								  button, modifiers);
}

void TimelineView::draw_blocks(QPainter *painter, bool foreground)
{
	Rational start_time = scene_to_time(get_timeline_left_bound());
	Rational end_time = scene_to_time(get_timeline_right_bound());

	foreach (Track *track, connected_track_list_->get_tracks()) {
		// Get first visible block in this track
		Block *block = track->nearest_block_before_or_at(start_time);

		qreal track_top = get_track_y(track->index());
		qreal track_height = get_track_height(track->index());

		while (block) {
			draw_block(painter, foreground, block, track_top, track_height);

			if (block->out() >= end_time) {
				// Rest of the clips are offscreen, can break loop now
				break;
			}

			block = block->next();
		}
	}
}

void TimelineView::draw_block(QPainter *painter, bool foreground, Block *block,
							 qreal block_top, qreal block_height,
							 const Rational &in, const Rational &out,
							 const Rational &media_in)
{
	if (dynamic_cast<ClipBlock *>(block) ||
		dynamic_cast<TransitionBlock *>(block)) {
		qreal block_in = time_to_scene(in);

		qreal block_left = qMax(get_timeline_left_bound(), block_in);
		qreal block_right = qMin(get_timeline_right_bound(), time_to_scene(out)) - 1;

		QRectF r(block_left, block_top, block_right - block_left, block_height);

		QColor shadow_color = block->is_enabled() ?
								  QtUtils::to_q_color(block->color()).darker() :
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
					QString using_label = block->get_label_or_name();

					QRectF text_rect = r.adjusted(text_padding, text_padding,
												  -text_padding, -text_padding);
					painter->setPen(
						block->is_enabled() ?
							ColorCoding::get_ui_selector_color(block->color()) :
							Qt::lightGray);
					painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignTop,
									  using_label);

					if (block->has_links()) {
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
					block->is_enabled() ?
						block->brush(block_top, block_top + block_height) :
						Qt::gray);
				painter->drawRect(r);

				if (r.width() > minimum_detail_width) {
					if (ClipBlock *clip = dynamic_cast<ClipBlock *>(block)) {
						QRect preview_rect = r.toRect();

						// Draw clip thumbnails
						if (clip->get_track_type() == Track::k_video &&
							OAK_CONFIG("TimelineThumbnailMode").toInt() !=
								Timeline::k_thumbnail_off) {
							// Start thumbnails underneath clip name
							preview_rect.adjust(0, text_total_height, 0, 0);

							if (preview_rect.height() > r.height() / 3) {
								if (const FrameHashCache *thumbs =
										clip->thumbnails()) {
									QRect thumb_rect;
									painter->setRenderHint(
										QPainter::SmoothPixmapTransform);
									painter->setClipRect(preview_rect);

									if (OAK_CONFIG("TimelineThumbnailMode") ==
										Timeline::k_thumbnail_on) {
										Sequence *s = clip->track()->sequence();
										int width = s->get_video_params().width();
										int height =
											s->get_video_params().height();
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
													connected_track_list_
														->parent()
														->get_video_params()
														.frame_rate_as_time_base()) +
												media_in;
											draw_thumbnail(painter, thumbs,
														  time_here, i,
														  preview_rect,
														  &thumb_rect);
										}

									} else {
										Rational time =
											clip->media_range().in();
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
						if (clip->get_track_type() == Track::k_audio &&
							OAK_CONFIG("TimelineWaveformMode").toInt() ==
								Timeline::k_waveforms_enabled) {
							if (const AudioWaveformCache *wave =
									clip->waveform()) {
								Rational waveform_start =
									scene_to_time(
										block_left - block_in, get_scale(),
										connected_track_list_->parent()
											->get_audio_params()
											.sample_rate_as_time_base()) +
									media_in;
								painter->setPen(shadow_color);

								wave->Draw(painter, preview_rect,
										   this->get_scale(), waveform_start);
							}
						}

						// Draw zebra stripes and markers
						if (clip->connected_viewer()) {
							if (!clip->connected_viewer()->get_length().isNull()) {
								painter->setPen(shadow_color);

								if (clip->media_in() < 0) {
									qreal zebra_right = time_to_scene(
										clip->in() - clip->media_in());

									switch (clip->loop_mode()) {
									case LoopMode::k_loop_mode_off:
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
									case LoopMode::k_loop_mode_loop:
										for (qreal i = zebra_right;
											 i > block_left;
											 i -= time_to_scene(
												 clip->connected_viewer()
													 ->get_length())) {
											painter->drawLine(i, block_top, i,
															  block_top +
																  block_height);
										}
										break;
									case LoopMode::k_loop_mode_clamp:
										painter->drawLine(
											zebra_right, block_top, zebra_right,
											block_top + block_height);
										break;
									}
								}

								if (clip->length() + clip->media_in() >
									clip->connected_viewer()->get_length()) {
									qreal zebra_left = time_to_scene(
										clip->out() -
										(clip->media_in() + clip->length() -
										 clip->connected_viewer()->get_length()));
									switch (clip->loop_mode()) {
									case LoopMode::k_loop_mode_off:
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
									case LoopMode::k_loop_mode_loop:
										for (qreal i = zebra_left;
											 i < block_right;
											 i += time_to_scene(
												 clip->connected_viewer()
													 ->get_length())) {
											painter->drawLine(i, block_top, i,
															  block_top +
																  block_height);
										}
										break;
									case LoopMode::k_loop_mode_clamp:
										painter->drawLine(
											zebra_left, block_top, zebra_left,
											block_top + block_height);
										break;
									}
								}
							}

							TimelineMarkerList *marker_list =
								clip->connected_viewer()->get_markers();
							if (!marker_list->empty()) {
								clip_marker_rects_.clear();

								for (auto it = marker_list->cbegin();
									 it != marker_list->cend(); it++) {
									TimelineMarker *marker = *it;
									// Make sure marker is within In/Out points of the clip
									if (marker->time().in() >=
											clip->media_in() &&
										marker->time().out() <=
											clip->media_in() + clip->length()) {
										QPoint marker_pt(
											time_to_scene(clip->in() -
														clip->media_in() +
														marker->time().in()),
											block_top + block_height);
										painter->setClipRect(r);
										QRect marker_rect =
											marker->draw(painter, marker_pt, -1,
														 get_scale(), false);
										clip_marker_rects_.insert(marker,
																  marker_rect);
										painter->setClipping(false);
									}
								}
							}
						}

						if (const FrameHashCache *cache =
								clip->connected_video_cache()) {
							if (cache->has_validated_ranges()) {
								QRect cache_rect =
									r.adjusted(
										 0,
										 r.height() -
											 PlaybackCache::
												 get_cache_indicator_height(),
										 0, 0)
										.toRect();
								cache->draw(painter, clip->media_in(),
											get_scale(), cache_rect);
							}
						}
					}

					// For transitions, show lines representing a transition
					if (TransitionBlock *transition =
							dynamic_cast<TransitionBlock *>(block)) {
						QVector<QLineF> lines;

						if (transition->connected_in_block()) {
							lines.append(QLineF(r.bottomLeft(), r.topRight()));
						}

						if (transition->connected_out_block()) {
							lines.append(QLineF(r.topLeft(), r.bottomRight()));
						}

						painter->setPen(shadow_color);
						painter->drawLines(lines);
					}

					if (transition_overlay_out_ == block ||
						transition_overlay_in_ == block) {
						QRectF transition_overlay_rect = r;

						qreal transition_overlay_width =
							time_to_scene(block->length()) * 0.5;
						if (transition_overlay_out_ && transition_overlay_in_) {
							// This is a dual transition, use the smallest width
							Block *other_block =
								(transition_overlay_out_ == block) ?
									transition_overlay_in_ :
									transition_overlay_out_;

							qreal other_width =
								time_to_scene(other_block->length()) * 0.5;

							transition_overlay_width =
								qMin(transition_overlay_width, other_width);
						}

						if (transition_overlay_out_ == block) {
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
	if (connected_track_list_) {
		if (alignment() & Qt::AlignTop) {
			return get_track_y(connected_track_list_->get_track_count());
		} else {
			return get_track_y(connected_track_list_->get_track_count() - 1);
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
	if (!connected_track_list_ || !connected_track_list_->get_track_count()) {
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
	if (!connected_track_list_ || connected_track_list_->get_track_count() == 0) {
		// Handle null or empty track list
		return Track::get_default_track_height_in_pixels();
	}

	if (track_index >= connected_track_list_->get_track_count()) {
		// Handle new track at the end of the list
		return connected_track_list_
			->get_track_at(connected_track_list_->get_track_count() - 1)
			->get_track_height_in_pixels();
	}

	if (track_index < 0) {
		// Handle new track at the beginning of the list
		return connected_track_list_->get_track_at(0)->get_track_height_in_pixels();
	}

	// Track definitely exists, return its actual height
	return connected_track_list_->get_track_at(track_index)
		->get_track_height_in_pixels();
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

void TimelineView::connect_track_list(TrackList *list)
{
	if (connected_track_list_) {
		disconnect(connected_track_list_, &TrackList::track_list_changed, this,
				   &TimelineView::track_list_changed);
		disconnect(connected_track_list_, &TrackList::track_height_changed, this,
				   &TimelineView::track_list_changed);
	}

	connected_track_list_ = list;

	if (connected_track_list_) {
		connect(connected_track_list_, &TrackList::track_list_changed, this,
				&TimelineView::track_list_changed);
		connect(connected_track_list_, &TrackList::track_height_changed, this,
				&TimelineView::track_list_changed);
	}
}

void TimelineView::set_beam_cursor(const TimelineCoordinate &coord)
{
	if (!connected_track_list_) {
		return;
	}

	bool update_required =
		coord.get_track().type() == connected_track_list_->type() ||
		cursor_coord_.get_track().type() == connected_track_list_->type();

	show_beam_cursor_ = true;
	cursor_coord_ = coord;

	if (update_required) {
		viewport()->update();
	}
}

void TimelineView::set_transition_overlay(ClipBlock *out, ClipBlock *in)
{
	if (transition_overlay_out_ != out || transition_overlay_in_ != in) {
		Track::Type type = Track::k_none;

		if (out) {
			type = out->track()->type();
		} else if (in) {
			type = in->track()->type();
		}

		if (type == this->connected_track_list_->type()) {
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

Block *TimelineView::get_item_at_scene_pos(const Rational &time,
									   int track_index) const
{
	if (connected_track_list_) {
		Track *track = connected_track_list_->get_track_at(track_index);

		if (track) {
			foreach (Block *b, track->blocks()) {
				if (b->in() <= time && b->out() > time) {
					return b;
				}
			}
		}
	}

	return nullptr;
}

QVector<Block *> TimelineView::get_items_at_scene_rect(const QRectF &rect) const
{
	QVector<Block *> list;

	if (connected_track_list_) {
		Rational start = this->scene_to_time(rect.left());
		Rational end = this->scene_to_time(rect.right());

		for (int i = 0; i < connected_track_list_->get_track_count(); i++) {
			Track *track = connected_track_list_->get_track_at(i);
			int track_top = get_track_y(i);
			int track_bottom = track_top + get_track_height(i);

			if (track) {
				if (!(track_bottom < rect.top() || track_top > rect.bottom())) {
					Block *b = track->nearest_block_before_or_at(start);
					while (b && b->in() < end) {
						list.append(b);
						b = b->next();
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
