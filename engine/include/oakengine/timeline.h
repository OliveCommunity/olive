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

#ifndef OAKENGINE_TIMELINE_H
#define OAKENGINE_TIMELINE_H

#include <stdint.h>

#include "export.h"
#include "footage.h"
#include "init.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file timeline.h
 * @brief C ABI for sequences (Oak timelines)
 *
 * An OakEngineSequence wraps the engine's olive::Sequence node
 * (engine/node/project/sequence/sequence.h, a ViewerOutput). This family is
 * intentionally read-mostly this round: playhead and workarea are simple
 * writes, everything else is inspection. Clip/track editing is a later
 * milestone.
 *
 * Handles are borrowed from their owning OakEngineProject (Qt QObject parent
 * chain; sequences are added to the project graph which becomes their
 * parent). There is deliberately no oakengine_sequence_free(): a sequence
 * handle becomes invalid when its project is freed. Sequences created with
 * oakengine_sequence_new() are additionally undoable -- undoing the creation
 * removes the sequence from the project (but keeps it alive under the undo
 * command), redoing brings it back.
 *
 * Time is exposed in the two representations used across the engine
 * (olive::core::Timecode terminology, core/util/timecodefunctions.h):
 *
 *   - seconds: a plain double (`*_seconds` accessors), or a rational seconds
 *     value as a numerator/denominator int pair (`*_rational`);
 *
 *   - timestamp: an int64 count of timebase units, where the timebase is the
 *     sequence's frame duration (the frame rate flipped, e.g. 1001/30000 for
 *     a 30000/1001 sequence) -- i.e. a frame number. This matches how the
 *     engine converts between Rational times and frame timestamps
 *     (Timecode::time_to_timestamp / timestamp_to_time).
 *
 * Conventions match oakengine/project.h: booleans are int, 0
 * (OAKENGINE_OK)/negative OAKENGINE_E_* return codes, buf/size string output,
 * NULL handles yield no-ops or OAKENGINE_E_INVALID.
 */

/**
 * @brief Create a new sequence named `name` in `project` and return its
 * borrowed handle (NULL on failure; `project` NULL -> NULL).
 *
 * The sequence gets the application's default parameters
 * (ViewerOutput::set_default_parameters(): width/height/pixel aspect/
 * interlacing/audio layout from Config, frame rate from the
 * DefaultSequenceFrameRate config entry, 30000/1001 by default) and starts
 * with zero tracks. The creation is pushed onto the global undo stack like
 * the application's "Create New Sequence" action (minus opening a viewer).
 */
OAKENGINE_API OakEngineSequence *
oakengine_sequence_new(OakEngineProject *project, const char *name);

/**
 * @brief Sequence name (Node::get_label()). Uses the buf/size convention.
 */
OAKENGINE_API int oakengine_sequence_name(const OakEngineSequence *self,
										  char *buf, int buf_size);

/**
 * @brief Length of the sequence content in seconds
 * (ViewerOutput::get_length()). 0 for an empty sequence.
 */
OAKENGINE_API int oakengine_sequence_get_length(const OakEngineSequence *self,
												double *seconds);

/**
 * @brief Length of the sequence content as rational seconds
 * (ViewerOutput::get_length().numerator()/denominator()).
 */
OAKENGINE_API int
oakengine_sequence_get_length_rational(const OakEngineSequence *self, int *num,
									   int *den);

/**
 * @brief Sequence frame rate as a num/den pair, e.g. 30000/1001
 * (ViewerOutput::get_video_params().frame_rate()).
 */
OAKENGINE_API int
oakengine_sequence_get_frame_rate(const OakEngineSequence *self, int *num,
								  int *den);

/**
 * @brief Sequence video dimensions and pixel aspect ratio
 * (ViewerOutput::get_video_params()). Any output pointer may be NULL.
 */
OAKENGINE_API int
oakengine_sequence_get_video_params(const OakEngineSequence *self, int *width,
									int *height, int *par_num, int *par_den);

/**
 * @brief Number of tracks per track type (Sequence::track_list(type)->
 * get_track_count()). Any of `video`/`audio`/`subtitle` may be NULL.
 */
OAKENGINE_API int oakengine_sequence_track_count(const OakEngineSequence *self,
												 int *video, int *audio,
												 int *subtitle);

/**
 * @brief Playhead position as a timestamp in timebase units (frame number;
 * ViewerOutput::get_playhead() rescaled to the frame-rate timebase).
 */
