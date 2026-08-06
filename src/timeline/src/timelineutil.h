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

#ifndef OAK_TIMELINEUTIL_H
#define OAK_TIMELINEUTIL_H

#include <olive/core/core.h>

#include "node/block.h"
#include "node/project.h"
#include "node/sequence.h"
#include "node/track.h"

namespace olive
{

/**
 * @brief Small helpers translating between olive::core::Rational and the
 * oaknode C ABI's numerator/denominator pairs
 */
inline void rat_nd(const olive::core::Rational &r, int *n, int *d)
{
	*n = r.numerator();
	*d = r.denominator();
}

inline olive::core::Rational block_in(OakNodeBlock *b)
{
	int n, d;
	oaknode_block_get_in(b, &n, &d);
	return olive::core::Rational(n, d);
}

inline olive::core::Rational block_out(OakNodeBlock *b)
{
	int n, d;
	oaknode_block_get_out(b, &n, &d);
	return olive::core::Rational(n, d);
}

inline olive::core::Rational block_length(OakNodeBlock *b)
{
	int n, d;
	oaknode_block_get_length(b, &n, &d);
	return olive::core::Rational(n, d);
}

inline void block_set_length_and_media_out(OakNodeBlock *b,
										   const olive::core::Rational &len)
{
	int n, d;
	rat_nd(len, &n, &d);
	oaknode_block_set_length_and_media_out(b, n, d);
}

inline void block_set_length_and_media_in(OakNodeBlock *b,
										  const olive::core::Rational &len)
{
	int n, d;
	rat_nd(len, &n, &d);
	oaknode_block_set_length_and_media_in(b, n, d);
}

inline olive::core::Rational track_length(OakNodeTrack *t)
{
	int n, d;
	oaknode_track_get_length(t, &n, &d);
	return olive::core::Rational(n, d);
}

inline OakNodeBlock *block_previous(OakNodeBlock *b)
{
	OakNodeBlock *out = nullptr;
	oaknode_block_get_previous(b, &out);
	return out;
}

inline OakNodeBlock *block_next(OakNodeBlock *b)
{
	OakNodeBlock *out = nullptr;
	oaknode_block_get_next(b, &out);
	return out;
}

inline OakNodeTrack *block_track(OakNodeBlock *b)
{
	OakNodeTrack *out = nullptr;
	oaknode_block_get_track(b, &out);
	return out;
}

/**
 * @brief Attach/detach a block to/from the project graph that owns a
 * track (replaces the original setParent(graph) / setParent(memory))
 */
inline OakNodeProject *track_project(OakNodeTrack *track)
{
	OakNodeSequence *sequence = nullptr;
	if (oaknode_track_get_sequence(track, &sequence) != OAKNODE_OK ||
		!sequence) {
		return nullptr;
	}
	OakNodeProject *project = nullptr;
	oaknode_node_get_project(oaknode_sequence_as_node(sequence), &project);
	return project;
}

inline void block_add_to_graph(OakNodeBlock *b, OakNodeTrack *track)
{
	OakNodeProject *project = track_project(track);
	if (project) {
		oaknode_project_add_node(project, oaknode_block_as_node(b));
	}
}

inline void block_remove_from_graph(OakNodeBlock *b, OakNodeTrack *track)
{
	OakNodeProject *project = track_project(track);
	if (project) {
		oaknode_project_remove_node(project, oaknode_block_as_node(b));
	}
}

}

#endif // OAK_TIMELINEUTIL_H
