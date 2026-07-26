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

#ifndef OAKENGINE_SYNC_H
#define OAKENGINE_SYNC_H

#include "export.h"
#include "timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sync.h
 * @brief C ABI for waveform-based audio synchronization estimation
 *
 * Wraps the engine's AudioWaveformSync estimators
 * (engine/audio/audiowaveformsync.h): the full audio of two clips is
 * rendered through the renderer family and cross-correlated, yielding
 * the time offset that aligns the target clip with the reference clip
 * (the application's timeline "synchronize clips by waveform" feature).
 *
 * Both estimators validate first and change nothing on failure (these
 * are pure measurements). They return OAKENGINE_OK when the correlation
 * is conclusive (OffsetResult::valid), OAKENGINE_E_STATE when it is
 * inconclusive -- in that case `out_confidence` is still written so the
 * caller can compare it against a fallback estimator, and the offset
 * outputs are set to 0 / the stretch output to 1. OAKENGINE_E_INVALID
 * covers NULL handles, clips without an on-track range, and sequences
 * without audio; estimation itself requires the engine initialized
 * with OAKENGINE_INIT_RENDER (OAKENGINE_E_STATE as well).
 *
 * Note this family renders audio freshly per call (no waveform-cache
 * dependency); the application keeps its cache-envelope path for the
 * envelope source and uses these functions for the estimation step.
 * Errors follow the family model: per-thread human-readable reason via
 * oakengine_sync_last_error().
 */

/**
 * @brief Estimate the time offset aligning `target` to `reference`
 * (AudioWaveformSync::estimate_envelope_offset).
 *
 * `out_offset_seconds` receives the signed offset in seconds: the
 * shift to ADD to the target's timeline position so it aligns with the
 * reference (negative = move the target earlier -- e.g. the
 * application's AudioSynchronizer adds it to the reference in-point).
 * The estimate is quantized to the RMS envelope window
 * (sample_rate/20 seconds), so callers should expect up to one window
 * of quantization error. `out_confidence` receives the correlation
 * confidence in [0, 1] and is always written. Any output pointer may
 * be NULL. Search bounds mirror the application (sample_rate/20
 * window, 10-minute maximum offset).
 */
OAKENGINE_API int oakengine_sync_estimate_offset(
	OakEngineSequence *seq, OakEngineClip *reference, OakEngineClip *target,
	double *out_offset_seconds, double *out_confidence);

/**
 * @brief Estimate a playback-rate change plus offset aligning `target`
 * to `reference` (AudioWaveformSync::estimate_stretch_and_offset).
 *
 * `out_stretch` receives the rate the target must be played at to
 * align (> 1 = the target runs slower and must be sped up; the search
 * range mirrors the application: 0.75..1.34 in 0.005 steps, 30-second
 * offset radius). `out_offset_seconds` and `out_confidence` behave
 * like oakengine_sync_estimate_offset(). Any output pointer may be
 * NULL.
 */
OAKENGINE_API int oakengine_sync_estimate_stretch_offset(
	OakEngineSequence *seq, OakEngineClip *reference, OakEngineClip *target,
	double *out_stretch, double *out_offset_seconds,
	double *out_confidence);

/**
 * @brief Human-readable reason for the last failed sync call on this
 * thread (buf/size convention). Empty when the last call succeeded.
 */
OAKENGINE_API int oakengine_sync_last_error(char *buf, int buf_size);

/* ---- Place by source time / waveform offset (replaces AudioSynchronizer) - */

/** @brief POD for sync placement result (timeline_in rational). */
typedef struct oak_sync_placement {
	int64_t timeline_in_num;
	int64_t timeline_in_den;
} oak_sync_placement;

/**
 * @brief Place a clip by source time (AudioSynchronizer::place_by_source_time).
 *
 * Computes: timeline_in = anchor_in + (cand_source_start + cand_media_in)
 *                             - (ref_source_start + ref_media_in)
 * Returns OAKENGINE_OK and fills `out`, or OAKENGINE_E_INVALID on NaN input.
 */
OAKENGINE_API int oakengine_sync_place_by_source_time(
    int64_t ref_source_start_num, int64_t ref_source_start_den,
    int64_t ref_media_in_num, int64_t ref_media_in_den,
    int64_t cand_source_start_num, int64_t cand_source_start_den,
    int64_t cand_media_in_num, int64_t cand_media_in_den,
    int64_t anchor_num, int64_t anchor_den,
    oak_sync_placement *out);

/**
 * @brief Place a clip by waveform offset (AudioSynchronizer::place_by_waveform_offset).
 *
 * Computes: timeline_in = ref_timeline_in + candidate_offset_samples / sample_rate
 * Returns OAKENGINE_OK and fills `out`, or OAKENGINE_E_INVALID when sample_rate <= 0.
 */
OAKENGINE_API int oakengine_sync_place_by_waveform_offset(
    int64_t ref_timeline_in_num, int64_t ref_timeline_in_den,
    int64_t candidate_offset_samples, int sample_rate,
    oak_sync_placement *out);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_SYNC_H */
