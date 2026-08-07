/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "../../../include/render/renderer.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <string>

#include "alivecount.h"
#include "internalhandles.h"

#include "backend/dynamicrenderer.h"
#include "job/colortransformjob.h"
#include "opengl/openglrenderer.h"
#include "renderer.h"
#include "rendermanager.h"
#include "texture.h"

namespace
{

olive::VideoParams pod_to_cpp(const oakrender_video_params &v)
{
	olive::VideoParams vp(
		v.width, v.height, olive::Rational(v.time_base_num, v.time_base_den),
		static_cast<olive::PixelFormat::Format>(v.format),
		olive::VideoParams::k_internal_channel_count,
		olive::Rational(v.pixel_aspect_num, v.pixel_aspect_den),
		static_cast<olive::VideoParams::Interlacing>(v.interlacing),
		v.divider > 0 ? v.divider : 1);
	vp.set_color_range(static_cast<olive::VideoParams::ColorRange>(v.color_range));
	vp.set_premultiplied_alpha(v.premultiplied_alpha != 0);
	return vp;
}

oakrender_video_params cpp_to_pod(const olive::VideoParams &vp)
{
	oakrender_video_params p = {};
	p.width = vp.width();
	p.height = vp.height();
	p.time_base_num = vp.time_base().numerator();
	p.time_base_den = vp.time_base().denominator();
	p.format = static_cast<int>(vp.format());
	p.pixel_aspect_num = vp.pixel_aspect_ratio().numerator();
	p.pixel_aspect_den = vp.pixel_aspect_ratio().denominator();
	p.interlacing = static_cast<int>(vp.interlacing());
	p.color_range = static_cast<int>(vp.color_range());
	p.divider = vp.divider();
	p.video_type = 0;
	p.premultiplied_alpha = vp.premultiplied_alpha() ? 1 : 0;
	return p;
}

olive::Renderer *ren(OakRenderRenderer h)
{
	return oakrender_c_api::to_native<olive::Renderer>(h);
}

void renderer_delete(void *object)
{
	auto *r = static_cast<olive::Renderer *>(object);
	try {
		r->destroy();
		delete r;
	} catch (...) {
	}
}

olive::Matrix4x4 mat_from_float(const float *f)
{
	olive::Matrix4x4 m;
	if (!f) {
		return m;
	}
	// All-zero means identity (R7-A §A.2 convention)
	bool all_zero = true;
	for (int i = 0; i < 16; i++) {
		if (f[i] != 0.0f) {
			all_zero = false;
			break;
		}
	}
	if (all_zero) {
		return m;
	}
	// POD is column-major (QMatrix4x4 layout); Matrix4x4 stores [row][col]
	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			m(row, col) = f[col * 4 + row];
		}
	}
	return m;
}

int write_string(const std::string &s, char *buf, int n)
{
	const int required = int(s.size()) + 1;
	if (buf && n >= required) {
		std::memcpy(buf, s.c_str(), size_t(required));
	}
	return required;
}

// Requested backend recorded through oakrender_set_backend(); applied to
// the RenderManager instance when one is created by the facade.
std::string g_requested_backend = "opengl";

} // namespace

/* ---- Renderer lifecycle -------------------------------------------------- */

OakRenderRenderer oakrender_display_renderer_create_dynamic(
	const char *backend_id)
{
	if (!backend_id || !*backend_id) {
		return OakRenderRenderer{};
	}
	try {
		auto *r = new olive::DynamicRenderer(backend_id);
		if (!r->load()) {
			delete r;
			return OakRenderRenderer{};
		}
		return oakrender_c_api::make_handle<OakRenderRenderer>(
			r, true, &renderer_delete);
	} catch (...) {
		return OakRenderRenderer{};
	}
}

OakRenderRenderer oakrender_display_renderer_create_opengl(void)
{
	try {
		return oakrender_c_api::make_handle<OakRenderRenderer>(
			new olive::OpenGLRenderer(), true, &renderer_delete);
	} catch (...) {
		return OakRenderRenderer{};
	}
}

