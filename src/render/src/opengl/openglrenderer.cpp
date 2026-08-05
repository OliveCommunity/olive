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

#include "openglrenderer.h"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <regex>
#include <thread>
#include <vector>

#include "filefunctions.h"
#if !defined(OAK_RENDER_BACKEND_PLUGIN)
#include "config/config.h"
#endif
#include "render/job/shaderjob.h"

namespace olive
{

const int OpenGLRenderer::k_texture_cache_max_size = 5000;

const std::vector<GLfloat> blit_vertices = { -1.0f, -1.0f, 0.0f, 1.0f,	-1.0f,
											 0.0f,	1.0f,  1.0f, 0.0f,

											 -1.0f, -1.0f, 0.0f, -1.0f, 1.0f,
											 0.0f,	1.0f,  1.0f, 0.0f };

const std::vector<GLfloat> blit_texcoords = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,

											  0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };

class ErrorPrinter {
public:
	ErrorPrinter(const char *name, OpenGLFunctions *f)
	{
		GLuint err = f->glGetError();
		if (err > 0)
			fprintf(stderr, "%s entered with %u\n", name, err);

		name_ = name;
		functions_ = f;
	}

	~ErrorPrinter()
	{
		GLuint err = functions_->glGetError();
		if (err > 0)
			fprintf(stderr, "%s exited with %u\n", name_, err);
	}

private:
	const char *name_;

	OpenGLFunctions *functions_;
};

#define OAK_PRINT_GL_ERRORS ErrorPrinter __e(__FUNCTION__, &functions_)

#define OAK_GL_PREAMBLE //std::lock_guard<std::mutex> __l(global_opengl_mutex);

//std::mutex global_opengl_mutex;

OpenGLRenderer::OpenGLRenderer()
	: context_(nullptr)
	, context_owned_(false)
	, functions_resolved_(false)
	, framebuffer_(0)
{
}

OpenGLRenderer::~OpenGLRenderer()
{
	destroy();
	post_destroy();
}

void OpenGLRenderer::init(OpenGLContext *existing_ctx)
{
	if (context_) {
		fprintf(stderr, "Can't initialize already initialized OpenGLRenderer\n");
		return;
	}

	context_ = existing_ctx;
	context_owned_ = false;
}

bool OpenGLRenderer::init()
{
	OAK_GL_PREAMBLE;

	if (context_) {
		fprintf(stderr, "Can't initialize already initialized OpenGLRenderer\n");
		return false;
	}

	// NOTE: the Qt version shared with QOpenGLContext::globalShareContext() so
	// textures were visible to viewer widgets. The app layer must now hand a
	// share context down (via an external context or a future factory arg);
	// until then offscreen contexts are created unshared.
	context_ = OpenGLContext::create_offscreen();
	if (!context_ || !context_->is_valid()) {
		fprintf(stderr, "Failed to create OpenGL context\n");
		delete context_;
		context_ = nullptr;
		return false;
	}

	context_owned_ = true;

	// The context is bound to the calling (render) thread, mirroring the
	// former moveToThread(this->thread()).
	context_->set_owner_thread(std::this_thread::get_id());

	return true;
}

void OpenGLRenderer::post_destroy()
{
	// Nothing to release here: owned contexts carry their platform surface
	// (EGL pbuffer / WGL window) and free it in destroy_internal()/dtor.
}

void OpenGLRenderer::post_init()
{
	OAK_GL_PREAMBLE;

	if (!context_) {
		fprintf(stderr, "%s called without an OpenGL context\n", __FUNCTION__);
		return;
	}

	if (context_->owner_thread() != std::this_thread::get_id()) {
		fprintf(stderr,
				"%s called from the wrong thread for this OpenGL context\n",
				__FUNCTION__);
		return;
	}

	if (context_->is_current()) {
		functions_resolved_ = context_->resolve_functions(&functions_);
	}
}

void OpenGLRenderer::destroy_internal()
{
	if (context_) {
		OAK_GL_PREAMBLE;

		if (functions_resolved_ && framebuffer_) {
			functions_.glDeleteFramebuffers(1, &framebuffer_);
		}

		// Delete context if it belongs to us
		if (context_owned_) {
			delete context_;
		}
	}

	// functions_ is derived from the context, so it is unusable once the
	// context is gone; reset both regardless of the path taken above.
	framebuffer_ = 0;
	context_ = nullptr;
	context_owned_ = false;
	functions_resolved_ = false;
}

