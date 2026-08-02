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

#ifndef OAKENGINE_PREVIEW_H
#define OAKENGINE_PREVIEW_H

#include <stdint.h>

#include "export.h"
#include "footage.h"
#include "init.h"
#include "timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file preview.h
 * @brief C ABI for preview state and readouts (loop mode, audio levels,
 * waveform summary)
 *
 * This family is deliberately NOT realtime playback: the application's
 * realtime transport is deeply coupled to its viewer widgets (viewer
 * playback timer, audio queue, PortAudio manager). What a headless
 * consumer needs is renderable through the renderer family; this family
 * adds the remaining preview state and readouts.
 *
 * Loop mode note: the engine has no sequence-level loop state. LoopMode
 * (engine/render/loopmode.h: off / loop / clamp) is a property of a CLIP
 * (ClipBlock::k_loop_mode_input), which is what the application edits.
 * The loop functions here therefore operate on clip handles, persisted
 * through the node-graph and undoable like other parameter writes.
 *
 * Audio readouts are computed by rendering the exact range through
 * RenderManager::render_audio() and reducing the samples in-process --
 * the exact semantics are documented per function. Rendering audio may
 * conform sources on first use; the wait is driven with an event loop
 * like the export family (up to 120 s). Everything here works without
 * GL. Errors follow the family model (oakengine_preview_last_error()).
 */

/** @brief Loop modes, mirroring olive::LoopMode. */
#define OAKENGINE_LOOP_MODE_OFF 0 /**< Play once (olive k_loop_mode_off). */
#define OAKENGINE_LOOP_MODE_LOOP 1 /**< Repeat the clip (olive k_loop_mode_loop). */
#define OAKENGINE_LOOP_MODE_CLAMP 2 /**< Hold first/last frame (olive k_loop_mode_clamp). */

/**
 * @brief Opaque preview request handle (an active render ticket for
 * single-frame or audio-range preview).
 */
typedef struct OakEnginePreviewRequest OakEnginePreviewRequest;

/**
 * @brief POD for a single video frame from a preview request
 * (borrowed data, valid until the request is freed).
 */
typedef struct oak_playback_frame {
	int width;
	int height;
	int format;       /**< olive::PixelFormat::Format value. */
	const void *data; /**< Planar data pointer (first plane). */
	int linesize;     /**< Bytes per row of the first plane. */
	int64_t timestamp_num; /**< Frame timestamp (seconds) as a rational. */
	int64_t timestamp_den; /**< 0 when the frame carries no timestamp. */
} oak_playback_frame;

/**
 * @brief Human-readable reason for the last failed preview call on this
 * thread (buf/size convention).
 */
OAKENGINE_API int oakengine_preview_last_error(char *buf, int buf_size);

/**
 * @brief The clip's loop mode (OAKENGINE_LOOP_MODE_*;
 * ClipBlock::loop_mode()). Returns the mode or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_clip_get_loop_mode(const OakEngineClip *self);

/**
 * @brief Set the clip's loop mode (undoable parameter write, same command
 * path as the node family). OAKENGINE_E_INVALID for an unknown mode.
 */
OAKENGINE_API int oakengine_clip_set_loop_mode(OakEngineClip *self,
											   int mode);

/**
 * @brief Per-channel audio level of a sequence at `time_ts`.
 *
 * Exact semantics: the sequence's audio is rendered over ONE frame
 * starting at `time_ts` (a frame timestamp in the sequence's frame-rate
 * timebase, like the rest of the family) and the linear RMS
 * (root-mean-square) of every channel is written to `values`, in the
 * [0, 1] range for normalized audio. Ranges with no audio content yield
 * exact zeros (the engine returns no allocated samples there).
 * `channel_count` is the capacity of `values`; up to that many of the
 * sequence's channels are written, the return value is the number of
 * channels written, and a negative OAKENGINE_E_* code signals an error
 * (e.g. no RENDER-less engine state issue -- audio renders without GL,
 * but the engine must be initialized with OAKENGINE_INIT_RENDER because
 * rendering goes through RenderManager).
 */
OAKENGINE_API int oakengine_preview_get_audio_levels(
	OakEngineSequence *seq, int64_t time_ts, double *values,
	int channel_count);

/**
 * @brief Waveform min/max summary of a footage's audio over a range.
 *
 * The footage's audio is rendered from `start_ts` to `end_ts` (frame
 * timestamps in the timebase of the project's first sequence's frame
 * rate, same convention as the keyframe family) and each of the `count`
 * equal-sized buckets covering the range yields the minimum and maximum
 * sample value of `channel` in `min_vals`/`max_vals` (linear, unclamped
 * source samples; silent or content-free buckets are exact zeros). The
 * footage handle must be a borrowed import handle (probe handles are
 * rejected with OAKENGINE_E_INVALID); `channel` must be within the
 * footage's channel count and `count` must be > 0.
 */
