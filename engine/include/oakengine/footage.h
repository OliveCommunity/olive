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
#include "node.h"
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
 *   - Borrowing (oakengine_footage_borrow()): wrap a Footage node the
 *     caller already holds (e.g. selected in the UI) in the same borrowed
 *     handle model, without probing or importing anything.
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
 * @brief POD proxy generation parameters (olive::ProxyManager::ProxyParams).
 *
 * divider: source resolution divider (1 = use absolute width/height,
 * 2/4/8 = fraction of the source resolution). extension/preset are the
 * ffmpeg output container and encoder preset (e.g. "mp4"/"veryfast").
 */
typedef struct oak_proxy_params {
	int width;
	int height;
	int divider;
	int version;
	int crf;
	int include_audio; /**< 1/0. */
	char extension[32];
	char preset[32];
} oak_proxy_params;

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

/**
 * @brief Wrap an existing project footage node in a BORROWED handle.
 *
 * For callers that already hold an engine node (e.g. the application
 * wrapping the Footage it has selected) and want to use the media
 * management / stream override functions below. `node` must be a footage
 * node (anything else yields NULL, see oakengine_footage_last_error()).
 * The handle borrows the node -- it becomes invalid when the node's
 * project is freed, and oakengine_footage_free() only releases the
 * wrapper.
 */
OAKENGINE_API OakEngineFootage *oakengine_footage_borrow(OakEngineNode *node);

/**
 * @brief 1 if the footage node is valid (Footage::is_valid(): the media
 * was probed successfully and is ready to use), 0 otherwise.
 *
 * Takes the footage NODE handle directly (like oakengine_footage_borrow())
 * rather than an OakEngineFootage wrapper, so callers that already hold a
 * project node do not need to borrow/free a wrapper for a one-shot check.
 * Returns 0 on a NULL handle or a node that is not a Footage.
 */
OAKENGINE_API int oakengine_footage_is_valid(const OakEngineNode *node);

/* ---- Media management: relink and proxies -----------------------------------
 *
 * These functions operate on BORROWED import handles (footage nodes living
 * in a project). Probe handles carry no node and are rejected with
 * OAKENGINE_E_INVALID everywhere in this section. Relinking and proxy
 * state changes are not undoable in the engine (the application's relink
 * and proxy dialogs call the same setters directly).
 */

/**
 * @brief Point the footage at a different media file (relink).
 *
 * Calls Footage::set_filename(), which triggers the engine's full reprobe
 * cascade (Footage::clear(): streams, decoder link and the proxy state are
 * reset before re-probing). The footage label is left unchanged. Fails
 * with OAKENGINE_E_NOT_FOUND when `new_path` does not exist, and with
 * OAKENGINE_E_FAILED when the new file cannot be probed as media. Not
 * undoable (same as the application's relink action).
 */
OAKENGINE_API int oakengine_footage_relink(OakEngineFootage *footage,
										   const char *new_path);

/**
 * @brief Try to bring every offline footage item in the project back
 * online by looking up its file name under `search_dir`.
 *
 * Simplified version of the application's relink dialog matching: each
 * footage whose stored filename does not exist is relinked to
 * `search_dir`/<file name> when that file exists (exact file-name match,
 * no recursion; labels are left unchanged). Returns the number of
 * relinked items (>= 0) or a negative code.
 */
OAKENGINE_API int
oakengine_project_find_offline_footage(OakEngineProject *project,
									   const char *search_dir);

/**
 * @brief Proxy state (olive::ProxyManager::ProxyState): 0 = missing,
 * 1 = generating, 2 = ready, 3 = failed.
 */
OAKENGINE_API int oakengine_footage_proxy_get_state(OakEngineFootage *self);

/**
 * @brief Generate the proxy synchronously
 * (ProxyManager::get_or_start_proxy() + wait).
 *
 * Drives the proxy task with an event loop like the export family (the
 * engine hands proxy tasks to the TaskManager thread via queued calls, so
 * the calling thread must process events while waiting; up to 120 s). A
 * ready proxy is a success; a failed generation returns
 * OAKENGINE_E_FAILED with the reason in oakengine_footage_last_error().
 * Not undoable.
 */
OAKENGINE_API int oakengine_footage_proxy_generate(OakEngineFootage *self);

