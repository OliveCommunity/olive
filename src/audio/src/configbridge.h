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

#ifndef OAK_AUDIO_CONFIGBRIDGE_H
#define OAK_AUDIO_CONFIGBRIDGE_H

#include <string>

namespace olive::audio_config
{

/**
 * @brief Thin wrappers over the oakcommon config C ABI
 *        (include/common/config.h)
 *
 * The old engine code read these keys through OAK_CONFIG/OAK_CONFIG_STR;
 * oakaudio reaches the same store through oakcommon_config_*. Typed
 * getters fall back when the key is absent (the compiled-in defaults do
 * not carry the audio device keys).
 */

/** "AudioOutputBufferSize": PortAudio framesPerBuffer (0 = auto). */
int output_buffer_size();

/** "AudioOutput" / "AudioInput": saved device name ("" when unset). */
std::string device_name(bool is_output_device);

}

#endif // OAK_AUDIO_CONFIGBRIDGE_H
