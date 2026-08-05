#pragma once
// Syntax-check stub only (not in repo). Minimal de-Qt shape of engine PlaybackCache.
#include <string>
#include <vector>
#include "olive/core/util/timerange.h"
namespace olive {
class Node;
class ViewerOutput;
class PlaybackCache {
public:
	template <typename T> explicit PlaybackCache(T *) {}
	virtual ~PlaybackCache() {}
	void set_uuid(const std::string &) {}
	std::string get_uuid() const { return {}; }
	core::TimeRangeList get_invalidated_ranges(core::TimeRange intersecting) const
	{
		(void) intersecting;
		return {};
	}
	core::TimeRangeList get_invalidated_ranges(const core::Rational &length) const
	{
		return get_invalidated_ranges(core::TimeRange(0, length));
	}
	void invalidate(const core::TimeRange &) {}
	void invalidate_all() {}
	void request(ViewerOutput *, const core::TimeRange &) {}
	virtual void set_passthrough(PlaybackCache *) {}
	class Passthrough : public core::TimeRange {
	public:
		Passthrough(const core::TimeRange &r, PlaybackCache *cache = nullptr)
			: core::TimeRange(r)
			, cache_(cache)
		{
		}
		PlaybackCache *cache() const { return cache_; }

	private:
		PlaybackCache *cache_;
	};
	const std::vector<Passthrough> &get_passthroughs() const { return passthroughs_; }

private:
	std::vector<Passthrough> passthroughs_;
};
}
