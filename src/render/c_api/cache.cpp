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

#include "../../../include/render/cache.h"

#include <atomic>
#include <new>

#include "alivecount.h"
#include "internalhandles.h"

#include "framehashcache.h"
#include "playbackcache.h"

namespace
{

std::atomic<int> g_alive_count(0);

/**
 * @brief FrameHashCache with the protected PlaybackCache::validate()
 *        exposed for the ABI. Adds no data members, so a handle created
 *        as OakRenderCacheImpl reinterpret-casts safely both ways.
 */
class OakRenderCacheImpl : public olive::FrameHashCache {
public:
	OakRenderCacheImpl()
		: olive::FrameHashCache(nullptr)
	{
	}

	using olive::PlaybackCache::validate;
};

OakRenderCacheImpl *impl(OakRenderCache *c)
{
	return reinterpret_cast<OakRenderCacheImpl *>(c);
}

const OakRenderCacheImpl *impl(const OakRenderCache *c)
{
	return reinterpret_cast<const OakRenderCacheImpl *>(c);
}

/**
 * @brief Convert an int64 timestamp to a time using the cache's
 *        timebase; timestamps are whole seconds when no valid timebase
 *        is set.
 */
olive::Rational ts_to_time(const OakRenderCacheImpl *c, int64_t ts)
{
	const olive::Rational &tb = c->get_timebase();
	if (tb.isNull()) {
		return olive::Rational::from_double(double(ts));
	}
	return olive::core::Timecode::timestamp_to_time(ts, tb);
}

} // namespace

namespace oakrender_c_api
{

void alive_inc()
{
	g_alive_count.fetch_add(1, std::memory_order_relaxed);
}

void alive_dec()
{
	g_alive_count.fetch_sub(1, std::memory_order_relaxed);
}

}

int oakrender_debug_alive_count(void)
{
	return g_alive_count.load(std::memory_order_relaxed);
}

OakRenderCache *oakrender_cache_create(void)
{
	try {
		auto *c = new OakRenderCacheImpl();
		oakrender_c_api::alive_inc();
		return reinterpret_cast<OakRenderCache *>(c);
	} catch (...) {
		return nullptr;
	}
}

void oakrender_cache_free(OakRenderCache *cache)
{
	if (!cache) {
		return;
	}
	delete impl(cache);
	oakrender_c_api::alive_dec();
}

int oakrender_cache_set_timebase(OakRenderCache *cache, int num, int den)
{
	if (!cache || num <= 0 || den <= 0) {
		return OAKRENDER_E_INVALID;
	}
	impl(cache)->set_timebase(olive::Rational(num, den));
	return OAKRENDER_OK;
}

int oakrender_cache_set_uuid(OakRenderCache *cache, const char *uuid)
{
	if (!cache || !uuid) {
		return OAKRENDER_E_INVALID;
	}
	impl(cache)->set_uuid(uuid);
	return OAKRENDER_OK;
}

void oakrender_cache_invalidate(OakRenderCache *cache, int64_t in_ts,
								int64_t out_ts)
{
	if (!cache) {
		return;
	}
	OakRenderCacheImpl *c = impl(cache);
	c->invalidate(
		olive::core::TimeRange(ts_to_time(c, in_ts), ts_to_time(c, out_ts)));
}

void oakrender_cache_validate(OakRenderCache *cache, int64_t in_ts,
							  int64_t out_ts)
{
	if (!cache) {
		return;
	}
	OakRenderCacheImpl *c = impl(cache);
	c->validate(
		olive::core::TimeRange(ts_to_time(c, in_ts), ts_to_time(c, out_ts)));
}

int oakrender_cache_has_validated_ranges(const OakRenderCache *cache)
{
	return cache && impl(cache)->has_validated_ranges() ? 1 : 0;
}

int oakrender_cache_indicator_height(void)
{
	return olive::PlaybackCache::get_cache_indicator_height();
}

int oakrender_frame_cache_load(OakRenderCache *cache, const char *path,
							   const char *uuid, int64_t ts,
							   OakCodecFrame **out_frame)
{
	if (!cache || !path || !uuid || !out_frame) {
		return OAKRENDER_E_INVALID;
	}
	try {
		olive::FramePtr f =
			olive::FrameHashCache::load_cache_frame(path, uuid, ts);
		if (!f) {
			return OAKRENDER_E_NOT_FOUND;
		}
		auto *block = new OakCodecFrame;
		block->ptr = std::move(f);
		oakrender_c_api::alive_inc();
		*out_frame = block;
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

void oakrender_frame_cache_save(OakRenderCache *cache, const char *path,
								const char *uuid, const OakCodecFrame *frame)
{
	if (!cache || !path || !uuid || !frame || !frame->ptr) {
		return;
	}
	try {
		OakRenderCacheImpl *c = impl(cache);
		olive::Rational tb = c->get_timebase();
		if (tb.isNull()) {
			tb = olive::Rational(1, 1);
		}
		olive::FrameHashCache::save_cache_frame(path, uuid,
												frame->ptr->timestamp(), tb,
												frame->ptr);
	} catch (...) {
	}
}
