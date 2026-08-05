#pragma once
// Transitional stub for engine/render/framehashcache.h (still Qt-based).
// Only the surface oaknode uses. M7 replaces this with the real oakrender
// boundary.
#include <mutex>
#include <string>
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"
#include "render/playbackcache.h"
namespace olive {
class Node;
class FrameHashCache : public PlaybackCache {
public:
	template <typename T> explicit FrameHashCache(T *) : PlaybackCache(this) {}
	const core::Rational &get_timebase() const { return timebase_; }
	void set_timebase(const core::Rational &t) { timebase_ = t; }
	std::mutex &mutex() { return mutex_; }
	void load_state() {}
	std::string get_valid_cache_filename(const core::Rational &) const
	{
		return std::string();
	}

private:
	core::Rational timebase_;
	std::mutex mutex_;
};
class ThumbnailCache : public FrameHashCache {
public:
	template <typename T> explicit ThumbnailCache(T *) : FrameHashCache(this) {}
};
}
