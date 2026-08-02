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

#ifndef OAKENGINE_AUDIO_H
#define OAKENGINE_AUDIO_H

#include <stdint.h>

#include "export.h"
#include "encoding.h"
#include "init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file audio.h
 * @brief C ABI for the engine's audio I/O singleton (olive::AudioManager)
 *
 * A thin facade over AudioManager's instance lifecycle, input/output device
 * selection, output buffer management and recording stop control. The
 * AudioManager handle returned by oakengine_audio_manager_handle() is a
 * borrowed opaque pointer intended only for event subscription
 * (OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED); it is not a general
 * purpose object handle and must not be freed.
 *
 * Conventions match the other facade families:
 *   - 0 (OAKENGINE_OK) / negative OAKENGINE_E_* codes.
 *   - Device indices are PortAudio PaDeviceIndex values (int64_t across the
 *     boundary); paNoDevice is -1.
 *   - String output uses the buf/size convention (error_buf for
 *     oakengine_audio_push_to_output).
 */

typedef struct OakAudioParams OakAudioParams;

/**
 * @brief Create the AudioManager singleton.
 *
 * Safe to call when the instance already exists (no-op). Returns
 * OAKENGINE_OK or OAKENGINE_E_FAILED.
 */
OAKENGINE_API int oakengine_audio_create_instance(void);

/**
 * @brief Destroy the AudioManager singleton.
 *
 * Safe to call when no instance exists (no-op). Returns OAKENGINE_OK.
 */
OAKENGINE_API int oakengine_audio_destroy_instance(void);

/**
 * @brief Borrowed handle to the AudioManager singleton, or NULL if none.
 *
 * Intended only for subscribing to
 * OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED. The pointer is owned
 * by the engine and becomes NULL after oakengine_audio_destroy_instance().
 */
OAKENGINE_API void *oakengine_audio_manager_handle(void);

/**
 * @brief Current output device index (paNoDevice = -1 when none).
 *
 * Returns the current value from the AudioManager singleton, or paNoDevice if
 * no instance exists.
 */
OAKENGINE_API int64_t oakengine_audio_get_output_device(void);

/**
 * @brief Set the output device index.
 *
 * Changing the device may emit output_params_changed. Returns OAKENGINE_OK or
 * OAKENGINE_E_FAILED.
 */
OAKENGINE_API int oakengine_audio_set_output_device(int64_t device);

/**
 * @brief Current input device index (paNoDevice = -1 when none).
 */
OAKENGINE_API int64_t oakengine_audio_get_input_device(void);

/**
 * @brief Set the input device index.
 */
OAKENGINE_API int oakengine_audio_set_input_device(int64_t device);

/**
 * @brief Re-initialize PortAudio and refresh the device lists.
 */
OAKENGINE_API int oakengine_audio_hard_reset(void);

/**
 * @brief Clear any buffered output samples.
 */
OAKENGINE_API int oakengine_audio_clear_buffered_output(void);

/**
 * @brief Push a packed sample buffer to the current output device.
 *
 * `params` is an owned or borrowed OakAudioParams handle describing the
 * sample data. `samples` points to `samples_size` bytes of interleaved audio
 * data in the format described by `params`. On failure a human-readable
 * message is written into `error_buf` (up to `error_buf_size` bytes including
 * the terminating NUL) and OAKENGINE_E_FAILED is returned.
 */
OAKENGINE_API int oakengine_audio_push_to_output(const OakAudioParams *params,
                                                 const char *samples,
                                                 int64_t samples_size,
                                                 char *error_buf,
                                                 int error_buf_size);

/**
 * @brief Stop an active recording session.
 */
OAKENGINE_API int oakengine_audio_stop_recording(void);

/**
 * @brief Stop audio output.
 */
OAKENGINE_API int oakengine_audio_stop_output(void);

/**
 * @brief Restart the output clock at zero for a new playback run.
 */
OAKENGINE_API int oakengine_audio_reset_output_clock(void);

/**
 * @brief Set the output notify interval in bytes.
 *
 * After this many bytes of audio have been consumed by the output device,
 * the AudioManager emits output_notify (which translates to the
 * OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_NOTIFY event for C subscribers).
 */
OAKENGINE_API int oakengine_audio_set_output_notify_interval(int64_t bytes);

/**
 * @brief Start audio recording.
 *
 * Takes ownership of `params`: the handle is destroyed when the recording
 * ends. On failure a human-readable message is written into `error_buf`
 * (up to `error_buf_size` bytes including the terminating NUL).
 *
 * @return OAKENGINE_OK on success, OAKENGINE_E_FAILED on error.
 */
OAKENGINE_API int oakengine_audio_start_recording(
	OakEngineEncodingParams *params, char *error_buf, int error_buf_size);

/* ---- Audio synchronization (R6 P1.3) ------------------------------------ */

/** @brief Result of envelope-offset correlation. */
typedef struct oak_audio_waveform_offset {
	int64_t offset_samples;
	double confidence;
	/** 1 if the offset is usable, 0 otherwise. */
	int valid;
} oak_audio_waveform_offset;

/** @brief Result of rate+offset correlation. */
typedef struct oak_audio_waveform_stretch_offset {
	double rate;
	int64_t offset_samples;
	double confidence;
	/** 1 if the result is usable, 0 otherwise. */
	int valid;
} oak_audio_waveform_stretch_offset;

