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

#ifndef OAK_OPENGLRENDERER_H
#define OAK_OPENGLRENDERER_H

#include <map>
#include <string>

#include "render/opengl/openglcontext.h"
#include "render/opengl/openglcontextprovider.h"
#include "render/opengl/openglfunctions.h"
#include "render/renderer.h"

namespace olive
{

class OpenGLRenderer : public Renderer, public OpenGLContextProvider {
public:
	OpenGLRenderer();

	virtual ~OpenGLRenderer() override;

	void init(OpenGLContext *existing_ctx);

	virtual bool init() override;

	virtual void post_destroy() override;

	virtual void post_init() override;

	virtual void clear_destination(olive::Texture *texture = nullptr,
								  double r = 0.0, double g = 0.0,
								  double b = 0.0, double a = 0.0) override;

	virtual Variant create_native_shader(olive::ShaderCode code) override;

	virtual void destroy_native_shader(Variant shader) override;

	virtual void upload_to_texture(const Variant &handle,
								 const VideoParams &params, const void *data,
								 int linesize) override;

	virtual void download_from_texture(const Variant &handle,
									 const VideoParams &params, void *data,
									 int linesize) override;

	virtual void flush() override;

	virtual Color get_pixel_from_texture(olive::Texture *texture,
									  const PointF &pt) override;

	OpenGLContext *context() const
	{
		return context_;
	}

	virtual OpenGLContext *open_gl_context() const override
	{
		return context();
	}

	virtual bool is_open_gl() const override
	{
		return true;
	}

	virtual void attach_output_texture(olive::Texture *texture) override;

	virtual void detach_output_texture() override;

	bool ensure_context_current(const char *caller);

protected:
	virtual void blit(Variant shader, olive::AcceleratedJob &job,
					  olive::Texture *destination,
					  olive::VideoParams destination_params,
					  bool clear_destination) override;

	virtual Variant create_native_texture(int width, int height, int depth,
										 PixelFormat format, int channel_count,
										 const void *data = nullptr,
										 int linesize = 0) override;

	virtual void destroy_native_texture(Variant texture) override;

	virtual void destroy_internal() override;

	void attach_texture_as_destination(const Variant &texture);

	void detach_texture_as_destination();

private:
	static GLint get_internal_format(PixelFormat format, int channel_layout);

	static GLenum get_pixel_type(PixelFormat format);

	static GLenum get_pixel_format(int channel_count);

	void prepare_input_texture(GLenum target, Texture::Interpolation interp);

	void clear_destination_internal(double r = 0.0, double g = 0.0,
								  double b = 0.0, double a = 0.0);

	GLuint compile_shader(GLenum type, const std::string &code);

	// Viewer contexts are owned by the app layer and may be destroyed before
	// this renderer (e.g. when a viewer widget tears down its context). There
	// is no QPointer auto-nulling anymore: the app must call init(nullptr)
	// (or destroy the renderer) before tearing down an external context.
	OpenGLContext *context_;

	// True when context_ was created by init() and is owned by this renderer.
	bool context_owned_;

	OpenGLFunctions functions_;

	bool functions_resolved_;

	GLuint framebuffer_;

	struct TextureCacheKey {
		int width;
		int height;
		int depth;
		PixelFormat format;
		int channel_count;

		bool operator==(const TextureCacheKey &rhs) const
		{
			return width == rhs.width && height == rhs.height &&
				   depth == rhs.depth && format == rhs.format &&
				   channel_count == rhs.channel_count;
		}
	};

	std::map<GLuint, TextureCacheKey> texture_params_;

	static const int k_texture_cache_max_size;
};

}

#endif // OAK_OPENGLRENDERER_H
