#pragma once
#include <memory>
#include "videoparams.h"
#include "render/samplebuffer.h"
#include "olive/core/render/audioparams.h"
namespace olive { using core::AudioParams; }
namespace olive {
class AcceleratedJob;
class CacheJob;
class Texture;
using TexturePtr = std::shared_ptr<Texture>;
class Texture {
public:
	enum Interpolation { k_nearest, k_linear, k_mipmapped_linear };
	Texture(const VideoParams &) {}
	int width() const { return 0; }
	int height() const { return 0; }
	int channel_count() const { return 0; }
	core::Rational pixel_aspect_ratio() const { return core::Rational(); }
	Vector2D virtual_resolution() const { return Vector2D(); }
	AcceleratedJob *job() { return nullptr; }
	template <typename T> static TexturePtr job(const VideoParams &, const T &) { return nullptr; }
	const VideoParams &params() const { static VideoParams p; return p; }
	template <typename T> TexturePtr to_job(const T &) { return nullptr; }
};
}