OAKENGINE_API int
oakengine_sequence_get_playhead(const OakEngineSequence *self,
								int64_t *timestamp);

/**
 * @brief Move the playhead to `timestamp` (frame number;
 * ViewerOutput::set_playhead()).
 */
OAKENGINE_API int oakengine_sequence_set_playhead(OakEngineSequence *self,
												  int64_t timestamp);

/**
 * @brief Playhead position in seconds.
 */
OAKENGINE_API int
oakengine_sequence_get_playhead_seconds(const OakEngineSequence *self,
										double *seconds);

/**
 * @brief 1 if the workarea (in/out range) is enabled
 * (TimelineWorkArea::enabled()).
 */
OAKENGINE_API int
oakengine_sequence_workarea_is_enabled(const OakEngineSequence *self);

/**
 * @brief Workarea in/out points as timestamps in timebase units
 * (TimelineWorkArea::in()/out()). Either pointer may be NULL.
 */
OAKENGINE_API int
oakengine_sequence_get_workarea(const OakEngineSequence *self, int64_t *in,
								int64_t *out);

/**
 * @brief Set the workarea: enable flag plus in/out timestamps in timebase
 * units (TimelineWorkArea::set_enabled()/set_range()).
 */
OAKENGINE_API int oakengine_sequence_set_workarea(OakEngineSequence *self,
												  int enabled, int64_t in,
												  int64_t out);

/**
 * @brief Number of timeline markers (TimelineMarkerList::size()).
 */
OAKENGINE_API int
oakengine_sequence_marker_count(const OakEngineSequence *self);

/**
 * @brief Marker at `index`: `time` receives its in-point as a timestamp in
 * timebase units (may be NULL), `name` its label using the buf/size
 * truncation convention (may be NULL to only fetch the time). Returns
 * OAKENGINE_OK on success, OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_sequence_marker_at(const OakEngineSequence *self,
											   int index, int64_t *time,
											   char *name, int name_size);

/* ---- Timeline editing primitives ---------------------------------------- */

/**
 * @brief Track types, matching olive::Track::Type.
 */
#define OAKENGINE_TRACK_TYPE_VIDEO 0
#define OAKENGINE_TRACK_TYPE_AUDIO 1
#define OAKENGINE_TRACK_TYPE_SUBTITLE 2

/**
 * @brief Opaque clip handle (a ClipBlock on a track).
 *
 * Handles are borrowed from their owning project (QObject parent chain) and
 * become invalid when the project is freed or the clip is removed (e.g. by
 * undoing the add). There is no oakengine_clip_free().
 */
typedef struct OakEngineClip OakEngineClip;

/**
 * @brief Human-readable reason for the last failed editing call on this
 * thread (buf/size convention). Editing calls return NULL or a negative
 * OAKENGINE_E_* code; the text explains why.
 */
OAKENGINE_API int oakengine_sequence_last_error(char *buf, int buf_size);

/**
 * @brief Append a track of `track_type` (OAKENGINE_TRACK_TYPE_*) to the
 * sequence and return its index in that type's track list.
 *
 * Uses the engine's TimelineAddTrackCommand without auto-merge: the first
 * video/audio track is connected straight to the sequence's texture/samples
 * input (tracks beyond the first stay unconnected until a merge node is
 * added -- multi-track compositing is a later milestone). The add is
 * undoable like the other editing primitives. Returns the new track index
 * (>= 0) or a negative OAKENGINE_E_* code.
 */
OAKENGINE_API int oakengine_sequence_add_track(OakEngineSequence *self,
											   int track_type);

/**
 * @brief Place a clip of `footage` on a track (undoable).
 *
 * Creates an olive::ClipBlock whose buffer input is fed by the footage node
 * and places it on the track at `track_index` (within the track list of
 * `track_type`, OAKENGINE_TRACK_TYPE_VIDEO or _AUDIO; subtitle clips are
 * rejected with OAKENGINE_E_INVALID). The footage handle must be a borrowed
 * import handle belonging to the same project as the sequence (probed
 * handles carry no node and are rejected).
 *
 * `in`/`out` are the clip's timeline range and `media_in` the source in-
 * point, all as frame timestamps in the sequence's frame-rate timebase
 * (same convention as the rest of this family); `out` must be greater than
 * `in` and `media_in` must be >= 0. No track is created implicitly: an
 * out-of-range `track_index` fails with OAKENGINE_E_NOT_FOUND.
 *
 * The add mirrors the application's drop-import chain reduced to its
 * editing core (NodeAddCommand + NodeEdgeAddCommand onto
 * ClipBlock::k_buffer_in + TrackPlaceBlockCommand, pushed as one undoable
 * MultiUndoCommand). Returns a borrowed clip handle, or NULL on failure
 * (see oakengine_sequence_last_error()).
 */
