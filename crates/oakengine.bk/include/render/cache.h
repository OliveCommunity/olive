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
#include "node/node.h" /* OakNodeNode */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cache.h
 * @brief C ABI for the oakrender playback/frame-hash caches
 *        (olive::PlaybackCache / olive::FrameHashCache), M7 §2.2.
 *
 * An OakRenderCache is a by-value reference-counted handle (shared_ptr
 * semantics, see oakcommon's common/handle.h) boxing an
 * olive::FrameHashCache (created without a parent node). Handles from
 * oakrender_cache_create() are owned by the caller (reference count 1)
 * and must be released with oakrender_cache_free(); handles from
 * oakrender_cache_wrap_borrowed() are borrowed (release only frees the
 * box).
 *
 * All timestamps are int64 frame numbers in the cache's timebase (see
 * oakrender_cache_set_timebase()); a cache without a valid timebase
 * treats timestamps as whole seconds.
 *
 * No cache events cross the boundary (M7 §2.2, 2026-08 revision):
 * invalidate/validate are triggered by and known to the caller; the
 * facade re-emits notifications after the triggering command.
 */
typedef struct OakRenderCache {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKRENDER_ABI_VERSION. */
} OakRenderCache;

/**
 * @brief Create a detached frame hash cache (no parent node, no
 * timebase). Owned by the caller.
 *
 * @return Cache handle with reference count 1; ctx is NULL on
 *         allocation failure.
 */
OakRenderCache oakrender_cache_create(void);

/**
 * @brief Release one reference to a cache created by
 * oakrender_cache_create(). Convenience wrapper around
 * cache->release(cache->ctx). NULL / empty-handle no-op; clears
 * cache->ctx after releasing.
 */
void oakrender_cache_free(OakRenderCache *cache);

/**
 * @brief Borrowed handle wrapping a native frame cache pointer obtained
 * through oaknode (oaknode_node_get_video_frame_cache()).
 *
 * The cache itself stays owned by its node: release() on this handle
 * only frees the box. Empty handle (ctx == NULL) for a NULL native
 * pointer.
 */
OakRenderCache oakrender_cache_wrap_borrowed(void *native_cache);

/**
 * @brief Cache flavours owned by a node
 * (olive::Node's video/thumbnail/audio/waveform caches).
 */
enum OakRenderCacheKind {
	OAKRENDER_CACHE_VIDEO_FRAME = 0, /**< olive::FrameHashCache */
	OAKRENDER_CACHE_THUMBNAIL = 1, /**< olive::ThumbnailCache */
	OAKRENDER_CACHE_AUDIO_PLAYBACK = 2, /**< olive::AudioPlaybackCache */
	OAKRENDER_CACHE_AUDIO_WAVEFORM = 3 /**< olive::AudioWaveformCache */
};

/**
 * @brief Create a cache of the given kind with a parent node (the
 *        native back-pointer stays inside oakrender; it is used for
 *        project cache-path resolution and job bookkeeping only).
 *
 * Owned by the caller (reference count 1); release with
 * oakrender_cache_free(). Empty handle for an empty parent handle, an
 * unknown kind, or on allocation failure.
 */
OakRenderCache oakrender_cache_create_for_node(OakNodeNode parent,
											   int kind);

/**
 * @brief Cache UUID as canonical text, two-stage
 *        (PlaybackCache::get_uuid()).
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 *         OAKRENDER_E_* code for an empty cache.
 */
int oakrender_cache_get_uuid(OakRenderCache cache, char *buf,
							 int buf_size);

/**
 * @brief Request caching of a time range on behalf of a viewer
 *        (PlaybackCache::request()).
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for an empty cache /
 *         context handle or a context that is not a viewer.
 */
int oakrender_cache_request(OakRenderCache cache, OakNodeNode context,
							int64_t in_num, int64_t in_den,
							int64_t out_num, int64_t out_den);

/**
 * @brief Load/save the cache's on-disk state (PlaybackCache::load_state()
 *        / save_state()). OAKRENDER_E_INVALID for an empty cache.
 */
int oakrender_cache_load_state(OakRenderCache cache);
int oakrender_cache_save_state(OakRenderCache cache);

/**
 * @brief Enable/disable persisting this cache
 *        (PlaybackCache::set_saving_enabled()).
 */
int oakrender_cache_set_saving_enabled(OakRenderCache cache, int enabled);

/**
 * @brief Pass this cache's ranges through to another cache
 *        (PlaybackCache::set_passthrough()). OAKRENDER_E_INVALID for an
 *        empty cache or an empty `other`.
 */
int oakrender_cache_set_passthrough(OakRenderCache cache,
									OakRenderCache other);

/**
 * @brief The on-disk filename for the frame at a time
 *        (FrameHashCache::get_valid_cache_filename()), two-stage.
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 *         OAKRENDER_E_* code (OAKRENDER_E_INVALID when the cache is not
 *         a frame hash cache).
 */
int oakrender_cache_get_valid_cache_filename(OakRenderCache cache,
											 int64_t time_num,
											 int64_t time_den, char *buf,
											 int buf_size);

