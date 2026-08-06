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

#ifndef OAK_EDITOR_CODEC_CONFORM_H
#define OAK_EDITOR_CODEC_CONFORM_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file conform.h
 * @brief C ABI for the oakcodec audio conform manager
 *        (olive::ConformManager): pcm waveform cache files used for fast
 *        audio scrubbing.
 *
 * Interim state (pre-M8): actual conform work is delegated to the global
 * task submit callback (see task.h). While no callback is registered,
 * state queries report OAKCODEC_CONFORM_UNAVAILABLE.
 */

#define OAKCODEC_CONFORM_EXISTS 0
#define OAKCODEC_CONFORM_GENERATING 1
#define OAKCODEC_CONFORM_UNAVAILABLE 2

/**
 * @brief Create the ConformManager singleton (no-op when it exists).
 */
OAKCODEC_API int oakcodec_conform_create_instance(void);

/**
 * @brief Destroy the ConformManager singleton (no-op when absent).
 */
OAKCODEC_API int oakcodec_conform_destroy_instance(void);

/**
 * @brief Query the conform state of one audio stream, starting the
 * conform when needed and possible.
 *
 * Addresses the source by filename/stream_index and the target audio
 * format by sample_rate/channel_layout/sample_format
 * (olive::core::SampleFormat::Format as int).
 *
 * When the conform files do not exist and a task submit callback is
 * registered (task.h), the conform is submitted synchronously and the
 * filesystem is re-checked; `wait` only controls whether a post-submit
 * miss is reported as OAKCODEC_CONFORM_UNAVAILABLE (wait != 0) or
 * OAKCODEC_CONFORM_GENERATING (wait == 0). Without a registrar the
 * result is always OAKCODEC_CONFORM_UNAVAILABLE.
 *
 * @return One of OAKCODEC_CONFORM_* (non-negative), or a negative
 *         OAKCODEC_E_* code for invalid arguments.
 */
OAKCODEC_API int oakcodec_conform_get_state(const char *cache_path,
							   const char *source_filename, int stream_index,
							   int sample_rate, uint64_t channel_layout,
							   int sample_format, int wait);

/**
 * @brief Number of conform (pcm) files for the given stream/params — one
 * per channel; 0 on invalid arguments.
 */
OAKCODEC_API int oakcodec_conform_filename_count(const char *cache_path,
								const char *source_filename, int stream_index,
								int sample_rate, uint64_t channel_layout,
								int sample_format);

/**
 * @brief The `index`-th conform filename (buf/size getter).
 *
 * @return Required buffer size including NUL (non-negative), or a
 *         negative OAKCODEC_E_* code (OAKCODEC_E_NOT_FOUND when index is
 *         out of range).
 */
OAKCODEC_API int oakcodec_conform_filename_at(const char *cache_path,
								 const char *source_filename,
								 int stream_index, int sample_rate,
								 uint64_t channel_layout, int sample_format,
								 int index, char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_CODEC_CONFORM_H
