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

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_PREVIEW_H */