void OpenGLRenderer::clear_destination(Texture *texture, double r, double g,
									  double b, double a)
{
	OAK_GL_PREAMBLE;

	if (!ensure_context_current(__FUNCTION__)) {
		return;
	}

	if (texture) {
		attach_texture_as_destination(texture->id());
	}

	clear_destination_internal(r, g, b, a);

	if (texture) {
		detach_texture_as_destination();
	}
}

Variant OpenGLRenderer::create_native_texture(int width, int height, int depth,
											 PixelFormat format,
											 int channel_count,
											 const void *data, int linesize)
{
	OAK_GL_PREAMBLE;
	if (!ensure_context_current(__FUNCTION__)) {
		return Variant();
	}

	bool is_3d = depth > 1;

	// Generate new texture
	GLuint texture;
	functions_.glGenTextures(1, &texture);
	texture_params_.insert({ texture,
							 { width, height, depth, format, channel_count } });

	functions_.glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize);

	GLenum target_current = is_3d ? GL_TEXTURE_BINDING_3D :
									GL_TEXTURE_BINDING_2D;
	GLenum target = is_3d ? GL_TEXTURE_3D : GL_TEXTURE_2D;

	GLint current_tex;
	functions_.glGetIntegerv(target_current, &current_tex);

	functions_.glBindTexture(target, texture);

	if (is_3d) {
		functions_.glTexImage3D(
			target, 0, get_internal_format(format, channel_count), width, height,
			depth, 0, get_pixel_format(channel_count), get_pixel_type(format),
			data);
	} else {
		functions_.glTexImage2D(
			target, 0, get_internal_format(format, channel_count), width, height,
			0, get_pixel_format(channel_count), get_pixel_type(format), data);
	}

	functions_.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	functions_.glBindTexture(target, current_tex);

	return Variant::from_value(texture);
}

void OpenGLRenderer::attach_texture_as_destination(const Variant &texture)
{
	OAK_PRINT_GL_ERRORS;

	if (!framebuffer_) {
		functions_.glGenFramebuffers(1, &framebuffer_);
	}

	functions_.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
	functions_.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
									  GL_TEXTURE_2D, texture.value<GLuint>(),
									  0);
}

void OpenGLRenderer::detach_texture_as_destination()
{
	// Viewer widgets render to a non-zero default FBO.
	const GLuint default_fbo = context_ ? context_->default_framebuffer() : 0;
	functions_.glBindFramebuffer(GL_FRAMEBUFFER, default_fbo);
}

void OpenGLRenderer::destroy_native_texture(Variant texture)
{
	if (!ensure_context_current(__FUNCTION__)) {
		return;
	}

	GLuint t = texture.value<GLuint>();

	if (t > 0) {
		functions_.glDeleteTextures(1, &t);
	}
}

Variant OpenGLRenderer::create_native_shader(ShaderCode code)
{
	OAK_GL_PREAMBLE;

	if (!ensure_context_current(__FUNCTION__)) {
		return Variant();
	}

	OAK_PRINT_GL_ERRORS;

	GLuint vert = compile_shader(GL_VERTEX_SHADER, code.vert_code());
	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, code.frag_code());

	GLuint program = 0;

	if (frag && vert) {
		program = functions_.glCreateProgram();
		functions_.glAttachShader(program, frag);
		functions_.glAttachShader(program, vert);
		functions_.glLinkProgram(program);

		GLint success;
		functions_.glGetProgramiv(program, GL_LINK_STATUS, &success);
		if (!success) {
			fprintf(stderr, "Failed to link OpenGL shader program\n");
			functions_.glDeleteProgram(program);
			program = 0;
		}
	}

	functions_.glDeleteShader(frag);
	functions_.glDeleteShader(vert);

	return Variant::from_value(program);
}

void OpenGLRenderer::destroy_native_shader(Variant shader)
{
	OAK_GL_PREAMBLE;

	if (!ensure_context_current(__FUNCTION__)) {
		return;
	}

	GLuint program = shader.value<GLuint>();
	functions_.glDeleteProgram(program);
}

