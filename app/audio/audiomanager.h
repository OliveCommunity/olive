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

#ifndef OAK_AUDIOMANAGER_H
#define OAK_AUDIOMANAGER_H

#include <memory>
#include <QtConcurrent/QtConcurrent>
#include <QThread>
#include <portaudio.h>

#include "audiovisualwaveform.h"
#include "audio/audioprocessor.h"
#include "common/define.h"
#include "codec/ffmpeg/ffmpegencoder.h"
#include "render/audioplaybackcache.h"
#include "render/previewaudiodevice.h"

namespace olive
{

/**
 * @brief Audio input and output management class
 *
 * Wraps around a QAudioOutput and AudioHybridDevice, connecting them together and exposing audio functionality to
 * the rest of the system.
 */
class AudioManager : public QObject {
	Q_OBJECT
public:
	static void create_instance();
	static void destroy_instance();

	static AudioManager *instance();

	void set_output_notify_interval(int n);

	bool push_to_output(const AudioParams &params, const QByteArray &samples,
					  QString *error = nullptr);

	void clear_buffered_output();

	void stop_output();

	PaDeviceIndex get_output_device() const
	{
		return output_device_;
	}

	PaDeviceIndex get_input_device() const
	{
		return input_device_;
	}

	void set_output_device(PaDeviceIndex device);

	void set_input_device(PaDeviceIndex device);

	void hard_reset();

	bool start_recording(const EncodingParams &params,
						QString *error_str = nullptr);

	void stop_recording();

	static PaDeviceIndex find_config_device_by_name(bool is_output_device);
	static PaDeviceIndex find_device_by_name(const QString &s,
										  bool is_output_device);

	static PaStreamParameters get_port_audio_params(const AudioParams &p,
												 PaDeviceIndex device);

signals:
	void output_notify();

	void output_params_changed();

private:
	AudioManager();

	virtual ~AudioManager() override;

	static PaSampleFormat get_port_audio_sample_format(SampleFormat fmt);

	void close_output_stream();

	static AudioManager *instance_;

	PaDeviceIndex output_device_;
	PaStream *output_stream_;
	AudioParams output_params_;
	PreviewAudioDevice *output_buffer_;

	PaDeviceIndex input_device_;
	PaStream *input_stream_;

	FFmpegEncoder *input_encoder_;
};

}

#endif // OAK_AUDIOMANAGER_H
