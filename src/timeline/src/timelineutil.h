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
#include "node/node.h"
#include "node/project.h"
#include "node/sequence.h"
#include "node/track.h"

#include "../../node/c_api/nodehandle.h"

namespace olive
{

class Block;
class Node;
class Project;
class Sequence;
class Track;

/**
 * @brief Small helpers translating between olive::core::Rational and the
 * oaknode C ABI's numerator/denominator pairs
 */
inline void rat_nd(const olive::core::Rational &r, int *n, int *d)
{
	*n = r.numerator();
	*d = r.denominator();
}

/**
 * @brief Identity comparison for value handles: two handles refer to the
 * same object when they wrap the same native pointer (borrowed accessors
 * hand out a fresh handle box per call, so comparing ctx is NOT an
 * identity check)
 */
inline bool same_block(OakNodeBlock a, OakNodeBlock b)
{
	return oaknode_c_api::to_native<Block>(a) ==
		oaknode_c_api::to_native<Block>(b);
}

inline bool same_track(OakNodeTrack a, OakNodeTrack b)
{
	return oaknode_c_api::to_native<Track>(a) ==
		oaknode_c_api::to_native<Track>(b);
}

inline bool same_node(OakNodeNode a, OakNodeNode b)
{
	return oaknode_c_api::to_native<Node>(a) ==
		oaknode_c_api::to_native<Node>(b);
}

/**
 * @brief Handle-map comparators ordering by the wrapped native object
 * (identity), so lookups work across freshly-boxed borrowed handles
 */
struct BlockHandleLess {
	bool operator()(OakNodeBlock a, OakNodeBlock b) const
	{
		return oaknode_c_api::to_native<Block>(a) <
			oaknode_c_api::to_native<Block>(b);
	}
};

struct TrackHandleLess {
	bool operator()(OakNodeTrack a, OakNodeTrack b) const
	{
		return oaknode_c_api::to_native<Track>(a) <
			oaknode_c_api::to_native<Track>(b);
	}
};

/**
 * @brief Free a handle whose object is currently detached from any graph
 * or container.
 *
 * Insertion into a graph/track flips the handle to non-owning, so a
 * plain free() would only release the handle box and leak the detached
 * object. This helper re-takes ownership (the caller guarantees the
 * object is detached, as with the old raw-pointer delete semantics) and
 * then releases, destroying the object.
 */
template <typename Handle>
inline void free_detached_handle(Handle *h)
{
	if (!h || !h->ctx) {
		return;
	}
	static_cast<OakNodeBox *>(h->ctx)->owns = true;
	oaknode_c_api::free_handle(h);
}

inline olive::core::Rational block_in(OakNodeBlock b)
{
	int n, d;
	oaknode_block_get_in(b, &n, &d);
	return olive::core::Rational(n, d);
}

inline olive::core::Rational block_out(OakNodeBlock b)
{
	int n, d;
	oaknode_block_get_out(b, &n, &d);
	return olive::core::Rational(n, d);
}

inline olive::core::Rational block_length(OakNodeBlock b)
{
	int n, d;
	oaknode_block_get_length(b, &n, &d);
	return olive::core::Rational(n, d);
}

inline void block_set_length_and_media_out(OakNodeBlock b,
										   const olive::core::Rational &len)
{
	int n, d;
	rat_nd(len, &n, &d);
	oaknode_block_set_length_and_media_out(b, n, d);
}

inline void block_set_length_and_media_in(OakNodeBlock b,
										  const olive::core::Rational &len)
{
	int n, d;
	rat_nd(len, &n, &d);
	oaknode_block_set_length_and_media_in(b, n, d);
}

inline olive::core::Rational track_length(OakNodeTrack t)
{
	int n, d;
	oaknode_track_get_length(t, &n, &d);
	return olive::core::Rational(n, d);
}

inline OakNodeBlock block_previous(OakNodeBlock b)
{
	OakNodeBlock out = {};
	oaknode_block_get_previous(b, &out);
	return out;
}

inline OakNodeBlock block_next(OakNodeBlock b)
{
	OakNodeBlock out = {};
	oaknode_block_get_next(b, &out);
	return out;
}

inline OakNodeTrack block_track(OakNodeBlock b)
{
	OakNodeTrack out = {};
	oaknode_block_get_track(b, &out);
	return out;
}

/**
 * @brief Attach/detach a block to/from the project graph that owns a
 * track (replaces the original setParent(graph) / setParent(memory))
 */
inline OakNodeProject track_project(OakNodeTrack track)
{
	OakNodeSequence sequence = {};
	if (oaknode_track_get_sequence(track, &sequence) != OAKNODE_OK ||
		!sequence.ctx) {
		return OakNodeProject{};
	}
	OakNodeProject project = {};
	oaknode_node_get_project(oaknode_sequence_as_node(sequence), &project);
	return project;
}

inline void block_add_to_graph(OakNodeBlock b, OakNodeTrack track)
{
	OakNodeProject project = track_project(track);
	if (project.ctx) {
		oaknode_project_add_node(project, oaknode_block_as_node(b));
	}
}

inline void block_remove_from_graph(OakNodeBlock b, OakNodeTrack track)
{
	OakNodeProject project = track_project(track);
	if (project.ctx) {
		oaknode_project_remove_node(project, oaknode_block_as_node(b));
	}
}

}

#endif // OAK_TIMELINEUTIL_H
