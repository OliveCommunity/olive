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

#include "common/configwrapper.h"
#include "core.h"
#include "engineeventbridge.h"
#include "widget/viewer/vieweroutpututils.h"

namespace olive
{

#define super SeekableWidget

AudioWaveformView::AudioWaveformView(QWidget *parent)
	: super(parent)
	, playback_(nullptr)
	, waveform_bridge_(nullptr)
	, waveform_subscription_(0)
{
	setAutoFillBackground(true);
	setBackgroundRole(QPalette::Base);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	// NOTE: At some point it might make sense for this to be AlignCenter since the waveform
	//       originates from the center. But we're leaving it top/left for now since it was just
	//       ported from a QWidget's paintEvent.
	setAlignment(Qt::AlignLeft | Qt::AlignTop);

	// The connected-waveform notification arrives through the liboakengine
	// event C ABI (replaces the old ViewerOutput::connected_waveform_changed
	// connect); set_viewer() only subscribes/unsubscribes on the handle.
	waveform_bridge_ = new EngineEventBridge(this);
	connect(waveform_bridge_, &EngineEventBridge::viewer_connected_waveform_changed,
			this, [this](OakEngineNode *) { viewport()->update(); });

	// Issue 20: reuse the issue 7 undo signal so connected-waveform changes
	// replayed from the undo stack refresh the waveform view.
	connect(Core::instance(), &Core::undo_index_changed, this, [this](int) {
		viewport()->update();
	});
}

void AudioWaveformView::set_viewer(OakEngineNode *playback)
{
	if (playback_) {
		pool_.clear();
		pool_.waitForDone();

		waveform_bridge_->unsubscribe(waveform_subscription_);
		waveform_subscription_ = 0;

		set_timebase(0);
	}

	playback_ = playback;

	if (playback_) {
		waveform_subscription_ = waveform_bridge_->subscribe(
			playback_, OAKENGINE_EVENT_VIEWER_CONNECTED_WAVEFORM_CHANGED);

		Rational tb =
			viewer_output_video_params(playback_).frame_rate_as_time_base();
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

	const AudioWaveformCache *wave =
		static_cast<const AudioWaveformCache *>(
			oakengine_viewer_get_connected_waveform(playback_));
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

	int playhead_x = time_to_scene(viewer_output_playhead(get_viewer_node()));
	p->drawLine(playhead_x, 0, playhead_x, height());
}

}
