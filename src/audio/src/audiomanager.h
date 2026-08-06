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

#include <functional>
#include <memory>
#include <string>

#include <portaudio.h>

#include "codec/encoder.h"
#include "olive/core/render/audioparams.h"
#include "previewaudiodevice.h"

namespace olive
{

/**
 * @brief Audio input and output management class
 *
 * Wraps a PortAudio output stream and a PreviewAudioDevice pull buffer,
 * exposing audio functionality to the rest of the system.
 *
 * De-Qt notes:
 *  - No longer a QObject and no longer inherits PlaybackAudioClock (the
 *    clock interface lives in engine/common, which is not split); the
 *    seconds() method is kept with the same semantics.
 *  - The output_params_changed / output_notify signals are gone; the
 *    notify-interval pulse is delivered through an optional
 *    std::function (set_output_notify_callback) instead.
 *  - Recording goes through the oakcodec encoder C ABI (OakEncoder)
 *    instead of the FFmpegEncoder C++ class; the input stream is always
 *    captured as interleaved float32.
 */
class AudioManager {
public:
	static void create_instance();
	static void destroy_instance();

	static AudioManager *instance();

	void set_output_notify_interval(int64_t n);

	/**
	 * @brief Optional callback fired when a notify interval boundary is
	 *        crossed (called from the PortAudio callback thread)
	 */
	void set_output_notify_callback(std::function<void()> callback);

	bool push_to_output(const core::AudioParams &params, const char *samples,
					  int64_t samples_size, std::string *error = nullptr);

	void clear_buffered_output();

	void stop_output();

	/**
	 * @brief Seconds of audio consumed by the output device since the last reset
	 *
	 * Compensated for output latency so it represents what is actually
	 * audible. Returns a negative value when no output stream is running.
	 */
	double seconds() const;

	/**
	 * @brief Restarts the output clock at zero for a new playback run
	 */
	void reset_output_clock();

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

	bool start_recording(const oakcodec_encoding_params &params,
					  std::string *error_str = nullptr);

	void stop_recording();

	static PaDeviceIndex find_config_device_by_name(bool is_output_device);
	static PaDeviceIndex find_device_by_name(const std::string &s,
										  bool is_output_device);

	static PaStreamParameters get_port_audio_params(const core::AudioParams &p,
												 PaDeviceIndex device);

private:
	AudioManager();

	~AudioManager();

	static PaSampleFormat get_port_audio_sample_format(core::SampleFormat fmt);

	void close_output_stream();

	static AudioManager *instance_;

	PaDeviceIndex output_device_;
	PaStream *output_stream_;
	core::AudioParams output_params_;
	PreviewAudioDevice *output_buffer_;

	PaDeviceIndex input_device_;
	PaStream *input_stream_;

	OakEncoder input_encoder_;
};

}

#endif // OAK_AUDIOMANAGER_H
