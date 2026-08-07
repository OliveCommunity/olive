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

#ifndef OAK_EDITOR_NODE_SEQUENCE_H
#define OAK_EDITOR_NODE_SEQUENCE_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include <stdint.h>

#include "common/videoparams.h"
#include "node/error.h"
#include "olive/core/oakcore/audioparams.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sequence texture/samples input ids (ViewerOutput::k_texture_input
 * / k_samples_input) and the track input id format
 * (Sequence::k_track_input_format). Pinned by test.
 */
#define OAKNODE_SEQUENCE_TEXTURE_INPUT "tex_in"
#define OAKNODE_SEQUENCE_SAMPLES_INPUT "samples_in"
#define OAKNODE_SEQUENCE_TRACK_INPUT_FORMAT "track_in_%1"

/* Re-declared here so sequence.h is self-contained; see node/node.h. */
typedef struct OakNodeNode OakNodeNode;

/**
 * @brief Reference-counted handle to a sequence (olive::Sequence).
 *
 * The object never leaves the library that created it; every external
 * reference is one of these handles. Semantics are shared_ptr-like:
 * oaknode_sequence_create() returns a handle with count 1, addref(ctx)
 * takes another reference, release(ctx) drops one and the library
 * destroys the object when the count reaches zero.
 *
 * Handles obtained from accessors (track lists, tracks) are borrowed:
 * releasing them does not destroy the underlying object, which stays
 * owned by the sequence graph.
 */
typedef struct OakNodeSequence {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeSequence;

/**
 * @brief Reference-counted handle to a track list (olive::TrackList),
 * see node/track.h.
 */
typedef struct OakNodeTrackList OakNodeTrackList;

/**
 * @brief Reference-counted handle to a track (olive::Track), see
 * node/track.h.
 */
typedef struct OakNodeTrack OakNodeTrack;

/**
 * @brief Create an empty sequence with zero tracks.
 *
 * @return Sequence handle with reference count 1 (release with
 *         oaknode_sequence_free()); ctx is NULL on allocation failure.
 */
OakNodeSequence oaknode_sequence_create(void);

/**
 * @brief Release one reference to a sequence handle.
 *
 * Destroys the sequence (and its owned track lists) when the reference
 * count reaches zero. NULL handle or NULL ctx is a no-op; clears
 * `sequence->ctx` after releasing.
 *
 * Tracks and blocks connected to the sequence are owned by the graph and
 * are not deleted here; the caller must have torn them down first.
 */
void oaknode_sequence_free(OakNodeSequence *sequence);

/**
 * @brief Apply the default video/audio parameters
 *        (ViewerOutput::set_default_parameters()).
 */
int oaknode_sequence_set_default_parameters(OakNodeSequence sequence);

/**
 * @brief Borrowed cast from a sequence handle to its node handle.
 * Empty handle for an empty handle.
 */
OakNodeNode oaknode_sequence_as_node(OakNodeSequence sequence);

/**
 * @brief Borrowed handle to the per-type track list.
 *
 * @param type One of OAKNODE_TRACK_TYPE_VIDEO / _AUDIO / _SUBTITLE.
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_NOT_FOUND (bad type).
 */
int oaknode_sequence_get_track_list(OakNodeSequence sequence, int type,
									OakNodeTrackList *out);

/**
 * @brief Number of connected tracks of the given type.
 */
int oaknode_sequence_get_track_count(OakNodeSequence sequence, int type,
									 int *count);

/**
 * @brief Borrowed handle to the track of `type` at `index`.
 */
int oaknode_sequence_get_track_at(OakNodeSequence sequence, int type,
								  int index, OakNodeTrack *out);

/**
 * @brief Flat track cache across all types (olive::Sequence::get_tracks()).
 */
int oaknode_sequence_get_all_track_count(OakNodeSequence sequence, int *count);
int oaknode_sequence_get_all_track_at(OakNodeSequence sequence, int index,
									  OakNodeTrack *out);

/**
 * @brief Playhead position in sequence time.
 */
int oaknode_sequence_get_playhead(OakNodeSequence sequence, int *numerator,
								  int *denominator);
int oaknode_sequence_set_playhead(OakNodeSequence sequence, int numerator,
								  int denominator);

/**
 * @brief Cached overall/video/audio lengths (olive::ViewerOutput).
 */
int oaknode_sequence_get_length(OakNodeSequence sequence, int *numerator,
								int *denominator);
int oaknode_sequence_get_video_length(OakNodeSequence sequence,
									  int *numerator, int *denominator);
int oaknode_sequence_get_audio_length(OakNodeSequence sequence,
									  int *numerator, int *denominator);

/**
 * @brief Recompute the cached lengths from the track lists
 * (olive::ViewerOutput::verify_length()).
 */
int oaknode_sequence_verify_length(OakNodeSequence sequence);

/* --------------------------------------------------- Video/audio params */

/**
 * @brief Number of video/audio parameter slots.
 */
int oaknode_sequence_get_video_stream_count(OakNodeSequence sequence,
											int *count);
int oaknode_sequence_get_audio_stream_count(OakNodeSequence sequence,
											int *count);

/**
 * @brief Video parameters at `index` as a NEW by-value handle owned by
 * the caller (reference count 1, release with
 * oakcommon_videoparams_free()).
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID, OAKNODE_E_NOT_FOUND or
 * OAKNODE_E_NOMEM.
 */
int oaknode_sequence_get_video_params(OakNodeSequence sequence, int index,
									  OakVideoParams *out);

/**
 * @brief Replace the video parameters at `index` with a copy of `params`.
 *
 * @return OAKNODE_E_INVALID if the sequence handle is empty, params.ctx is
 * NULL, or index is negative.
 */
int oaknode_sequence_set_video_params(OakNodeSequence sequence, int index,
									  OakVideoParams params);

/**
 * @brief Audio parameters at `index` as a NEW handle owned by the caller
 * (release with oakcore_audioparams_free()).
 */
int oaknode_sequence_get_audio_params(OakNodeSequence sequence, int index,
									  OakAudioParams **out);

/**
 * @brief Replace the audio parameters at `index` with a copy of `params`.
 */
int oaknode_sequence_set_audio_params(OakNodeSequence sequence, int index,
									  const OakAudioParams *params);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_SEQUENCE_H
