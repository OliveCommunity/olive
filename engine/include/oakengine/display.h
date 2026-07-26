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

#ifndef OAKENGINE_DISPLAY_H
#define OAKENGINE_DISPLAY_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "videoparams.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file display.h
 * @brief Pure C ABI for the GPU display renderer used by viewer/scope widgets
 *
 * Ownership protocol: textures and frames are opaque handles pointing to
 * engine-heap control blocks (internally holding std::shared_ptr; invisible
 * to the ABI). Ownership transfers via explicit retain/free. Every retain
 * must be paired with exactly one free. NULL is accepted by all functions
 * and yields a no-op / zero result.
 *
 * Conventions:
 *   - `renderer` is an opaque renderer handle (olive::Renderer* internally).
 *   - `texture` is an OakEngineDisplayTexture handle (refcounted).
 *   - `frame` is an OakEngineCodecFrame handle (refcounted).
 *   - Video parameters use the oak_video_params POD (oakengine/videoparams.h).
 *   - Color jobs use the oak_color_transform_job POD defined below.
 *   - `gl_context` is a QOpenGLContext* or NULL.
 *   - `parent` is the owning QObject* (the display widget).
 */

/* ---- oak_color_transform_job POD ---------------------------------------- */

/**
 * @brief Flattened POD of the engine's ColorTransformJob for the display blit
 * path. Fields map 1:1 to ColorTransformJob members used by viewer/scope.
 */
typedef struct oak_color_transform_job {
	const void *processor; /**< OakEngineColorProcessor* (borrowed). */
	void *input_texture; /**< Texture handle (borrowed, not retained). */
	int input_alpha_association; /**< 0=none, 1=associated. */
	int clear_destination; /**< 0/1. */
	int force_opaque; /**< 0/1. */
	float matrix[16]; /**< QMatrix4x4 (column-major, identity if all 0). */
	float crop_matrix[16]; /**< QMatrix4x4 (column-major). */
} oak_color_transform_job;

/* ---- Display renderer lifecycle ----------------------------------------- */

OAKENGINE_API void *
oakengine_display_renderer_create_dynamic(const char *backend_name,
										  void *parent);

OAKENGINE_API void *oakengine_display_renderer_create_opengl(void *parent);

OAKENGINE_API int oakengine_display_renderer_init(void *renderer,
												  void *gl_context);

OAKENGINE_API void oakengine_display_renderer_destroy(void *renderer);

/* ---- Renderer queries --------------------------------------------------- */

OAKENGINE_API int oakengine_display_renderer_is_open_gl(const void *renderer);
OAKENGINE_API int oakengine_display_renderer_is_vulkan(const void *renderer);

/* ---- Texture handle (opaque, refcounted) -------------------------------- */

/**
 * @brief Create a GPU texture on `renderer`.
 * @return New texture handle (refcount=1), or NULL on failure.
 */
OAKENGINE_API void *oakengine_display_texture_create(
	void *renderer, const oak_video_params *params,
	const void *pixels, int linesize);

/** @brief Increment refcount, return same handle. NULL-safe. */
OAKENGINE_API void *oakengine_display_texture_retain(void *texture);

/** @brief Decrement refcount; frees at zero. NULL-safe. */
OAKENGINE_API void oakengine_display_texture_free(void *texture);

OAKENGINE_API int oakengine_display_texture_upload(
	void *texture, const void *pixels, int linesize);

OAKENGINE_API int oakengine_display_texture_download(
	void *texture, void *pixels, int linesize);

/* ---- Texture queries ---------------------------------------------------- */

OAKENGINE_API int oakengine_display_texture_get_params(
	const void *texture, oak_video_params *out);

OAKENGINE_API int oakengine_display_texture_id(const void *texture);

OAKENGINE_API int oakengine_display_texture_is_dummy(const void *texture);

/** @brief The renderer that owns this texture (borrowed, do NOT free). */
OAKENGINE_API void *oakengine_display_texture_renderer(const void *texture);

OAKENGINE_API int oakengine_display_texture_width(const void *texture);
OAKENGINE_API int oakengine_display_texture_height(const void *texture);
OAKENGINE_API int oakengine_display_texture_format(const void *texture);
OAKENGINE_API int oakengine_display_texture_channel_count(const void *texture);

/**
 * @brief Compare two texture handles' video params for equality.
 * @return 1 if equal, 0 otherwise (NULL handles compare unequal).
 */
OAKENGINE_API int oakengine_display_texture_params_equal(
	const void *a, const void *b);

/* ---- Frame handle (opaque, refcounted) ---------------------------------- */

/** @brief Create an empty CPU frame. Returns handle (refcount=1). */
OAKENGINE_API void *oakengine_codec_frame_create(void);

/** @brief Increment refcount, return same handle. NULL-safe. */
OAKENGINE_API void *oakengine_codec_frame_retain(void *frame);

/** @brief Decrement refcount; frees at zero. NULL-safe. */
OAKENGINE_API void oakengine_codec_frame_free(void *frame);

OAKENGINE_API int oakengine_codec_frame_set_video_params(
	void *frame, const oak_video_params *params);

OAKENGINE_API int oakengine_codec_frame_get_params(
	const void *frame, oak_video_params *out);

OAKENGINE_API int oakengine_codec_frame_allocate(void *frame);

/** @brief Borrowed pixel data pointer (valid until free). */
OAKENGINE_API void *oakengine_codec_frame_data(void *frame);

/** @brief Borrowed const pixel data pointer. */
OAKENGINE_API const void *oakengine_codec_frame_const_data(const void *frame);

/** @brief Line stride in pixels. */
OAKENGINE_API int oakengine_codec_frame_linesize(const void *frame);

