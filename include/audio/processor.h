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

#ifndef OAK_EDITOR_AUDIO_PROCESSOR_H
#define OAK_EDITOR_AUDIO_PROCESSOR_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file processor.h
 * @brief C ABI for the oakaudio real-time resampler/format converter
 *        (olive::AudioProcessor).
 *
 * OakAudioProcessor follows the neutral by-value handle convention (see
 * oakcommon's common/handle.h): oakaudio_processor_init() returns a handle
 * whose underlying object has reference count 1, the addref and release
 * function pointers adjust that count atomically (release destroys the
 * object at zero), and abi_version is always OAKAUDIO_ABI_VERSION.
 * Functions that only use a handle take it BY VALUE; an empty handle
 * (ctx == NULL) is reported as OAKAUDIO_E_INVALID.
 *
 * Sample formats are passed as ints matching the
 * olive::core::SampleFormat::Format enum values (invalid = -1, u8_p = 0,
 * s16_p, s32_p, s64_p, f32_p, f64_p, u8, s16, s32, s64, f32, f64,
 * count). Channel layouts are ffmpeg-style channel masks.
 */
typedef struct OakAudioProcessor {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKAUDIO_ABI_VERSION. */
} OakAudioProcessor;

/** oakaudio_processor_convert() delivers planar 32-bit float output. */
#define OAKAUDIO_PROCESSOR_OUTPUT_FORMAT 4 /**< SampleFormat::f32_p. */

/**
 * @brief Create a closed audio processor (count 1).
 *
 * @return Handle with reference count 1; ctx is NULL on allocation
 *         failure.
 */
OAKAUDIO_API OakAudioProcessor oakaudio_processor_init(void);

/**
 * @brief Release one reference to a processor.
 *
 * Convenience wrapper around self->release(self->ctx); nulls self->ctx.
 * No-op when self is NULL or self->ctx is NULL.
 */
OAKAUDIO_API void oakaudio_processor_free(OakAudioProcessor *self);

/**
 * @brief Open the resampling/format-conversion graph.
 *
 * out_format is accepted for interface completeness but the conversion
 * output is always planar 32-bit float (see
 * OAKAUDIO_PROCESSOR_OUTPUT_FORMAT); passing any other format returns
 * OAKAUDIO_E_INVALID. A channel layout mask of 0 falls back to the
 * default layout for the channel count (stereo when unknown), matching
 * the C++ implementation.
 *
 * @param speed Tempo factor (1.0 = unchanged).
 * @return OAKAUDIO_OK, OAKAUDIO_E_STATE when already open,
 *         OAKAUDIO_E_INVALID for bad arguments, or OAKAUDIO_E_FAILED when
 *         the filter graph could not be created.
 */
OAKAUDIO_API int oakaudio_processor_open(OakAudioProcessor self,
		int in_rate, uint64_t in_layout, int in_format,
		int out_rate, uint64_t out_layout, int out_format, double speed);

/**
 * @brief Close the graph (safe when closed; self must be non-empty).
 */
OAKAUDIO_API int oakaudio_processor_close(OakAudioProcessor self);

/**
 * @brief 1 when open, 0 when closed, OAKAUDIO_E_INVALID for empty handle.
 */
OAKAUDIO_API int oakaudio_processor_is_open(OakAudioProcessor self);

/**
 * @brief Push planar float input and pull converted output.
 *
 * @param in_planar Per-channel float input planes (in channel count);
 *        NULL with in_frame_count == 0 only pulls pending output.
 * @param in_frame_count Frames per input channel.
 * @param out_planar Per-channel float output planes (out channel count);
 *        NULL to discard/pull nothing (returns 0).
 * @param out_capacity_frames Capacity of each output plane in frames.
 * @return Number of output frames written (>= 0), or a negative
 *         OAKAUDIO_E_* code. Output is clamped to out_capacity_frames;
 *         remaining frames stay queued in the graph.
 */
OAKAUDIO_API int oakaudio_processor_convert(OakAudioProcessor self,
		const float *const *in_planar, int in_frame_count,
		float *const *out_planar, int out_capacity_frames);

/**
 * @brief Signal end-of-input to the graph (flushes internal delay).
 */
OAKAUDIO_API int oakaudio_processor_flush(OakAudioProcessor self);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_AUDIO_PROCESSOR_H
