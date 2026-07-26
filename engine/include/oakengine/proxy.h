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

#ifndef OAKENGINE_PROXY_H
#define OAKENGINE_PROXY_H

#include <stdint.h>

#include "export.h"
#include "footage.h"
#include "init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file proxy.h
 * @brief C ABI for the engine's proxy generation singleton (olive::ProxyManager)
 *
 * A thin facade over ProxyManager's instance lifecycle, proxy parameter
 * configuration, proxy state queries and proxy generation. The opaque task
 * handle returned in oak_proxy_result::task is a borrowed pointer to the
 * engine's internal ProxyTask; it is intended only for logging and becomes
 * invalid when the proxy operation finishes.
 *
 * Conventions match the other facade families:
 *   - 0 (OAKENGINE_OK) / negative OAKENGINE_E_* codes.
 *   - String output uses the buf/size convention.
 *   - Booleans are int (1/0).
 */

#define OAKENGINE_PROXY_STATE_MISSING 0
#define OAKENGINE_PROXY_STATE_GENERATING 1
#define OAKENGINE_PROXY_STATE_READY 2
#define OAKENGINE_PROXY_STATE_FAILED 3

typedef struct oak_proxy_result {
    int state;       /**< OAKENGINE_PROXY_STATE_* */
    char filename[1024];
    int64_t task;    /**< ProxyTask* as opaque handle, or 0 if none */
} oak_proxy_result;

/**
 * @brief Create the ProxyManager singleton.
 *
 * Safe to call when the instance already exists (no-op). Returns
 * OAKENGINE_OK or OAKENGINE_E_FAILED.
 */
OAKENGINE_API int oakengine_proxy_create_instance(void);

/**
 * @brief Destroy the ProxyManager singleton.
 *
 * Safe to call when no instance exists (no-op). Returns OAKENGINE_OK.
 */
OAKENGINE_API int oakengine_proxy_destroy_instance(void);

/**
 * @brief Build proxy parameters from the global application config.
 *
 * Fills `out` with the configured width/height/divider/version/crf/extension
 * /preset/include_audio values. Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_proxy_params_from_config(oak_proxy_params *out);

/**
 * @brief Query the state of a proxy file on disk.
 *
 * Returns one of the OAKENGINE_PROXY_STATE_* values, or
 * OAKENGINE_PROXY_STATE_MISSING if `proxy_filename` is NULL/empty or the
 * proxy does not exist.
 */
OAKENGINE_API int oakengine_proxy_get_state(const char *proxy_filename);

/**
 * @brief Human-readable string for a proxy state (buf/size convention).
 *
 * Returns the string length on success, or a negative OAKENGINE_E_* code for
 * an unknown state.
 */
OAKENGINE_API int oakengine_proxy_state_to_string(int state, char *buf,
                                                  int buf_size);

/**
 * @brief Get or start generating a proxy for `source_filename`.
 *
 * `cache_path` is the project cache directory. `stream_index` is the source
 * stream to proxy. `params` are the proxy generation parameters (width/height
 * etc.). On return `out->state` and `out->filename` describe the proxy; if a
 * generation task was started, `out->task` is a borrowed opaque handle to it,
 * otherwise it is 0.
 */
OAKENGINE_API int oakengine_proxy_get_or_start(const char *cache_path,
                                               const char *source_filename,
                                               int stream_index,
                                               const oak_proxy_params *params,
                                               oak_proxy_result *out);

/**
 * @brief Get the "working" filename for a proxy file (buf/size convention).
 *
 * The working filename is used by the proxy generator while the proxy is being
 * generated. Returns the string length on success, or a negative
 * OAKENGINE_E_* code on error.
 */
OAKENGINE_API int oakengine_proxy_get_working_filename(const char *proxy_filename,
                                                       char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_PROXY_H */
