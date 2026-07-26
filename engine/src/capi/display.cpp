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
#include "displayinternal.h"

#include <atomic>
#include <cstring>

#include <QObject>
#include <QOpenGLContext>
#include <QMatrix4x4>
#include <QString>
#include <QVariant>
#include <QVector2D>
#include <QVector3D>

#include "codec/frame.h"
#include "colorinternal.h"
#include "render/job/colortransformjob.h"
#include "render/job/shaderjob.h"
#include "render/opengl/openglrenderer.h"
#include "render/renderer.h"
#include "render/shadercode.h"
#include "render/texture.h"
#include "node/value.h"
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#endif

/* ---- Internal control blocks -------------------------------------------- */

struct OakEngineDisplayTexture {
	olive::TexturePtr ptr;
	std::atomic<int> refcount{1};
};

struct OakEngineCodecFrame {
	olive::FramePtr ptr;
	std::atomic<int> refcount{1};
};

/* ---- Internal helpers --------------------------------------------------- */

namespace
{

olive::VideoParams pod_to_cpp(const oak_video_params &v)
{
	olive::VideoParams vp(
		v.width, v.height, olive::Rational(v.time_base_num, v.time_base_den),
		static_cast<olive::PixelFormat::Format>(v.format),
		olive::VideoParams::k_internal_channel_count,
		olive::Rational(v.pixel_aspect_num, v.pixel_aspect_den),
		static_cast<olive::VideoParams::Interlacing>(v.interlacing),
		v.divider > 0 ? v.divider : 1);
	vp.set_color_range(static_cast<olive::VideoParams::ColorRange>(v.color_range));
	return vp;
}

oak_video_params cpp_to_pod(const olive::VideoParams &vp)
{
	oak_video_params p = {};
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
	return p;
}

OakEngineDisplayTexture *tex(void *h)
{
	return static_cast<OakEngineDisplayTexture *>(h);
}

OakEngineCodecFrame *frm(void *h)
{
	return static_cast<OakEngineCodecFrame *>(h);
}

olive::Renderer *ren(void *h)
{
	return static_cast<olive::Renderer *>(h);
}

QMatrix4x4 mat_from_float(const float *f)
{
	if (!f) {
		return QMatrix4x4();
	}
	// Check if all zeros → identity
	bool all_zero = true;
	for (int i = 0; i < 16; i++) {
		if (f[i] != 0.0f) {
			all_zero = false;
			break;
		}
	}
	if (all_zero) {
		return QMatrix4x4();
	}
	return QMatrix4x4(f);
}

} // namespace

extern "C" {

/* ---- Renderer lifecycle ------------------------------------------------- */

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
	olive::Renderer *r = ren(renderer);
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
	olive::Renderer *r = ren(renderer);
	if (!r) {
		return;
	}
	r->destroy();
	r->post_destroy();
}

/* ---- Renderer queries --------------------------------------------------- */

int oakengine_display_renderer_is_open_gl(const void *renderer)
{
	olive::Renderer *r = ren(const_cast<void *>(renderer));
	return r ? r->is_open_gl() : 0;
}

int oakengine_display_renderer_is_vulkan(const void *renderer)
{
	olive::Renderer *r = ren(const_cast<void *>(renderer));
	return r ? r->is_vulkan() : 0;
}

/* ---- Texture handle ----------------------------------------------------- */

void *oakengine_display_texture_create(void *renderer,
									   const oak_video_params *params,
									   const void *pixels, int linesize)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !params) {
		return nullptr;
	}
	olive::VideoParams vp = pod_to_cpp(*params);
	olive::TexturePtr tp = r->create_texture(vp, pixels, linesize);
	if (!tp) {
		return nullptr;
	}
	return new OakEngineDisplayTexture{tp, {1}};
}

void *oakengine_display_texture_retain(void *texture)
{
	if (!texture) {
		return nullptr;
	}
	tex(texture)->refcount.fetch_add(1, std::memory_order_relaxed);
	return texture;
}