void OpenGLRenderer::upload_to_texture(const Variant &handle,
									 const VideoParams &p, const void *data,
									 int linesize)
{
	OAK_GL_PREAMBLE;

	GLuint t = handle.value<GLuint>();

	bool is_3d = p.is_3d();

	GLenum tex_type = !is_3d ? GL_TEXTURE_2D : GL_TEXTURE_3D;
	GLenum tex_binding = !is_3d ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_3D;

	// Store currently bound texture so it can be restored later
	GLint current_tex;
	functions_.glGetIntegerv(tex_binding, &current_tex);

	functions_.glBindTexture(tex_type, t);

	functions_.glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize);

	{
		OAK_PRINT_GL_ERRORS;

		if (!is_3d) {
			functions_.glTexSubImage2D(tex_type, 0, 0, 0, p.effective_width(),
										p.effective_height(),
										get_pixel_format(p.channel_count()),
										get_pixel_type(p.format()), data);
		} else {
			functions_.glTexSubImage3D(
				tex_type, 0, 0, 0, 0, p.effective_width(), p.effective_height(),
				p.effective_depth(), get_pixel_format(p.channel_count()),
				get_pixel_type(p.format()), data);
		}
	}

	functions_.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	functions_.glBindTexture(tex_type, current_tex);
}

void OpenGLRenderer::download_from_texture(const Variant &id,
										 const VideoParams &p, void *data,
										 int linesize)
{
	OAK_GL_PREAMBLE;

	if (!ensure_context_current(__FUNCTION__)) {
		return;
	}

	GLuint texture_id = id.value<GLuint>();
	if (!texture_id || !functions_.glIsTexture(texture_id)) {
		fprintf(stderr, "DownloadFromTexture called with invalid texture\n");
		return;
	}

	GLint current_tex;
	functions_.glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_tex);

	attach_texture_as_destination(id);

	GLenum status = functions_.glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "DownloadFromTexture framebuffer incomplete %u\n",
				status);
		detach_texture_as_destination();
		return;
	}

	functions_.glPixelStorei(GL_PACK_ROW_LENGTH, linesize);

	// Ensure all rendering is complete before reading back on TBDR architectures (macOS)
	functions_.glFinish();

	{
		OAK_PRINT_GL_ERRORS;
		functions_.glReadPixels(0, 0, p.effective_width(),
								p.effective_height(),
								get_pixel_format(p.channel_count()),
								get_pixel_type(p.format()), data);
	}

	functions_.glPixelStorei(GL_PACK_ROW_LENGTH, 0);

	detach_texture_as_destination();

	functions_.glBindTexture(GL_TEXTURE_2D, current_tex);
}

void OpenGLRenderer::flush()
{
	OAK_GL_PREAMBLE;

	if (!ensure_context_current(__FUNCTION__)) {
		return;
	}

#if !defined(OAK_RENDER_BACKEND_PLUGIN)
	if (OAK_CONFIG("UseGLFinish").toBool()) {
		functions_.glFinish();
		return;
	}
#endif

#if defined(__APPLE__)
	// The dynamically loaded OpenGL backend cannot depend on the editor Config
	// singleton because that pulls UI/Core symbols into the plugin. This
	// platform default also preserves the previous macOS-safe behavior for
	// texture consumers on TBDR drivers.
	functions_.glFinish();
#else
	functions_.glFlush();
#endif
}

// Adapts the generic Renderer output attachment hook to OpenGL's framebuffer
// attachment path used by OFX OpenGL rendering.
void OpenGLRenderer::attach_output_texture(olive::Texture *texture)
{
	if (!ensure_context_current(__FUNCTION__)) {
		return;
	}

	if (texture) {
		attach_texture_as_destination(texture->id());
	}
}

// Clears the framebuffer attachment installed by AttachOutputTexture().
void OpenGLRenderer::detach_output_texture()
{
	if (!ensure_context_current(__FUNCTION__)) {
		return;
	}

	detach_texture_as_destination();
}

