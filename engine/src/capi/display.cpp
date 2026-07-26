/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "oakengine/display.h"

#include <QObject>
#include <QOpenGLContext>
#include <QString>

#include "codec/frame.h"
#include "render/job/colortransformjob.h"
#include "render/opengl/openglrenderer.h"
#include "render/renderer.h"
#include "render/texture.h"
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#endif

extern "C" {

void *oakengine_display_renderer_create_dynamic(const char *backend_name,
												void *parent)
{
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	QObject *p = static_cast<QObject *>(parent);
	auto *dyn = new olive::DynamicRenderer(
		QString::fromUtf8(backend_name ? backend_name : ""), p);
	if (dyn->load()) {
		return dyn;
	}
	// Backend library failed to load: drop it so the caller can fall back
	// to the built-in OpenGL renderer.
	delete dyn;
	return nullptr;
#else
	(void)backend_name;
	(void)parent;
	return nullptr;
#endif
}

void *oakengine_display_renderer_create_opengl(void *parent)
{
	return new olive::OpenGLRenderer(static_cast<QObject *>(parent));
}

int oakengine_display_renderer_init(void *renderer, void *gl_context)
{
	olive::Renderer *r = static_cast<olive::Renderer *>(renderer);
	if (!r) {
		return OAKENGINE_E_INVALID;
	}

	if (gl_context) {
		QOpenGLContext *ctx = static_cast<QOpenGLContext *>(gl_context);
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
		if (auto *dyn = dynamic_cast<olive::DynamicRenderer *>(r)) {
			dyn->init_with_open_gl_context(ctx);
			dyn->post_init();
			return OAKENGINE_OK;
		}
#endif
		auto *gl = static_cast<olive::OpenGLRenderer *>(r);
		gl->init(ctx);
		gl->post_init();
		return OAKENGINE_OK;
	}

	r->init();
	r->post_init();
	return OAKENGINE_OK;
}

void oakengine_display_renderer_destroy(void *renderer)
{
	olive::Renderer *r = static_cast<olive::Renderer *>(renderer);
	if (!r) {
		return;
	}
	r->destroy();
	r->post_destroy();
}

void oakengine_display_renderer_create_texture(void *renderer,
											   const void *video_params,
											   const void *pixels, int linesize,
											   void *out_texture)
{
	olive::Renderer *r = static_cast<olive::Renderer *>(renderer);
	if (!r || !video_params || !out_texture) {
		return;
	}
	const olive::VideoParams &params =
		*static_cast<const olive::VideoParams *>(video_params);
	*static_cast<olive::TexturePtr *>(out_texture) =
		r->create_texture(params, pixels, linesize);
}

void oakengine_display_renderer_blit_color_managed(void *renderer,
												   const void *color_job,
												   void *dst_texture,
												   const void *video_params)
{
	olive::Renderer *r = static_cast<olive::Renderer *>(renderer);
	if (!r || !color_job) {
		return;
	}
	const olive::ColorTransformJob &job =
		*static_cast<const olive::ColorTransformJob *>(color_job);
	olive::Texture *dst = static_cast<olive::Texture *>(dst_texture);
	if (video_params) {
		r->blit_color_managed(
			job, dst, *static_cast<const olive::VideoParams *>(video_params));
	} else if (dst) {
		r->blit_color_managed(job, dst, dst->params());
	}
}

void oakengine_display_texture_upload(void *texture, void *pixels, int linesize)
{
	olive::Texture *t = static_cast<olive::Texture *>(texture);
	if (!t) {
		return;
	}
	t->upload(pixels, linesize);
}

void oakengine_display_texture_download(void *texture, void *pixels,
										int linesize)
{
	olive::Texture *t = static_cast<olive::Texture *>(texture);
	if (!t) {
		return;
	}
	t->download(pixels, linesize);
}

void oakengine_codec_frame_create(void *out_frame)
{
	if (!out_frame) {
		return;
	}
	*static_cast<olive::FramePtr *>(out_frame) = olive::Frame::create();
}

void oakengine_codec_frame_set_video_params(void *frame,
											const void *video_params)
{
	olive::Frame *f = static_cast<olive::Frame *>(frame);
	if (!f || !video_params) {
		return;
	}
	f->set_video_params(*static_cast<const olive::VideoParams *>(video_params));
}

int oakengine_codec_frame_allocate(void *frame)
{
	olive::Frame *f = static_cast<olive::Frame *>(frame);
	if (!f) {
		return 0;
	}
	return f->allocate() ? 1 : 0;
}

} // extern "C"
