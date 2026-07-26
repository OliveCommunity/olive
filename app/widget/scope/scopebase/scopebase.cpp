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

#include "scopebase.h"

#include <QPainter>

#include "common/configwrapper.h"
#include "oakengine/display.h"
#include "oakengine/videoparams.h"

namespace olive
{

#define super ManagedDisplayWidget

ScopeBase::ScopeBase(QWidget *parent)
	: super(parent)
	, pipeline_(nullptr)
	, texture_(nullptr)
	, managed_tex_(nullptr)
	, managed_tex_up_to_date_(false)
	, software_tex_(nullptr)
	, software_image_up_to_date_(false)
	, local_texture_(nullptr)
{
	enable_default_context_menu();
}

void ScopeBase::set_buffer(void *frame)
{
	if (texture_) {
		oakengine_display_texture_free(texture_);
	}
	texture_ = oakengine_display_texture_retain(frame);
	managed_tex_up_to_date_ = false;
	software_image_up_to_date_ = false;
	update();
}

void ScopeBase::showEvent(QShowEvent *e)
{
	super::showEvent(e);
}

void ScopeBase::draw_scope(void *managed_tex, void *pipeline)
{
	oak_video_params vp = get_viewport_params();
	oakengine_display_renderer_blit_shader(renderer(), pipeline, managed_tex,
										   &vp);
}

void ScopeBase::update_software_image()
{
	if (!texture_ || oakengine_display_texture_is_dummy(texture_) ||
		!renderer()) {
		software_image_ = QImage();
		software_image_up_to_date_ = true;
		return;
	}

	// Backend-neutral widgets each have their own renderer instance (e.g. a
	// separate Vulkan device). The reference texture emitted by the viewer lives
	// in the viewer's renderer, so we must download it to the CPU and re-upload
	// it into this scope's renderer before we can sample it.
	void *source_tex = texture_;
	void *tex_renderer = oakengine_display_texture_renderer(texture_);
	if (tex_renderer && tex_renderer != renderer()) {
		void *temp_frame = oakengine_codec_frame_create();
		oak_video_params tex_params = {};
		oakengine_display_texture_get_params(texture_, &tex_params);
		oakengine_codec_frame_set_video_params(temp_frame, &tex_params);
		oakengine_codec_frame_allocate(temp_frame);
		oakengine_display_texture_download(
			texture_, oakengine_codec_frame_data(temp_frame),
			oakengine_codec_frame_linesize(temp_frame));

		if (local_texture_) {
			oakengine_display_texture_free(local_texture_);
		}
		local_texture_ = oakengine_display_texture_create(
			renderer(), &tex_params, oakengine_codec_frame_const_data(temp_frame),
			oakengine_codec_frame_linesize(temp_frame));
		oakengine_codec_frame_free(temp_frame);
		source_tex = local_texture_;
	}

	if (!source_tex || oakengine_display_texture_is_dummy(source_tex)) {
		software_image_ = QImage();
		software_image_up_to_date_ = true;
		return;
	}

	const int texture_width = static_cast<int>(width() * devicePixelRatioF());
	const int texture_height = static_cast<int>(height() * devicePixelRatioF());

	oak_video_params offscreen_pod = {};
	offscreen_pod.width = texture_width;
	offscreen_pod.height = texture_height;
	offscreen_pod.format = 0; // PixelFormat::u8

	bool need_recreate = false;
	if (!software_tex_) {
		need_recreate = true;
	} else {
		oak_video_params cur = {};
		oakengine_display_texture_get_params(software_tex_, &cur);
		if (cur.width != offscreen_pod.width ||
			cur.height != offscreen_pod.height ||
			cur.format != offscreen_pod.format) {
			need_recreate = true;
		}
	}

	if (need_recreate) {
		if (software_tex_) {
			oakengine_display_texture_free(software_tex_);
		}
		software_tex_ = oakengine_display_texture_create(
			renderer(), &offscreen_pod, nullptr, 0);
		software_buffer_.resize(texture_width * texture_height *
								oakengine_video_params_bytes_per_pixel(0, 4));
	}

	if (!software_tex_ || oakengine_display_texture_is_dummy(software_tex_)) {
		software_image_ = QImage();
		software_image_up_to_date_ = true;
		return;
	}

	oak_color_transform_job job = {};
	job.processor = color_service().get();
	job.input_texture = source_tex;
	job.input_alpha_association = 0; // k_alpha_none
	job.clear_destination = 1;
	job.force_opaque = 1;

	oakengine_display_renderer_blit_color_managed(renderer(), &job,
												  software_tex_, nullptr);

	int sw_id = oakengine_display_texture_id(software_tex_);
	oak_video_params sw_params = {};
	oakengine_display_texture_get_params(software_tex_, &sw_params);
	oakengine_display_renderer_download_from_texture(
		renderer(), sw_id, &sw_params, software_buffer_.data(), 0);

	software_image_ = QImage(
		reinterpret_cast<const uchar *>(software_buffer_.constData()),
		texture_width, texture_height,
		texture_width * oakengine_video_params_bytes_per_pixel(0, 4),
		QImage::Format_RGBA8888_Premultiplied);
	software_image_.setDevicePixelRatio(devicePixelRatioF());

	software_image_up_to_date_ = true;
}

void ScopeBase::on_init()
{
	super::on_init();

	if (!is_backend_neutral()) {
		ScopeShaderCode code = generate_shader_code();
		pipeline_ = oakengine_display_renderer_create_shader(
			renderer(), code.frag.toUtf8().constData(),
			code.vert.isEmpty() ? nullptr
								: code.vert.toUtf8().constData());
	}
}

void ScopeBase::on_paint()
{
	if (is_backend_neutral()) {
		if (!software_image_up_to_date_) {
			update_software_image();
		}

		QPainter p(paint_device());
		p.fillRect(rect(), Qt::black);

		if (!software_image_.isNull()) {
			draw_scope_software(p, software_image_);
		}
		return;
	}

	// Clear display surface
	oakengine_display_renderer_clear(renderer(), 0.0, 0.0, 0.0);

	if (texture_) {
		// Convert reference frame to display space
		bool need_recreate = false;
		if (!managed_tex_ || !managed_tex_up_to_date_) {
			need_recreate = true;
		} else if (!oakengine_display_texture_params_equal(managed_tex_,
														   texture_)) {
			need_recreate = true;
		}

		if (need_recreate) {
			if (managed_tex_) {
				oakengine_display_texture_free(managed_tex_);
			}
			oak_video_params tex_params = {};
			oakengine_display_texture_get_params(texture_, &tex_params);
			managed_tex_ = oakengine_display_texture_create(
				renderer(), &tex_params, nullptr, 0);

			oak_color_transform_job job = {};
			job.processor = color_service().get();
			job.input_texture = texture_;
			job.input_alpha_association = 0; // k_alpha_none
			job.clear_destination = 0;
			job.force_opaque = 0;

			oakengine_display_renderer_blit_color_managed(
				renderer(), &job, managed_tex_, nullptr);
			managed_tex_up_to_date_ = true;
		}

		draw_scope(managed_tex_, pipeline_);
	}
}

void ScopeBase::on_destroy()
{
	if (local_texture_) {
		oakengine_display_texture_free(local_texture_);
		local_texture_ = nullptr;
	}
	if (software_tex_) {
		oakengine_display_texture_free(software_tex_);
		software_tex_ = nullptr;
	}
	software_buffer_.clear();
	software_image_ = QImage();
	if (managed_tex_) {
		oakengine_display_texture_free(managed_tex_);
		managed_tex_ = nullptr;
	}
	if (texture_) {
		oakengine_display_texture_free(texture_);
		texture_ = nullptr;
	}
	if (pipeline_) {
		oakengine_display_renderer_destroy_shader(renderer(), pipeline_);
		pipeline_ = nullptr;
	}

	super::on_destroy();
}

}
