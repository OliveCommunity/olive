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

#ifndef OAK_TRACKHANDLE_H
#define OAK_TRACKHANDLE_H

#include "node/output/track/track.h"
#include "oakengine/timeline.h"

namespace olive
{

/**
 * @brief Facade accessors for Track pointers held by the timeline tools.
 *
 * Track::is_locked()/is_muted()/type() are out-of-line engine symbols; the
 * timeline code keeps Track* as opaque identity pointers and routes the
 * queries through the liboakengine C ABI instead (same pattern as
 * app/widget/keyframeview/keyframehandle.h). Track::sequence()/index() are
 * header-inline and used directly.
 */

inline OakEngineTrack *trackhandle(Track *track)
{
	return reinterpret_cast<OakEngineTrack *>(track);
}

inline OakEngineSequence *track_sequence_handle(Track *track)
{
	return reinterpret_cast<OakEngineSequence *>(track ? track->sequence() :
														 nullptr);
}

inline int track_type_of(Track *track)
{
	return oakengine_track_type(trackhandle(track));
}

inline bool track_is_locked(Track *track)
{
	return track &&
		   oakengine_track_is_locked(track_sequence_handle(track),
									 track_type_of(track),
									 track->index()) != 0;
}

inline bool track_is_muted(Track *track)
{
	return track &&
		   oakengine_track_is_muted(track_sequence_handle(track),
									track_type_of(track),
									track->index()) != 0;
}

} // namespace olive

#endif // OAK_TRACKHANDLE_H
