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

#include "export.h"
#include "init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file display.h
 * @brief C ABI for the GPU display renderer used by viewer/scope widgets
 *
 * This family wraps the engine's interactive display renderer
 * (olive::Renderer and its OpenGLRenderer/DynamicRenderer implementations,
 * engine/render/renderer.h) plus the GPU texture (olive::Texture) and the
 * CPU frame buffer (olive::Frame) that viewer/scope widgets use to move
 * pixels between the CPU and the GPU.
 *
 * It is distinct from the sequence-rendering facade in oakengine/renderer.h
 * (OakEngineRenderer), which pulls finished CPU frames out of the async
 * render pipeline. This family drives the *on-screen* paint path instead:
 * a widget creates a renderer, initializes it with the widget's GL context,
 * uploads/downloads textures, and blits color-managed images each paint.
 *
 * Conventions (matching the other facade families):
 *   - All object pointers are opaque. `renderer` is an olive::Renderer*,
 *     `texture` an olive::Texture*, `frame` an olive::Frame*.
 *   - `out_texture` / `out_frame` are pointers to caller-owned
 *     olive::TexturePtr / olive::FramePtr (std::shared_ptr) storage; the
 *     callee assigns a newly created smart pointer into them, releasing any
 *     previously held object. This keeps shared-pointer ownership/deleter
 *     bookkeeping entirely on the engine side.
 *   - `video_params` is a `const olive::VideoParams*`; `color_job` is a
 *     `const olive::ColorTransformJob*`. These are passed as opaque pointers
 *     because they are C++ types; both the caller (app) and the callee
 *     (engine) are compiled as C++ against the same headers.
 *   - `gl_context` is a `QOpenGLContext*` or NULL.
 *   - `parent` is the owning `QObject*` (the display widget); the created
 *     renderer is a QObject child of it and is destroyed by Qt ownership.
 *     Do NOT call oakengine_display_renderer_destroy() and then also rely on
 *     Qt deletion of the same renderer's GPU resources -- destroy() releases
 *     GPU state, Qt deletion releases the object.
 */

/* ---- Display renderer lifecycle ---------------------------------------- */

/**
 * @brief Create a dynamic-backend renderer (olive::DynamicRenderer) for
 * `backend_name` and load() it.
 *
 * @return The renderer (olive::Renderer*), or NULL if the backend library
 *         could not be loaded (the failed renderer is deleted internally and
 *         the caller should fall back to
 *         oakengine_display_renderer_create_opengl()). NULL is also returned
 *         when the engine was built without dynamic-backend support.
 */
OAKENGINE_API void *
oakengine_display_renderer_create_dynamic(const char *backend_name,
										  void *parent);

/**
 * @brief Create the built-in OpenGL renderer (olive::OpenGLRenderer).
 *
 * @return The renderer (olive::Renderer*), never NULL.
 */
OAKENGINE_API void *oakengine_display_renderer_create_opengl(void *parent);

/**
 * @brief Initialize a display renderer and run its post-init step.
 *
 * If `gl_context` is non-NULL the OpenGL/dynamic path is taken (the renderer
 * is initialized against the widget's shared QOpenGLContext); otherwise the
 * backend-neutral path (Renderer::init()/post_init()) is used.
 *
 * @return OAKENGINE_OK on success, OAKENGINE_E_INVALID for a NULL renderer.
 */
OAKENGINE_API int oakengine_display_renderer_init(void *renderer,
												  void *gl_context);

/**
 * @brief Release a display renderer's GPU resources (Renderer::destroy()
 * followed by post_destroy()). The renderer object itself remains owned by
 * its Qt parent.
 */
OAKENGINE_API void oakengine_display_renderer_destroy(void *renderer);

/* ---- Texture creation and pixel transfer -------------------------------- */

/**
 * @brief Create a GPU texture on `renderer` (Renderer::create_texture()).
 *
 * @param renderer     olive::Renderer*.
 * @param video_params const olive::VideoParams* describing the texture.
 * @param pixels       Initial pixel data, or NULL for an empty texture.
 * @param linesize     Line stride of `pixels` (ignored when NULL).
 * @param out_texture  Pointer to an olive::TexturePtr to receive the result.
 */
OAKENGINE_API void
oakengine_display_renderer_create_texture(void *renderer,
										  const void *video_params,
										  const void *pixels, int linesize,
										  void *out_texture);

/**
 * @brief Blit a color-managed image (Renderer::blit_color_managed()).
 *
 * @param renderer     olive::Renderer*.
 * @param color_job    const olive::ColorTransformJob*.
 * @param dst_texture  Destination olive::Texture*, or NULL to blit to the
 *                     current output destination.
 * @param video_params const olive::VideoParams* for the destination, or NULL
 *                     to use dst_texture's own parameters (in which case
 *                     dst_texture must be non-NULL).
 */
OAKENGINE_API void
oakengine_display_renderer_blit_color_managed(void *renderer,
											  const void *color_job,
											  void *dst_texture,
											  const void *video_params);

/**
 * @brief Upload CPU pixels into a GPU texture (Texture::upload()).
 */
OAKENGINE_API void oakengine_display_texture_upload(void *texture,
													void *pixels, int linesize);

/**
 * @brief Download GPU texture pixels into CPU memory (Texture::download()).
 */
OAKENGINE_API void oakengine_display_texture_download(void *texture,
													  void *pixels,
													  int linesize);

/* ---- CPU frame buffer --------------------------------------------------- */

/**
 * @brief Create an empty CPU frame (olive::Frame::create()).
 *
 * @param out_frame Pointer to an olive::FramePtr to receive the new frame.
 */
OAKENGINE_API void oakengine_codec_frame_create(void *out_frame);

/**
 * @brief Set a frame's video parameters (Frame::set_video_params()).
 *
 * @param frame        olive::Frame*.
 * @param video_params const olive::VideoParams*.
 */
OAKENGINE_API void oakengine_codec_frame_set_video_params(void *frame,
														  const void
															  *video_params);

/**
 * @brief Allocate the frame's pixel buffer (Frame::allocate()).
 *
 * @return 1 on success, 0 on failure or NULL frame.
 */
OAKENGINE_API int oakengine_codec_frame_allocate(void *frame);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_DISPLAY_H */
