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

#ifndef OAK_PLAYBACKAUDIOCLOCK_H
#define OAK_PLAYBACKAUDIOCLOCK_H

namespace olive
{

/**
 * @brief Source of an audio output clock for playback timing
 */
class PlaybackAudioClock {
public:
	virtual ~PlaybackAudioClock() = default;

	/**
	 * @brief Seconds of audio consumed by the output device
	 *
	 * Must return a negative value when no clocked output is running, in
	 * which case the caller should fall back to the wall clock.
	 */
	virtual double seconds() const = 0;
};

}

#endif // OAK_PLAYBACKAUDIOCLOCK_H