OAKENGINE_API int oakengine_preview_get_waveform_summary(
	OakEngineFootage *footage, int channel, int64_t start_ts,
	int64_t end_ts, double *min_vals, double *max_vals, int count);

/* ---- R4: waveform, audio levels, cacher, preview requests ------------------ */

/** @brief Maximum sample rate for waveform generation. > 0. */
OAKENGINE_API int oakengine_waveform_max_sample_rate(void);

/**
 * @brief Analyze audio levels (linear RMS) from raw float sample data.
 * `data` is an array of `channels` float pointers, each with `count` samples.
 * Writes RMS values into `levels` (one per channel). Returns OAKENGINE_OK
 * or OAKENGINE_E_INVALID on NULL/bad arguments.
 */
OAKENGINE_API int oakengine_audio_analyze_levels(const float *const *data,
												 int channels, int64_t count,
												 double *levels);

/**
 * @brief Set the preview cacher's playhead position (num/den seconds).
 * Returns OAKENGINE_E_STATE when the cacher is not available.
 */
OAKENGINE_API int oakengine_preview_cacher_set_playhead(int64_t num,
														int64_t den);

/**
 * @brief Pause or resume thumbnail generation in the cacher.
 * Returns OAKENGINE_E_STATE when the cacher is not available.
 */
OAKENGINE_API int oakengine_preview_cacher_set_thumbnails_paused(int paused);

/**
 * @brief Clear pending single-frame render requests from the cacher.
 * Returns OAKENGINE_E_STATE when the cacher is not available.
 */
OAKENGINE_API int
oakengine_preview_cacher_clear_single_frame_renders(int only_finished);

/**
 * @brief Force the cacher to cache a range (num/den seconds in/out).
 * Returns OAKENGINE_E_INVALID on NULL node.
 */
OAKENGINE_API int oakengine_preview_cacher_force_cache_range(
	OakEngineNode *node, int64_t in_num, int64_t in_den, int64_t out_num,
	int64_t out_den);

/**
 * @brief Request a single video frame at (num/den) seconds from `viewer`.
 * Returns a request handle (caller owns it, must free) or NULL on failure.
 */
OAKENGINE_API OakEnginePreviewRequest *
oakengine_preview_request_single_frame(OakEngineNode *viewer, int64_t num,
									   int64_t den, int dry);

/**
 * @brief Request an audio range (num/den seconds in/out) from `viewer`.
 * Returns a request handle (caller owns it, must free) or NULL on failure.
 */
OAKENGINE_API OakEnginePreviewRequest *
oakengine_preview_request_audio_range(OakEngineNode *viewer, int64_t in_num,
									  int64_t in_den, int64_t out_num,
									  int64_t out_den);

/** @brief 1 if the request is done, 0 otherwise. 0 on NULL. */
OAKENGINE_API int oakengine_preview_request_is_done(
	const OakEnginePreviewRequest *req);

/** @brief 1 if the request has a result, 0 otherwise. 0 on NULL. */
OAKENGINE_API int oakengine_preview_request_has_result(
	const OakEnginePreviewRequest *req);

/** @brief Set a finished callback (called when the ticket completes).
 *  `callback` receives `user_data`. Returns OAKENGINE_E_INVALID on NULL
 *  request. */
OAKENGINE_API int oakengine_preview_request_set_finished_callback(
	OakEnginePreviewRequest *req, void (*callback)(void *),
	void *user_data);

/** @brief Copy the frame data into `out`. Returns OAKENGINE_OK or
 *  OAKENGINE_E_INVALID when the request has no video frame result. */
OAKENGINE_API int oakengine_preview_request_get_frame(
	OakEnginePreviewRequest *req, oak_playback_frame *out);

/** @brief Number of audio channels in the result, or 0 if none. */
OAKENGINE_API int oakengine_preview_request_get_audio_channel_count(
	const OakEnginePreviewRequest *req);

/** @brief Samples per channel in the result, or 0 if none. */
OAKENGINE_API int oakengine_preview_request_get_audio_sample_count(
	const OakEnginePreviewRequest *req);

/** @brief Sample rate of the audio result, or 0 if none. */
OAKENGINE_API int oakengine_preview_request_get_audio_sample_rate(
	const OakEnginePreviewRequest *req);

/**
 * @brief Get audio sample data from the result.
 * `channel` is the 0-based channel index. Writes up to `max_samples` float
 * values into `samples`. Returns the number of samples written, or
 * OAKENGINE_E_INVALID on bad arguments.
 */
OAKENGINE_API int oakengine_preview_request_get_audio_samples(
	OakEnginePreviewRequest *req, int channel, const float *samples,
	int max_samples);

/** @brief Free a preview request handle (NULL-safe). */
OAKENGINE_API void oakengine_preview_request_free(
	OakEnginePreviewRequest *req);

#ifdef __cplusplus
}
Q_DECLARE_OPAQUE_POINTER(OakEnginePreviewRequest *)
#endif

#endif /* OAKENGINE_PREVIEW_H */