/**
 * @brief Estimate the sample offset between two RMS envelopes.
 *
 * `reference_valid`/`candidate_valid` may be NULL to mean "all windows valid";
 * if non-NULL their lengths must equal `reference_len`/`candidate_len`.
 *
 * @return OAKENGINE_OK with `out` filled, or OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_audio_estimate_envelope_offset(
	const double *reference, int reference_len,
	const double *candidate, int candidate_len,
	const bool *reference_valid, int reference_valid_len,
	const bool *candidate_valid, int candidate_valid_len,
	uint64_t window_samples, int64_t max_offset_windows,
	oak_audio_waveform_offset *out);

/**
 * @brief Estimate a playback-rate change plus offset aligning candidate to
 * reference.
 *
 * See AudioWaveformSync::estimate_stretch_and_offset().
 */
OAKENGINE_API int oakengine_audio_estimate_stretch_and_offset(
	const double *reference, int reference_len,
	const double *candidate, int candidate_len,
	const bool *reference_valid, int reference_valid_len,
	const bool *candidate_valid, int candidate_valid_len,
	uint64_t window_samples, int64_t max_offset_windows,
	double min_rate, double max_rate, double rate_step,
	oak_audio_waveform_stretch_offset *out);

/** @brief Source-clip description for source-time synchronization. */
typedef struct oak_audio_sync_source_clip {
	/** Source start time as a Rational num/den pair. */
	int64_t source_start_time_num;
	int64_t source_start_time_den;
	/** Media in-point as a Rational num/den pair. */
	int64_t media_in_num;
	int64_t media_in_den;
	/** 1 if source_start_time is meaningful, 0 otherwise. */
	int has_source_start_time;
} oak_audio_sync_source_clip;

/** @brief Timeline placement result from AudioSynchronizer. */
typedef struct oak_audio_sync_placement {
	/** Timeline in-point as a Rational num/den pair. */
	int64_t timeline_in_num;
	int64_t timeline_in_den;
	/** 1 if the placement is usable, 0 otherwise. */
	int valid;
} oak_audio_sync_placement;

/**
 * @brief Compute a candidate clip's timeline placement from source timecodes.
 *
 * `reference_timeline_in` is the reference clip's timeline in-point as a
 * Rational num/den pair.
 */
OAKENGINE_API int oakengine_audio_sync_place_by_source_time(
	const oak_audio_sync_source_clip *reference,
	const oak_audio_sync_source_clip *candidate,
	int64_t reference_timeline_in_num, int64_t reference_timeline_in_den,
	oak_audio_sync_placement *out);

/**
 * @brief Compute a candidate clip's timeline placement from a waveform offset.
 */
OAKENGINE_API int oakengine_audio_sync_place_by_waveform_offset(
	int64_t reference_timeline_in_num, int64_t reference_timeline_in_den,
	int64_t candidate_offset_samples, int sample_rate,
	oak_audio_sync_placement *out);

/* ---- Audio format processor (R6 P5) ------------------------------------- */

/**
 * @brief Opaque audio format converter (olive::AudioProcessor).
 *
 * Converts planar float samples from one format to a packed output format,
 * optionally applying tempo (speed) scaling. Used by the viewer to feed the
 * audio output device. Create with oakengine_audio_processor_create() and
 * destroy with oakengine_audio_processor_free().
 */
typedef struct OakEngineAudioProcessor OakEngineAudioProcessor;

/**
 * @brief Create an audio processor with no open graph.
 *
 * Returns NULL on allocation failure.
 */
OAKENGINE_API OakEngineAudioProcessor *oakengine_audio_processor_create(void);

/**
 * @brief Destroy the processor, closing any open graph. Safe to call with
 * NULL.
 */
OAKENGINE_API void oakengine_audio_processor_free(OakEngineAudioProcessor *p);

/**
 * @brief Open the conversion graph.
 *
 * `from` describes the planar float input format and `to` the packed output
 * format (both borrowed handles, copied internally). `tempo` is the playback
 * speed (1.0 = normal). The processor must not already be open. Returns
 * OAKENGINE_OK, OAKENGINE_E_INVALID, or OAKENGINE_E_FAILED.
 */
OAKENGINE_API int oakengine_audio_processor_open(OakEngineAudioProcessor *p,
	const OakAudioParams *from, const OakAudioParams *to, double tempo);

/**
 * @brief Close the conversion graph. Safe to call when not open or with
 * NULL.
 */
OAKENGINE_API void oakengine_audio_processor_close(OakEngineAudioProcessor *p);

/**
 * @brief 1 if the processor has an open graph, 0 otherwise (or NULL).
 */
OAKENGINE_API int oakengine_audio_processor_is_open(OakEngineAudioProcessor *p);

/**
 * @brief Convert planar float samples to the packed output format.
 *
 * `in` is an array of per-channel float pointers (channel count as given to
 * open()); `nb_in_samples` is the number of frames. On success (>= 0),
 * `*out_data` points to the packed output bytes owned by `p` (valid until the
 * next convert/close/free) and `*out_size` holds the byte count, which may be
 * 0 when the tempo buffer absorbed the block. Returns a negative error code
 * on failure.
 */
OAKENGINE_API int oakengine_audio_processor_convert(OakEngineAudioProcessor *p,
	float **in, int nb_in_samples, const void **out_data, int *out_size);

/**
 * @brief Output (packed) parameters as a new OakAudioParams handle.
 *
 * The caller owns the result and must free it with oakcore_audioparams_free().
 * Returns NULL if `p` is NULL or not open.
 */
OAKENGINE_API OakAudioParams *oakengine_audio_processor_output_params(
	OakEngineAudioProcessor *p);

#ifdef __cplusplus
}
Q_DECLARE_OPAQUE_POINTER(OakEngineAudioProcessor *)
#endif

#endif /* OAKENGINE_AUDIO_H */
