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

#include "previewaudiodevice.h"

#include <algorithm>
#include <cstring>

namespace olive
{

PreviewAudioDevice::PreviewAudioDevice()
	: bytes_per_frame_(0)
	, notify_interval_(0)
	, bytes_read_(0)
{
}

PreviewAudioDevice::~PreviewAudioDevice() = default;

void PreviewAudioDevice::set_params(const core::AudioParams &params)
{
	set_bytes_per_frame(params.samples_to_bytes(1));
}

int64_t PreviewAudioDevice::read(char *data, int64_t max_size)
{
	bool notify = false;
	int64_t copy_length;
	{
		std::lock_guard<std::mutex> locker(lock_);

		copy_length = std::min(max_size, int64_t(buffer_.size()));

		if (copy_length) {
			int64_t new_bytes_read = bytes_read_ + copy_length;

			if (notify_interval_ > 0 && notify_callback_) {
				if ((bytes_read_ / notify_interval_) !=
					(new_bytes_read / notify_interval_)) {
					notify = true;
				}
			}

			bytes_read_ = new_bytes_read;

			memcpy(data, buffer_.data(), copy_length);
			buffer_.erase(buffer_.begin(), buffer_.begin() + copy_length);
		}
	}

	// Fired outside the lock (see set_notify_callback())
	if (notify) {
		notify_callback_();
	}

	return copy_length;
}

int64_t PreviewAudioDevice::write(const char *data, int64_t length)
{
	std::lock_guard<std::mutex> locker(lock_);

	buffer_.insert(buffer_.end(), data, data + length);

	return length;
}

void PreviewAudioDevice::clear()
{
	std::lock_guard<std::mutex> locker(lock_);

	buffer_.clear();
	bytes_read_ = 0;
	output_frames_consumed_.store(0);
}

}