int oakrender_display_renderer_init(OakRenderRenderer renderer,
									void *gl_context)
{
	olive::Renderer *r = ren(renderer);
	if (!r) {
		return OAKRENDER_E_INVALID;
	}
	try {
		if (gl_context) {
			auto *ctx = static_cast<olive::OpenGLContext *>(gl_context);
			if (auto *gl = dynamic_cast<olive::OpenGLRenderer *>(r)) {
				gl->init(ctx);
				return OAKRENDER_OK;
			}
			if (auto *dyn = dynamic_cast<olive::DynamicRenderer *>(r)) {
				return dyn->init_with_open_gl_context(ctx) ? OAKRENDER_OK :
															 OAKRENDER_E_FAILED;
			}
			return OAKRENDER_E_INVALID;
		}
		return r->init() ? OAKRENDER_OK : OAKRENDER_E_FAILED;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

void oakrender_display_renderer_destroy(OakRenderRenderer *renderer)
{
	oakrender_c_api::free_handle(renderer);
}

/* ---- Renderer queries ---------------------------------------------------- */

int oakrender_display_renderer_is_open_gl(OakRenderRenderer renderer)
{
	olive::Renderer *r = ren(renderer);
	return r && r->is_open_gl() ? 1 : 0;
}

int oakrender_display_renderer_is_vulkan(OakRenderRenderer renderer)
{
	olive::Renderer *r = ren(renderer);
	return r && r->is_vulkan() ? 1 : 0;
}

/* ---- Texture handle ------------------------------------------------------ */

OakRenderTexture oakrender_display_texture_create(
	OakRenderRenderer renderer, const oakrender_video_params *params,
	const void *pixels, int linesize)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !params) {
		return OakRenderTexture{};
	}
	try {
		olive::TexturePtr t = r->create_texture(pod_to_cpp(*params), pixels,
												linesize);
		if (!t) {
			return OakRenderTexture{};
		}
		auto *impl = new OakRenderTextureImpl;
		impl->ptr = std::move(t);
		return oakrender_c_api::make_handle<OakRenderTexture>(
			impl, true, &oakrender_c_api::delete_as<OakRenderTextureImpl>);
	} catch (...) {
		return OakRenderTexture{};
	}
}

OakRenderTexture oakrender_display_texture_retain(OakRenderTexture texture)
{
	if (texture.ctx) {
		texture.addref(texture.ctx);
	}
	return texture;
}

void oakrender_display_texture_free(OakRenderTexture *texture)
{
	oakrender_c_api::free_handle(texture);
}

int oakrender_display_texture_upload(OakRenderTexture texture,
									 const void *pixels, int linesize)
{
	OakRenderTextureImpl *t =
		oakrender_c_api::to_native<OakRenderTextureImpl>(texture);
	if (!t || !pixels || !t->ptr) {
		return OAKRENDER_E_INVALID;
	}
	try {
		t->ptr->upload(const_cast<void *>(pixels), linesize);
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_display_texture_download(OakRenderTexture texture, void *pixels,
									   int linesize)
{
	OakRenderTextureImpl *t =
		oakrender_c_api::to_native<OakRenderTextureImpl>(texture);
	if (!t || !pixels || !t->ptr) {
		return OAKRENDER_E_INVALID;
	}
	try {
		t->ptr->download(pixels, linesize);
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_display_texture_get_params(OakRenderTexture texture,
										 oakrender_video_params *out)
{
	OakRenderTextureImpl *t =
		oakrender_c_api::to_native<OakRenderTextureImpl>(texture);
	if (!t || !out || !t->ptr) {
		return OAKRENDER_E_INVALID;
	}
	*out = cpp_to_pod(t->ptr->params());
	return OAKRENDER_OK;
}

int oakrender_display_texture_id(OakRenderTexture texture)
{
	OakRenderTextureImpl *t =
		oakrender_c_api::to_native<OakRenderTextureImpl>(texture);
	if (!t || !t->ptr) {
		return 0;
	}
	return t->ptr->id().to_int();
}

/* ---- Frame handle -------------------------------------------------------- */

OakCodecFrame oakrender_codec_frame_create(void)
{
	try {
		auto *impl = new OakCodecFrameImpl;
		impl->ptr = olive::Frame::create();
		return oakrender_c_api::make_handle<OakCodecFrame>(
			impl, true, &oakrender_c_api::delete_as<OakCodecFrameImpl>);
	} catch (...) {
		return OakCodecFrame{};
	}
}

OakCodecFrame oakrender_codec_frame_retain(OakCodecFrame frame)
{
	if (frame.ctx) {
		frame.addref(frame.ctx);
	}
	return frame;
}

void oakrender_codec_frame_free(OakCodecFrame *frame)
{
	oakrender_c_api::free_handle(frame);
}

int oakrender_codec_frame_set_video_params(
	OakCodecFrame frame, const oakrender_video_params *params)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f || !params || !f->ptr) {
		return OAKRENDER_E_INVALID;
	}
	f->ptr->set_video_params(pod_to_cpp(*params));
	return OAKRENDER_OK;
}

int oakrender_codec_frame_get_params(OakCodecFrame frame,
									 oakrender_video_params *out)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f || !out || !f->ptr) {
		return OAKRENDER_E_INVALID;
	}
	*out = cpp_to_pod(f->ptr->video_params());
	return OAKRENDER_OK;
}