Color OpenGLRenderer::get_pixel_from_texture(Texture *texture, const PointF &pt)
{
	if (!texture || !ensure_context_current(__FUNCTION__)) {
		return Color();
	}

	attach_texture_as_destination(texture->id());

	std::vector<char> data(VideoParams::get_bytes_per_pixel(
		texture->format(), texture->channel_count()));

	functions_.glReadPixels(static_cast<GLint>(pt.x()),
							static_cast<GLint>(pt.y()), 1, 1,
							get_pixel_format(texture->channel_count()),
							get_pixel_type(texture->format()), data.data());

	Color c = Color::from_data(data.data(), texture->format(),
							  texture->channel_count());

	if (texture->channel_count() == VideoParams::k_rgb_channel_count) {
		// No alpha channel, set to 1.0
		c.set_alpha(1.0);
	}

	detach_texture_as_destination();

	return c;
}

struct TextureToBind {
	TexturePtr texture;
	Texture::Interpolation interpolation;
};

void OpenGLRenderer::blit(Variant s, AcceleratedJob &a_job,
						  Texture *destination, VideoParams destination_params,
						  bool clear_destination)
{
	OAK_GL_PREAMBLE;
	if (!ensure_context_current(__FUNCTION__)) {
		return;
	}
	try {
		if (!destination) {
			// Ensure we're drawing to the default framebuffer for this context.
			GLuint fbo = context_ ? context_->default_framebuffer() : 0;
			functions_.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		}

		ShaderJob &s_job = dynamic_cast<ShaderJob &>(a_job);
		ShaderJob job(s_job);
		// If this node is iterative, we'll pick up which input here
		std::map<std::string, GLuint> texture_index_map;
		std::vector<TextureToBind> textures_to_bind;

		GLuint shader = s.value<GLuint>();

		functions_.glUseProgram(shader);

		for (const auto &pair : job.get_values()) {
			// See if the shader has takes this parameter as an input
			GLint variable_location = functions_.glGetUniformLocation(
				shader, pair.first.c_str());

			if (variable_location == -1) {
				continue;
			}

			// This variable is used in the shader, let's set it
			const NodeValue &value = pair.second;

			// Arrays are not currently supported in this system
			if (value.array()) {
				continue;
			}

			switch (value.type()) {
			case NodeValue::k_int:
				// kInt technically specifies a LongLong, but OpenGL doesn't support those. This may lead to
				// over/underflows if the number is large enough, but the likelihood of that is quite low.
				functions_.glUniform1i(variable_location,
									   static_cast<GLint>(value.to_int()));
				break;
			default:
				break;
			case NodeValue::k_float:
				// kFloat technically specifies a double but as above, OpenGL doesn't support those.
				functions_.glUniform1f(variable_location,
									   static_cast<GLfloat>(value.to_double()));
				break;
			case NodeValue::k_vec2: {
				Vector2D v = value.to_vec2();
				functions_.glUniform2fv(variable_location, 1,
										reinterpret_cast<const GLfloat *>(&v));
				break;
			}
			case NodeValue::k_vec3: {
				Vector3D v = value.to_vec3();
				functions_.glUniform3fv(variable_location, 1,
										reinterpret_cast<const GLfloat *>(&v));
				break;
			}
			case NodeValue::k_vec4: {
				Vector4D v = value.to_vec4();
				functions_.glUniform4fv(variable_location, 1,
										reinterpret_cast<const GLfloat *>(&v));
				break;
			}
			case NodeValue::k_matrix:
				functions_.glUniformMatrix4fv(variable_location, 1, false,
											  value.to_matrix().const_data());
				break;
			case NodeValue::k_combo:
				functions_.glUniform1i(variable_location,
									   static_cast<GLint>(value.to_int()));
				break;
			case NodeValue::k_color: {
				Color color = value.to_color();
				functions_.glUniform4f(variable_location,
									   static_cast<GLfloat>(color.red()),
									   static_cast<GLfloat>(color.green()),
									   static_cast<GLfloat>(color.blue()),
									   static_cast<GLfloat>(color.alpha()));
				break;
			}
			case NodeValue::k_boolean:
				functions_.glUniform1i(variable_location, value.to_bool());
				break;
			case NodeValue::k_texture: {
				TexturePtr texture = value.to_texture();

				// Set value to bound texture
				functions_.glUniform1i(variable_location,
									   static_cast<GLint>(
										   textures_to_bind.size()));

				texture_index_map.insert(
					{ pair.first,
					  static_cast<GLuint>(textures_to_bind.size()) });

				textures_to_bind.push_back(
					{ texture, job.get_interpolation(pair.first) });

				// Set enable flag if shader wants it
				GLuint tex_id = texture ? texture->id().value<GLuint>() : 0;
				std::string enabled_name = pair.first + "_enabled";
				int enable_param_location = functions_.glGetUniformLocation(
					shader, enabled_name.c_str());
				if (enable_param_location > -1) {
					functions_.glUniform1i(enable_param_location, tex_id > 0);
				}
				break;
			}
			case NodeValue::k_samples:
			case NodeValue::k_text:
			case NodeValue::k_rational:
			case NodeValue::k_font:
			case NodeValue::k_file:
			case NodeValue::k_video_params:
			case NodeValue::k_audio_params:
			case NodeValue::k_subtitle_params:
			case NodeValue::k_bezier:
			case NodeValue::k_binary:
			case NodeValue::k_none:
			case NodeValue::k_data_type_count:
				break;
			}
		}

		// Bind all textures
		for (int i = 0; i < int(textures_to_bind.size()); i++) {
			const TextureToBind &t = textures_to_bind.at(i);
			TexturePtr texture = t.texture;

			GLuint tex_id = texture ? texture->id().value<GLuint>() : 0;

			functions_.glActiveTexture(GL_TEXTURE0 + i);

			GLenum target = (texture && texture->params().is_3d()) ?
								GL_TEXTURE_3D :
								GL_TEXTURE_2D;
			functions_.glBindTexture(target, tex_id);

			if (tex_id) {
				prepare_input_texture(target, t.interpolation);

				if (texture->channel_count() == 1 &&
					destination_params.channel_count() != 1) {
					// Interpret this texture as a grayscale texture
					functions_.glTexParameteri(GL_TEXTURE_2D,
											   GL_TEXTURE_SWIZZLE_R, GL_RED);
					functions_.glTexParameteri(GL_TEXTURE_2D,
											   GL_TEXTURE_SWIZZLE_G, GL_RED);
					functions_.glTexParameteri(GL_TEXTURE_2D,
											   GL_TEXTURE_SWIZZLE_B, GL_RED);
				}
			}
		}

		// Ensure matrix is set, at least to identity
		GLint mvpmat_location =
			functions_.glGetUniformLocation(shader, "ove_mvpmat");
		if (mvpmat_location > -1) {
			functions_.glUniformMatrix4fv(
				mvpmat_location, 1, false,
				job.get("ove_mvpmat").to_matrix().const_data());
		}

		// Set the viewport to the "physical" resolution of the destination
		functions_.glViewport(0, 0, destination_params.effective_width(),
							  destination_params.effective_height());

		// Bind vertex array object
		GLuint vao = 0;
		functions_.glGenVertexArrays(1, &vao);
		functions_.glBindVertexArray(vao);

		// Set buffers
		GLuint vert_vbo = 0;
		functions_.glGenBuffers(1, &vert_vbo);
		functions_.glBindBuffer(GL_ARRAY_BUFFER, vert_vbo);
		// If the job has vertex coordinate overrides use them instead of the defaults.
		if (!job.get_vertex_coordinates().empty()) {
			assert(job.get_vertex_coordinates().size() == 18);
			functions_.glBufferData(
				GL_ARRAY_BUFFER,
				job.get_vertex_coordinates().size() * sizeof(float),
				job.get_vertex_coordinates().data(), GL_STATIC_DRAW);
		} else {
			functions_.glBufferData(GL_ARRAY_BUFFER,
									blit_vertices.size() * sizeof(GLfloat),
									blit_vertices.data(), GL_STATIC_DRAW);
		}
		functions_.glBindBuffer(GL_ARRAY_BUFFER, 0);

		GLuint frag_vbo = 0;
		functions_.glGenBuffers(1, &frag_vbo);
		functions_.glBindBuffer(GL_ARRAY_BUFFER, frag_vbo);
		functions_.glBufferData(GL_ARRAY_BUFFER,
								blit_texcoords.size() * sizeof(GLfloat),
								blit_texcoords.data(), GL_STATIC_DRAW);
		functions_.glBindBuffer(GL_ARRAY_BUFFER, 0);

		GLint vertex_location =
			functions_.glGetAttribLocation(shader, "a_position");
		if (vertex_location != -1) {
			functions_.glBindBuffer(GL_ARRAY_BUFFER, vert_vbo);
			functions_.glEnableVertexAttribArray(vertex_location);
			functions_.glVertexAttribPointer(vertex_location, 3, GL_FLOAT,
											 GL_FALSE, 0, nullptr);
			functions_.glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		GLint tex_location =
			functions_.glGetAttribLocation(shader, "a_texcoord");
		if (tex_location != -1) {
			functions_.glBindBuffer(GL_ARRAY_BUFFER, frag_vbo);
			functions_.glEnableVertexAttribArray(tex_location);
			functions_.glVertexAttribPointer(tex_location, 2, GL_FLOAT,
											 GL_FALSE, 0, nullptr);
			functions_.glBindBuffer(GL_ARRAY_BUFFER, 0);
		}

		// Some shaders optimize through multiple iterations which requires ping-ponging textures
		// - If there are only two iterations, we can just create one backend texture and then the
		//   destination can be the second
		// - If there are more than two iterations, we need to ping pong back and forth between two
		//   textures. We can still use the destination as the last iteration, but we'll need textures
		//   for the iterative process.
		int real_iteration_count;
		if (job.get_iteration_count() > 1 && !job.get_iterative_input().empty()) {
			real_iteration_count = job.get_iteration_count();
		} else {
			real_iteration_count = 1;
		}

		TexturePtr output_tex, input_tex;
		if (real_iteration_count > 1) {
			// Create one texture to bounce off
			output_tex = create_texture(destination_params);

			if (real_iteration_count > 2) {
				// Create a second texture bounce off
				input_tex = create_texture(destination_params);
			}
		}

		GLint iteration_location =
			functions_.glGetUniformLocation(shader, "ove_iteration");
		for (int iteration = 0; iteration < real_iteration_count; iteration++) {
			// Set iteration number
			if (iteration_location > -1) {
				functions_.glUniform1i(iteration_location, iteration);
			}

			// Replace iterative input
			if (iteration == real_iteration_count - 1) {
				// This is the last iteration, draw to the destination
				if (destination) {
					// If we have a destination texture, draw to it
					attach_texture_as_destination(destination->id());
				} else if (iteration > 0) {
					// Otherwise, if we were iterating before, detach texture now
					detach_texture_as_destination();
				}

				// Clear the destination if the caller requested it
				if (clear_destination) {
					clear_destination_internal();
				}
			} else {
				// Always draw to output_tex, which gets swapped with input_tex every iteration
				attach_texture_as_destination(output_tex->id());
			}

			if (iteration > 0) {
				// If this is not the first iteration, replace the iterative texture with the one we
				// last drew
				const std::string &iterative_input = job.get_iterative_input();
				GLuint texture_index = 0;
				auto index_it = texture_index_map.find(iterative_input);
				if (index_it != texture_index_map.end()) {
					texture_index = index_it->second;
				}
				functions_.glActiveTexture(GL_TEXTURE0 + texture_index);
				functions_.glBindTexture(GL_TEXTURE_2D,
										 input_tex->id().value<GLuint>());

				// At this time, we only support iterating 2D textures
				prepare_input_texture(GL_TEXTURE_2D,
									  job.get_interpolation(iterative_input));
			}

			// Swap so that the next iteration, the texture we draw now will be the input texture next
			std::swap(output_tex, input_tex);

			// Blit this texture through this shader
			{
				OAK_PRINT_GL_ERRORS;
				functions_.glDrawArrays(GL_TRIANGLES, 0,
										static_cast<GLsizei>(
											blit_vertices.size() / 3));
			}
		}

		if (destination) {
			// Reset framebuffer to default if we were drawing to a texture
			detach_texture_as_destination();
		}

		// Release any textures we bound before
		for (int i = int(textures_to_bind.size()) - 1; i >= 0; i--) {
			TexturePtr texture = textures_to_bind.at(i).texture;
			GLenum target = (texture && texture->params().is_3d()) ?
								GL_TEXTURE_3D :
								GL_TEXTURE_2D;
			functions_.glActiveTexture(GL_TEXTURE0 + i);
			functions_.glBindTexture(target, 0);
		}

		// Release shader
		functions_.glUseProgram(0);

		// Release vertex array object
		functions_.glDeleteBuffers(1, &frag_vbo);
		functions_.glDeleteBuffers(1, &vert_vbo);
		functions_.glBindVertexArray(0);
		functions_.glDeleteVertexArrays(1, &vao);
	} catch (const std::bad_cast &e) {
	}
}

GLint OpenGLRenderer::get_internal_format(PixelFormat format, int channel_layout)
{
	switch (format) {
	case PixelFormat::u8:
		switch (channel_layout) {
		case 1:
			return GL_R8;
		case 2:
			return GL_RG8;
		case 3:
			return GL_RGB8;
		case 4:
			return GL_RGBA8;
		}
		break;
	case PixelFormat::u10:
		if (channel_layout == 4) {
			return GL_RGB10_A2;
		}
		break;
	case PixelFormat::u16:
		switch (channel_layout) {
		case 1:
			return GL_R16;
		case 2:
			return GL_RG16;
		case 3:
			return GL_RGB16;
		case 4:
			return GL_RGBA16;
		}
		break;
	case PixelFormat::f16:
		switch (channel_layout) {
		case 1:
			return GL_R16F;
		case 2:
			return GL_RG16F;
		case 3:
			return GL_RGB16F;
		case 4:
			return GL_RGBA16F;
		}
		break;
	case PixelFormat::f32:
		switch (channel_layout) {
		case 1:
			return GL_R32F;
		case 2:
			return GL_RG32F;
		case 3:
			return GL_RGB32F;
		case 4:
			return GL_RGBA32F;
		}
		break;
	case PixelFormat::invalid:
	case PixelFormat::count:
		break;
	}

	return GL_INVALID_VALUE;
}

GLenum OpenGLRenderer::get_pixel_type(PixelFormat format)
{
	switch (format) {
	case PixelFormat::u8:
		return GL_UNSIGNED_BYTE;
	case PixelFormat::u10:
		return GL_UNSIGNED_INT_2_10_10_10_REV;
	case PixelFormat::u16:
		return GL_UNSIGNED_SHORT;
	case PixelFormat::f16:
		return GL_HALF_FLOAT;
	case PixelFormat::f32:
		return GL_FLOAT;

	case PixelFormat::invalid:
	case PixelFormat::count:
		break;
	}

	return GL_INVALID_VALUE;
}

GLenum OpenGLRenderer::get_pixel_format(int channel_count)
{
	switch (channel_count) {
	case 1:
		return GL_RED;
	case 3:
		return GL_RGB;
	case 4:
		return GL_RGBA;
	default:
		return GL_INVALID_VALUE;
	}
}

void OpenGLRenderer::prepare_input_texture(GLenum target,
										 Texture::Interpolation interp)
{
	switch (interp) {
	case Texture::k_nearest:
		functions_.glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		functions_.glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		break;
	case Texture::k_linear:
		functions_.glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		functions_.glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		break;
	case Texture::k_mipmapped_linear:
		functions_.glGenerateMipmap(target);
		functions_.glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
								   GL_LINEAR_MIPMAP_LINEAR);
		functions_.glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		break;
	}

	functions_.glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	functions_.glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (target == GL_TEXTURE_3D) {
		functions_.glTexParameteri(target, GL_TEXTURE_WRAP_R,
								   GL_CLAMP_TO_EDGE);
	}
}

