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

#include "renderer.h"

#include <cstdio>

#include "filefunctions.h"
#include "node.h"
#include "render/colorprocessor.h"
#include "render/job/colortransformjob.h"
#include "render/job/shaderjob.h"

namespace olive
{

namespace
{
// Replaces the first "%1" marker, mirroring the QString::arg() call the
// shader template substitution below used before de-Qt.
std::string arg1(const std::string &fmt, const std::string &arg)
{
	std::string result = fmt;
	std::string::size_type pos = result.find("%1");
	if (pos != std::string::npos) {
		result.replace(pos, 2, arg);
	}
	return result;
}
}

bool Renderer::get_color_context(const ColorTransformJob &color_job,
							   Renderer::ColorContext *ctx)
{
	std::unique_lock<std::mutex> locker(color_cache_mutex_);

	ColorContext &color_ctx = *ctx;

	std::string proc_id = color_job.id();

	if (color_cache_.count(proc_id)) {
		color_ctx = color_cache_.at(proc_id);
		return true;
	} else {
		locker.unlock();

		// Create shader description
		std::string ocio_func_name;
		if (color_job.get_function_name().empty()) {
			ocio_func_name = "OCIODisplay";
		} else {
			ocio_func_name = color_job.get_function_name();
		}
		auto shader_desc = ocio::GpuShaderDesc::CreateShaderDesc();
		shader_desc->setLanguage(ocio::GPU_LANGUAGE_GLSL_ES_3_0);
		shader_desc->setFunctionName(ocio_func_name.c_str());
		shader_desc->setResourcePrefix("ocio_");

		// Generate shader
		color_job.get_color_processor()
			->get_processor()
			->getDefaultGPUProcessor()
			->extractGpuShaderInfo(shader_desc);

		ShaderCode code;
		if (const Node *shader_src = color_job.custom_shader_source()) {
			// Use shader code from associated node
			code = shader_src->get_shader_code(
				{ color_job.custom_shader_id(), shader_desc->getShaderText() });
		} else {
			// Generate shader code using OCIO stub and our auto-generated name
			code = FileFunctions::read_file_as_string(
				":/shaders/colormanage.frag");
			code.set_frag_code(
				arg1(code.frag_code(), shader_desc->getShaderText()));
		}

		// Try to compile shader
		color_ctx.compiled_shader = create_native_shader(code);

		if (color_ctx.compiled_shader.is_null()) {
			return false;
		}

		color_ctx.lut3d_textures.resize(shader_desc->getNum3DTextures());
		for (unsigned int i = 0; i < shader_desc->getNum3DTextures(); i++) {
			const char *tex_name = nullptr;
			const char *sampler_name = nullptr;
			unsigned int edge_len = 0;
			ocio::Interpolation interpolation = ocio::INTERP_LINEAR;

			shader_desc->get3DTexture(i, tex_name, sampler_name, edge_len,
									  interpolation);

			if (!tex_name || !*tex_name || !sampler_name || !*sampler_name ||
				!edge_len) {
				fprintf(stderr, "3D LUT texture data is corrupted\n");
				return false;
			}

			const float *values = nullptr;
			shader_desc->get3DTextureValues(i, values);
			if (!values) {
				fprintf(stderr, "3D LUT texture values are missing\n");
				return false;
			}

			// Allocate 3D LUT
			color_ctx.lut3d_textures[i].texture = create_texture(
				VideoParams(edge_len, edge_len, edge_len, PixelFormat::f32,
							VideoParams::k_rgb_channel_count),
				values);
			color_ctx.lut3d_textures[i].name = sampler_name;
			color_ctx.lut3d_textures[i].interpolation =
				(interpolation == ocio::INTERP_NEAREST) ? Texture::k_nearest :
														  Texture::k_linear;
		}

		color_ctx.lut1d_textures.resize(shader_desc->getNumTextures());
		for (unsigned int i = 0; i < shader_desc->getNumTextures(); i++) {
			const char *tex_name = nullptr;
			const char *sampler_name = nullptr;
			unsigned int width = 0, height = 0;
			ocio::GpuShaderDesc::TextureType channel =
				ocio::GpuShaderDesc::TEXTURE_RGB_CHANNEL;
			ocio::Interpolation interpolation = ocio::INTERP_LINEAR;
#if OCIO_VERSION_MAJOR > 2 || \
	(OCIO_VERSION_MAJOR == 2 && OCIO_VERSION_MINOR >= 3)
			ocio::GpuShaderDesc::TextureDimensions dimensions =
				ocio::GpuShaderDesc::TEXTURE_2D;
			shader_desc->getTexture(i, tex_name, sampler_name, width, height,
									channel, dimensions, interpolation);
#else
			shader_desc->getTexture(i, tex_name, sampler_name, width, height,
									channel, interpolation);
#endif

			if (!tex_name || !*tex_name || !sampler_name || !*sampler_name ||
				!width) {
				fprintf(stderr, "1D LUT texture data is corrupted\n");
				return false;
			}

			const float *values = nullptr;
			shader_desc->getTextureValues(i, values);
			if (!values) {
				fprintf(stderr, "1D LUT texture values are missing\n");
				return false;
			}

			// Allocate 1D LUT
			int lut_channels =
				(channel == ocio::GpuShaderDesc::TEXTURE_RED_CHANNEL) ?
					1 :
					VideoParams::k_rgb_channel_count;
			VideoParams lut_params(width, height, PixelFormat::f32,
								   lut_channels);
			color_ctx.lut1d_textures[i].texture =
				create_texture(lut_params, values);
			color_ctx.lut1d_textures[i].name = sampler_name;
			color_ctx.lut1d_textures[i].interpolation =
				(interpolation == ocio::INTERP_NEAREST) ? Texture::k_nearest :
														  Texture::k_linear;
		}

		locker.lock();
		color_cache_.insert({ proc_id, color_ctx });

		return true;
	}
}

void Renderer::blit_color_managed(const ColorTransformJob &color_job,
								Texture *destination, const VideoParams &params)
{
	ColorContext color_ctx;
	if (!get_color_context(color_job, &color_ctx)) {
		ShaderJob fallback_job;
		fallback_job.insert("ove_maintex",
							color_job.get_input_texture());
		fallback_job.insert("ove_mvpmat",
							NodeValue(NodeValue::k_matrix,
									  color_job.get_transform_matrix()));

		if (destination) {
			blit_to_texture(get_default_shader(), fallback_job, destination,
						  color_job.is_clear_destination_enabled());
		} else {
			blit(get_default_shader(), fallback_job, params,
				 color_job.is_clear_destination_enabled());
		}
		return;
	}

	ShaderJob job;
	job.insert("ove_maintex", color_job.get_input_texture());
	job.insert("ove_mvpmat",
			   NodeValue(NodeValue::k_matrix, color_job.get_transform_matrix()));
	job.insert("ove_cropmatrix",
			   NodeValue(NodeValue::k_matrix,
						 color_job.get_crop_matrix().inverted()));
	job.insert("ove_maintex_alpha",
			   NodeValue(NodeValue::k_int,
						 int(color_job.get_input_alpha_association())));
	job.insert("ove_force_opaque",
			   NodeValue(NodeValue::k_boolean, color_job.get_force_opaque()));
	job.insert(color_job.get_values());

	for (const ColorContext::LUT &l : color_ctx.lut3d_textures) {
		job.insert(l.name, NodeValue(NodeValue::k_texture,
									 Variant::from_value(l.texture)));
		job.set_interpolation(l.name, l.interpolation);
	}
	for (const ColorContext::LUT &l : color_ctx.lut1d_textures) {
		job.insert(l.name, NodeValue(NodeValue::k_texture,
									 Variant::from_value(l.texture)));
		job.set_interpolation(l.name, l.interpolation);
	}

	if (destination) {
		blit_to_texture(color_ctx.compiled_shader, job, destination,
					  color_job.is_clear_destination_enabled());
	} else {
		blit(color_ctx.compiled_shader, job, params,
			 color_job.is_clear_destination_enabled());
	}
}

}
