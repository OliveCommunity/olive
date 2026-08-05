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

OakRenderTexture *tex(OakRenderTexture *h)
{
	return h;
}

OakCodecFrame *frm(OakCodecFrame *h)
{
	return h;
}

olive::Renderer *ren(OakRenderRenderer *h)
{
	return reinterpret_cast<olive::Renderer *>(h);
}

const olive::Renderer *ren(const OakRenderRenderer *h)
{
	return reinterpret_cast<const olive::Renderer *>(h);
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

OakRenderRenderer *oakrender_display_renderer_create_dynamic(
	const char *backend_id)
{
	if (!backend_id || !*backend_id) {
		return nullptr;
	}
	try {
		auto *r = new olive::DynamicRenderer(backend_id);
		if (!r->load()) {
			delete r;
			return nullptr;
		}
		return reinterpret_cast<OakRenderRenderer *>(r);
	} catch (...) {
		return nullptr;
	}
}

OakRenderRenderer *oakrender_display_renderer_create_opengl(void)
{
	try {
		return reinterpret_cast<OakRenderRenderer *>(new olive::OpenGLRenderer());
	} catch (...) {
		return nullptr;
	}
}

int oakrender_display_renderer_init(OakRenderRenderer *renderer,
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
	olive::Renderer *r = ren(renderer);
	if (!r) {
		return;
	}
	try {
		r->destroy();
		delete r;
	} catch (...) {
	}
}

/* ---- Renderer queries ---------------------------------------------------- */

int oakrender_display_renderer_is_open_gl(const OakRenderRenderer *renderer)
{
	return renderer && ren(renderer)->is_open_gl() ? 1 : 0;
}

int oakrender_display_renderer_is_vulkan(const OakRenderRenderer *renderer)
{
	return renderer && ren(renderer)->is_vulkan() ? 1 : 0;
}

/* ---- Texture handle ------------------------------------------------------ */

OakRenderTexture *oakrender_display_texture_create(
	OakRenderRenderer *renderer, const oakrender_video_params *params,
	const void *pixels, int linesize)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !params) {
		return nullptr;
	}
	try {
		olive::TexturePtr t = r->create_texture(pod_to_cpp(*params), pixels,
												linesize);
		if (!t) {
			return nullptr;
		}
		auto *block = new OakRenderTexture;
		block->ptr = std::move(t);
		oakrender_c_api::alive_inc();
		return block;
	} catch (...) {
		return nullptr;
	}
}

OakRenderTexture *oakrender_display_texture_retain(OakRenderTexture *texture)
{
	if (!texture) {
		return nullptr;
	}
	tex(texture)->refcount.fetch_add(1, std::memory_order_relaxed);
	return texture;
}

void oakrender_display_texture_free(OakRenderTexture *texture)
{
	if (!texture) {
		return;
	}
	if (tex(texture)->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete tex(texture);
		oakrender_c_api::alive_dec();
	}
}

