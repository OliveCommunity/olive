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

#ifndef OAKCORE_AUDIOPARAMS_H
#define OAKCORE_AUDIOPARAMS_H

#include <stdint.h>

#include "export.h"
#include "rational.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file audioparams.h
 * @brief C ABI for the audio stream parameter value type
 *
 * Opaque handle + free functions. All returned OakAudioParams* and
 * OakRational* are owned by the caller and must be released with
 * oakcore_audioparams_free() / oakcore_rational_free() respectively.
 *
 * Sample formats cross the boundary as plain ints carrying the
 * olive::core::SampleFormat::Format enum values (render/sampleformat.h);
 * channel layouts are plain 64-bit masks (render/channellayout.h).
 */
typedef struct OakAudioParams OakAudioParams;

/**
 * Creates parameters for the given sample rate, channel layout mask and
 * sample format. The timebase is initialized to 1/sample_rate.
 */
OAKCORE_API OakAudioParams *oakcore_audioparams_create(int sample_rate,
													   uint64_t channel_layout,
													   int format);

/**
 * Creates default (invalid) parameters: sample_rate=0, empty channel
 * layout, invalid format.
 */
OAKCORE_API OakAudioParams *oakcore_audioparams_create_invalid(void);

OAKCORE_API OakAudioParams *oakcore_audioparams_copy(const OakAudioParams *self);
OAKCORE_API void oakcore_audioparams_free(OakAudioParams *self);

OAKCORE_API int oakcore_audioparams_sample_rate(const OakAudioParams *self);
OAKCORE_API void oakcore_audioparams_set_sample_rate(OakAudioParams *self,
													 int sample_rate);

OAKCORE_API uint64_t oakcore_audioparams_channel_layout(const OakAudioParams *self);
OAKCORE_API void oakcore_audioparams_set_channel_layout(OakAudioParams *self,
														uint64_t mask);

OAKCORE_API OakRational *oakcore_audioparams_time_base(const OakAudioParams *self);
OAKCORE_API void oakcore_audioparams_set_time_base(OakAudioParams *self,
												   const OakRational *timebase);

/**
 * Returns a new owned handle to Rational(1, sample_rate).
 */
OAKCORE_API OakRational *oakcore_audioparams_sample_rate_as_time_base(
	const OakAudioParams *self);

OAKCORE_API int oakcore_audioparams_format(const OakAudioParams *self);
OAKCORE_API void oakcore_audioparams_set_format(OakAudioParams *self, int format);

OAKCORE_API int oakcore_audioparams_enabled(const OakAudioParams *self);
OAKCORE_API void oakcore_audioparams_set_enabled(OakAudioParams *self, int enabled);

OAKCORE_API int oakcore_audioparams_stream_index(const OakAudioParams *self);
OAKCORE_API void oakcore_audioparams_set_stream_index(OakAudioParams *self,
													  int stream_index);

OAKCORE_API int64_t oakcore_audioparams_duration(const OakAudioParams *self);
OAKCORE_API void oakcore_audioparams_set_duration(OakAudioParams *self,
												  int64_t duration);

/**
 * Time/byte/sample conversions. All of these require valid parameters
 * (oakcore_audioparams_is_valid()); calling them on invalid parameters is
 * an error (the library asserts in debug builds).
 */
OAKCORE_API int64_t oakcore_audioparams_time_to_bytes(const OakAudioParams *self,
													  double time);
OAKCORE_API int64_t oakcore_audioparams_time_to_bytes_rational(
	const OakAudioParams *self, const OakRational *time);
OAKCORE_API int64_t oakcore_audioparams_time_to_bytes_per_channel(
	const OakAudioParams *self, double time);
OAKCORE_API int64_t oakcore_audioparams_time_to_bytes_per_channel_rational(
	const OakAudioParams *self, const OakRational *time);
OAKCORE_API int64_t oakcore_audioparams_time_to_samples(const OakAudioParams *self,
														double time);
OAKCORE_API int64_t oakcore_audioparams_time_to_samples_rational(
	const OakAudioParams *self, const OakRational *time);
OAKCORE_API int64_t oakcore_audioparams_samples_to_bytes(const OakAudioParams *self,
														 int64_t samples);
OAKCORE_API int64_t oakcore_audioparams_samples_to_bytes_per_channel(
	const OakAudioParams *self, int64_t samples);
OAKCORE_API OakRational *oakcore_audioparams_samples_to_time(
	const OakAudioParams *self, int64_t samples);
OAKCORE_API int64_t oakcore_audioparams_bytes_to_samples(const OakAudioParams *self,
														 int64_t bytes);
OAKCORE_API OakRational *oakcore_audioparams_bytes_to_time(
	const OakAudioParams *self, int64_t bytes);
OAKCORE_API OakRational *oakcore_audioparams_bytes_per_channel_to_time(
	const OakAudioParams *self, int64_t bytes);

OAKCORE_API int oakcore_audioparams_channel_count(const OakAudioParams *self);
OAKCORE_API int oakcore_audioparams_bytes_per_sample_per_channel(
	const OakAudioParams *self);
OAKCORE_API int oakcore_audioparams_bits_per_sample(const OakAudioParams *self);
OAKCORE_API int oakcore_audioparams_is_valid(const OakAudioParams *self);

/**
 * Equality like operator==: 1 when equal, 0 otherwise. Compares format,
 * sample rate, timebase and channel layout (not the footage parameters).
 */
OAKCORE_API int oakcore_audioparams_equals(const OakAudioParams *self,
										   const OakAudioParams *other);

/**
 * Enumerates AudioParams::k_supported_channel_layouts. Out-of-range indices
 * return 0.
 */
OAKCORE_API int oakcore_audioparams_supported_channel_layout_count(void);
OAKCORE_API uint64_t oakcore_audioparams_supported_channel_layout_at(int index);

/**
 * Enumerates AudioParams::k_supported_sample_rates. Out-of-range indices
 * return 0.
 */
OAKCORE_API int oakcore_audioparams_supported_sample_rate_count(void);
OAKCORE_API int oakcore_audioparams_supported_sample_rate_at(int index);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_AUDIOPARAMS_H */
