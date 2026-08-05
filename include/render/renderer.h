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

#ifndef OAK_EDITOR_RENDER_RENDERER_H
#define OAK_EDITOR_RENDER_RENDERER_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file renderer.h
 * @brief C ABI for the oakrender display renderer (olive::Renderer) —
 *        renderer/texture/frame/blit families plus backend management.
 *
 * Signatures follow the R7-A display.h rewrite
 * (docs/zh/plans/completed/r7-pure-abi-plan.md §A.2) with the
 * oakrender_ prefix (M7 §2.1).
 *
 * Ownership protocol: textures and frames are opaque handles pointing to
 * oakrender-heap control blocks (internally holding std::shared_ptr;
 * invisible to the ABI). Ownership transfers via explicit retain/free.
 * Every retain must be paired with exactly one free. NULL is accepted by
 * every function and yields a no-op / zero result / OAKRENDER_E_INVALID.
 *
 * Cross-thread handoff (§A.3): the producing side retains before
 * publishing a handle into a shared slot; the consuming side frees the
 * handle it replaced. The side holding the slot when it is torn down
 * frees the remaining handle.
 *
 * Handles:
 *   - OakRenderRenderer IS a reinterpreted olive::Renderer (no wrapper).
 *   - OakRenderTexture / OakCodecFrame are refcounted control blocks.
 *   - `gl_context` is an opaque borrowed olive::OpenGLContext* (or NULL
 *     to let the backend create its own offscreen surface).
 */

/**
 * @brief POD mirror of olive::VideoParams' user-facing fields.
 *
 * Same layout and field semantics as oak_video_params
 * (engine/include/oakengine/videoparams.h): `time_base_*` is the frame
 * duration (frame rate flipped), `format` an olive::PixelFormat::Format
 * value, `interlacing` an olive::VideoParams::Interlacing value,
 * `color_range` an olive::VideoParams::ColorRange value. The video
 * channel count is an engine-internal constant and not exposed.
 */
typedef struct oakrender_video_params {
	int width;
	int height;
	int time_base_num; /**< Frame duration numerator (e.g. 1001/30000 s). */
	int time_base_den;
	int format; /**< olive::PixelFormat::Format. */
	int pixel_aspect_num;
	int pixel_aspect_den;
	int interlacing; /**< olive::VideoParams::Interlacing. */
	int color_range; /**< olive::VideoParams::ColorRange. */
	int divider; /**< Preview resolution divider (1 = full). */
	int video_type; /**< olive::VideoParams::Type (0 = video). */
	int premultiplied_alpha; /**< 0/1. */
} oakrender_video_params;

typedef struct OakRenderRenderer OakRenderRenderer;
typedef struct OakRenderTexture OakRenderTexture;

/**
 * @brief Opaque CPU frame handle (refcounted control block around an
 * olive::FramePtr). Declared here so the cache family (render/cache.h)
 * can use the same type; the frame functions live in this header.
 * Named OakCodecFrame per the M7 §2.2 contract; the oakcodec wave (M5)
 * adopts the same handle.
 */
typedef struct OakCodecFrame OakCodecFrame;

/**
 * @brief Flattened POD of olive::ColorTransformJob for the display blit
 * path. `matrix`/`crop_matrix` are column-major 4x4; an all-zero matrix
 * means identity.
 */
typedef struct oakrender_color_transform_job {
	const void *processor; /**< OakColorProcessor* (borrowed), may be NULL. */
	void *input_texture; /**< OakRenderTexture* (borrowed, not retained). */
	int input_alpha_association; /**< 0=none, 1=associated. */
	int clear_destination; /**< 0/1. */
	int force_opaque; /**< 0/1. */
	float matrix[16];
	float crop_matrix[16];
} oakrender_color_transform_job;

/* ---- Renderer lifecycle -------------------------------------------------- */

/**
 * @brief Create a renderer on the named dynamic backend ("opengl",
 * "vulkan"; olive::DynamicRenderer). Loads the backend shared library;
 * falls back per DynamicRenderer rules.
 *
 * @return Renderer handle, or NULL on NULL/empty backend id, load
 *         failure, or allocation failure.
 */
OakRenderRenderer *oakrender_display_renderer_create_dynamic(
	const char *backend_id);

/**
 * @brief Create an OpenGL renderer (olive::OpenGLRenderer). The renderer
 * is not initialized; call oakrender_display_renderer_init() before use.
 *
 * @return Renderer handle, or NULL on allocation failure.
 */
OakRenderRenderer *oakrender_display_renderer_create_opengl(void);

/**
 * @brief Initialize a renderer. `gl_context` is a borrowed opaque
 * olive::OpenGLContext*, or NULL to use the backend's default
 * device/context path (Renderer::init()).
 *
 * @return OAKRENDER_OK, OAKRENDER_E_INVALID (NULL renderer), or
 *         OAKRENDER_E_FAILED (backend init failed).
 */
int oakrender_display_renderer_init(OakRenderRenderer *renderer,
									void *gl_context);

/**
 * @brief Destroy a renderer (Renderer::destroy() + delete). NULL-safe
 * no-op.
 */
void oakrender_display_renderer_destroy(OakRenderRenderer *renderer);

/* ---- Renderer queries ---------------------------------------------------- */

