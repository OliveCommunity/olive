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

#include "audiowaveformview.h"

#include <QFile>
#include <QPainter>
#include <QtMath>

#include "config/config.h"
#include "timeline/timelinecommon.h"

namespace olive
{

#define super SeekableWidget

AudioWaveformView::AudioWaveformView(QWidget *parent)
	: super(parent)
	, playback_(nullptr)
{
	setAutoFillBackground(true);
	setBackgroundRole(QPalette::Base);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	// NOTE: At some point it might make sense for this to be AlignCenter since the waveform
	//       originates from the center. But we're leaving it top/left for now since it was just
	//       ported from a QWidget's paintEvent.
	setAlignment(Qt::AlignLeft | Qt::AlignTop);
}

void AudioWaveformView::set_viewer(ViewerOutput *playback)
{
	if (playback_) {
		pool_.clear();
		pool_.waitForDone();

		disconnect(playback_, &ViewerOutput::connected_waveform_changed,
				   viewport(),
				   static_cast<void (QWidget::*)()>(&QWidget::update));

		set_timebase(0);
	}

	playback_ = playback;

	if (playback_) {
		connect(playback_, &ViewerOutput::connected_waveform_changed, viewport(),
				static_cast<void (QWidget::*)()>(&QWidget::update));

		Rational tb = playback_->get_video_params().frame_rate_as_time_base();
		if (tb.isNull()) {
			tb = OAK_CONFIG("DefaultSequenceFrameRate")
					 .value<Rational>()
					 .flipped();
		}
		set_timebase(tb);
		update_scene_rect();
	}
}

void AudioWaveformView::drawForeground(QPainter *p, const QRectF &rect)
{
	super::drawForeground(p, rect);

	if (!playback_) {
		return;
	}

	const AudioWaveformCache *wave = playback_->get_connected_waveform();
	if (!wave) {
		return;
	}

	const AudioParams &params = wave->get_parameters();
	if (!params.is_valid()) {
		return;
	}

	// Draw in/out points
	draw_work_area(p);
	draw_markers(p);

	// Draw waveform
	p->setPen(QColor(64, 255, 160)); // FIXME: Hardcoded color
	wave->Draw(p, rect.toRect(), get_scale(), scene_to_time(get_scroll()));

	// Draw playhead
	p->setPen(PLAYHEAD_COLOR);

	int playhead_x = time_to_scene(get_viewer_node()->get_playhead());
	p->drawLine(playhead_x, 0, playhead_x, height());
}

}
