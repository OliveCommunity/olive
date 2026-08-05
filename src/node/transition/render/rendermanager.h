#pragma once
// Transitional stub for engine/render/rendermanager.h (still Qt-based). Only
// the surface oaknode uses. M7 replaces this with the real oakrender
// boundary.
namespace olive {
class PreviewAutoCacher;
class RenderManager {
public:
	static RenderManager *instance() { return nullptr; }
	PreviewAutoCacher *get_cacher() { return nullptr; }
};
}
