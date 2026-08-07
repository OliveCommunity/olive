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

OakRenderCacheImpl *impl(OakRenderCache c)
{
	return oakrender_c_api::to_native<OakRenderCacheImpl>(c);
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

OakRenderCache oakrender_cache_create(void)
{
	try {
		return oakrender_c_api::make_handle<OakRenderCache>(
			new OakRenderCacheImpl(), true,
			&oakrender_c_api::delete_as<OakRenderCacheImpl>);
	} catch (...) {
		return OakRenderCache{};
	}
}

void oakrender_cache_free(OakRenderCache *cache)
{
	oakrender_c_api::free_handle(cache);
}

OakRenderCache oakrender_cache_wrap_borrowed(void *native_cache)
{
	// Borrowed: the cache is owned by its node; releasing this handle
	// only frees the box.
	return oakrender_c_api::make_handle<OakRenderCache>(
		native_cache, false, nullptr);
}

int oakrender_cache_set_timebase(OakRenderCache cache, int num, int den)
{
	if (!cache.ctx || num <= 0 || den <= 0) {
		return OAKRENDER_E_INVALID;
	}
	impl(cache)->set_timebase(olive::Rational(num, den));
	return OAKRENDER_OK;
}

int oakrender_cache_set_uuid(OakRenderCache cache, const char *uuid)
{
	if (!cache.ctx || !uuid) {
		return OAKRENDER_E_INVALID;
	}
	impl(cache)->set_uuid(uuid);
	return OAKRENDER_OK;
}

void oakrender_cache_invalidate(OakRenderCache cache, int64_t in_ts,
								int64_t out_ts)
{
	if (!cache.ctx) {
		return;
	}
	OakRenderCacheImpl *c = impl(cache);
	c->invalidate(
		olive::core::TimeRange(ts_to_time(c, in_ts), ts_to_time(c, out_ts)));
}

void oakrender_cache_validate(OakRenderCache cache, int64_t in_ts,
							  int64_t out_ts)
{
	if (!cache.ctx) {
		return;
	}
	OakRenderCacheImpl *c = impl(cache);
	c->validate(
		olive::core::TimeRange(ts_to_time(c, in_ts), ts_to_time(c, out_ts)));
}

int oakrender_cache_has_validated_ranges(OakRenderCache cache)
{
	return cache.ctx && impl(cache)->has_validated_ranges() ? 1 : 0;
}

int oakrender_cache_indicator_height(void)
{
	return olive::PlaybackCache::get_cache_indicator_height();
}

int oakrender_frame_cache_load(OakRenderCache cache, const char *path,
							   const char *uuid, int64_t ts,
							   OakCodecFrame *out_frame)
{
	if (!cache.ctx || !path || !uuid || !out_frame) {
		return OAKRENDER_E_INVALID;
	}
	try {
		olive::FramePtr f =
			olive::FrameHashCache::load_cache_frame(path, uuid, ts);
		if (!f) {
			return OAKRENDER_E_NOT_FOUND;
		}
		auto *frame_impl = new OakCodecFrameImpl;
		frame_impl->ptr = std::move(f);
		*out_frame = oakrender_c_api::make_handle<OakCodecFrame>(
			frame_impl, true,
			&oakrender_c_api::delete_as<OakCodecFrameImpl>);
		return out_frame->ctx ? OAKRENDER_OK : OAKRENDER_E_NOMEM;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

void oakrender_frame_cache_save(OakRenderCache cache, const char *path,
								const char *uuid, OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!cache.ctx || !path || !uuid || !f || !f->ptr) {
		return;
	}
	try {
		OakRenderCacheImpl *c = impl(cache);
		olive::Rational tb = c->get_timebase();
		if (tb.isNull()) {
			tb = olive::Rational(1, 1);
		}
		olive::FrameHashCache::save_cache_frame(path, uuid,
												f->ptr->timestamp(), tb,
												f->ptr);
	} catch (...) {
	}
}

int oakrender_cache_get_invalidated_ranges(OakRenderCache cache,
		int64_t in_num, int64_t in_den, int64_t out_num, int64_t out_den,
		int64_t *ranges, int max_ranges)
{
	if (!cache.ctx || max_ranges < 0) {
		return OAKRENDER_E_INVALID;
	}

	try {
		olive::core::TimeRangeList list = impl(cache)->get_invalidated_ranges(
			olive::core::TimeRange(
				olive::core::Rational(int(in_num), int(in_den)),
				olive::core::Rational(int(out_num), int(out_den))));

		int count = int(list.size());
		if (ranges) {
			int written = 0;
			for (const olive::core::TimeRange &r : list) {
				if (written >= max_ranges) {
					break;
				}
				ranges[written * 4 + 0] = r.in().numerator();
				ranges[written * 4 + 1] = r.in().denominator();
				ranges[written * 4 + 2] = r.out().numerator();
				ranges[written * 4 + 3] = r.out().denominator();
				written++;
			}
		}
		return count;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}
