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

#include "config/config.h"
#include "render/job/colortransformjob.h"

namespace olive
{

#define super ManagedDisplayWidget

ScopeBase::ScopeBase(QWidget *parent)
	: super(parent)
	, texture_(nullptr)
	, managed_tex_up_to_date_(false)
	, software_image_up_to_date_(false)
{
	enable_default_context_menu();
}

void ScopeBase::set_buffer(TexturePtr frame)
{
	texture_ = frame;
	managed_tex_up_to_date_ = false;
	software_image_up_to_date_ = false;
	update();
}

void ScopeBase::showEvent(QShowEvent *e)
{
	super::showEvent(e);
}

void ScopeBase::draw_scope(TexturePtr managed_tex, QVariant pipeline)
{
	ShaderJob job;

	job.insert(QStringLiteral("ove_maintex"),
			   NodeValue(NodeValue::k_texture,
						 QVariant::fromValue(managed_tex)));

	renderer()->blit(pipeline, job, get_viewport_params());
}

void ScopeBase::update_software_image()
{
	if (!texture_ || texture_->is_dummy() || !renderer()) {
		software_image_ = QImage();
		software_image_up_to_date_ = true;
		return;
	}

	// Backend-neutral widgets each have their own renderer instance (e.g. a
	// separate Vulkan device). The reference texture emitted by the viewer lives
	// in the viewer's renderer, so we must download it to the CPU and re-upload
	// it into this scope's renderer before we can sample it.
	TexturePtr source_tex = texture_;
	if (texture_->renderer() && texture_->renderer() != renderer()) {
		FramePtr temp_frame = Frame::create();
		temp_frame->set_video_params(texture_->params());
		temp_frame->allocate();
		texture_->download(temp_frame->data(), temp_frame->linesize_pixels());

		local_texture_ = renderer()->create_texture(
			temp_frame->video_params(), temp_frame->data(),
			temp_frame->linesize_pixels());
		source_tex = local_texture_;
	}

	if (!source_tex || source_tex->is_dummy()) {
		software_image_ = QImage();
		software_image_up_to_date_ = true;
		return;
	}

	const int texture_width = static_cast<int>(width() * devicePixelRatioF());
	const int texture_height = static_cast<int>(height() * devicePixelRatioF());

	const VideoParams offscreen_params(texture_width, texture_height,
									   PixelFormat::u8,
									   VideoParams::k_rgba_channel_count);

	if (!software_tex_ || software_tex_->params() != offscreen_params) {
		software_tex_ = renderer()->create_texture(offscreen_params);
		software_buffer_.resize(
			texture_width * texture_height *
			VideoParams::get_bytes_per_pixel(PixelFormat::u8,
										  VideoParams::k_rgba_channel_count));
	}

	if (!software_tex_ || software_tex_->is_dummy()) {
		software_image_ = QImage();
		software_image_up_to_date_ = true;
		return;
	}

	ColorTransformJob job;
	job.set_color_processor(color_service());
	job.set_input_texture(source_tex);
	job.set_input_alpha_association(k_alpha_none);
	job.set_clear_destination_enabled(true);
	job.set_force_opaque(true);

	renderer()->blit_color_managed(job, software_tex_.get());
	renderer()->download_from_texture(software_tex_->id(),
									software_tex_->params(),
									software_buffer_.data(), 0);

	software_image_ = QImage(
		reinterpret_cast<const uchar *>(software_buffer_.constData()),
		texture_width, texture_height,
		texture_width * VideoParams::get_bytes_per_pixel(
							PixelFormat::u8, VideoParams::k_rgba_channel_count),
		QImage::Format_RGBA8888_Premultiplied);
	software_image_.setDevicePixelRatio(devicePixelRatioF());

	software_image_up_to_date_ = true;
}

void ScopeBase::on_init()
{
	super::on_init();

	if (!is_backend_neutral()) {
		pipeline_ = renderer()->create_native_shader(generate_shader_code());
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
	renderer()->clear_destination();

	if (texture_) {
		// Convert reference frame to display space
		if (!managed_tex_ || !managed_tex_up_to_date_ ||
			managed_tex_->params() != texture_->params()) {
			managed_tex_ = renderer()->create_texture(texture_->params());

			ColorTransformJob job;
			job.set_color_processor(color_service());
			job.set_input_texture(texture_);
			job.set_input_alpha_association(k_alpha_none);

			renderer()->blit_color_managed(job, managed_tex_.get());
			managed_tex_up_to_date_ = true;
		}

		draw_scope(managed_tex_, pipeline_);
	}
}

void ScopeBase::on_destroy()
{
	local_texture_ = nullptr;
	software_tex_ = nullptr;
	software_buffer_.clear();
	software_image_ = QImage();
	managed_tex_ = nullptr;
	texture_ = nullptr;
	pipeline_.clear();

	super::on_destroy();
}

}
