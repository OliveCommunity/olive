/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_RENDERTEXTURE_H
#define OAK_RENDERTEXTURE_H

#include "avframeptr.h"

#include <atomic>
#include <memory>

#include "mathtypes.h"
#include "variant.h"
#include "videoparams.h"

namespace olive
{

class AcceleratedJob;
class Renderer;
struct RendererLifetime {
	std::atomic<bool> alive{ true };
};

class Texture;
using TexturePtr = std::shared_ptr<Texture>;

class Texture {
public:
	enum Interpolation { k_nearest, k_linear, k_mipmapped_linear };

	static const Interpolation k_default_interpolation;

	/**
   * @brief Construct a dummy texture with no renderer backend
   */
	Texture(const VideoParams &param)
		: renderer_(nullptr)
		, renderer_lifetime_(nullptr)
		, params_(param)
		, job_(nullptr)
	{
	}

	template <typename T>
	Texture(const VideoParams &p, const T &j)
		: Texture(p)
	{
		job_ = new T(j);
	}

	/**
   * @brief Construct a real texture linked to a renderer backend
   */
	Texture(Renderer *renderer, const Variant &native,
			const VideoParams &param,
			std::shared_ptr<RendererLifetime> lifetime = nullptr)
		: renderer_(renderer)
		, renderer_lifetime_(lifetime)
		, params_(param)
		, id_(native)
		, job_(nullptr)
	{
	}

	~Texture();

	Variant id() const
	{
		return id_;
	}

	const VideoParams &params() const
	{
		return params_;
	}

	template <typename T>
	static TexturePtr job(const VideoParams &p, const T &j)
	{
		return std::make_shared<Texture>(p, j);
	}

	template <typename T> TexturePtr to_job(const T &job)
	{
		return Texture::job(params_, job);
	}

	void upload(void *data, int linesize);

	void download(void *data, int linesize);

	bool is_dummy() const
	{
		return !renderer_;
	}

	int width() const
	{
		return params_.effective_width();
	}

	int height() const
	{
		return params_.effective_height();
	}

	Vector2D virtual_resolution() const
	{
		return Vector2D(params_.square_pixel_width(), params_.height());
	}

	PixelFormat format() const
	{
		return params_.format();
	}

	int channel_count() const
	{
		return params_.channel_count();
	}

	int divider() const
	{
		return params_.divider();
	}

	const Rational &pixel_aspect_ratio() const
	{
		return params_.pixel_aspect_ratio();
	}

	Renderer *renderer() const
	{
		return renderer_;
	}

	bool is_job() const
	{
		return job_;
	}
	AcceleratedJob *job() const
	{
		return job_;
	}
	void handle_frame(AVFramePtr ptr)
	{
		frame_ = ptr;
	}
	AVFramePtr frame()
	{
		return frame_;
	}

private:
	bool is_renderer_alive() const
	{
		return renderer_ &&
			   (!renderer_lifetime_ || renderer_lifetime_->alive.load());
	}

	Renderer *renderer_;
	std::shared_ptr<RendererLifetime> renderer_lifetime_;

	VideoParams params_;

	Variant id_;

	AcceleratedJob *job_;

	AVFramePtr frame_;
};

}

#endif // OAK_RENDERTEXTURE_H