/**
 * @brief Delete the proxy file (and any in-progress working file) and
 * reset the footage's proxy state (mirrors the application's proxy
 * dialog). Not undoable.
 */
OAKENGINE_API int oakengine_footage_proxy_delete(OakEngineFootage *self);

/**
 * @brief 1 if proxy usage is enabled for this footage
 * (Footage::proxy_enabled()).
 */
OAKENGINE_API int oakengine_footage_proxy_is_enabled(OakEngineFootage *self);

/**
 * @brief Enable or disable proxy usage (Footage::set_proxy_enabled(); not
 * undoable).
 */
OAKENGINE_API int oakengine_footage_proxy_set_enabled(OakEngineFootage *self,
													  int enabled);

/**
 * @brief Path of the proxy file (Footage::proxy_path(); empty when the
 * footage has no proxy). buf/size convention.
 */
OAKENGINE_API int oakengine_footage_proxy_get_path(OakEngineFootage *self,
												   char *buf, int buf_size);

/* ---- Stream parameter overrides (footage properties) ------------------------
 *
 * Per-stream overrides shown by the application's footage properties
 * dialog (app/dialog/footageproperties). All setters here are UNDOABLE
 * (pushed onto the global undo stack as one command each, matching the
 * dialog's undoable commands; the engine stores these as plain
 * VideoParams/AudioParams values on the footage node). Getters are direct
 * reads. Everything in this section requires a borrowed import handle
 * (probe handles are rejected with OAKENGINE_E_INVALID), and stream
 * indexes are the index within the stream's own type (0-based).
 */

/**
 * @brief Read a video stream's override parameters.
 *
 * Fills the current values (VideoParams): colorspace name (buf/size
 * convention, may be NULL to skip), color_range
 * (VideoParams::ColorRange), interlacing (VideoParams::Interlacing) and
 * premultiplied-alpha flag. Any output pointer may be NULL. Returns
 * OAKENGINE_E_NOT_FOUND for an out-of-range stream index.
 */
OAKENGINE_API int oakengine_footage_get_video_stream_overrides(
	OakEngineFootage *self, int stream_index, char *colorspace_buf,
	int colorspace_size, int *color_range, int *interlacing,
	int *premultiplied);

/**
 * @brief Write a video stream's overrides (undoable).
 *
 * Pass NULL for `colorspace` and -1 for any int field to leave that field
 * unchanged. An empty colorspace string ("") clears the override back to
 * the project's default input color space.
 */
OAKENGINE_API int oakengine_footage_set_video_stream_overrides(
	OakEngineFootage *self, int stream_index, const char *colorspace,
	int color_range, int interlacing, int premultiplied);

/**
 * @brief Video stream pixel aspect ratio (num/den).
 */
OAKENGINE_API int oakengine_footage_get_pixel_aspect(OakEngineFootage *self,
													 int stream_index, int *num,
													 int *den);

/**
 * @brief Set the video stream pixel aspect ratio (undoable). Both values
 * must be > 0.
 */
OAKENGINE_API int oakengine_footage_set_pixel_aspect(OakEngineFootage *self,
													 int stream_index, int num,
													 int den);

/**
 * @brief Image-sequence parameters of a video stream: start index,
 * duration in frames and frame rate (num/den). Returns
 * OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_footage_get_image_sequence_params(
	OakEngineFootage *self, int stream_index, int64_t *start_index,
	int64_t *duration, int *frame_rate_num, int *frame_rate_den);

/**
 * @brief Set image-sequence parameters (undoable). `duration` must be > 0
 * and the frame rate positive.
 */
OAKENGINE_API int oakengine_footage_set_image_sequence_params(
	OakEngineFootage *self, int stream_index, int64_t start_index,
	int64_t duration, int frame_rate_num, int frame_rate_den);

/**
 * @brief 1 if a stream is enabled (VideoParams/AudioParams/SubtitleParams
 * ::enabled()). `track_type` is an OAKENGINE_TRACK_TYPE_* value;
 * OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_footage_get_stream_enabled(OakEngineFootage *self,
													   int track_type,
													   int index);

/**
 * @brief Enable or disable a stream (undoable).
 */
OAKENGINE_API int oakengine_footage_set_stream_enabled(OakEngineFootage *self,
													   int track_type,
													   int index, int enabled);

/**
 * @brief Set or clear the source start time (undoable; mirrors the
 * dialog's FootageSetSourceStartTimeCommand with source "manual").
 * `enabled` == 0 clears it; otherwise the time is num/den seconds.
 */
