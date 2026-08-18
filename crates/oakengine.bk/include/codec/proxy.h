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

#ifndef OAK_EDITOR_CODEC_PROXY_H
#define OAK_EDITOR_CODEC_PROXY_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file proxy.h
 * @brief C ABI for the oakcodec proxy generation singleton
 *        (olive::ProxyManager).
 *
 * Interim state (pre-M8): actual transcodes are delegated to the global
 * task submit callback (see task.h). While no callback is registered,
 * oakcodec_proxy_get_or_start() reports the proxy as missing instead of
 * starting background work.
 */

#define OAKCODEC_PROXY_STATE_MISSING 0
#define OAKCODEC_PROXY_STATE_GENERATING 1
#define OAKCODEC_PROXY_STATE_READY 2
#define OAKCODEC_PROXY_STATE_FAILED 3

/**
 * @brief POD proxy generation parameters (olive::ProxyManager::ProxyParams).
 *
 * divider: source resolution divider (1 = use absolute width/height,
 * 2/4/8 = fraction of the source resolution). extension/preset are the
 * ffmpeg output container and encoder preset (e.g. "mp4"/"veryfast").
 */
typedef struct oakcodec_proxy_params {
	int width;
	int height;
	int divider;
	int version;
	int crf;
	int include_audio; /**< 1/0. */
	char extension[32];
	char preset[32];
} oakcodec_proxy_params;

typedef struct oakcodec_proxy_result {
	int state; /**< OAKCODEC_PROXY_STATE_* */
	char filename[1024];
} oakcodec_proxy_result;

/**
 * @brief Create the ProxyManager singleton (no-op when it exists).
 */
OAKCODEC_API int oakcodec_proxy_create_instance(void);

/**
 * @brief Destroy the ProxyManager singleton (no-op when absent).
 */
OAKCODEC_API int oakcodec_proxy_destroy_instance(void);

/**
 * @brief Compiled-in default proxy parameters (1280x720, divider 1, mp4,
 * crf 23, "veryfast", audio included). Interim state: until the config
 * milestone wires a real store these do not reflect user settings.
 */
OAKCODEC_API int oakcodec_proxy_params_default(oakcodec_proxy_params *out);

/**
 * @brief State of a proxy file on disk (OAKCODEC_PROXY_STATE_*;
 * OAKCODEC_PROXY_STATE_MISSING for NULL/empty/absent).
 */
OAKCODEC_API int oakcodec_proxy_get_state(const char *proxy_filename);

/** @brief Human-readable string for a proxy state (buf/size getter). */
OAKCODEC_API int oakcodec_proxy_state_to_string(int state, char *buf, int buf_size);

/** @brief Proxy directory for a project cache path (buf/size getter). */
OAKCODEC_API int oakcodec_proxy_get_proxy_directory(const char *cache_path, char *buf,
								   int buf_size);

/**
 * @brief Deterministic proxy filename for a source stream (buf/size
 * getter).
 */
OAKCODEC_API int oakcodec_proxy_get_proxy_filename(const char *cache_path,
								  const char *source_filename,
								  int stream_index,
								  const oakcodec_proxy_params *params,
								  char *buf, int buf_size);

/** @brief Working (in-progress) filename of a proxy (buf/size getter). */
OAKCODEC_API int oakcodec_proxy_get_working_filename(const char *proxy_filename,
									char *buf, int buf_size);

/**
 * @brief Get or start generating a proxy for `source_filename`.
 *
 * `cache_path` is the project cache directory. On return `out->state`
 * and `out->filename` describe the proxy. When a task submit callback is
 * registered (task.h) and no proxy exists, generation is submitted
 * synchronously before the state is re-derived; without a registrar the
 * state stays OAKCODEC_PROXY_STATE_MISSING.
 */
OAKCODEC_API int oakcodec_proxy_get_or_start(const char *cache_path,
							const char *source_filename, int stream_index,
							const oakcodec_proxy_params *params,
							oakcodec_proxy_result *out);

/**
 * @brief Locate an ffmpeg executable for proxy generation (buf/size
 * getter; empty string when none is found).
 */
OAKCODEC_API int oakcodec_proxy_find_ffmpeg(const char *configured_path, char *buf,
							   int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_CODEC_PROXY_H