void oakengine_display_texture_free(void *texture)
{
	if (!texture) {
		return;
	}
	if (tex(texture)->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete tex(texture);
	}
}

int oakengine_display_texture_upload(void *texture, const void *pixels,
									 int linesize)
{
	if (!texture || !pixels) {
		return OAKENGINE_E_INVALID;
	}
	tex(texture)->ptr->upload(const_cast<void *>(pixels), linesize);
	return OAKENGINE_OK;
}

int oakengine_display_texture_download(void *texture, void *pixels,
									   int linesize)
{
	if (!texture || !pixels) {
		return OAKENGINE_E_INVALID;
	}
	tex(texture)->ptr->download(pixels, linesize);
	return OAKENGINE_OK;
}

/* ---- Texture queries ---------------------------------------------------- */

int oakengine_display_texture_get_params(const void *texture,
										 oak_video_params *out)
{
	if (!texture || !out) {
		return OAKENGINE_E_INVALID;
	}
	*out = cpp_to_pod(tex(const_cast<void *>(texture))->ptr->params());
	return OAKENGINE_OK;
}

int oakengine_display_texture_id(const void *texture)
{
	if (!texture) {
		return 0;
	}
	return tex(const_cast<void *>(texture))->ptr->id().toInt();
}

int oakengine_display_texture_is_dummy(const void *texture)
{
	if (!texture) {
		return 1;
	}
	return tex(const_cast<void *>(texture))->ptr->is_dummy() ? 1 : 0;
}

void *oakengine_display_texture_renderer(const void *texture)
{
	if (!texture) {
		return nullptr;
	}
	return tex(const_cast<void *>(texture))->ptr->renderer();
}

int oakengine_display_texture_width(const void *texture)
{
	if (!texture) {
		return 0;
	}
	return tex(const_cast<void *>(texture))->ptr->width();
}

int oakengine_display_texture_height(const void *texture)
{
	if (!texture) {
		return 0;
	}
	return tex(const_cast<void *>(texture))->ptr->height();
}

int oakengine_display_texture_format(const void *texture)
{
	if (!texture) {
		return 0;
	}
	return static_cast<int>(tex(const_cast<void *>(texture))->ptr->format());
}

int oakengine_display_texture_channel_count(const void *texture)
{
	if (!texture) {
		return 0;
	}
	return tex(const_cast<void *>(texture))->ptr->channel_count();
}

int oakengine_display_texture_params_equal(const void *a, const void *b)
{
	if (!a || !b) {
		return 0;
	}
	return tex(const_cast<void *>(a))->ptr->params() ==
		   tex(const_cast<void *>(b))->ptr->params();
}

/* ---- Frame handle ------------------------------------------------------- */

void *oakengine_codec_frame_create(void)
{
	return new OakEngineCodecFrame{olive::Frame::create(), {1}};
}

void *oakengine_codec_frame_retain(void *frame)
{
	if (!frame) {
		return nullptr;
	}
	frm(frame)->refcount.fetch_add(1, std::memory_order_relaxed);
	return frame;
}

void oakengine_codec_frame_free(void *frame)
{
	if (!frame) {
		return;
	}
	if (frm(frame)->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete frm(frame);
	}
}

int oakengine_codec_frame_set_video_params(void *frame,
										   const oak_video_params *params)
{
	if (!frame || !params) {
		return OAKENGINE_E_INVALID;
	}
	frm(frame)->ptr->set_video_params(pod_to_cpp(*params));
	return OAKENGINE_OK;
}

int oakengine_codec_frame_get_params(const void *frame, oak_video_params *out)
{
	if (!frame || !out) {
		return OAKENGINE_E_INVALID;
	}
	*out = cpp_to_pod(frm(const_cast<void *>(frame))->ptr->video_params());
	return OAKENGINE_OK;
}

