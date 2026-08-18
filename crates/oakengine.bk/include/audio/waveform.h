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

#ifndef OAK_EDITOR_AUDIO_WAVEFORM_H
#define OAK_EDITOR_AUDIO_WAVEFORM_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file waveform.h
 * @brief C ABI for the oakaudio visual waveform store
 *        (olive::AudioVisualWaveform) and whole-file waveform extraction.
 *
 * OakAudioWaveform follows the neutral by-value handle convention (see
 * oakcommon's common/handle.h). Times are rationals as (num, den) pairs
 * of int64_t in seconds; den must be non-zero.
 *
 * Summaries are stored as channel-interleaved min/max pairs: point p of
 * channel c lives at pairs[p * channel_count + c]. This matches the
 * on-disk/cache layout of the engine's waveform data (min/max float
 * pairs), so the extraction output is drop-in compatible.
 */

/** One summarized waveform point of one channel. */
typedef struct oakaudio_min_max {
	float min;
	float max;
} oakaudio_min_max;

typedef struct OakAudioWaveform {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKAUDIO_ABI_VERSION. */
} OakAudioWaveform;

/**
 * @brief Create an empty waveform (count 1, channel count 0).
 */
OAKAUDIO_API OakAudioWaveform oakaudio_waveform_init(void);

/**
 * @brief Release one reference. No-op on NULL/empty handle.
 */
OAKAUDIO_API void oakaudio_waveform_free(OakAudioWaveform *self);

/**
 * @brief Channel count, or a negative OAKAUDIO_E_* code.
 */
OAKAUDIO_API int oakaudio_waveform_get_channel_count(OakAudioWaveform self);
OAKAUDIO_API int oakaudio_waveform_set_channel_count(OakAudioWaveform self,
		int channels);

/**
 * @brief Waveform length in seconds as a rational pair.
 */
OAKAUDIO_API int oakaudio_waveform_length(OakAudioWaveform self,
		int64_t *num, int64_t *den);

/**
 * @brief Write planar float samples into the waveform at `start` seconds,
 *        expanding it if necessary.
 *
 * @param planar Per-channel float planes; channel count is taken from the
 *        waveform (set it first with oakaudio_waveform_set_channel_count).
 */
OAKAUDIO_API int oakaudio_waveform_overwrite_samples(OakAudioWaveform self,
		const float *const *planar, int frame_count, int sample_rate,
		int64_t start_num, int64_t start_den);

/**
 * @brief Copy summarized data from another waveform over this one.
 *
 * @param dest_num/dest_den Where in `self` the sums start being written.
 * @param offset_num/offset_den Where in `src` reading starts.
 * @param length_num/length_den Maximum amount to copy; 0/1 = all of src.
 */
OAKAUDIO_API int oakaudio_waveform_overwrite_sums(OakAudioWaveform self,
		OakAudioWaveform src,
		int64_t dest_num, int64_t dest_den,
		int64_t offset_num, int64_t offset_den,
		int64_t length_num, int64_t length_den);

OAKAUDIO_API int oakaudio_waveform_overwrite_silence(OakAudioWaveform self,
		int64_t start_num, int64_t start_den,
		int64_t length_num, int64_t length_den);

/**
 * @brief Drop `length` seconds from the front (negative prepends silence).
 */
OAKAUDIO_API int oakaudio_waveform_trim_in(OakAudioWaveform self,
		int64_t length_num, int64_t length_den);

OAKAUDIO_API int oakaudio_waveform_resize(OakAudioWaveform self,
		int64_t length_num, int64_t length_den);

OAKAUDIO_API int oakaudio_waveform_trim_range(OakAudioWaveform self,
		int64_t in_num, int64_t in_den,
		int64_t length_num, int64_t length_den);

/**
 * @brief Summarized min/max pairs covering [start, start+length).
 *
 * @param out_pairs Receives points * channel_count channel-interleaved
 *        pairs; may be NULL to query the point count.
 * @param capacity_points Capacity of out_pairs in points.
 * @return Number of points (>= 0), or a negative OAKAUDIO_E_* code.
 *         When out_pairs is NULL or too small the required count is
 *         returned and nothing is written.
 */
OAKAUDIO_API int oakaudio_waveform_get_summary(OakAudioWaveform self,
		int64_t start_num, int64_t start_den,
		int64_t length_num, int64_t length_den,
		oakaudio_min_max *out_pairs, int capacity_points);

/**
 * @brief Min/max of `length` samples starting at `start_index` for every
 *        channel (static, no handle).
 */
OAKAUDIO_API int oakaudio_waveform_sum_samples_s(const float *const *planar,
		int channel_count, int start_index, int length,
		oakaudio_min_max *out);

/**
 * @brief Re-summarize channel-interleaved pairs into one point per
 *        channel (static, no handle).
 */
OAKAUDIO_API int oakaudio_waveform_re_sum_s(const oakaudio_min_max *in,
		int nb_entries, int nb_channels, oakaudio_min_max *out);

/**
 * @brief Extract a whole-file waveform summary from a media file through
 *        the oakcodec decoder C ABI.
 *
 * Decodes `filename`'s audio stream `stream_index` (index within the
 * file's audio stream list) and reduces it to channel-interleaved
 * min/max pairs, one point per `samples_per_point` source samples.
 *
 * @param out_pairs Receives the pairs; may be NULL to query the size.
 * @param capacity_points Capacity of out_pairs in points.
 * @param out_channel_count Receives the channel count (may be NULL).
 * @return Number of points (>= 0); when out_pairs is NULL or too small,
 *         the required count is returned and nothing is written.
 *         Negative OAKAUDIO_E_* code on failure
 *         (OAKAUDIO_E_NOT_FOUND when the file/stream does not exist).
 */
OAKAUDIO_API int oakaudio_waveform_extract(const char *filename,
		int stream_index, int samples_per_point,
		oakaudio_min_max *out_pairs, int capacity_points,
		int *out_channel_count);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_AUDIO_WAVEFORM_H