int oakrender_codec_frame_allocate(OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f || !f->ptr) {
		return OAKRENDER_E_INVALID;
	}
	return f->ptr->allocate() ? OAKRENDER_OK : OAKRENDER_E_FAILED;
}

void *oakrender_codec_frame_data(OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f || !f->ptr) {
		return nullptr;
	}
	if (f->avptr) {
		return f->avptr->data(0);
	}
	return f->ptr->data();
}

const void *oakrender_codec_frame_const_data(OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f || !f->ptr) {
		return nullptr;
	}
	return f->ptr->const_data();
}

int oakrender_codec_frame_linesize_bytes(OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (f && f->avptr) {
		return f->avptr->linesize(0);
	}
	if (!f || !f->ptr) {
		return 0;
	}
	return f->ptr->linesize_bytes();
}

int oakrender_codec_frame_is_allocated(OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f || !f->ptr) {
		return 0;
	}
	return f->ptr->is_allocated() ? 1 : 0;
}

/* ---- Color-managed blit -------------------------------------------------- */

int oakrender_display_renderer_blit_color_managed(
	OakRenderRenderer renderer, const oakrender_color_transform_job *job,
	OakRenderTexture dst_texture, const oakrender_video_params *params)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !job) {
		return OAKRENDER_E_INVALID;
	}
	try {
		olive::ColorTransformJob ctj;
		if (job->processor) {
			ctj.set_color_processor(
				static_cast<OakColorProcessorImpl *>(
					oakrender_c_api::box_object(job->processor))
					->ptr);
		}
		if (job->input_texture) {
			ctj.set_input_texture(
				static_cast<OakRenderTextureImpl *>(
					oakrender_c_api::box_object(job->input_texture))
					->ptr);
		}
		ctj.set_input_alpha_association(
			static_cast<olive::AlphaAssociated>(job->input_alpha_association));
		ctj.set_clear_destination_enabled(job->clear_destination != 0);
		ctj.set_force_opaque(job->force_opaque != 0);
		ctj.set_transform_matrix(mat_from_float(job->matrix));
		ctj.set_crop_matrix(mat_from_float(job->crop_matrix));

		OakRenderTextureImpl *dst_impl =
			oakrender_c_api::to_native<OakRenderTextureImpl>(dst_texture);
		olive::Texture *dst = dst_impl ? dst_impl->ptr.get() : nullptr;
		if (params) {
			r->blit_color_managed(ctj, dst, pod_to_cpp(*params));
		} else if (dst) {
			r->blit_color_managed(ctj, dst, dst->params());
		} else {
			return OAKRENDER_E_INVALID;
		}
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

/* ---- Cross-backend texture download -------------------------------------- */

int oakrender_display_renderer_download_from_texture(
	OakRenderRenderer renderer, int texture_id,
	const oakrender_video_params *params, void *dst_pixels, int linesize)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !params || !dst_pixels) {
		return OAKRENDER_E_INVALID;
	}
	try {
		r->download_from_texture(texture_id, pod_to_cpp(*params), dst_pixels,
								 linesize);
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

/* ---- Backend management -------------------------------------------------- */

int oakrender_backend_count(void)
{
	// olive::RenderManager::Backend: k_open_gl, k_vulkan, k_multi_process,
	// k_dummy
	return 4;
}

int oakrender_backend_id_at(int i, char *buf, int n)
{
	if (i < 0 || i >= oakrender_backend_count()) {
		return OAKRENDER_E_NOT_FOUND;
	}
	return write_string(
		olive::RenderManager::backend_to_string(
			static_cast<olive::RenderManager::Backend>(i)),
		buf, n);
}

int oakrender_set_backend(const char *backend_id)
{
	if (!backend_id) {
		return OAKRENDER_E_INVALID;
	}
	std::string lower = backend_id;
	std::transform(lower.begin(), lower.end(), lower.begin(),
				   [](unsigned char c) { return char(std::tolower(c)); });
	if (lower != "opengl" && lower != "vulkan" && lower != "multiprocess" &&
		lower != "dummy") {
		return OAKRENDER_E_INVALID;
	}
	g_requested_backend = lower;
	return OAKRENDER_OK;
}

int oakrender_current_backend(char *buf, int n)
{
	if (olive::RenderManager::instance()) {
		return write_string(
			olive::RenderManager::backend_to_string(
				olive::RenderManager::instance()->backend()),
			buf, n);
	}
	return write_string(g_requested_backend, buf, n);
}

int oakrender_display_texture_is_dummy(OakRenderTexture texture)
{
	if (!texture.ctx) {
		return 0;
	}
	return oakrender_c_api::to_native<OakRenderTextureImpl>(texture)
				   ->ptr->is_dummy()
			   ? 1
			   : 0;
}

int oakrender_display_texture_get_frame(OakRenderTexture texture,
										OakCodecFrame *out)
{
	if (!texture.ctx || !out) {
		return OAKRENDER_E_INVALID;
	}
	*out = OakCodecFrame{};

	olive::AVFramePtr frame =
		oakrender_c_api::to_native<OakRenderTextureImpl>(texture)->ptr->frame();
	if (!frame) {
		return OAKRENDER_E_NOT_FOUND;
	}
	// The texture's CPU copy is an AVFramePtr (ffmpeg_bridge RAII); wrap
	// the shared_ptr so the frame stays alive with the handle.
	auto *fimpl = new OakCodecFrameImpl;
	fimpl->avptr = frame;
	*out = oakrender_c_api::make_handle<OakCodecFrame>(
		fimpl, true, &oakrender_c_api::delete_as<OakCodecFrameImpl>);
	return out->ctx ? OAKRENDER_OK : OAKRENDER_E_NOMEM;
}

OakRenderTexture oakrender_display_texture_wrap_native(
	const olive::TexturePtr &texture)
{
	if (!texture) {
		return OakRenderTexture{};
	}
	auto *impl = new OakRenderTextureImpl;
	impl->ptr = texture;
	return oakrender_c_api::make_handle<OakRenderTexture>(
		impl, true, &oakrender_c_api::delete_as<OakRenderTextureImpl>);
}

int oakrender_codec_frame_width(OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f) {
		return 0;
	}
	if (f->avptr) {
		return f->avptr->width();
	}
	return f->ptr ? f->ptr->width() : 0;
}

int oakrender_codec_frame_height(OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f) {
		return 0;
	}
	if (f->avptr) {
		return f->avptr->height();
	}
	return f->ptr ? f->ptr->height() : 0;
}

int oakrender_codec_frame_fb_format(OakCodecFrame frame)
{
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!f || !f->avptr) {
		return -1;
	}
	return f->avptr->format();
}