int oakengine_codec_frame_allocate(void *frame)
{
	if (!frame) {
		return 0;
	}
	return frm(frame)->ptr->allocate() ? 1 : 0;
}

void *oakengine_codec_frame_data(void *frame)
{
	if (!frame) {
		return nullptr;
	}
	return frm(frame)->ptr->data();
}

const void *oakengine_codec_frame_const_data(const void *frame)
{
	if (!frame) {
		return nullptr;
	}
	return frm(const_cast<void *>(frame))->ptr->const_data();
}

int oakengine_codec_frame_linesize(const void *frame)
{
	if (!frame) {
		return 0;
	}
	return frm(const_cast<void *>(frame))->ptr->linesize_pixels();
}

int oakengine_codec_frame_linesize_bytes(const void *frame)
{
	if (!frame) {
		return 0;
	}
	return frm(const_cast<void *>(frame))->ptr->linesize_bytes();
}

/* ---- Frame queries ------------------------------------------------------ */

int oakengine_codec_frame_width(const void *frame)
{
	if (!frame) {
		return 0;
	}
	return frm(const_cast<void *>(frame))->ptr->width();
}

int oakengine_codec_frame_height(const void *frame)
{
	if (!frame) {
		return 0;
	}
	return frm(const_cast<void *>(frame))->ptr->height();
}

int oakengine_codec_frame_format(const void *frame)
{
	if (!frame) {
		return 0;
	}
	return static_cast<int>(frm(const_cast<void *>(frame))->ptr->format());
}

int oakengine_codec_frame_channel_count(const void *frame)
{
	if (!frame) {
		return 0;
	}
	return frm(const_cast<void *>(frame))->ptr->channel_count();
}

int oakengine_codec_frame_is_allocated(const void *frame)
{
	if (!frame) {
		return 0;
	}
	return frm(const_cast<void *>(frame))->ptr->is_allocated() ? 1 : 0;
}

/* ---- Color-managed blit ------------------------------------------------- */

int oakengine_display_renderer_blit_color_managed(
	void *renderer, const oak_color_transform_job *job,
	void *dst_texture, const oak_video_params *params)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !job) {
		return OAKENGINE_E_INVALID;
	}

	olive::ColorTransformJob ctj;
	if (job->processor) {
		ctj.set_color_processor(
			static_cast<const OakEngineColorProcessor *>(job->processor)->ptr);
	}
	if (job->input_texture) {
		ctj.set_input_texture(tex(job->input_texture)->ptr);
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
	}

	return OAKENGINE_OK;
}

/* ---- Cross-backend texture download ------------------------------------- */

int oakengine_display_renderer_download_from_texture(
	void *renderer, int texture_id, const oak_video_params *params,
	void *dst_pixels, int linesize)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !params || !dst_pixels) {
		return OAKENGINE_E_INVALID;
	}
	r->download_from_texture(texture_id, pod_to_cpp(*params), dst_pixels,
							 linesize);
	return OAKENGINE_OK;
}

/* ---- Shader management -------------------------------------------------- */

void *oakengine_display_renderer_create_shader(void *renderer,
											   const char *frag_src,
											   const char *vert_src)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !frag_src) {
		return nullptr;
	}
	olive::ShaderCode code(QString::fromUtf8(frag_src),
						   vert_src ? QString::fromUtf8(vert_src) : QString());
	QVariant *v = new QVariant(r->create_native_shader(code));
	return v;
}

void *oakengine_display_renderer_create_blank_shader(void *renderer)
{
	olive::Renderer *r = ren(renderer);
	if (!r) {
		return nullptr;
	}
	QVariant *v = new QVariant(r->create_native_shader(olive::ShaderCode()));
	return v;
}

void oakengine_display_renderer_destroy_shader(void *renderer, void *shader)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !shader) {
		return;
	}
	QVariant *v = static_cast<QVariant *>(shader);
	r->destroy_native_shader(*v);
	delete v;
}

