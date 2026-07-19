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

#ifndef OAKENGINE_EXPORTER_H
#define OAKENGINE_EXPORTER_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file exporter.h
 * @brief C ABI for synchronous offline export (render + encode)
 *
 * oakengine_export_render() renders a sequence range offline
 * (RenderMode::k_online) and encodes it straight to a file, driving the
 * engine's own export path (ExportTask over EncodingParams + the
 * FFmpeg/OIIO encoders, engine/task/export/export.cpp) synchronously on the
 * calling thread. The engine's task machinery itself has no UI dependency;
 * the export dialog stays out of the picture by design. (Named exporter.h
 * because oakengine/export.h already holds the symbol visibility macros.)
 *
 * Rendering requires OAKENGINE_INIT_RENDER (video frames go through the
 * render worker pool and may need GL); codec probing
 * (oakengine_export_has_video_codec()/_has_audio_codec()) does not.
 *
 * Conventions match the other facade families: 0 (OAKENGINE_OK) / negative
 * OAKENGINE_E_* codes, buf/size strings, NULL handles are no-ops. Failures
 * record a human-readable reason in the thread-local last-error string
 * (oakengine_export_last_error()).
 */

/** @brief Video codecs for oak_export_options::video_codec. */
#define OAKENGINE_EXPORT_VIDEO_H264 0 /**< H.264 in an MP4 container. */
#define OAKENGINE_EXPORT_VIDEO_H265 1 /**< H.265/HEVC in an MP4 container. */
#define OAKENGINE_EXPORT_VIDEO_PNG_SEQUENCE 2 /**< PNG still-image sequence. */

/** @brief Audio codecs for oak_export_options::audio_codec. */
#define OAKENGINE_EXPORT_AUDIO_AAC 0
#define OAKENGINE_EXPORT_AUDIO_PCM 1
/** @brief Disable the audio track entirely (not a codec). */
#define OAKENGINE_EXPORT_AUDIO_NONE (-1)

/**
 * @brief POD export parameters. 0 (or negative) fields select the default
 * documented per field.
 */
typedef struct oak_export_options {
	/** OAKENGINE_EXPORT_VIDEO_* value; default H264. */
	int video_codec;
	/** OAKENGINE_EXPORT_AUDIO_* value; default AAC; AUDIO_NONE disables. */
	int audio_codec;
	/** Video bit rate in bit/s; <= 0 lets the encoder choose (FFmpeg
	 * defaults). */
	int64_t video_bit_rate;
	/** Audio sample rate in Hz; <= 0 uses the sequence's rate. */
	int audio_sample_rate;
	/** Audio channel count (1 = mono, 2 = stereo); <= 0 uses the
	 * sequence's layout. */
	int audio_channel_count;
} oak_export_options;

/**
 * @brief Render `seq`'s [in_ts, out_ts) range offline and encode it to
 * `path`.
 *
 * `in_ts`/`out_ts` are frame timestamps in the sequence's frame-rate
 * timebase (the export frame rate is the sequence frame rate). `width` and
 * `height` <= 0 fall back to the sequence's video dimensions; when they
 * differ, the frames are scaled to fit (EncodingParams::k_fit). Video is
 * encoded with the options' codec (PNG sequence: `path` is the filename
 * template -- a "-%04d" frame placeholder is inserted before the extension
 * when absent), audio with the options' codec at the requested rate/layout,
 * and color is transformed from the project's reference space to sRGB OETF
 * (the application export dialog's default output).
 *
 * The call blocks until the export finishes. Progress is reported through
 * the callback set with oakengine_export_set_progress_callback().
 *
 * @return OAKENGINE_OK on success; OAKENGINE_E_INVALID for bad arguments;
 * OAKENGINE_E_STATE when the engine lacks OAKENGINE_INIT_RENDER;
 * OAKENGINE_E_FAILED for render/encode failures (see
 * oakengine_export_last_error()).
 */
OAKENGINE_API int oakengine_export_render(OakEngineSequence *seq,
										  const char *path, int64_t in_ts,
										  int64_t out_ts, int width,
										  int height,
										  const oak_export_options *opts);

/**
 * @brief Human-readable reason for the last failed export on this thread
 * (buf/size convention).
 */
OAKENGINE_API int oakengine_export_last_error(char *buf, int buf_size);

/**
 * @brief 1 if the OAKENGINE_EXPORT_VIDEO_* codec is encodable here, 0
 * otherwise (unknown codec ids included).
 */
OAKENGINE_API int oakengine_export_has_video_codec(int codec);

/**
 * @brief 1 if the OAKENGINE_EXPORT_AUDIO_* codec is encodable here, 0
 * otherwise (AUDIO_NONE and unknown ids included).
 */
OAKENGINE_API int oakengine_export_has_audio_codec(int codec);

/**
 * @brief Progress callback signature: `fraction` in [0, 1], monotonically
 * non-decreasing during one export.
 */
typedef void (*oakengine_export_progress_fn)(double fraction,
											 void *userdata);

/**
 * @brief Install the progress callback used by subsequent
 * oakengine_export_render() calls on this thread (NULL disables).
 */
OAKENGINE_API void
oakengine_export_set_progress_callback(oakengine_export_progress_fn fn,
									   void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_EXPORTER_H */