/** @brief Line stride in bytes. */
OAKENGINE_API int oakengine_codec_frame_linesize_bytes(const void *frame);

/* ---- Frame queries ------------------------------------------------------ */

OAKENGINE_API int oakengine_codec_frame_width(const void *frame);
OAKENGINE_API int oakengine_codec_frame_height(const void *frame);
OAKENGINE_API int oakengine_codec_frame_format(const void *frame);
OAKENGINE_API int oakengine_codec_frame_channel_count(const void *frame);
OAKENGINE_API int oakengine_codec_frame_is_allocated(const void *frame);

/* ---- Color-managed blit ------------------------------------------------- */

/**
 * @brief Blit a color-managed image through the OCIO pipeline.
 *
 * @param renderer   Renderer handle.
 * @param job        POD color transform job (processor + input texture + flags).
 * @param dst_texture Destination texture handle, or NULL for screen.
 * @param params     Destination video params, or NULL to use dst_texture's.
 */
OAKENGINE_API int oakengine_display_renderer_blit_color_managed(
	void *renderer, const oak_color_transform_job *job,
	void *dst_texture, const oak_video_params *params);

/* ---- Cross-backend texture download ------------------------------------- */

OAKENGINE_API int oakengine_display_renderer_download_from_texture(
	void *renderer, int texture_id, const oak_video_params *params,
	void *dst_pixels, int linesize);

/* ---- Shader management -------------------------------------------------- */

/**
 * @brief Compile a native shader from GLSL source.
 * @param frag_src Fragment shader source (required).
 * @param vert_src Vertex shader source, or NULL for engine default.
 * @return Shader pipeline handle (QVariant*), or NULL on failure.
 *         Free with oakengine_display_renderer_destroy_shader().
 */
OAKENGINE_API void *oakengine_display_renderer_create_shader(
	void *renderer, const char *frag_src, const char *vert_src);

/**
 * @brief Create the engine's default blank shader (no custom source).
 */
OAKENGINE_API void *oakengine_display_renderer_create_blank_shader(
	void *renderer);

OAKENGINE_API void oakengine_display_renderer_destroy_shader(
	void *renderer, void *shader);

/* ---- Shader blit operations --------------------------------------------- */

/**
 * @brief Blit a single texture through a shader to the screen.
 * Used by scope draw_scope() path.
 */
OAKENGINE_API int oakengine_display_renderer_blit_shader(
	void *renderer, void *shader, void *texture,
	const oak_video_params *viewport_params);

/**
 * @brief Blit a single texture through a shader to a destination texture.
 * Used by histogram blit_to_texture path.
 */
OAKENGINE_API int oakengine_display_renderer_blit_shader_to_texture(
	void *renderer, void *shader, void *texture, void *dst_texture);

/**
 * @brief Blit with a vec2 uniform + texture to a destination texture.
 * Used by deinterlace path (resolution_in uniform).
 */
OAKENGINE_API int oakengine_display_renderer_blit_shader_vec2_to_texture(
	void *renderer, void *shader, void *texture,
	const char *vec2_name, float vec2_x, float vec2_y,
	void *dst_texture);

/**
 * @brief Blit the blank shader with MVP + crop matrices.
 * Used by draw_blank().
 */
OAKENGINE_API int oakengine_display_renderer_blit_blank(
	void *renderer, void *shader,
	const float *mvp_matrix, const float *crop_matrix,
	const oak_video_params *params);

/**
 * @brief Blit multiple named textures through a shader to a dst texture.
 * Used by multicam compositing.
 *
 * @param names  Array of `count` UTF-8 uniform names.
 * @param textures Array of `count` texture handles.
 */
OAKENGINE_API int oakengine_display_renderer_blit_shader_multi(
	void *renderer, void *shader,
	const char *const *names, void *const *textures, int count,
	void *dst_texture);

/* ---- Generic shader blit with named uniforms ----------------------------- */

/**
 * @brief Named uniform descriptor for scope shader blits.
 */
typedef struct oak_shader_uniform {
	const char *name; /**< Uniform name (UTF-8). */
	int type; /**< 0=float, 1=vec2, 2=int/bool, 3=vec3. */
	float values[4]; /**< float: [0]; vec2: [0],[1]; vec3: [0],[1],[2]; int: [0]. */
} oak_shader_uniform;

/**
 * @brief Blit a single texture through a shader with arbitrary named uniforms.
 *
 * @param renderer    Renderer handle.
 * @param shader      Shader pipeline handle.
 * @param texture     Main input texture (bound as "ove_maintex").
 * @param uniforms    Array of extra uniforms (may be NULL).
 * @param uniform_count Number of entries in `uniforms`.
 * @param dst_texture Destination texture, or NULL for screen.
 * @param params      Viewport params (used when dst_texture is NULL).
 */
OAKENGINE_API int oakengine_display_renderer_blit_shader_uniforms(
	void *renderer, void *shader, void *texture,
	const oak_shader_uniform *uniforms, int uniform_count,
	void *dst_texture, const oak_video_params *params);

/* ---- Renderer clear ----------------------------------------------------- */

/**
 * @brief Clear the current destination.
 * @param mask  NULL for default, or a string mask (unused currently).
 * @param r,g,b  Clear color (0.0-1.0).
 */
OAKENGINE_API void oakengine_display_renderer_clear(
	void *renderer, double r, double g, double b);

/* ---- Pixel readback ----------------------------------------------------- */

/**
 * @brief Read a single pixel from a texture (Renderer::get_pixel_from_texture).
 * @param x,y  Pixel coordinates.
 * @param out_rgba  Output 4 doubles (RGBA).
 */
OAKENGINE_API int oakengine_display_renderer_get_pixel(
	void *renderer, void *texture, int x, int y, double *out_rgba);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_DISPLAY_H */