/** @brief 1 when the renderer is OpenGL-based, 0 otherwise / on NULL. */
int oakrender_display_renderer_is_open_gl(const OakRenderRenderer *renderer);

/** @brief 1 when the renderer is Vulkan-based, 0 otherwise / on NULL. */
int oakrender_display_renderer_is_vulkan(const OakRenderRenderer *renderer);

/* ---- Texture handle (opaque, refcounted) --------------------------------- */

/**
 * @brief Create a GPU texture on `renderer`.
 *
 * @param pixels Initial pixel data, or NULL for an uninitialized texture.
 * @param linesize Stride of `pixels` in bytes (0 when pixels is NULL).
 * @return New texture handle (refcount=1), or NULL on invalid arguments /
 *         allocation failure.
 */
OakRenderTexture *oakrender_display_texture_create(
	OakRenderRenderer *renderer, const oakrender_video_params *params,
	const void *pixels, int linesize);

/** @brief Increment refcount, return the same handle. NULL-safe. */
OakRenderTexture *oakrender_display_texture_retain(OakRenderTexture *texture);

/** @brief Decrement refcount; frees at zero. NULL-safe. */
void oakrender_display_texture_free(OakRenderTexture *texture);

int oakrender_display_texture_upload(OakRenderTexture *texture,
									 const void *pixels, int linesize);

int oakrender_display_texture_download(OakRenderTexture *texture, void *pixels,
									   int linesize);

/* ---- Texture queries ----------------------------------------------------- */

int oakrender_display_texture_get_params(const OakRenderTexture *texture,
										 oakrender_video_params *out);

/** @brief Native texture id (0 on NULL or a dummy/id-less texture). */
int oakrender_display_texture_id(const OakRenderTexture *texture);

/* ---- Frame handle (opaque, refcounted) ----------------------------------- */

/** @brief Create an empty CPU frame. Returns handle (refcount=1). */
OakCodecFrame *oakrender_codec_frame_create(void);

/** @brief Increment refcount, return the same handle. NULL-safe. */
OakCodecFrame *oakrender_codec_frame_retain(OakCodecFrame *frame);

/** @brief Decrement refcount; frees at zero. NULL-safe. */
void oakrender_codec_frame_free(OakCodecFrame *frame);

int oakrender_codec_frame_set_video_params(
	OakCodecFrame *frame, const oakrender_video_params *params);

int oakrender_codec_frame_get_params(const OakCodecFrame *frame,
									 oakrender_video_params *out);

/**
 * @brief Allocate the pixel buffer per the frame's video params
 * (Frame::allocate()).
 *
 * @return OAKRENDER_OK, OAKRENDER_E_INVALID (NULL frame), or
 *         OAKRENDER_E_FAILED (invalid params / allocation failed).
 */
int oakrender_codec_frame_allocate(OakCodecFrame *frame);

/** @brief Borrowed pixel data pointer (valid until free). */
void *oakrender_codec_frame_data(OakCodecFrame *frame);

/** @brief Borrowed const pixel data pointer. */
const void *oakrender_codec_frame_const_data(const OakCodecFrame *frame);

/** @brief Line stride in bytes. */
int oakrender_codec_frame_linesize_bytes(const OakCodecFrame *frame);

/** @brief 1 when the pixel buffer is allocated, 0 otherwise / on NULL. */
int oakrender_codec_frame_is_allocated(const OakCodecFrame *frame);

/* ---- Color-managed blit -------------------------------------------------- */

/**
 * @brief Blit a color-managed image through the OCIO pipeline
 * (Renderer::blit_color_managed()).
 *
 * @param dst_texture Destination texture handle, or NULL for the current
 *        output target.
 * @param params Destination video params, or NULL to use dst_texture's.
 */
int oakrender_display_renderer_blit_color_managed(
	OakRenderRenderer *renderer, const oakrender_color_transform_job *job,
	OakRenderTexture *dst_texture, const oakrender_video_params *params);

/* ---- Cross-backend texture download -------------------------------------- */

int oakrender_display_renderer_download_from_texture(
	OakRenderRenderer *renderer, int texture_id,
	const oakrender_video_params *params, void *dst_pixels, int linesize);

/* ---- Backend management (M7 §2.1) ---------------------------------------- */

/**
 * @brief Number of known render backends (olive::RenderManager::Backend:
 * opengl, vulkan, multiprocess, dummy).
 */
int oakrender_backend_count(void);

/**
 * @brief Id string of the `i`-th backend ("opengl", ...). Two-stage
 * string getter: returns the required buffer size including NUL; pass
 * buf == NULL or too small a buffer to query the size.
 *
 * @return Required size (non-negative), or OAKRENDER_E_NOT_FOUND when
 *         `i` is out of range.
 */
int oakrender_backend_id_at(int i, char *buf, int n);

/**
 * @brief Record the requested backend id (applied to the RenderManager
 * instance when one exists).
 *
 * @return OAKRENDER_OK, or OAKRENDER_E_INVALID for a NULL/unknown id.
 */
int oakrender_set_backend(const char *backend_id);

/**
 * @brief The effective backend: the RenderManager instance's backend when
 * an instance exists, otherwise the requested backend. Two-stage string
 * getter (same convention as oakrender_backend_id_at()).
 */
int oakrender_current_backend(char *buf, int n);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_RENDER_RENDERER_H