/* ---- Shader blit operations --------------------------------------------- */

int oakengine_display_renderer_blit_shader(void *renderer, void *shader,
										   void *texture,
										   const oak_video_params *viewport_params)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !shader || !texture || !viewport_params) {
		return OAKENGINE_E_INVALID;
	}
	QVariant *sv = static_cast<QVariant *>(shader);
	olive::ShaderJob job;
	job.insert(QStringLiteral("ove_maintex"),
			   olive::NodeValue(olive::NodeValue::k_texture,
								QVariant::fromValue(tex(texture)->ptr)));
	r->blit(*sv, job, pod_to_cpp(*viewport_params));
	return OAKENGINE_OK;
}

int oakengine_display_renderer_blit_shader_to_texture(
	void *renderer, void *shader, void *texture, void *dst_texture)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !shader || !texture || !dst_texture) {
		return OAKENGINE_E_INVALID;
	}
	QVariant *sv = static_cast<QVariant *>(shader);
	olive::ShaderJob job;
	job.insert(QStringLiteral("ove_maintex"),
			   olive::NodeValue(olive::NodeValue::k_texture,
								QVariant::fromValue(tex(texture)->ptr)));
	r->blit_to_texture(*sv, job, tex(dst_texture)->ptr.get());
	return OAKENGINE_OK;
}

int oakengine_display_renderer_blit_shader_vec2_to_texture(
	void *renderer, void *shader, void *texture,
	const char *vec2_name, float vec2_x, float vec2_y,
	void *dst_texture)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !shader || !texture || !dst_texture) {
		return OAKENGINE_E_INVALID;
	}
	QVariant *sv = static_cast<QVariant *>(shader);
	olive::ShaderJob job;
	job.insert(QStringLiteral("ove_maintex"),
			   olive::NodeValue(olive::NodeValue::k_texture,
								QVariant::fromValue(tex(texture)->ptr)));
	if (vec2_name) {
		job.insert(QString::fromUtf8(vec2_name),
				   olive::NodeValue(olive::NodeValue::k_vec2,
									QVector2D(vec2_x, vec2_y)));
	}
	r->blit_to_texture(*sv, job, tex(dst_texture)->ptr.get());
	return OAKENGINE_OK;
}

int oakengine_display_renderer_blit_blank(
	void *renderer, void *shader,
	const float *mvp_matrix, const float *crop_matrix,
	const oak_video_params *params)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !shader || !params) {
		return OAKENGINE_E_INVALID;
	}
	QVariant *sv = static_cast<QVariant *>(shader);
	olive::ShaderJob job;
	job.insert(QStringLiteral("ove_mvpmat"),
			   olive::NodeValue(olive::NodeValue::k_matrix,
								mat_from_float(mvp_matrix)));
	job.insert(QStringLiteral("ove_cropmatrix"),
			   olive::NodeValue(olive::NodeValue::k_matrix,
								mat_from_float(crop_matrix)));
	r->blit(*sv, job, pod_to_cpp(*params), false);
	return OAKENGINE_OK;
}

int oakengine_display_renderer_blit_shader_multi(
	void *renderer, void *shader,
	const char *const *names, void *const *textures, int count,
	void *dst_texture)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !shader || !names || !textures || count <= 0 || !dst_texture) {
		return OAKENGINE_E_INVALID;
	}
	QVariant *sv = static_cast<QVariant *>(shader);
	olive::ShaderJob job;
	for (int i = 0; i < count; i++) {
		if (names[i] && textures[i]) {
			job.insert(QString::fromUtf8(names[i]),
					   olive::NodeValue(olive::NodeValue::k_texture,
										QVariant::fromValue(tex(textures[i])->ptr)));
		}
	}
	r->blit_to_texture(*sv, job, tex(dst_texture)->ptr.get());
	return OAKENGINE_OK;
}

