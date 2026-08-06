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

#ifndef OAK_TIMELINECOMMON_H
#define OAK_TIMELINECOMMON_H

#include <olive/core/core.h>


using namespace olive::core;

namespace olive
{

class Block;
class Track;

class Timeline {
public:
	enum MovementMode { k_none, k_move, k_trim_in, k_trim_out };

	enum ThumbnailMode { k_thumbnail_off, k_thumbnail_in_out, k_thumbnail_on };

	enum WaveformMode { k_waveforms_disabled, k_waveforms_enabled };

	static bool is_a_trim_mode(MovementMode mode)
	{
		return mode == k_trim_in || mode == k_trim_out;
	}

	struct EditToInfo {
		Track *track;
		Rational nearest_time;
		Block *nearest_block;
	};
};

#define PLAYHEAD_COLOR palette().highlight().color()

}

#endif // OAK_TIMELINECOMMON_H
