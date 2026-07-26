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

#ifndef OAK_TIMERULER_H
#define OAK_TIMERULER_H

#include <QTimer>
#include <QWidget>

#include "engineeventbridge.h"
#include "seekablewidget.h"
#include "render/playbackcache.h"

namespace olive
{

class TimeRuler : public SeekableWidget {
	Q_OBJECT
public:
	TimeRuler(bool text_visible = true, bool cache_status_visible = false,
			  QWidget *parent = nullptr);

	void set_centered_text(bool c);

	void set_playback_cache(PlaybackCache *cache);

protected:
	virtual void drawForeground(QPainter *painter, const QRectF &rect) override;

	virtual void TimebaseChangedEvent(const Rational &tb) override;

protected slots:
	virtual bool show_context_menu(const QPoint &p) override;

private:
	void update_height();

	int cache_status_height() const;

	int minimum_gap_between_lines_;

	bool text_visible_;

	bool centered_text_;

	double timebase_flipped_dbl_;

	bool show_cache_status_;

	PlaybackCache *playback_cache_;

	EngineEventBridge *bridge_;
	int64_t cache_sub_invalidated_ = 0;
	int64_t cache_sub_validated_ = 0;
};

}

#endif // OAK_TIMERULER_H
