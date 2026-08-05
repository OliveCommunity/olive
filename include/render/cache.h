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

#ifndef OAK_EDITOR_RENDER_CACHE_H
#define OAK_EDITOR_RENDER_CACHE_H

#include <stdint.h>

// Same-dir quoted includes: inside this build the engine-style spelling
// "render/renderer.h" resolves to the transition bridge headers, so the
// public headers reference each other relative to their own directory.
#include "error.h"
#include "renderer.h" /* OakCodecFrame */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cache.h
 * @brief C ABI for the oakrender playback/frame-hash caches
 *        (olive::PlaybackCache / olive::FrameHashCache), M7 §2.2.
 *
 * An OakRenderCache IS a reinterpreted olive::FrameHashCache (created
 * without a parent node), no wrapper allocation. Handles from
 * oakrender_cache_create() are owned by the caller and must be released
 * with oakrender_cache_free().
 *
 * All timestamps are int64 frame numbers in the cache's timebase (see
 * oakrender_cache_set_timebase()); a cache without a valid timebase
 * treats timestamps as whole seconds.
 *
 * No cache events cross the boundary (M7 §2.2, 2026-08 revision):
 * invalidate/validate are triggered by and known to the caller; the
 * facade re-emits notifications after the triggering command.
 */
typedef struct OakRenderCache OakRenderCache;

/**
 * @brief Create a detached frame hash cache (no parent node, no
 * timebase). Owned by the caller.
 *
 * @return Cache handle, or NULL on allocation failure.
 */
OakRenderCache *oakrender_cache_create(void);

/** @brief Destroy a cache created by oakrender_cache_create(). NULL-safe. */
void oakrender_cache_free(OakRenderCache *cache);

/**
 * @brief Set the frame timebase used to interpret all timestamps of this
 * cache (FrameHashCache::set_timebase()).
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for NULL cache or
 *         non-positive num/den.
 */
int oakrender_cache_set_timebase(OakRenderCache *cache, int num, int den);

/**
 * @brief Set the cache UUID used in on-disk frame cache filenames
 * (PlaybackCache::set_uuid()).
 *
 * @return OAKRENDER_OK or OAKRENDER_E_INVALID.
 */
int oakrender_cache_set_uuid(OakRenderCache *cache, const char *uuid);

/**
 * @brief Mark the timestamp range [in_ts, out_ts) invalidated
 * (PlaybackCache::invalidate()). NULL cache is a no-op.
 */
void oakrender_cache_invalidate(OakRenderCache *cache, int64_t in_ts,
								int64_t out_ts);

/**
 * @brief Mark the timestamp range [in_ts, out_ts) validated
 * (PlaybackCache::validate()). NULL cache is a no-op.
 */
void oakrender_cache_validate(OakRenderCache *cache, int64_t in_ts,
							  int64_t out_ts);

/**
 * @brief 1 when the cache holds any validated range
 * (PlaybackCache::has_validated_ranges()), 0 otherwise / on NULL.
 */
int oakrender_cache_has_validated_ranges(const OakRenderCache *cache);

/**
 * @brief Timeline cache indicator height in pixels
 * (PlaybackCache::get_cache_indicator_height()). Constant query.
 */
int oakrender_cache_indicator_height(void);

/**
 * @brief Load a cached frame from disk
 * (FrameHashCache::load_cache_frame(cache_path, uuid, ts)).
 *
 * @param path Cache directory (e.g. oakrender_disk_cache_path()).
 * @param uuid Cache UUID of the producing node.
 * @param out_frame Receives an owned frame handle (release with
 *        oakrender_codec_frame_free()).
 *
 * @return OAKRENDER_OK, OAKRENDER_E_INVALID (NULL argument), or
 *         OAKRENDER_E_NOT_FOUND (no cached frame at `ts` / undecodable).
 */
int oakrender_frame_cache_load(OakRenderCache *cache, const char *path,
							   const char *uuid, int64_t ts,
							   OakCodecFrame **out_frame);

/**
 * @brief Save a frame to the disk cache under the cache's timebase and
 * the frame's own timestamp (FrameHashCache::save_cache_frame()).
 * NULL arguments are a no-op.
 */
void oakrender_frame_cache_save(OakRenderCache *cache, const char *path,
								const char *uuid, const OakCodecFrame *frame);

/* ---- Debug --------------------------------------------------------------- */

/**
 * @brief Number of live oakrender-owned objects (caches, textures,
 * frames, color processors) for leak assertions in tests.
 */
int oakrender_debug_alive_count(void);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_RENDER_CACHE_H
