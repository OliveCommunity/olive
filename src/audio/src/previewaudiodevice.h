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

#ifndef OAK_PREVIEWAUDIODEVICE_H
#define OAK_PREVIEWAUDIODEVICE_H

#include "olive/core/render/audioparams.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace olive
{

/**
 * @brief Pull-style sample buffer fed to the audio output callback
 *
 * Formerly a QIODevice subclass consumed by QAudioOutput. Now a plain class:
 * the audio backend (PortAudio, see engine/audio AudioManager) pulls samples
 * through read() from its stream callback and the render side pushes samples
 * through write(). The callback-driven pull semantics are unchanged.
 */
class PreviewAudioDevice {
public:
	PreviewAudioDevice();

	virtual ~PreviewAudioDevice();

	/**
	 * @brief Read up to `max_size` bytes from the queued buffer
	 *
	 * Called from the audio output callback. Returns the number of bytes
	 * actually copied (0 when the buffer is empty, i.e. underrun).
	 */
	int64_t read(char *data, int64_t max_size);

	/**
	 * @brief Append `length` bytes to the queued buffer
	 */
	int64_t write(const char *data, int64_t length);

	// Derives the frame size from the audio format (bytes per sample per
	// channel * channel count). Until params are set, bytes_per_frame()
	// reports 0, i.e. "unknown".
	void set_params(const core::AudioParams &params);

	int bytes_per_frame() const
	{
		return bytes_per_frame_;
	}

	void set_bytes_per_frame(int b)
	{
		bytes_per_frame_ = b;
	}

	void set_notify_interval(int64_t i)
	{
		notify_interval_ = i;
	}

	/**
	 * @brief Install the callback fired when a notify interval boundary is crossed
	 *
	 * Replaces the former `notify` signal. The callback is invoked from read(),
	 * i.e. from the audio output callback thread, AFTER the internal lock has
	 * been released (the Qt version emitted while holding the lock; receivers
	 * lived on another thread so it was effectively queued). The callback must
	 * therefore be thread-safe and must not call back into this device.
	 */
	void set_notify_callback(std::function<void()> callback)
	{
		std::lock_guard<std::mutex> locker(lock_);
		notify_callback_ = std::move(callback);
	}

	void clear();

	/**
	 * @brief Frames consumed by the audio output callback
	 *
	 * Counted in the callback itself so underrun (zero-filled) frames are
	 * included, making the value usable as a playback clock.
	 */
	void add_output_frames(int64_t frame_count)
	{
		output_frames_consumed_.fetch_add(frame_count);
	}

	int64_t output_frames_consumed() const
	{
		return output_frames_consumed_.load();
	}

	void reset_output_frames()
	{
		output_frames_consumed_.store(0);
	}

private:
	std::mutex lock_;

	std::vector<char> buffer_;

	int bytes_per_frame_;

	int64_t notify_interval_;

	int64_t bytes_read_;

	std::function<void()> notify_callback_;

	std::atomic<int64_t> output_frames_consumed_{0};
};

}

#endif // OAK_PREVIEWAUDIODEVICE_H