/**
 * @brief The passthrough ranges as flat {in_n, in_d, out_n, out_d}
 *        quadruples (PlaybackCache::get_passthroughs(); only the ranges
 *        cross the boundary, the per-range cache UUID text stays
 *        internal).
 *
 * Two-stage: call with ranges == NULL (or max_ranges == 0) to get the
 * count; then call with a buffer of max_ranges * 4 int64_t values.
 *
 * @return Range count (>= 0), or a negative OAKRENDER_E_* code.
 */
int oakrender_cache_get_passthroughs(OakRenderCache cache, int64_t *ranges,
									 int max_ranges);

/**
 * @brief The cache's frame timebase (FrameHashCache::get_timebase()).
 *        Out params may individually be NULL. OAKRENDER_E_INVALID for
 *        an empty cache or a non-frame-hash cache.
 */
int oakrender_cache_get_timebase(OakRenderCache cache, int *num,
								 int *den);

/**
 * @brief Lock/unlock the cache's internal mutex (PlaybackCache::mutex()).
 *        Empty cache is a no-op. Always pair the calls.
 */
void oakrender_cache_lock(OakRenderCache cache);
void oakrender_cache_unlock(OakRenderCache cache);

#ifdef __cplusplus
} /* extern "C" */

namespace olive { class PlaybackCache; }

extern "C" {
#endif

/**
 * @brief Borrowed access to the underlying C++ cache (C++ only, for
 *        oakrender-internal adapters such as PreviewAutoCacher). Valid
 *        while the handle is held. NULL-safe.
 */
olive::PlaybackCache *oakrender_cache_get_native(OakRenderCache cache);

/**
 * @brief Set the frame timebase used to interpret all timestamps of this
 * cache (FrameHashCache::set_timebase()).
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for an empty cache or
 *         non-positive num/den.
 */
int oakrender_cache_set_timebase(OakRenderCache cache, int num, int den);

/**
 * @brief Set the cache UUID used in on-disk frame cache filenames
 * (PlaybackCache::set_uuid()).
 *
 * @return OAKRENDER_OK or OAKRENDER_E_INVALID.
 */
int oakrender_cache_set_uuid(OakRenderCache cache, const char *uuid);

/**
 * @brief Mark the timestamp range [in_ts, out_ts) invalidated
 * (PlaybackCache::invalidate()). Empty cache is a no-op.
 */
void oakrender_cache_invalidate(OakRenderCache cache, int64_t in_ts,
								int64_t out_ts);

/**
 * @brief Mark a rational time range invalidated
 *        (PlaybackCache::invalidate(TimeRange)). Empty cache is a no-op.
 */
void oakrender_cache_invalidate_range(OakRenderCache cache,
									  int64_t in_num, int64_t in_den,
									  int64_t out_num, int64_t out_den);

/**
 * @brief Mark the timestamp range [in_ts, out_ts) validated
 * (PlaybackCache::validate()). Empty cache is a no-op.
 */
void oakrender_cache_validate(OakRenderCache cache, int64_t in_ts,
							  int64_t out_ts);

/**
 * @brief 1 when the cache holds any validated range
 * (PlaybackCache::has_validated_ranges()), 0 otherwise / empty.
 */
int oakrender_cache_has_validated_ranges(OakRenderCache cache);

/**
 * @brief Timeline cache indicator height in pixels
 * (PlaybackCache::get_cache_indicator_height()). Constant query.
 */
int oakrender_cache_indicator_height(void);

/**
 * @brief The invalidated sub-ranges of [in, out) as flat
 *        {in_n, in_d, out_n, out_d} quadruples
 *        (PlaybackCache::get_invalidated_ranges()).
 *
 * Two-stage: call with ranges == NULL (or max_ranges == 0) to get the
 * count; then call with a buffer of max_ranges * 4 int64_t values.
 *
 * @return Range count (>= 0), or a negative OAKRENDER_E_* code.
 */
int oakrender_cache_get_invalidated_ranges(OakRenderCache c,
		int64_t in_num, int64_t in_den, int64_t out_num, int64_t out_den,
		int64_t *ranges, int max_ranges);

/**
 * @brief Load a cached frame from disk
 * (FrameHashCache::load_cache_frame(cache_path, uuid, ts)).
 *
 * @param path Cache directory (e.g. oakrender_disk_cache_path()).
 * @param uuid Cache UUID of the producing node.
 * @param out_frame Receives an owned frame handle (release with
 *        oakrender_codec_frame_free()).
 *
 * @return OAKRENDER_OK, OAKRENDER_E_INVALID (empty/NULL argument), or
 *         OAKRENDER_E_NOT_FOUND (no cached frame at `ts` / undecodable).
 */
int oakrender_frame_cache_load(OakRenderCache cache, const char *path,
							   const char *uuid, int64_t ts,
							   OakCodecFrame *out_frame);

/**
 * @brief Save a frame to the disk cache under the cache's timebase and
 * the frame's own timestamp (FrameHashCache::save_cache_frame()).
 * Empty/NULL arguments are a no-op.
 */
void oakrender_frame_cache_save(OakRenderCache cache, const char *path,
								const char *uuid, OakCodecFrame frame);

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