int oakengine_display_renderer_blit_shader_uniforms(
	void *renderer, void *shader, void *texture,
	const oak_shader_uniform *uniforms, int uniform_count,
	void *dst_texture, const oak_video_params *params)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !shader || !texture) {
		return OAKENGINE_E_INVALID;
	}
	QVariant *sv = static_cast<QVariant *>(shader);
	olive::ShaderJob job;
	job.insert(QStringLiteral("ove_maintex"),
			   olive::NodeValue(olive::NodeValue::k_texture,
								QVariant::fromValue(tex(texture)->ptr)));
	for (int i = 0; i < uniform_count; i++) {
		if (!uniforms[i].name) {
			continue;
		}
		QString name = QString::fromUtf8(uniforms[i].name);
		switch (uniforms[i].type) {
		case 0: // float
			job.insert(name, olive::NodeValue(olive::NodeValue::k_float,
											  uniforms[i].values[0]));
			break;
		case 1: // vec2
			job.insert(name, olive::NodeValue(olive::NodeValue::k_vec2,
											  QVector2D(uniforms[i].values[0],
														 uniforms[i].values[1])));
			break;
		case 2: // int/bool
			job.insert(name, olive::NodeValue(olive::NodeValue::k_boolean,
											  static_cast<int>(uniforms[i].values[0]) != 0));
			break;
		case 3: // vec3
			job.insert(name, olive::NodeValue(olive::NodeValue::k_vec3,
											  QVector3D(uniforms[i].values[0],
														 uniforms[i].values[1],
														 uniforms[i].values[2])));
			break;
		default:
			break;
		}
	}
	if (dst_texture) {
		r->blit_to_texture(*sv, job, tex(dst_texture)->ptr.get());
	} else if (params) {
		r->blit(*sv, job, pod_to_cpp(*params));
	} else {
		return OAKENGINE_E_INVALID;
	}
	return OAKENGINE_OK;
}

/* ---- Renderer clear ----------------------------------------------------- */

void oakengine_display_renderer_clear(void *renderer, double r, double g,
									  double b)
{
	olive::Renderer *rn = ren(renderer);
	if (!rn) {
		return;
	}
	rn->clear_destination(nullptr, r, g, b);
}

/* ---- Pixel readback ----------------------------------------------------- */

int oakengine_display_renderer_get_pixel(void *renderer, void *texture,
										 int x, int y, double *out_rgba)
{
	olive::Renderer *r = ren(renderer);
	if (!r || !texture || !out_rgba) {
		return OAKENGINE_E_INVALID;
	}
	olive::Color c = r->get_pixel_from_texture(tex(texture)->ptr.get(),
											   QPoint(x, y));
	out_rgba[0] = c.red();
	out_rgba[1] = c.green();
	out_rgba[2] = c.blue();
	out_rgba[3] = c.alpha();
	return OAKENGINE_OK;
}

} // extern "C"

/* ---- Internal C++ helpers (not exported via C ABI) ---------------------- */

void *oakengine_internal_wrap_texture(const olive::TexturePtr &tp)
{
	if (!tp) {
		return nullptr;
	}
	auto *blk = new OakEngineDisplayTexture;
	blk->ptr = tp;
	blk->refcount.store(1);
	return blk;
}

void *oakengine_internal_wrap_frame(const olive::FramePtr &fp)
{
	if (!fp) {
		return nullptr;
	}
	auto *blk = new OakEngineCodecFrame;
	blk->ptr = fp;
	blk->refcount.store(1);
	return blk;
}

olive::TexturePtr oakengine_internal_unwrap_texture(void *handle)
{
	if (!handle) {
		return nullptr;
	}
	return tex(handle)->ptr;
}

olive::FramePtr oakengine_internal_unwrap_frame(void *handle)
{
	if (!handle) {
		return nullptr;
	}
	return frm(handle)->ptr;
}
