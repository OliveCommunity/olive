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

#ifndef OAK_EDITOR_AUDIO_SYNC_H
#define OAK_EDITOR_AUDIO_SYNC_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sync.h
 * @brief C ABI for the oakaudio synchronization helpers
 *        (olive::AudioSynchronizer and olive::AudioWaveformSync):
 *        stateless source-time placement and envelope-correlation offset
 *        estimation.
 */

/** Result of an offset estimation. */
typedef struct oakaudio_offset_result {
	int64_t offset_samples;
	double confidence; /**< 0..1 correlation score. */
	int valid; /**< 1 when an estimate was found. */
} oakaudio_offset_result;

/** Result of a stretch-plus-offset estimation. */
typedef struct oakaudio_stretch_offset_result {
	double rate; /**< Playback rate aligning the candidate (> 1 = speed up). */
	int64_t offset_samples;
	double confidence;
	int valid;
} oakaudio_stretch_offset_result;

/**
 * @brief Per-window RMS envelope of a planar float buffer (static).
 *
 * @return Number of envelope windows (>= 0) or a negative OAKAUDIO_E_*
 *         code. When out is NULL or too small, the required window count
 *         is returned and nothing is written.
 */
OAKAUDIO_API int oakaudio_sync_extract_rms_envelope(
		const float *const *planar, int channel_count, int frame_count,
		uint64_t window_samples, double *out, int capacity);

/**
 * @brief Estimate the candidate's offset against the reference by
 *        normalized cross-correlation of RMS envelopes.
 *
 * @param reference_valid/candidate_valid Optional per-window validity
 *        masks (NULL = all windows valid; when non-NULL the length must
 *        match the corresponding envelope length).
 */
OAKAUDIO_API int oakaudio_sync_estimate_envelope_offset(
		const double *reference, int reference_len,
		const double *candidate, int candidate_len,
		const uint8_t *reference_valid, const uint8_t *candidate_valid,
		uint64_t window_samples, int64_t max_offset_windows,
		oakaudio_offset_result *out);

/**
 * @brief Estimate a playback-rate change plus offset aligning the
 *        candidate to the reference.
 *
 * The candidate envelope is resampled at each rate in
 * [min_rate, max_rate] (step rate_step) and correlated against the
 * reference. O(rates * lags * overlap); bound max_offset_windows.
 */
OAKAUDIO_API int oakaudio_sync_estimate_stretch_and_offset(
		const double *reference, int reference_len,
		const double *candidate, int candidate_len,
		const uint8_t *reference_valid, const uint8_t *candidate_valid,
		uint64_t window_samples, int64_t max_offset_windows,
		double min_rate, double max_rate, double rate_step,
		oakaudio_stretch_offset_result *out);

/** One clip's source-time metadata (rational seconds). */
typedef struct oakaudio_source_clip {
	int64_t source_start_time_num;
	int64_t source_start_time_den;
	int64_t media_in_num;
	int64_t media_in_den;
	int has_source_start_time;
} oakaudio_source_clip;

/**
 * @brief Place the candidate on the timeline so its source time aligns
 *        with the reference clip.
 *
 * @param reference_timeline_in_num/den Reference clip's timeline in point.
 * @param out_num/out_den Receive the candidate's timeline in point.
 * @param out_valid Receives 1 when placement succeeded.
 */
OAKAUDIO_API int oakaudio_sync_place_by_source_time(
		const oakaudio_source_clip *reference,
		const oakaudio_source_clip *candidate,
		int64_t reference_timeline_in_num, int64_t reference_timeline_in_den,
		int64_t *out_num, int64_t *out_den, int *out_valid);

/**
 * @brief Timeline placement from a measured waveform offset.
 */
OAKAUDIO_API int oakaudio_sync_place_by_waveform_offset(
		int64_t reference_timeline_in_num, int64_t reference_timeline_in_den,
		int64_t candidate_offset_samples, int sample_rate,
		int64_t *out_num, int64_t *out_den, int *out_valid);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_AUDIO_SYNC_H