void OpenGLRenderer::clear_destination_internal(double r, double g, double b,
											  double a)
{
	if (!functions_resolved_) {
		return;
	}
	functions_.glClearColor(static_cast<GLfloat>(r), static_cast<GLfloat>(g),
						  static_cast<GLfloat>(b), static_cast<GLfloat>(a));
	functions_.glClear(GL_COLOR_BUFFER_BIT);
}

GLuint OpenGLRenderer::compile_shader(GLenum type, const std::string &code)
{
	const bool is_gles = context_ && context_->is_open_gles();
	const int major = context_ ? context_->major_version() : 0;
	const bool is_gles2 = is_gles && (major < 3);
	const char *gles_preamble =
		is_gles2 ? "#version 100\n\n"
				   "precision highp float;\n\n"
				   "#define frag_color gl_FragColor\n" :
				   "#version 300 es\n\n"
				   "precision highp float;\n\n";
	const char *desktop_preamble =
		// Use appropriate GL 3.2 shader header
		"#version 150\n\n"
		"precision highp float;\n\n";
	const std::string shader_preamble = is_gles ? gles_preamble :
												  desktop_preamble;

	std::string base_code = code;
	if (base_code.empty()) {
		// Use default code
		if (type == GL_FRAGMENT_SHADER) {
			base_code = FileFunctions::read_file_as_string(
				":/shaders/default.frag");
		} else if (type == GL_VERTEX_SHADER) {
			base_code = FileFunctions::read_file_as_string(
				":/shaders/default.vert");
		}
	}

	std::string complete_code;
	const std::string k_version_directive = "#version";
	if (base_code.compare(0, k_version_directive.size(),
						  k_version_directive) == 0) {
		if (is_gles ||
			std::string(desktop_preamble).compare(0, k_version_directive.size(),
												  k_version_directive) != 0) {
			std::string::size_type newline = base_code.find('\n');
			if (newline != std::string::npos) {
				complete_code = shader_preamble + base_code.substr(newline + 1);
			} else {
				complete_code = shader_preamble;
			}
		} else {
			complete_code = base_code;
		}
	} else {
		complete_code = shader_preamble + base_code;
	}

	if (is_gles2) {
		if (type == GL_VERTEX_SHADER) {
			complete_code = std::regex_replace(complete_code,
											   std::regex("\\bin\\b"),
											   "attribute");
			complete_code = std::regex_replace(complete_code,
											   std::regex("\\bout\\b"),
											   "varying");
		} else if (type == GL_FRAGMENT_SHADER) {
			complete_code = std::regex_replace(complete_code,
											   std::regex("\\bin\\b"),
											   "varying");
			complete_code = std::regex_replace(
				complete_code,
				std::regex("\\bout\\s+vec4\\s+frag_color\\s*;"),
				"// frag_color output");
			complete_code = std::regex_replace(complete_code,
											   std::regex("\\btexture\\b"),
											   "texture2D");
		}
	}

	const char *code_cstr = complete_code.c_str();

	GLuint shader = functions_.glCreateShader(type);
	functions_.glShaderSource(shader, 1, &code_cstr, nullptr);
	functions_.glCompileShader(shader);

	GLint success;
	functions_.glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		fprintf(stderr, "Failed to compile OpenGL shader\n");

		std::vector<char> error_log(10240);
		functions_.glGetShaderInfoLog(shader, static_cast<GLsizei>(error_log.size()),
									  nullptr, error_log.data());
		std::cout << error_log.data() << std::endl
				  << code_cstr << std::endl;

		functions_.glDeleteShader(shader);
		shader = 0;
	}

	return shader;
}

bool OpenGLRenderer::ensure_context_current(const char *caller)
{
	if (!context_) {
		fprintf(stderr, "%s called without an OpenGL context\n", caller);
		return false;
	}

	// A GL context may only be made current from its owning thread. Viewer
	// paint code can receive textures produced by a render-thread OpenGL
	// renderer, so guard here before make_current() can crash inside the
	// platform GL.
	if (context_->owner_thread() != std::this_thread::get_id()) {
		fprintf(stderr,
				"%s called from the wrong thread for this OpenGL context\n",
				caller);
		return false;
	}

	if (!context_->is_current()) {
		if (context_owned_) {
			if (!context_->make_current()) {
				fprintf(stderr, "%s failed to make context current\n", caller);
				return false;
			}
		} else {
			fprintf(stderr, "%s OpenGL context not current\n", caller);
			return false;
		}
	}

	if (!functions_resolved_) {
		functions_resolved_ = context_->resolve_functions(&functions_);
	}

	if (!functions_resolved_) {
		fprintf(stderr, "%s OpenGL functions not available\n", caller);
		return false;
	}

	return true;
}

}
