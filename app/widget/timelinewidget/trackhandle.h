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

#include "oakengine/node.h"
#include "oakengine/timeline.h"

namespace olive
{

/**
 * @brief Facade accessors for track handles held by the timeline tools.
 *
 * The engine's track accessors (is_locked()/is_muted()/sequence()/index())
 * are engine symbols; the timeline code keeps tracks as opaque
 * OakEngineTrack* handles and routes the queries through the liboakengine
 * C ABI instead (same pattern as app/widget/keyframeview/keyframehandle.h).
 */

inline OakEngineTrack *trackhandle(OakEngineTrack *track)
{
	return track;
}

inline OakEngineSequence *track_sequence_handle(OakEngineTrack *track)
{
	return track ? reinterpret_cast<OakEngineSequence *>(
					   oakengine_track_get_sequence(
						   reinterpret_cast<OakEngineNode *>(track))) :
				   nullptr;
}

inline int track_type_of(OakEngineTrack *track)
{
	return oakengine_track_type(trackhandle(track));
}

inline int track_index_of(OakEngineTrack *track)
{
	return track ? oakengine_track_get_index(
					   reinterpret_cast<OakEngineNode *>(track)) :
				   -1;
}

inline bool track_is_locked(OakEngineTrack *track)
{
	return track &&
		   oakengine_track_is_locked(track_sequence_handle(track),
									 track_type_of(track),
									 track_index_of(track)) != 0;
}

inline bool track_is_muted(OakEngineTrack *track)
{
	return track &&
		   oakengine_track_is_muted(track_sequence_handle(track),
									track_type_of(track),
									track_index_of(track)) != 0;
}

} // namespace olive

#endif // OAK_TRACKHANDLE_H
