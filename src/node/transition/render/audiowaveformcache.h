#pragma once
// Syntax-check stub only (not in repo).
#include <string>
#include <vector>
#include "olive/core/util/timerange.h"
#include "render/playbackcache.h"
namespace olive {
class Node;
class AudioWaveformCache : public PlaybackCache {
public:
	template <typename T> explicit AudioWaveformCache(T *) : PlaybackCache(this) {}
	void set_saving_enabled(bool) {}
};
}
