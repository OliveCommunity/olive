/***

  Oak Video Editor - Non-Linear Video Editor
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

#ifndef OAK_EDITOR_NODE_TRACK_H
#define OAK_EDITOR_NODE_TRACK_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include "node/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a track (olive::Track).
 *
 * The handle IS the C++ object pointer; no wrapper is allocated.
 */
typedef struct OakNodeTrack OakNodeTrack;

/**
 * @brief Opaque handle to a per-type track container (olive::TrackList).
 *
 * Borrowed from oaknode_sequence_get_track_list(); invalidated when the
 * owning sequence is destroyed.
 */
typedef struct OakNodeTrackList OakNodeTrackList;

/**
 * @brief Opaque handle to a block (olive::Block), see node/block.h.
 */
typedef struct OakNodeBlock OakNodeBlock;

/**
 * @brief Opaque handle to a sequence (olive::Sequence), see node/sequence.h.
 */
typedef struct OakNodeSequence OakNodeSequence;

/**
 * @brief Track types, matching olive::Track::Type.
 */
enum OakNodeTrackType {
	OAKNODE_TRACK_TYPE_NONE = -1,
	OAKNODE_TRACK_TYPE_VIDEO = 0,
	OAKNODE_TRACK_TYPE_AUDIO = 1,
	OAKNODE_TRACK_TYPE_SUBTITLE = 2,
	OAKNODE_TRACK_TYPE_COUNT = 3
};

/* ---------------------------------------------------------------- Track */

/**
 * @brief Create a track of the given type (OakNodeTrackType value).
 *
 * The caller owns the track until it is added to a track list; a track
 * that was never added must be released with oaknode_track_free().
 *
 * @return Track handle, or NULL on invalid type / allocation failure.
 */
OakNodeTrack *oaknode_track_create(int type);

/**
 * @brief Destroy a track. No-op on NULL.
 *
 * The track must have been removed from its track list first.
 */
void oaknode_track_free(OakNodeTrack *track);

/**
 * @brief Track type (OakNodeTrackType values).
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_track_get_type(OakNodeTrack *track, int *type);
int oaknode_track_set_type(OakNodeTrack *track, int type);

/**
 * @brief Track height in internal units (olive::Track::get/set_track_height).
 */
int oaknode_track_get_height(OakNodeTrack *track, double *height);
int oaknode_track_set_height(OakNodeTrack *track, double height);

/**
 * @brief Track height in pixels (converted through the default font height).
 */
int oaknode_track_get_height_in_pixels(OakNodeTrack *track, int *height);
int oaknode_track_set_height_in_pixels(OakNodeTrack *track, int height);

/**
 * @brief Default / minimum track heights in pixels (static).
 */
int oaknode_track_get_default_height_in_pixels(void);
int oaknode_track_get_minimum_height_in_pixels(void);

/**
 * @brief Index of the track inside its track list.
 */
int oaknode_track_get_index(OakNodeTrack *track, int *index);
int oaknode_track_set_index(OakNodeTrack *track, int index);

/**
 * @brief Mute / lock flags.
 */
int oaknode_track_get_muted(OakNodeTrack *track, int *muted);
int oaknode_track_set_muted(OakNodeTrack *track, int muted);
int oaknode_track_get_locked(OakNodeTrack *track, int *locked);
int oaknode_track_set_locked(OakNodeTrack *track, int locked);

/**
 * @brief Track reference as a (type, index) pair (olive::Track::Reference).
 */
int oaknode_track_get_reference(OakNodeTrack *track, int *type, int *index);

/**
 * @brief Total length of the track (end of the last block).
 */
int oaknode_track_get_length(OakNodeTrack *track, int *numerator,
							 int *denominator);

/**
 * @brief Owning sequence as a borrowed handle (NULL when trackless).
 */
int oaknode_track_get_sequence(OakNodeTrack *track, OakNodeSequence **out);

/* ------------------------------------------------------- Track blocks */

/**
 * @brief Number of blocks on the track.
 */
int oaknode_track_get_block_count(OakNodeTrack *track, int *count);

/**
 * @brief Borrowed handle to the block at `index`.
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_NOT_FOUND.
 */
