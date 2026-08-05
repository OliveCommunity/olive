#pragma once
// Syntax-check stub only (not in repo).
#include <string>
#include "olive/core/util/timerange.h"
#include "olive/core/render/audioparams.h"
namespace olive { using core::AudioParams; }
#include "render/playbackcache.h"
namespace olive {
class Node;
class AudioPlaybackCache : public PlaybackCache {
public:
	template <typename T> explicit AudioPlaybackCache(T *) : PlaybackCache(this) {}
};
}