OAKENGINE_API int oakengine_footage_set_source_start_time(
	OakEngineFootage *self, int enabled, int64_t num, int64_t den);

/**
 * @brief The detection source of the source start time (e.g. "manual" or
 * the metadata field it was read from; empty when unset). buf/size
 * convention.
 */
OAKENGINE_API int oakengine_footage_get_source_start_time_source(
	OakEngineFootage *self, char *buf, int buf_size);

/* ---- Colorspace candidates --------------------------------------------------- */

/**
 * @brief Number of color spaces in the project's color config (the same
 * list the footage properties dialog's color space dropdown shows).
 */
OAKENGINE_API int
oakengine_footage_colorspace_count(const OakEngineFootage *self);

/**
 * @brief Color space name at `index` in the project color config
 * (buf/size convention). OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_footage_colorspace_at(
	const OakEngineFootage *self, int index, char *buf, int buf_size);

/* ---- Footage extras ------------------------------------------------------- */

/** @brief Filename of the imported footage (buf/size). Returns
 *  OAKENGINE_E_INVALID on NULL. */
OAKENGINE_API int oakengine_footage_get_filename(const OakEngineFootage *self,
												 char *buf, int buf_size);

/** @brief Get the (track_type, stream_index) for the real stream at
 *  `stream_index_in_footage` (which iterates all streams regardless of type).
 *  Returns OAKENGINE_OK or OAKENGINE_E_NOT_FOUND. */
OAKENGINE_API int oakengine_footage_get_stream_reference(
	const OakEngineFootage *self, int stream_index_in_footage,
	int *out_track_type, int *out_stream_index);

/** @brief Human-readable description of a video stream (buf/size).
 *  Returns OAKENGINE_E_NOT_FOUND for an out-of-range index. */
OAKENGINE_API int oakengine_footage_describe_video_stream(
	const OakEngineFootage *self, int video_stream_index, char *buf,
	int buf_size);

/** @brief Human-readable description of an audio stream (buf/size).
 *  Returns OAKENGINE_E_NOT_FOUND for an out-of-range index. */
OAKENGINE_API int oakengine_footage_describe_audio_stream(
	const OakEngineFootage *self, int audio_stream_index, char *buf,
	int buf_size);

/** @brief Human-readable name of a stream type
 *  (OAKENGINE_TRACK_TYPE_* -> translated name). buf/size convention. */
OAKENGINE_API int oakengine_footage_stream_type_name(int track_type, char *buf,
													 int buf_size);

/** @brief 1 if the footage has custom proxy parameters, 0 otherwise. */
OAKENGINE_API int oakengine_footage_has_custom_proxy_params(
	const OakEngineFootage *self);

/** @brief Fill `out` with the effective proxy parameters
 *  (custom if set, otherwise the application defaults). */
OAKENGINE_API int oakengine_footage_get_effective_proxy_params(
	const OakEngineFootage *self, oak_proxy_params *out);

/** @brief Set custom proxy parameters (not undoable). */
OAKENGINE_API int oakengine_footage_set_custom_proxy_params(
	OakEngineFootage *self, const oak_proxy_params *params);

/** @brief Clear custom proxy parameters, reverting to defaults. */
OAKENGINE_API int oakengine_footage_clear_custom_proxy_params(
	OakEngineFootage *self);

/** @brief Generate a proxy with the given parameters (synchronous).
 *  `path` is the proxy file path, `state` the proxy state (0=missing,
 *  1=generating, 2=ready, 3=failed), `stream_index` the video stream index,
 *  `enabled` 1/0 to enable proxy, `version` the preset version. */
OAKENGINE_API int oakengine_footage_set_proxy(OakEngineFootage *self,
											  const char *path, int state,
											  int stream_index, int enabled,
											  int version);

/** @brief Delete the proxy file and reset state. */
OAKENGINE_API int oakengine_footage_clear_proxy(OakEngineFootage *self);

/** @brief Invalidate the footage (force re-probe on next use). */
OAKENGINE_API int oakengine_footage_invalidate(OakEngineFootage *self);

#ifdef __cplusplus
}
Q_DECLARE_OPAQUE_POINTER(OakEngineFootage *)
#endif

#endif /* OAKENGINE_FOOTAGE_H */