OAKENGINE_API OakEngineClip *oakengine_sequence_add_footage_clip(
	OakEngineSequence *seq, OakEngineFootage *footage, int track_type,
	int track_index, int64_t in, int64_t out, int64_t media_in);

/**
 * @brief Number of clips on the track at `track_index` (within the
 * `track_type` list). Gap blocks are not clips and are not counted.
 * Returns the count (>= 0) or a negative OAKENGINE_E_* code
 * (OAKENGINE_E_NOT_FOUND when the track does not exist).
 */
OAKENGINE_API int oakengine_sequence_clip_count(OakEngineSequence *self,
												int track_type,
												int track_index);

/**
 * @brief Borrowed handle of the clip at `clip_index` on the track (gap
 * blocks are skipped), or NULL when out of range.
 */
OAKENGINE_API OakEngineClip *oakengine_sequence_clip_at(
	OakEngineSequence *self, int track_type, int track_index, int clip_index);

/**
 * @brief The clip's timeline range (`in`/`out`) and source in-point
 * (`media_in`) as frame timestamps in the sequence's frame-rate timebase.
 * Any pointer may be NULL.
 */
OAKENGINE_API int oakengine_clip_get_range(const OakEngineClip *self,
										   int64_t *in, int64_t *out,
										   int64_t *media_in);

/* ---- Editing primitives, round 2: split / ripple delete / trim / move ----
 *
 * All four are undoable like the other editing primitives and report
 * failures through oakengine_sequence_last_error(). Clips are addressed by
 * (track_type, track_index, clip_index) exactly like
 * oakengine_sequence_clip_at() (gap blocks are skipped). All times are
 * frame timestamps in the sequence's frame-rate timebase.
 */

/**
 * @brief Split the addressed clip in two at timeline `time` (undoable;
 * olive::BlockSplitCommand).
 *
 * `time` must lie strictly inside the clip's range. The left part keeps the
 * clip's in-point, the right part starts at `time` with its media in-point
 * advanced accordingly (the engine's split semantics). Returns OAKENGINE_OK
 * or a negative code (OAKENGINE_E_NOT_FOUND for a missing clip,
 * OAKENGINE_E_INVALID for a time outside the clip).
 */
OAKENGINE_API int oakengine_sequence_split_clip(OakEngineSequence *seq,
												int track_type,
												int track_index,
												int clip_index, int64_t time);

/**
 * @brief Delete the addressed clip and shift all following clips on the
 * track left by its length (undoable;
 * olive::TrackRippleRemoveAreaCommand).
 */
OAKENGINE_API int oakengine_sequence_ripple_delete_clip(OakEngineSequence *seq,
														int track_type,
														int track_index,
														int clip_index);

/**
 * @brief Change the clip's timeline range (undoable; olive::BlockTrimCommand,
 * the application's trim command).
 *
 * Pass the current value for the end that should stay unchanged; changing
 * both ends is applied as an in-trim followed by an out-trim in one
 * undoable command. Requires new_out > new_in and new_in >= 0. When the
 * in-point moves, the clip's media in-point moves with it (the engine's
 * set_length_and_media_in() alignment); adjacent gaps absorb the difference
 * (the engine's trim semantics, adjacent clips are not rolled). The clip
 * handle must still be on a track.
 */
OAKENGINE_API int oakengine_clip_trim(OakEngineClip *clip, int64_t new_in,
									  int64_t new_out);

/**
 * @brief Move the addressed clip to start at `new_in` on the same track
 * (undoable).
 *
 * Length and media in-point are preserved; the old spot is filled with a
 * gap (olive::TrackReplaceBlockWithGapCommand) and the clip is placed at
 * the destination (olive::TrackPlaceBlockCommand, which ripples whatever
 * was there). Moving across tracks is a later milestone. `new_in` must be
 * >= 0.
 */
OAKENGINE_API int oakengine_sequence_move_clip(OakEngineSequence *seq,
											   int track_type,
											   int track_index,
											   int clip_index,
											   int64_t new_in);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_TIMELINE_H */