int oakrender_display_texture_upload(OakRenderTexture *texture,
									 const void *pixels, int linesize)
{
	if (!texture || !pixels || !tex(texture)->ptr) {
		return OAKRENDER_E_INVALID;
	}
	try {
		tex(texture)->ptr->upload(const_cast<void *>(pixels), linesize);
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_display_texture_download(OakRenderTexture *texture, void *pixels,
									   int linesize)
{
	if (!texture || !pixels || !tex(texture)->ptr) {
		return OAKRENDER_E_INVALID;
	}
	try {
		tex(texture)->ptr->download(pixels, linesize);
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_display_texture_get_params(const OakRenderTexture *texture,
										 oakrender_video_params *out)
{
	if (!texture || !out || !tex(const_cast<OakRenderTexture *>(texture))->ptr) {
		return OAKRENDER_E_INVALID;
	}
	*out = cpp_to_pod(tex(const_cast<OakRenderTexture *>(texture))->ptr->params());
	return OAKRENDER_OK;
}

int oakrender_display_texture_id(const OakRenderTexture *texture)
{
	if (!texture || !tex(const_cast<OakRenderTexture *>(texture))->ptr) {
		return 0;
	}
	return tex(const_cast<OakRenderTexture *>(texture))->ptr->id().to_int();
}

/* ---- Frame handle -------------------------------------------------------- */

OakCodecFrame *oakrender_codec_frame_create(void)
{
	try {
		auto *block = new OakCodecFrame;
		block->ptr = olive::Frame::create();
		oakrender_c_api::alive_inc();
		return block;
	} catch (...) {
		return nullptr;
	}
}

OakCodecFrame *oakrender_codec_frame_retain(OakCodecFrame *frame)
{
	if (!frame) {
		return nullptr;
	}
	frm(frame)->refcount.fetch_add(1, std::memory_order_relaxed);
	return frame;
}

void oakrender_codec_frame_free(OakCodecFrame *frame)
{
	if (!frame) {
		return;
	}
	if (frm(frame)->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete frm(frame);
		oakrender_c_api::alive_dec();
	}
}

int oakrender_codec_frame_set_video_params(
	OakCodecFrame *frame, const oakrender_video_params *params)
{
	if (!frame || !params || !frm(frame)->ptr) {
		return OAKRENDER_E_INVALID;
	}
	frm(frame)->ptr->set_video_params(pod_to_cpp(*params));
	return OAKRENDER_OK;
}

int oakrender_codec_frame_get_params(const OakCodecFrame *frame,
									 oakrender_video_params *out)
{
	if (!frame || !out || !frm(const_cast<OakCodecFrame *>(frame))->ptr) {
		return OAKRENDER_E_INVALID;
	}
	*out = cpp_to_pod(
		frm(const_cast<OakCodecFrame *>(frame))->ptr->video_params());
	return OAKRENDER_OK;
}

int oakrender_codec_frame_allocate(OakCodecFrame *frame)
{
	if (!frame || !frm(frame)->ptr) {
		return OAKRENDER_E_INVALID;
	}
	return frm(frame)->ptr->allocate() ? OAKRENDER_OK : OAKRENDER_E_FAILED;
}

void *oakrender_codec_frame_data(OakCodecFrame *frame)
{
	if (!frame || !frm(frame)->ptr) {
		return nullptr;
	}
	return frm(frame)->ptr->data();
}

const void *oakrender_codec_frame_const_data(const OakCodecFrame *frame)
{
	if (!frame || !frm(const_cast<OakCodecFrame *>(frame))->ptr) {
		return nullptr;
	}
	return frm(const_cast<OakCodecFrame *>(frame))->ptr->const_data();
}

int oakrender_codec_frame_linesize_bytes(const OakCodecFrame *frame)
{
	if (!frame || !frm(const_cast<OakCodecFrame *>(frame))->ptr) {
		return 0;
	}
	return frm(const_cast<OakCodecFrame *>(frame))->ptr->linesize_bytes();
}

int oakrender_codec_frame_is_allocated(const OakCodecFrame *frame)
{
	if (!frame || !frm(const_cast<OakCodecFrame *>(frame))->ptr) {
		return 0;
	}
	return frm(const_cast<OakCodecFrame *>(frame))->ptr->is_allocated() ? 1 : 0;
}

/* ---- Color-managed blit -------------------------------------------------- */

int oakrender_display_renderer_blit_color_managed(
	OakRenderRenderer *renderer, const oakrender_color_transform_job *job,
	OakRenderTexture *dst_texture, const oakrender_video_params *params)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !job) {
		return OAKRENDER_E_INVALID;
	}
	try {
		olive::ColorTransformJob ctj;
		if (job->processor) {
			ctj.set_color_processor(
				static_cast<const OakColorProcessor *>(job->processor)->ptr);
		}
		if (job->input_texture) {
			ctj.set_input_texture(
				static_cast<OakRenderTexture *>(job->input_texture)->ptr);
		}
		ctj.set_input_alpha_association(
			static_cast<olive::AlphaAssociated>(job->input_alpha_association));
		ctj.set_clear_destination_enabled(job->clear_destination != 0);
		ctj.set_force_opaque(job->force_opaque != 0);
		ctj.set_transform_matrix(mat_from_float(job->matrix));
		ctj.set_crop_matrix(mat_from_float(job->crop_matrix));

		olive::Texture *dst = dst_texture ? tex(dst_texture)->ptr.get() : nullptr;
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
	OakRenderRenderer *renderer, int texture_id,
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
