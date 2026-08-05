#pragma once
// Transitional stub for engine/pluginSupport/olivehost.h (still Qt-based).
// Only the surface oaknode uses. M9 replaces this with the real plugin
// boundary.
#include <string>
#include "ofxhImageEffectAPI.h"
#include "ofxhPluginCache.h"
namespace olive {
namespace plugin {
inline void load_plugins(const std::string &) {}
}
}
