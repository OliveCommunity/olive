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

#ifndef OAK_RENDERCONTEXT_H
#define OAK_RENDERCONTEXT_H

#include <QMutex>
#include <QObject>
#include <QVariant>
#include <atomic>
#include <memory>

#include "common/define.h"
#include "render/shadercode.h"
#include "render/videoparams.h"
#include "texture.h"

// Forward declarations to keep the render core header lightweight
namespace olive
{
class ColorTransformJob;
class Node;
}

namespace olive
{

class ShaderJob;

class Renderer : public QObject {
	Q_OBJECT
public:
	Renderer(QObject *parent = nullptr);
	virtual ~Renderer() override;

	virtual bool init() = 0;

	TexturePtr create_texture(const VideoParams &params,
							 const void *data = nullptr, int linesize = 0);

	void destroy_texture(Texture *texture);

	virtual void blit_to_texture(QVariant shader, olive::AcceleratedJob &job,
							   olive::Texture *destination,
							   bool clear_destination = true)
	{
		blit(shader, job, destination, destination->params(),
			 clear_destination);
	}

	void blit(QVariant shader, olive::AcceleratedJob &job,
			  olive::VideoParams params, bool clear_destination = true)
	{
		blit(shader, job, nullptr, params, clear_destination);
	}

	void blit_color_managed(const ColorTransformJob &color_job,
						  Texture *destination, const VideoParams &params);
	void blit_color_managed(const ColorTransformJob &job, Texture *destination)
	{
		blit_color_managed(job, destination, destination->params());
	}
	void blit_color_managed(const ColorTransformJob &job,
						  const VideoParams &params)
	{
		blit_color_managed(job, nullptr, params);
	}

	TexturePtr interlace_texture(TexturePtr top, TexturePtr bottom,
								const VideoParams &params);

	QVariant get_default_shader();

	void destroy();

	virtual void post_destroy() = 0;

	virtual void post_init() = 0;

	virtual void clear_destination(olive::Texture *texture = nullptr,
								  double r = 0.0, double g = 0.0,
								  double b = 0.0, double a = 1.0) = 0;

	virtual QVariant create_native_shader(olive::ShaderCode code) = 0;

	virtual void destroy_native_shader(QVariant shader) = 0;

	virtual void upload_to_texture(const QVariant &handle,
								 const VideoParams &params, const void *data,
								 int linesize) = 0;

	virtual void download_from_texture(const QVariant &handle,
									 const VideoParams &params, void *data,
									 int linesize) = 0;

	virtual void flush() = 0;

	virtual Color get_pixel_from_texture(olive::Texture *texture,
									  const QPointF &pt) = 0;
	std::shared_ptr<RendererLifetime> get_lifetime() const
	{
		return lifetime_;
	}

	virtual bool is_open_gl() const
	{
		return false;
	}

	virtual bool is_vulkan() const
	{
		return false;
	}

	/**
	 * @brief Attach a texture as the current output destination for OFX plugin
	 *        OpenGL rendering.
	 *
	 * Default implementation is a no-op. OpenGL-based renderers override this
	 * to bind the texture as a framebuffer render target.
	 */
	virtual void attach_output_texture(olive::Texture *texture)
	{
		(void)texture;
	}

	/**
	 * @brief Detach the current OFX plugin OpenGL output texture.
	 *
	 * Default implementation is a no-op.
	 */
	virtual void detach_output_texture()
	{
	}

protected:
	virtual void blit(QVariant shader, olive::AcceleratedJob &job,
					  olive::Texture *destination,
					  olive::VideoParams destination_params,
					  bool clear_destination) = 0;
	virtual QVariant create_native_texture(int width, int height, int depth,
										 PixelFormat format, int channel_count,
										 const void *data = nullptr,
										 int linesize = 0) = 0;

	virtual void destroy_native_texture(QVariant texture) = 0;

	virtual void destroy_internal() = 0;

private:
	std::atomic<bool> destroyed_{ false };
	std::shared_ptr<RendererLifetime> lifetime_;
	struct ColorContext {
		struct LUT {
			TexturePtr texture;
			Texture::Interpolation interpolation;
			QString name;
		};

		QVariant compiled_shader;
		QVector<LUT> lut3d_textures;
		QVector<LUT> lut1d_textures;
	};

	TexturePtr create_texture_from_native_handle(const QVariant &v,
											 const VideoParams &params);

	bool get_color_context(const ColorTransformJob &color_job, ColorContext *ctx);

	void clear_old_textures();

	QHash<QString, ColorContext> color_cache_;

	struct CachedTexture {
		int width;
		int height;
		int depth;
		PixelFormat format;
		int channel_count;
		QVariant handle;
		qint64 accessed;
	};

	static const int max_texture_life = 5000;
	static const bool use_texture_cache = true;
	std::list<CachedTexture> texture_cache_;

	QMutex color_cache_mutex_;

	QVariant default_shader_;

	QVariant interlace_texture_;

	QMutex texture_cache_lock_;
};

}

#endif // OAK_RENDERCONTEXT_H
