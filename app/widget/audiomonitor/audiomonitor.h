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

#ifndef OAK_AUDIOMONITORWIDGET_H
#define OAK_AUDIOMONITORWIDGET_H

#include <QFile>
#include <QOpenGLWidget>
#include <QTimer>

#include "audio/audiovisualwaveform.h"
#include "common/define.h"
#include "render/audiowaveformcache.h"

namespace olive
{

class AudioMonitor : public QOpenGLWidget {
	Q_OBJECT
public:
	AudioMonitor(QWidget *parent = nullptr);

	virtual ~AudioMonitor() override;

	bool is_playing() const
	{
		return waveform_;
	}

	static void start_waveform_on_all(const AudioWaveformCache *waveform,
								   const Rational &start, int playback_speed)
	{
		foreach (AudioMonitor *m, instances) {
			m->start_waveform(waveform, start, playback_speed);
		}
	}

	static void stop_on_all()
	{
		foreach (AudioMonitor *m, instances) {
			m->stop();
		}
	}

	static void push_sample_buffer_on_all(const SampleBuffer &d)
	{
		foreach (AudioMonitor *m, instances) {
			m->push_sample_buffer(d);
		}
	}

public slots:
	void set_params(const AudioParams &params);

	void stop();

	void push_sample_buffer(const SampleBuffer &samples);

	void start_waveform(const AudioWaveformCache *waveform,
					   const Rational &start, int playback_speed);

protected:
	virtual void paintGL() override;

	virtual void mousePressEvent(QMouseEvent *event) override;

private:
	void set_update_loop(bool e);

	void update_values_from_waveform(QVector<double> &v, qint64 delta_time);

	void audio_visual_waveform_sample_to_internal_values(
		const AudioVisualWaveform::Sample &in, QVector<double> &out);

	void push_value(const QVector<double> &v);

	void bytes_to_sample_summary(const QByteArray &bytes, QVector<double> &v);

	QVector<double> get_averages() const;

	AudioParams params_;

	qint64 last_time_;

	const AudioWaveformCache *waveform_;
	Rational waveform_time_;
	Rational waveform_length_;

	int playback_speed_;

	QVector<QVector<double>> values_;
	QVector<bool> peaked_;

	QPixmap cached_background_;
	int cached_channels_;

	static QVector<AudioMonitor *> instances;
};

}

#endif // OAK_AUDIOMONITORWIDGET_H
