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

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_TIMELINE_H */
