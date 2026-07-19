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

#ifndef OAKCORE_SAMPLEBUFFER_H
#define OAKCORE_SAMPLEBUFFER_H

#include <stddef.h>

#include "export.h"
#include "rational.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file samplebuffer.h
 * @brief C ABI for the planar audio sample buffer
 *
 * Opaque handle + free functions. Every returned OakSampleBuffer or
 * OakAudioParams handle is owned by the caller and must be released with
 * oakcore_samplebuffer_free() or oakcore_audioparams_free() respectively.
 * Samples are always stored planar (one float array per channel).
 */
typedef struct OakSampleBuffer OakSampleBuffer;
typedef struct OakAudioParams OakAudioParams;

OAKCORE_API OakSampleBuffer *oakcore_samplebuffer_create(void);
OAKCORE_API OakSampleBuffer *oakcore_samplebuffer_create_length(
	const OakAudioParams *params, const OakRational *length);
OAKCORE_API OakSampleBuffer *oakcore_samplebuffer_create_samples(
	const OakAudioParams *params, size_t samples_per_channel);
OAKCORE_API OakSampleBuffer *oakcore_samplebuffer_copy(
	const OakSampleBuffer *self);
OAKCORE_API void oakcore_samplebuffer_free(OakSampleBuffer *self);

OAKCORE_API OakSampleBuffer *oakcore_samplebuffer_rip_channel(
	const OakSampleBuffer *self, int channel);

/**
 * Copies the samples of one channel into out. Returns the total number of
 * floats in the channel (equal to the sample count), so out == NULL (or a
 * too-small buffer) can be used to query the required size; at most out_size
 * floats are written.
 */
OAKCORE_API int oakcore_samplebuffer_rip_channel_vector(
	const OakSampleBuffer *self, int channel, float *out, int out_size);

/**
 * Returns a copy of the buffer's audio parameters as a new owned handle
 * (release with oakcore_audioparams_free()).
 */
OAKCORE_API OakAudioParams *oakcore_samplebuffer_audio_params(
	const OakSampleBuffer *self);
OAKCORE_API void oakcore_samplebuffer_set_audio_params(
	OakSampleBuffer *self, const OakAudioParams *params);

OAKCORE_API size_t oakcore_samplebuffer_sample_count(
	const OakSampleBuffer *self);
OAKCORE_API void oakcore_samplebuffer_set_sample_count(OakSampleBuffer *self,
													   size_t sample_count);
OAKCORE_API void oakcore_samplebuffer_set_sample_count_length(
	OakSampleBuffer *self, const OakRational *length);

/**
 * Borrowed pointer to the samples of one channel; it becomes invalid when the
 * handle is destroyed or reallocated and must not be freed by the caller.
 * Returns NULL when the buffer is not allocated or the channel is out of
 * range.
 */
OAKCORE_API float *oakcore_samplebuffer_data(OakSampleBuffer *self,
											 int channel);

/**
 * Fills out with one borrowed sample pointer per channel (see
 * oakcore_samplebuffer_data()). out must have room for
 * oakcore_samplebuffer_channel_count() entries.
 */
OAKCORE_API void oakcore_samplebuffer_to_raw_ptrs(OakSampleBuffer *self,
												  float **out);

OAKCORE_API int oakcore_samplebuffer_channel_count(const OakSampleBuffer *self);
OAKCORE_API int oakcore_samplebuffer_is_allocated(const OakSampleBuffer *self);
OAKCORE_API void oakcore_samplebuffer_allocate(OakSampleBuffer *self);
OAKCORE_API void oakcore_samplebuffer_destroy(OakSampleBuffer *self);

OAKCORE_API void oakcore_samplebuffer_reverse(OakSampleBuffer *self);
OAKCORE_API void oakcore_samplebuffer_speed(OakSampleBuffer *self,
											double speed);

OAKCORE_API void oakcore_samplebuffer_transform_volume(OakSampleBuffer *self,
													   float f);
OAKCORE_API void oakcore_samplebuffer_transform_volume_for_channel(
	OakSampleBuffer *self, int channel, float volume);
OAKCORE_API void oakcore_samplebuffer_transform_volume_to(
	float f, const OakSampleBuffer *input, OakSampleBuffer *output);
OAKCORE_API void oakcore_samplebuffer_transform_volume_for_channel_to(
	int channel, float volume, const OakSampleBuffer *input,
	OakSampleBuffer *output);
OAKCORE_API void oakcore_samplebuffer_transform_volume_for_sample(
	OakSampleBuffer *self, size_t sample_index, float volume);
OAKCORE_API void oakcore_samplebuffer_transform_volume_for_sample_on_channel(
	OakSampleBuffer *self, size_t sample_index, int channel, float volume);

OAKCORE_API void oakcore_samplebuffer_clamp(OakSampleBuffer *self);

OAKCORE_API void oakcore_samplebuffer_silence(OakSampleBuffer *self);
OAKCORE_API void oakcore_samplebuffer_silence_range(OakSampleBuffer *self,
													size_t start_sample,
													size_t end_sample);
OAKCORE_API void oakcore_samplebuffer_silence_bytes(OakSampleBuffer *self,
													size_t start_byte,
													size_t end_byte);

OAKCORE_API void oakcore_samplebuffer_set(OakSampleBuffer *self, int channel,
										  const float *data,
										  size_t sample_offset,
										  size_t sample_length);

/**
 * Copies channel "from" of other into channel "to" of self; from == -1 uses
 * the same index as to.
 */
OAKCORE_API void oakcore_samplebuffer_fast_set(OakSampleBuffer *self,
											   const OakSampleBuffer *other,
											   int to, int from);

#ifdef __cplusplus
}
#endif

#endif /* OAKCORE_SAMPLEBUFFER_H */
