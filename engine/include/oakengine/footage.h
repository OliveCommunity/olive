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

#ifndef OAKENGINE_FOOTAGE_H
#define OAKENGINE_FOOTAGE_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file footage.h
 * @brief C ABI for media probing and project footage import
 *
 * Two models share the OakEngineFootage opaque handle:
 *
 *   - Probing (oakengine_footage_probe()): inspect a media file without any
 *     project, directly through the engine's decoder probe path
 *     (Decoder::probe(), the same code the import task uses, minus the
 *     task/UI). The handle wraps an owned olive::FootageDescription and must
 *     be released with oakengine_footage_free().
 *
 *   - Importing (oakengine_project_import_footage()): probe a file and add
 *     it as a Footage node to a project's root folder (the non-UI core of
 *     ProjectImportTask: probe via Footage::set_filename(), then an undoable
 *     NodeAddCommand + FolderAddChild). The returned handle is BORROWED --
 *     the underlying node is owned by the project (QObject parent chain) and
 *     the handle becomes invalid when the project is freed.
 *     oakengine_footage_free() on a borrowed handle only releases the handle
 *     wrapper, never the node.
 *
 * Image sequences: the application's import asks the user whether numbered
 * stills form a sequence (EngineCore::confirm_image_sequence_handler). No
 * such handler exists behind this facade, so imported stills are always
 * treated as single frames and are never merged into an image sequence.
 *
 * Errors: oakengine_footage_probe() and oakengine_project_import_footage()
 * return NULL on failure and record a human-readable reason retrievable
 * with oakengine_footage_last_error() (thread-local). Information functions
 * follow the usual conventions: booleans are int, 0 (OAKENGINE_OK) /
 * negative OAKENGINE_E_* codes, buf/size string output, NULL handles yield
 * no-ops / zero results / OAKENGINE_E_INVALID.
 *
 * Stream info uses the same timestamp/timebase convention as the timeline
 * family: `duration_ts` counts units of the stream's time base
 * (`time_base_num`/`time_base_den` seconds per unit), so
 * seconds = duration_ts * time_base_num / time_base_den.
 */

/**
 * @brief Opaque media handle (owned for probes, borrowed for imports; see
 * the file comment above).
 */
typedef struct OakEngineFootage OakEngineFootage;

/**
 * @brief POD description of one video stream (olive::VideoParams).
 *
 * color_primaries/color_trc carry the ISO/IEC 23001-8 code points the
 * decoder reports (1 = BT.709), 0 when unknown. interlaced is 1 when the
 * stream is interlaced (VideoParams::Interlacing != k_interlace_none).
 */
typedef struct oak_footage_video_info {
	int stream_index;
	int width;
	int height;
	int frame_rate_num;
	int frame_rate_den;
	int64_t duration_ts; /**< Duration in time-base units. */
	int time_base_num; /**< Seconds per time-base unit (numerator). */
	int time_base_den; /**< Seconds per time-base unit (denominator). */
	int color_primaries;
	int color_trc;
	int interlaced;
} oak_footage_video_info;

/**
 * @brief POD description of one audio stream (olive::AudioParams).
 *
 * channel_layout is the ffmpeg-style channel mask (e.g. 0x3 = stereo).
 */
typedef struct oak_footage_audio_info {
	int stream_index;
	int sample_rate;
	uint64_t channel_layout;
	int channel_count;
	int64_t duration_ts; /**< Duration in time-base units. */
	int time_base_num; /**< Seconds per time-base unit (numerator). */
	int time_base_den; /**< Seconds per time-base unit (denominator). */
} oak_footage_audio_info;

/**
 * @brief Probe a media file (decoder, streams, durations, color tags).
 *
 * Runs Decoder::create_from_id("ffmpeg")->probe() directly; requires the
 * engine to be initialized (OAKENGINE_INIT_HEADLESS is sufficient, no GL
 * needed). Returns an owned handle, or NULL on failure (see
 * oakengine_footage_last_error()).
 */
OAKENGINE_API OakEngineFootage *oakengine_footage_probe(const char *path);

/**
 * @brief Release a handle. For probe handles this frees the description;
 * for borrowed import handles it only frees the wrapper (the node stays
 * with its project). NULL-safe.
 */
OAKENGINE_API void oakengine_footage_free(OakEngineFootage *self);

/**
 * @brief Human-readable reason for the last failed probe/import on this
 * thread (buf/size convention). Empty when the last call succeeded.
 */
OAKENGINE_API int oakengine_footage_last_error(char *buf, int buf_size);

/**
 * @brief ID of the decoder that owns the media (e.g. "ffmpeg"), buf/size
 * convention.
 */
OAKENGINE_API int oakengine_footage_get_decoder_name(OakEngineFootage *self,
													 char *buf, int buf_size);

OAKENGINE_API int
oakengine_footage_get_video_stream_count(const OakEngineFootage *self);
OAKENGINE_API int
oakengine_footage_get_audio_stream_count(const OakEngineFootage *self);
OAKENGINE_API int
oakengine_footage_get_subtitle_stream_count(const OakEngineFootage *self);

/**
 * @brief Fill `out` with the video stream at `index`. Returns OAKENGINE_OK
 * or OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_footage_get_video_stream_info(
	OakEngineFootage *self, int index, oak_footage_video_info *out);

/**
 * @brief Fill `out` with the audio stream at `index`. Returns OAKENGINE_OK
 * or OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_footage_get_audio_stream_info(
	OakEngineFootage *self, int index, oak_footage_audio_info *out);

/**
 * @brief Media duration in seconds: the longest stream duration across all
 * video and audio streams.
 */
OAKENGINE_API int oakengine_footage_get_duration(OakEngineFootage *self,
												 double *seconds);

/**
 * @brief 1 if the media file exists on disk, 0 otherwise.
 */
OAKENGINE_API int oakengine_footage_is_online(OakEngineFootage *self);

/**
 * @brief Source start time as a rational (FootageDescription::
 * source_start_time(), e.g. from a timecode track). Returns 1 when the
 * media carries one, 0 when it does not, or a negative error code.
 */
OAKENGINE_API int oakengine_footage_get_source_start_time(
	OakEngineFootage *self, int *num, int *den);

/**
 * @brief Probe `path` and import it into `project`'s root folder.
 *
 * Mirrors the non-UI core of ProjectImportTask: the footage is probed on
 * assignment (Footage::set_filename()), invalid/unreadable media is
 * rejected, and the add is pushed onto the global undo stack as an undoable
 * command (direct, non-undoable application when the engine is not
 * initialized). Stills are imported as single frames (see the file comment
 * about image sequences).
 *
 * Returns a BORROWED handle (owned by the project; do not free the node,
 * oakengine_footage_free() only releases the wrapper), or NULL on failure
 * (see oakengine_footage_last_error()).
 */
OAKENGINE_API OakEngineFootage *oakengine_project_import_footage(
	OakEngineProject *project, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_FOOTAGE_H */
