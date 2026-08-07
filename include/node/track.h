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

#include <stdint.h>

#include "node/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reference-counted handle to a track (olive::Track).
 *
 * The object never leaves the library that created it; every external
 * reference is one of these handles. Semantics are shared_ptr-like:
 * oaknode_track_create() returns a handle with count 1, addref(ctx)
 * takes another reference, release(ctx) drops one and the library
 * destroys the object when the count reaches zero.
 *
 * Adding a track to a track list (oaknode_tracklist_add_track())
 * transfers ownership to the graph; handles obtained from accessors
 * (sequence/track-list lookups) are borrowed and never destroy the
 * underlying object.
 */
typedef struct OakNodeTrack {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeTrack;

/**
 * @brief Reference-counted handle to a per-type track container
 * (olive::TrackList).
 *
 * Always borrowed from oaknode_sequence_get_track_list(); releasing the
 * handle never destroys the list, which stays owned by its sequence.
 */
typedef struct OakNodeTrackList {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeTrackList;

/**
 * @brief Reference-counted handle to a block (olive::Block), see
 * node/block.h.
 */
typedef struct OakNodeBlock OakNodeBlock;

/**
 * @brief Reference-counted handle to a sequence (olive::Sequence), see
 * node/sequence.h.
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

/* Re-declared here so track.h is self-contained; see node/node.h. */
typedef struct OakNodeNode OakNodeNode;

/**
 * @brief Borrowed cast from a track handle to its node handle.
 * Empty handle for an empty handle.
 */
OakNodeNode oaknode_track_as_node(OakNodeTrack track);

/* ---------------------------------------------------------------- Track */

/**
 * @brief Create a track of the given type (OakNodeTrackType value).
 *
 * The caller owns the track until it is added to a track list; a track
 * that was never added must be released with oaknode_track_free().
 *
 * @return Track handle with reference count 1; ctx is NULL on invalid
 *         type / allocation failure.
 */
OakNodeTrack oaknode_track_create(int type);

/**
 * @brief Release one reference to a track handle.
 *
 * Destroys the track when the reference count reaches zero. NULL handle
 * or NULL ctx is a no-op; clears `track->ctx` after releasing.
 *
 * The track must have been removed from its track list first.
 */
void oaknode_track_free(OakNodeTrack *track);

/**
 * @brief Track type (OakNodeTrackType values).
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_track_get_type(OakNodeTrack track, int *type);
int oaknode_track_set_type(OakNodeTrack track, int type);

/**
 * @brief Track height in internal units (olive::Track::get/set_track_height).
 */
int oaknode_track_get_height(OakNodeTrack track, double *height);
int oaknode_track_set_height(OakNodeTrack track, double height);

/**
 * @brief Track height in pixels (converted through the default font height).
 */
int oaknode_track_get_height_in_pixels(OakNodeTrack track, int *height);
int oaknode_track_set_height_in_pixels(OakNodeTrack track, int height);

/**
 * @brief Default / minimum track heights in pixels (static).
 */
int oaknode_track_get_default_height_in_pixels(void);
int oaknode_track_get_minimum_height_in_pixels(void);

/**
 * @brief Index of the track inside its track list.
 */
int oaknode_track_get_index(OakNodeTrack track, int *index);
int oaknode_track_set_index(OakNodeTrack track, int index);

/**
 * @brief Mute / lock flags.
 */
int oaknode_track_get_muted(OakNodeTrack track, int *muted);
int oaknode_track_set_muted(OakNodeTrack track, int muted);
int oaknode_track_get_locked(OakNodeTrack track, int *locked);
int oaknode_track_set_locked(OakNodeTrack track, int locked);

/**
 * @brief Track reference as a (type, index) pair (olive::Track::Reference).
 */
int oaknode_track_get_reference(OakNodeTrack track, int *type, int *index);

/**
 * @brief Total length of the track (end of the last block).
 */
int oaknode_track_get_length(OakNodeTrack track, int *numerator,
							 int *denominator);

/**
 * @brief Owning sequence as a borrowed handle (empty when trackless).
 */
int oaknode_track_get_sequence(OakNodeTrack track, OakNodeSequence *out);

/* ------------------------------------------------------- Track blocks */

/**
 * @brief Number of blocks on the track.
 */
int oaknode_track_get_block_count(OakNodeTrack track, int *count);

/**
 * @brief Borrowed handle to the block at `index`.
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_NOT_FOUND.
 */
int oaknode_track_get_block_at(OakNodeTrack track, int index,
							   OakNodeBlock *out);

/**
 * @brief Append/prepend/insert primitives (olive::Track::*_block).
 *
 * The track takes over graph membership of the block; the block must have
 * a valid length before insertion.
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_track_append_block(OakNodeTrack track, OakNodeBlock block);
int oaknode_track_prepend_block(OakNodeTrack track, OakNodeBlock block);
int oaknode_track_insert_block_at_index(OakNodeTrack track,
										OakNodeBlock block, int index);
int oaknode_track_insert_block_after(OakNodeTrack track, OakNodeBlock block,
									 OakNodeBlock before);
int oaknode_track_insert_block_before(OakNodeTrack track, OakNodeBlock block,
									  OakNodeBlock after);

/**
 * @brief Remove `block` and shift all subsequent blocks earlier
 * (olive::Track::ripple_remove_block). The block is NOT deleted; ownership
 * returns to the caller.
 */
int oaknode_track_ripple_remove_block(OakNodeTrack track, OakNodeBlock block);

/**
 * @brief Replace `old_block` with `new_block`; both must have equal lengths.
 */
int oaknode_track_replace_block(OakNodeTrack track, OakNodeBlock old_block,
								OakNodeBlock new_block);

/**
 * @brief Index of `block` in the track's block array, or OAKNODE_E_NOT_FOUND.
 */
int oaknode_track_get_block_index(OakNodeTrack track, OakNodeBlock block,
								  int *index);

/**
 * @brief Block strictly containing `time` (in < time < out), or
 * OAKNODE_E_NOT_FOUND.
 */
int oaknode_track_get_block_containing_time(OakNodeTrack track, int numerator,
											int denominator,
											OakNodeBlock *out);

/**
 * @brief Block visible at `time` (in <= time < out), or OAKNODE_E_NOT_FOUND.
 */
int oaknode_track_get_visible_block_at_time(OakNodeTrack track, int numerator,
											int denominator,
											OakNodeBlock *out);

/**
 * @brief Whether the [in, out) range holds no block or only a gap
 * (olive::Track::is_range_free). `is_free` receives 1/0.
 */
int oaknode_track_is_range_free(OakNodeTrack track, int in_num, int in_den,
								int out_num, int out_den, int *is_free);

/* ------------------------------------------------------------ TrackList */

/**
 * @brief Track list type (OakNodeTrackType values).
 */
/**
 * @brief Nearest block lookups (Track::nearest_block_before_or_at /
 * nearest_block_after_or_at). *out is a borrowed handle (empty when none).
 */
int oaknode_track_get_nearest_block_before_or_at(OakNodeTrack track,
		int numerator, int denominator, OakNodeBlock *out);
int oaknode_track_get_nearest_block_after_or_at(OakNodeTrack track,
		int numerator, int denominator, OakNodeBlock *out);

/**
 * @brief Borrowed sequence owning this track list.
 */
int oaknode_tracklist_get_sequence(OakNodeTrackList list,
								   OakNodeSequence *out);

/**
 * @brief The list's track input id on the parent sequence
 * (e.g. "track_in_0"). Two-stage string getter.
 */
int oaknode_tracklist_get_track_input_id(OakNodeTrackList list,
										 char *buf, int buf_size);

/**
 * @brief Live input-array append/remove on the parent sequence for this
 * list's track input (TrackList::array_append/array_remove_last()).
 */
int oaknode_tracklist_array_append(OakNodeTrackList list);
int oaknode_tracklist_array_remove_last(OakNodeTrackList list);

/**
 * @brief Map a cached track index to the input-array element index
 * (TrackList::get_array_index_from_cache_index()).
 */
int oaknode_tracklist_get_array_index_from_cache_index(
	OakNodeTrackList list, int cache_index, int *out_index);

int oaknode_tracklist_get_type(OakNodeTrackList list, int *type);

/**
 * @brief Number of connected tracks.
 */
int oaknode_tracklist_get_track_count(OakNodeTrackList list, int *count);

/**
 * @brief Borrowed handle to the track at `index`.
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_NOT_FOUND.
 */
int oaknode_tracklist_get_track_at(OakNodeTrackList list, int index,
								   OakNodeTrack *out);

/**
 * @brief Combined length of the longest track in the list.
 */
int oaknode_tracklist_get_total_length(OakNodeTrackList list, int *numerator,
									   int *denominator);

/**
 * @brief Size of the underlying input array (>= track count; may contain
 * disconnected slots).
 */
int oaknode_tracklist_get_array_size(OakNodeTrackList list, int *size);

/**
 * @brief Add `track` to the list (non-undoable primitive).
 *
 * Mirrors the graph steps of TimelineAddTrackCommand::redo() minus the
 * auto-merge: the track is parented to the list's graph (when any),
 * inherits the previous track's height, a new array slot is appended and
 * the track is connected to it. The sequence's flat track cache and
 * lengths are refreshed before returning.
 *
 * The list takes ownership of the track on success; the caller's handle
 * becomes a non-owning reference.
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_tracklist_add_track(OakNodeTrackList list, OakNodeTrack track);

/**
 * @brief Remove `track` from the list (non-undoable primitive).
 *
 * Disconnects the track from its array slot and removes the slot
 * (Node::input_array_remove). The track is NOT deleted; ownership returns
 * to the caller.
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_NOT_FOUND.
 */
int oaknode_tracklist_remove_track(OakNodeTrackList list,
								   OakNodeTrack track);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_TRACK_H
