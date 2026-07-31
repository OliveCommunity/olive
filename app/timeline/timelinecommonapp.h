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

#ifndef OAK_TIMELINECOMMONAPP_H
#define OAK_TIMELINECOMMONAPP_H

#include <olive/core/core.h>

typedef struct OakEngineTrack OakEngineTrack;
typedef struct OakEngineBlock OakEngineBlock;

namespace olive
{

using olive::core::Rational;

/**
 * @brief App-side mirror of engine's olive::Timeline
 * (engine/timeline/timelinecommon.h).
 *
 * Pure enums + a POD struct, identical to the engine version. It is named
 * TimelineApp (not Timeline) because the engine class still reaches some
 * app translation units transitively (e.g. via engine/node/block/block.h)
 * and an identical name would be an ODR redefinition there.
 *
 * Enumerator ordinals must stay in sync with the engine enums: the C ABI
 * transports these values as ints (OAKENGINE_MOVEMENT_MODE_* in
 * oakengine/timeline.h, the `movement_mode` parameter of
 * oakengine_block_trim_command(), etc.).
 */
class TimelineApp {
public:
	enum MovementMode { k_none, k_move, k_trim_in, k_trim_out };

	enum ThumbnailMode { k_thumbnail_off, k_thumbnail_in_out, k_thumbnail_on };

	enum WaveformMode { k_waveforms_disabled, k_waveforms_enabled };

	static bool is_a_trim_mode(MovementMode mode)
	{
		return mode == k_trim_in || mode == k_trim_out;
	}

	struct EditToInfo {
		OakEngineTrack *track;
		Rational nearest_time;
		OakEngineBlock *nearest_block;
	};
};

}

#endif // OAK_TIMELINECOMMONAPP_H