int oaknode_track_get_block_at(OakNodeTrack *track, int index,
							   OakNodeBlock **out);

/**
 * @brief Append/prepend/insert primitives (olive::Track::*_block).
 *
 * The track takes over graph membership of the block; the block must have
 * a valid length before insertion.
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_track_append_block(OakNodeTrack *track, OakNodeBlock *block);
int oaknode_track_prepend_block(OakNodeTrack *track, OakNodeBlock *block);
int oaknode_track_insert_block_at_index(OakNodeTrack *track,
										OakNodeBlock *block, int index);
int oaknode_track_insert_block_after(OakNodeTrack *track, OakNodeBlock *block,
									 OakNodeBlock *before);
int oaknode_track_insert_block_before(OakNodeTrack *track, OakNodeBlock *block,
									  OakNodeBlock *after);

/**
 * @brief Remove `block` and shift all subsequent blocks earlier
 * (olive::Track::ripple_remove_block). The block is NOT deleted; ownership
 * returns to the caller.
 */
int oaknode_track_ripple_remove_block(OakNodeTrack *track, OakNodeBlock *block);

/**
 * @brief Replace `old_block` with `new_block`; both must have equal lengths.
 */
int oaknode_track_replace_block(OakNodeTrack *track, OakNodeBlock *old_block,
								OakNodeBlock *new_block);

/**
 * @brief Index of `block` in the track's block array, or OAKNODE_E_NOT_FOUND.
 */
int oaknode_track_get_block_index(OakNodeTrack *track, OakNodeBlock *block,
								  int *index);

/**
 * @brief Block strictly containing `time` (in < time < out), or
 * OAKNODE_E_NOT_FOUND.
 */
int oaknode_track_get_block_containing_time(OakNodeTrack *track, int numerator,
											int denominator,
											OakNodeBlock **out);

/**
 * @brief Block visible at `time` (in <= time < out), or OAKNODE_E_NOT_FOUND.
 */
int oaknode_track_get_visible_block_at_time(OakNodeTrack *track, int numerator,
											int denominator,
											OakNodeBlock **out);

/**
 * @brief Whether the [in, out) range holds no block or only a gap
 * (olive::Track::is_range_free). `is_free` receives 1/0.
 */
int oaknode_track_is_range_free(OakNodeTrack *track, int in_num, int in_den,
								int out_num, int out_den, int *is_free);

/* ------------------------------------------------------------ TrackList */

/**
 * @brief Track list type (OakNodeTrackType values).
 */
int oaknode_tracklist_get_type(OakNodeTrackList *list, int *type);

/**
 * @brief Number of connected tracks.
 */
int oaknode_tracklist_get_track_count(OakNodeTrackList *list, int *count);

/**
 * @brief Borrowed handle to the track at `index`.
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_NOT_FOUND.
 */
int oaknode_tracklist_get_track_at(OakNodeTrackList *list, int index,
								   OakNodeTrack **out);

/**
 * @brief Combined length of the longest track in the list.
 */
int oaknode_tracklist_get_total_length(OakNodeTrackList *list, int *numerator,
									   int *denominator);

/**
 * @brief Size of the underlying input array (>= track count; may contain
 * disconnected slots).
 */
int oaknode_tracklist_get_array_size(OakNodeTrackList *list, int *size);

/**
 * @brief Add `track` to the list (non-undoable primitive).
 *
 * Mirrors the graph steps of TimelineAddTrackCommand::redo() minus the
 * auto-merge: the track is parented to the list's graph (when any),
 * inherits the previous track's height, a new array slot is appended and
 * the track is connected to it. The sequence's flat track cache and
 * lengths are refreshed before returning.
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_tracklist_add_track(OakNodeTrackList *list, OakNodeTrack *track);

/**
 * @brief Remove `track` from the list (non-undoable primitive).
 *
 * Disconnects the track from its array slot and removes the slot
 * (Node::input_array_remove). The track is NOT deleted; ownership returns
 * to the caller.
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_NOT_FOUND.
 */
int oaknode_tracklist_remove_track(OakNodeTrackList *list,
								   OakNodeTrack *track);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_TRACK_H
