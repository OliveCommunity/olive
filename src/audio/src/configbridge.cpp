/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "configbridge.h"

#include <vector>

#include "common/config.h"

namespace olive::audio_config
{

int output_buffer_size()
{
	// 0 = let PortAudio choose the buffer size (old default)
	return oakcommon_config_get_int(nullptr, "AudioOutputBufferSize", 0);
}

std::string device_name(bool is_output_device)
{
	const char *key = is_output_device ? "AudioOutput" : "AudioInput";

	int size = oakcommon_config_get(nullptr, key, nullptr, 0);
	if (size <= 1) {
		// Absent (OAKCOMMON_E_NOT_FOUND) or empty
		return std::string();
	}

	const size_t buf_len = size_t(size);
	std::vector<char> buf(buf_len);
	if (oakcommon_config_get(nullptr, key, buf.data(), size) < 0) {
		return std::string();
	}
	return std::string(buf.data());
}

}
