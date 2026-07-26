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

#include "timeruler.h"

#include <QDebug>
#include <QPainter>

#include "common/qtutils.h"
#include "common/configwrapper.h"
#include "core.h"
#include "oakengine/viewer.h"
#include "markerpainting.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"

namespace olive
{

#define super SeekableWidget

TimeRuler::TimeRuler(bool text_visible, bool cache_status_visible,
					 QWidget *parent)
	: super(parent)
	, text_visible_(text_visible)
	, centered_text_(true)
	, show_cache_status_(cache_status_visible)
	, playback_cache_(nullptr)
{
	QFontMetrics fm = fontMetrics();

	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

	// Text height is used to calculate widget height

	// Get the "minimum" space allowed between two line markers on the ruler (in screen pixels)
	// Mediocre but reliable way of scaling UI objects by font/DPI size
	minimum_gap_between_lines_ = QtUtils::q_font_metrics_width(fm, "H");

	// Text visibility affects height, so we set that here
	update_height();

	// Force update if the default timecode display mode changes
	connect(Core::instance(), &Core::timecode_display_changed, this,
			static_cast<void (TimeRuler::*)()>(&TimeRuler::update));

	// Connect context menu
	connect(this, &TimeRuler::customContextMenuRequested, this,
			&TimeRuler::show_context_menu);

	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	//horizontalScrollBar()->setVisible(false);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setBackgroundRole(QPalette::Window);
	setFrameShape(QFrame::NoFrame);

	// NOTE: One day it might be preferable to use AlignBottom because the lines are anchored to
	//       the bottom of the widget. However, for now this makes sense since we just ported this
	//       from a QWidget's paintEvent.
	setAlignment(Qt::AlignLeft | Qt::AlignTop);

	bridge_ = new EngineEventBridge(this);
	connect(bridge_, &EngineEventBridge::playback_cache_invalidated,
			viewport(), [this]() { viewport()->update(); });
	connect(bridge_, &EngineEventBridge::playback_cache_validated,
			viewport(), [this]() { viewport()->update(); });
}

void TimeRuler::set_centered_text(bool c)
{
	centered_text_ = c;

	update();
}

void TimeRuler::set_playback_cache(PlaybackCache *cache)
{
	if (!show_cache_status_) {
		return;
	}

	if (playback_cache_) {
		bridge_->unsubscribe(cache_sub_invalidated_);
		bridge_->unsubscribe(cache_sub_validated_);
		cache_sub_invalidated_ = 0;
		cache_sub_validated_ = 0;
	}

	playback_cache_ = cache;

	if (playback_cache_) {
		cache_sub_invalidated_ = bridge_->subscribe(
			reinterpret_cast<void *>(playback_cache_),
			OAKENGINE_EVENT_PLAYBACK_CACHE_INVALIDATED);
		cache_sub_validated_ = bridge_->subscribe(
			reinterpret_cast<void *>(playback_cache_),
			OAKENGINE_EVENT_PLAYBACK_CACHE_VALIDATED);
	}

	update();
}

void TimeRuler::drawForeground(QPainter *p, const QRectF &rect)
{
	// Nothing to paint if the timebase is invalid
	if (timebase().isNull()) {
		return;
	}

	// Draw timeline points if connected
	int marker_height = MarkerPainting::height(p->fontMetrics());
	draw_work_area(p);
	draw_markers(p, marker_height);

	double width_of_frame = timebase_dbl() * get_scale();
	double width_of_second = 0;
	do {
		width_of_second += timebase_dbl();
	} while (width_of_second < 1.0);
	width_of_second *= get_scale();
	double width_of_minute = width_of_second * 60;
	double width_of_hour = width_of_minute * 60;
	double width_of_day = width_of_hour * 24;

	double long_interval, short_interval;
	int long_rate = 0;

	// Used for comparison, even if one unit can technically fit, we have to fit at least two for it to matter
	int doubled_gap = minimum_gap_between_lines_ * 2;

	if (width_of_day < doubled_gap) {
		long_interval = -1;
		short_interval = width_of_day;
	} else if (width_of_hour < doubled_gap) {
		long_interval = width_of_day;
		long_rate = 24;
		short_interval = width_of_hour;
	} else if (width_of_minute < doubled_gap) {
		long_interval = width_of_hour;
		long_rate = 60;
		short_interval = width_of_minute;
	} else if (width_of_second < doubled_gap) {
		long_interval = width_of_minute;
		long_rate = 60;
		short_interval = width_of_second;
	} else if (width_of_frame < doubled_gap) {
		long_interval = width_of_second;
		long_rate = qRound(timebase_flipped_dbl_);
		short_interval = width_of_frame;
	} else {
		// FIXME: Implement this...
		long_interval = width_of_second;
		short_interval = width_of_frame;
	}

	if (short_interval < minimum_gap_between_lines_) {
		if (long_interval <= 0) {
			do {
				short_interval *= 2;
			} while (short_interval < minimum_gap_between_lines_);
		} else {
			int div;

			short_interval = long_interval;

			for (div = long_rate; div > 0; div--) {
				if (long_rate % div == 0) {
					// This division produces a whole number
					double test_frame_width =
						long_interval / static_cast<double>(div);

					if (test_frame_width >= minimum_gap_between_lines_) {
						short_interval = test_frame_width;
						break;
					}
				}
			}
		}
	}

	// Set line color to main text color
	p->setBrush(Qt::NoBrush);
	p->setPen(palette().text().color());

	// Calculate line dimensions
	QFontMetrics fm = p->fontMetrics();
	int line_bottom = height();

	if (show_cache_status_) {
		line_bottom -= oakengine_playback_cache_indicator_height();
	}

	int long_height = fm.height();
	int short_height = long_height / 2;
	int long_y = line_bottom - long_height;
	int short_y = line_bottom - short_height;

	// Draw long lines
	int last_long_unit = -1;
	int last_short_unit = -1;
	int last_text_draw = INT_MIN;

	// FIXME: Hardcoded number
	const int k_average_text_width = 200;

	for (int i = get_scroll() - k_average_text_width;
		 i < get_scroll() + width() + k_average_text_width; i++) {
		double screen_pt = static_cast<double>(i);

		if (long_interval > -1) {
			int this_long_unit = std::floor(screen_pt / long_interval);
			if (this_long_unit != last_long_unit) {
				int line_y = long_y;

				if (text_visible_) {
					QRect text_rect;
					Qt::Alignment text_align;
					QString timecode_str =
						QString::fromStdString(Timecode::time_to_timecode(
							scene_to_time(i), timebase(),
							Core::instance()->get_timecode_display()));
					int timecode_width =
						QtUtils::q_font_metrics_width(fm, timecode_str);
					int timecode_left;

					if (centered_text_) {
						text_rect = QRect(i - k_average_text_width / 2,
										  marker_height, k_average_text_width,
										  fm.height());
						text_align = Qt::AlignCenter;
						timecode_left = i - timecode_width / 2;
					} else {
						text_rect = QRect(i, marker_height, k_average_text_width,
										  fm.height());
						text_align = Qt::AlignLeft | Qt::AlignVCenter;
						timecode_left = i;

						// Add gap to left between line and text
						timecode_str.prepend(' ');
					}

					if (timecode_left > last_text_draw) {
						p->drawText(text_rect, static_cast<int>(text_align),
									timecode_str);

						last_text_draw = timecode_left + timecode_width;

						if (!centered_text_) {
							line_y = 0;
						}
					}
				}

				p->drawLine(i, line_y, i, line_bottom);
				last_long_unit = this_long_unit;
			}
		}

		if (short_interval > -1) {
			int this_short_unit = std::floor(screen_pt / short_interval);
			if (this_short_unit != last_short_unit) {
				p->drawLine(i, short_y, i, line_bottom);
				last_short_unit = this_short_unit;
			}
		}
	}

	// If cache status is enabled
	if (show_cache_status_ && playback_cache_ &&
		playback_cache_->has_validated_ranges()) {
		// FIXME: Hardcoded to get video length, if we ever need audio length, this will have to change
		int h = oakengine_playback_cache_indicator_height();
		QRect cache_rect(0, height() - h, width(), h);

		if (ViewerOutput *viewer =
				dynamic_cast<ViewerOutput *>(playback_cache_->parent())) {
			int right = time_to_scene(viewer->get_video_length());
			cache_rect.setWidth(std::max(0, right));
		}

		if (cache_rect.width() > 0) {
			playback_cache_->draw(p, scene_to_time(get_scroll()), get_scale(),
								  cache_rect);
		}
	}

	// Draw the playhead if it's on screen at the moment
	int playhead_pos = time_to_scene(get_viewer_node()->get_playhead());
	p->setPen(Qt::NoPen);
	p->setBrush(PLAYHEAD_COLOR);
	draw_playhead(p, playhead_pos, line_bottom);
}

void TimeRuler::TimebaseChangedEvent(const Rational &tb)
{
	super::TimebaseChangedEvent(tb);

	timebase_flipped_dbl_ = tb.flipped().to_double();

	update();
}

int TimeRuler::cache_status_height() const
{
	return fontMetrics().height() / 4;
}

bool TimeRuler::show_context_menu(const QPoint &p)
{
	if (super::show_context_menu(p)) {
		return true;
	} else {
		Menu m(this);

		MenuShared::instance()->add_items_for_time_ruler_menu(&m);
		MenuShared::instance()->about_to_show_time_ruler_actions(timebase());

		m.exec(mapToGlobal(p));

		return true;
	}
}

void TimeRuler::update_height()
{
	int height = text_height();

	// Add text height
	if (text_visible_) {
		height += text_height();
	}

	// Add cache status height
	if (show_cache_status_) {
		height += oakengine_playback_cache_indicator_height();
	}

	// Add marker height
	height += MarkerPainting::height(fontMetrics());

	setFixedHeight(height);
}

}
