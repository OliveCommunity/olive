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

#ifndef OAK_AUDIOWAVEFORMVIEW_H
#define OAK_AUDIOWAVEFORMVIEW_H

#include <QtConcurrent/QtConcurrent>
#include <QWidget>

#include "render/audioplaybackcache.h"
#include "widget/timeruler/seekablewidget.h"

namespace olive
{

class EngineEventBridge;

class AudioWaveformView : public SeekableWidget {
	Q_OBJECT
public:
	AudioWaveformView(QWidget *parent = nullptr);

	void set_viewer(ViewerOutput *playback);

protected:
	virtual void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
	QThreadPool pool_;

	ViewerOutput *playback_;

	// Engine event bridge for the viewer's connected-waveform notification
	// (facade C ABI); subscription is per-viewer in set_viewer().
	EngineEventBridge *waveform_bridge_;

	int64_t waveform_subscription_;
};

}

#endif // OAK_AUDIOWAVEFORMVIEW_H
