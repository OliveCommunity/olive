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

#ifndef OAK_EDITOR_RENDER_MANAGER_H
#define OAK_EDITOR_RENDER_MANAGER_H

#include <stdint.h>

// See cache.h for why these are same-dir relative includes.
#include "node/node.h" /* OakNodeNode (by-value handle) */
#include "cache.h" /* OakCodecFrame */
#include "color.h" /* OakColorProcessor */
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file manager.h
 * @brief C ABI for the oakrender render manager / preview auto-cacher /
 *        disk cache singletons (olive::RenderManager,
 *        olive::PreviewAutoCacher, olive::DiskManager), M7 §2.4.
 *
 * The render manager is a process-wide singleton gated by
 * oakrender_manager_init() / oakrender_manager_shutdown(). Functions
 * that need it return OAKRENDER_E_STATE when it is not up.
 *
 * The frame request callback is the asynchronous command return channel
 * (M7 §2.2 note): it fires on a render worker thread, possibly after
 * cancellation. The delivered OakCodecFrame is owned by the callback
 * recipient (release with oakrender_codec_frame_free()); an empty frame
 * (ctx == NULL) signals "no result" (cancelled or failed). Beyond this
 * callback there are no event subscription interfaces.
 */

/**
 * @brief Create the RenderManager singleton (spawns render/audio
 * threads, loads the configured backend).
 *
 * @return OAKRENDER_OK, OAKRENDER_E_STATE (already initialized), or
 *         OAKRENDER_E_FAILED.
 */
int oakrender_manager_init(void);

/**
 * @brief Destroy the RenderManager singleton. No-op when not
 * initialized.
 */
void oakrender_manager_shutdown(void);

/**
 * @brief Completion callback of an asynchronous frame request.
 *
 * @param frame Owned frame handle, or an empty handle (ctx == NULL)
 *        when the request finished without a result (cancelled/failed).
 * @param ts The request's timestamp, passed back verbatim.
 */
typedef void (*oakrender_frame_ready_fn)(OakCodecFrame frame, int64_t ts,
										 void *userdata);

/**
 * @brief Asynchronously render one frame of `viewer` at `ts`
 * (PreviewAutoCacher::get_single_frame()).
 *
 * `ts` is a frame number in the viewer node's video timebase (a whole
 * second count when the viewer carries no valid timebase). The
 * completion is delivered through `cb`; until then the request can be
 * cancelled with oakrender_cancel_request().
 *
 * @return A positive request id, or a negative OAKRENDER_E_* code
 *         (OAKRENDER_E_INVALID for an empty viewer handle or NULL
 *         callback, OAKRENDER_E_STATE when the manager is not
 *         initialized, OAKRENDER_E_FAILED when no ticket could be
 *         created).
 */
int64_t oakrender_request_frame(OakNodeNode viewer, int64_t ts,
								oakrender_frame_ready_fn cb, void *userdata);

/**
 * @brief Cancel a pending frame request. The callback still fires with a
 * NULL frame.
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_NOT_FOUND for an unknown id.
 */
int oakrender_cancel_request(int64_t request_id);

/**
 * @brief Set the multicam node on the manager's auto-cacher
 * (PreviewAutoCacher::set_multicam_node()). `multicam_or_NULL` is a
 * borrowed oaknode handle to a MultiCamNode (empty handle to clear).
 *
 * @return OAKRENDER_OK or OAKRENDER_E_STATE.
 */
int oakrender_set_cacher_multicam(OakNodeNode multicam_or_NULL);

/**
 * @brief Set the display color processor on the manager's auto-cacher
 * (PreviewAutoCacher::set_display_color_processor()). Borrowed handle,
 * empty ctx to clear.
 *
 * @return OAKRENDER_OK or OAKRENDER_E_STATE.
 */
int oakrender_set_display_color_processor(OakColorProcessor p_or_NULL);

/**
 * @brief 1 when the process-wide RenderManager singleton exists
 *        (RenderManager::instance() != nullptr; only the main GUI
 *        process creates one), 0 otherwise.
 */
int oakrender_manager_available(void);

/**
 * @brief Cancel in-flight video cache tasks on the manager's
 *        auto-cacher (PreviewAutoCacher::cancel_video_tasks()). No-op
 *        when no manager/auto-cacher exists (e.g. a worker process).
 */
void oakrender_cancel_video_tasks(int wait_for_done);

/* ---- Disk cache (olive::DiskManager) -------------------------------------- */

/**
 * @brief The default disk cache directory
 * (DiskManager::get_default_disk_cache_path()). Two-stage string getter:
 * returns the required buffer size including NUL; pass buf == NULL or
 * too small a buffer to query the size. Does not require the manager.
 */
int oakrender_disk_cache_path(char *buf, int n);

/**
 * @brief Bytes currently consumed by the default disk cache folder.
 * Lazily creates the DiskManager singleton on first use.
 *
 * @return Consumption in bytes (>= 0), or OAKRENDER_E_FAILED.
 */
int64_t oakrender_disk_cache_size(void);

/**
 * @brief Clear the default disk cache folder
 * (DiskManager::clear_disk_cache()).
 *
 * @return OAKRENDER_OK or OAKRENDER_E_FAILED.
 */
int oakrender_disk_cache_clear(void);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_RENDER_MANAGER_H
