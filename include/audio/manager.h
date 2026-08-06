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

#ifndef OAK_EDITOR_AUDIO_MANAGER_H
#define OAK_EDITOR_AUDIO_MANAGER_H

#include <stdint.h>

#include "codec/encoder.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file manager.h
 * @brief C ABI for the oakaudio PortAudio output/input manager
 *        (olive::AudioManager singleton).
 *
 * OakAudioManager uses the standard handle layout (see oakcommon's
 * common/handle.h) but with singleton semantics: ctx points to the
 * process-wide instance created by oakaudio_manager_create_instance(), so
 * addref() and release() are intentionally no-ops and never destroy
 * anything (mirrors oakcommon's OakCurrent). abi_version is always
 * OAKAUDIO_ABI_VERSION.
 *
 * Device indices are PortAudio PaDeviceIndex values (-1 = paNoDevice).
 * Sample formats are olive::core::SampleFormat::Format values.
 */
typedef struct OakAudioManager {
	void *ctx; /**< Opaque pointer to the singleton object. */
	void (*addref)(void *ctx); /**< No-op (singleton). */
	void (*release)(void *ctx); /**< No-op (singleton). */
	uint32_t abi_version; /**< OAKAUDIO_ABI_VERSION. */
} OakAudioManager;

/**
 * @brief Create the process-wide AudioManager (no-op when it exists).
 *
 * Initializes PortAudio and picks the configured/default devices.
 *
 * @return OAKAUDIO_OK or OAKAUDIO_E_NOMEM.
 */
OAKAUDIO_API int oakaudio_manager_create_instance(void);

/**
 * @brief Destroy the process-wide AudioManager (no-op when absent).
 */
OAKAUDIO_API void oakaudio_manager_destroy_instance(void);

/**
 * @brief Return a handle to the process-wide AudioManager.
 *
 * The returned handle is borrowed; addref/release are no-ops. When no
 * instance exists the handle is empty (ctx == NULL) and all functions
 * report OAKAUDIO_E_STATE.
 */
OAKAUDIO_API OakAudioManager oakaudio_manager_instance(void);

/**
 * @brief Release a manager handle. No-op (singleton), safe on NULL/empty.
 */
OAKAUDIO_API void oakaudio_manager_free(OakAudioManager *self);

/**
 * @brief Bytes between output-notify pulses (0 disables).
 */
OAKAUDIO_API int oakaudio_manager_set_output_notify_interval(
		OakAudioManager self, int64_t bytes);

/**
 * @brief Push a block of samples to the output device, opening/restarting
 *        the stream when the params changed.
 *
 * @param rate/layout/format Stream params (ffmpeg-style layout mask,
 *        SampleFormat::Format int).
 * @param samples Packed samples in the given format.
 * @param samples_size Byte count of `samples`.
 * @param error_buf/error_buf_size Optional human-readable failure detail.
 * @return OAKAUDIO_OK, OAKAUDIO_E_INVALID, OAKAUDIO_E_STATE (no output
 *         device), or OAKAUDIO_E_FAILED (PortAudio error, see error_buf).
 */
OAKAUDIO_API int oakaudio_manager_push_to_output(OakAudioManager self,
		int rate, uint64_t layout, int format,
		const char *samples, int64_t samples_size,
		char *error_buf, int error_buf_size);

OAKAUDIO_API int oakaudio_manager_clear_buffered_output(OakAudioManager self);
OAKAUDIO_API int oakaudio_manager_stop_output(OakAudioManager self);

/**
 * @brief Seconds of audio consumed by the output device since the last
 *        reset, compensated for output latency; negative when no stream
 *        is running.
 */
OAKAUDIO_API int oakaudio_manager_seconds(OakAudioManager self, double *out);

OAKAUDIO_API int oakaudio_manager_reset_output_clock(OakAudioManager self);

/**
 * @brief Current output device index, paNoDevice (-1), or a negative
 *        OAKAUDIO_E_* code.
 */
OAKAUDIO_API int oakaudio_manager_get_output_device(OakAudioManager self);
OAKAUDIO_API int oakaudio_manager_set_output_device(OakAudioManager self,
		int device);
OAKAUDIO_API int oakaudio_manager_get_input_device(OakAudioManager self);
OAKAUDIO_API int oakaudio_manager_set_input_device(OakAudioManager self,
		int device);

/**
 * @brief Close the output stream and re-initialize PortAudio.
 */
OAKAUDIO_API int oakaudio_manager_hard_reset(OakAudioManager self);

/**
 * @brief Start recording the input device to a file via the oakcodec
 *        encoder C ABI.
 *
 * `params` must describe an audio-enabled encoding; the input stream is
 * always captured as interleaved 32-bit float (the only format the
 * oakcodec encoder write path accepts).
 *
 * @return OAKAUDIO_OK, OAKAUDIO_E_STATE (no input device), or
 *         OAKAUDIO_E_FAILED (see error_buf).
 */
OAKAUDIO_API int oakaudio_manager_start_recording(OakAudioManager self,
		const oakcodec_encoding_params *params,
		char *error_buf, int error_buf_size);

OAKAUDIO_API int oakaudio_manager_stop_recording(OakAudioManager self);

/**
 * @brief Device index named by the configuration ("AudioOutput" /
 *        "AudioInput"), or the default device when unset/unmatched.
 *        Static: valid without an instance (PortAudio must be initialized
 *        by an instance first; returns paNoDevice otherwise).
 */
OAKAUDIO_API int oakaudio_manager_find_config_device_by_name_s(
		int is_output_device);

/**
 * @brief Device index whose name matches `name` exactly (empty name
 *        matches nothing, falls through to the default device).
 */
OAKAUDIO_API int oakaudio_manager_find_device_by_name_s(const char *name,
		int is_output_device);

/**
 * @brief Number of live oakaudio reference-counted objects (leak check).
 */
OAKAUDIO_API int oakaudio_debug_alive_count(void);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_AUDIO_MANAGER_H
