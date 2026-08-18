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

#ifdef __cplusplus
#include <memory>
#endif

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
 * Ownership protocol: every public handle is a by-value
 * reference-counted struct (see oakcommon's common/handle.h; shared_ptr
 * semantics). init/create functions return a handle with reference
 * count 1, handle.addref(handle.ctx) takes another reference, and
 * handle.release(handle.ctx) (or the oakrender_*_free() convenience
 * wrappers, which also null the caller's ctx) drops one; the object is
 * destroyed in this library when the count reaches zero. Empty handles
 * (ctx == NULL) are accepted by every function and yield a no-op / zero
 * result / OAKRENDER_E_INVALID.
 *
 * Cross-thread handoff (§A.3): the producing side addrefs before
 * publishing a handle into a shared slot; the consuming side releases
 * the handle it replaced. The side holding the slot when it is torn
 * down releases the remaining handle.
 *
 * Handles:
 *   - OakRenderRenderer wraps a native olive::Renderer.
 *   - OakRenderTexture / OakCodecFrame box shared_ptr-managed engine
 *     objects.
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

/**
 * @brief Reference-counted handle to a display renderer
 * (olive::Renderer). See the file-level ownership protocol.
 */
typedef struct OakRenderRenderer {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKRENDER_ABI_VERSION. */
} OakRenderRenderer;

/**
 * @brief Reference-counted handle to a GPU texture (olive::Texture).
 * See the file-level ownership protocol.
 */
typedef struct OakRenderTexture {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKRENDER_ABI_VERSION. */
} OakRenderTexture;

/**
 * @brief Reference-counted handle to a CPU frame (an olive::FramePtr
 * boxed in a control block). Declared here so the cache family
 * (render/cache.h) can use the same type; the frame functions live in
 * this header.
 * Named OakCodecFrame per the M7 §2.2 contract; the oakcodec wave (M5)
 * adopts the same handle.
 */
typedef struct OakCodecFrame {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKRENDER_ABI_VERSION. */
} OakCodecFrame;

/**
 * @brief Flattened POD of olive::ColorTransformJob for the display blit
 * path. `matrix`/`crop_matrix` are column-major 4x4; an all-zero matrix
 * means identity.
 */
typedef struct oakrender_color_transform_job {
	const void *processor; /**< OakColorProcessor ctx (borrowed), may be NULL. */
	void *input_texture; /**< OakRenderTexture ctx (borrowed, not retained). */
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
 * @return Renderer handle with reference count 1; ctx is NULL on
 *         NULL/empty backend id, load failure, or allocation failure.
 */
OakRenderRenderer oakrender_display_renderer_create_dynamic(
	const char *backend_id);

/**
 * @brief Create an OpenGL renderer (olive::OpenGLRenderer). The renderer
 * is not initialized; call oakrender_display_renderer_init() before use.
 *
 * @return Renderer handle with reference count 1; ctx is NULL on
 *         allocation failure.
 */
OakRenderRenderer oakrender_display_renderer_create_opengl(void);

/**
 * @brief Initialize a renderer. `gl_context` is a borrowed opaque
 * olive::OpenGLContext*, or NULL to use the backend's default
 * device/context path (Renderer::init()).
 *
 * @return OAKRENDER_OK, OAKRENDER_E_INVALID (empty renderer), or
 *         OAKRENDER_E_FAILED (backend init failed).
 */
int oakrender_display_renderer_init(OakRenderRenderer renderer,
									void *gl_context);

/**
 * @brief Release one reference to a renderer (the final release runs
 * Renderer::destroy() + delete). Convenience wrapper around
 * renderer->release(renderer->ctx): NULL / empty-handle no-op; clears
 * renderer->ctx after releasing.
 */
void oakrender_display_renderer_destroy(OakRenderRenderer *renderer);

/* ---- Renderer queries ---------------------------------------------------- */

/** @brief 1 when the renderer is OpenGL-based, 0 otherwise / empty. */
int oakrender_display_renderer_is_open_gl(OakRenderRenderer renderer);

/** @brief 1 when the renderer is Vulkan-based, 0 otherwise / empty. */
int oakrender_display_renderer_is_vulkan(OakRenderRenderer renderer);

/* ---- Texture handle ------------------------------------------------------ */

/**
 * @brief Create a GPU texture on `renderer`.
 *
 * @param pixels Initial pixel data, or NULL for an uninitialized texture.
 * @param linesize Stride of `pixels` in bytes (0 when pixels is NULL).
 * @return New texture handle (reference count 1); ctx is NULL on invalid
 *         arguments / allocation failure.
 */
OakRenderTexture oakrender_display_texture_create(
	OakRenderRenderer renderer, const oakrender_video_params *params,
	const void *pixels, int linesize);

/**
 * @brief Take another reference to a texture and return the same handle.
 *
 * Convenience wrapper around handle.addref(handle.ctx). An empty handle
 * in yields an empty handle out. Every retain must be paired with
 * exactly one free/release.
 */
OakRenderTexture oakrender_display_texture_retain(OakRenderTexture texture);

/**
 * @brief Release one reference to a texture. Convenience wrapper around
 * texture->release(texture->ctx): frees the texture when the count
 * reaches zero. NULL / empty-handle no-op; clears texture->ctx after
 * releasing.
 */
void oakrender_display_texture_free(OakRenderTexture *texture);

int oakrender_display_texture_upload(OakRenderTexture texture,
									 const void *pixels, int linesize);

int oakrender_display_texture_download(OakRenderTexture texture, void *pixels,
									   int linesize);

/* ---- Texture queries ----------------------------------------------------- */

int oakrender_display_texture_get_params(OakRenderTexture texture,
										 oakrender_video_params *out);

/** @brief Frame width/height in pixels (0 on empty). */
int oakrender_codec_frame_width(OakCodecFrame frame);
int oakrender_codec_frame_height(OakCodecFrame frame);

/** @brief ffmpeg_bridge pixel format when the frame wraps a texture's
 *        CPU copy (an AVFramePtr); -1 otherwise. */
int oakrender_codec_frame_fb_format(OakCodecFrame frame);

/** @brief Native texture id (0 on empty or a dummy/id-less texture). */
int oakrender_display_texture_id(OakRenderTexture texture);

/** @brief 1 when the texture is a placeholder dummy (Texture::is_dummy()). */
int oakrender_display_texture_is_dummy(OakRenderTexture texture);

/**
 * @brief The CPU frame stored in the texture, if any (Texture::frame()).
 *        *out receives a retained frame handle (empty when none).
 */
int oakrender_display_texture_get_frame(OakRenderTexture texture,
										OakCodecFrame *out);

#ifdef __cplusplus
} /* extern "C" */

namespace olive { class Texture; using TexturePtr = std::shared_ptr<Texture>; }

/**
 * @brief Wrap a native TexturePtr in a retained handle (C++ only; used
 *        by oakrender internals when handing textures across the C ABI).
 */
OakRenderTexture oakrender_display_texture_wrap_native(
	const olive::TexturePtr &texture);

extern "C" {
#endif

/* ---- Frame handle -------------------------------------------------------- */

/** @brief Create an empty CPU frame. Returns a handle with count 1. */
OakCodecFrame oakrender_codec_frame_create(void);

/**
 * @brief Take another reference to a frame and return the same handle.
 * Empty in yields empty out (see oakrender_display_texture_retain()).
 */
OakCodecFrame oakrender_codec_frame_retain(OakCodecFrame frame);

/**
 * @brief Release one reference to a frame. Convenience wrapper around
 * frame->release(frame->ctx). NULL / empty-handle no-op; clears
 * frame->ctx after releasing.
 */
void oakrender_codec_frame_free(OakCodecFrame *frame);

int oakrender_codec_frame_set_video_params(
	OakCodecFrame frame, const oakrender_video_params *params);

int oakrender_codec_frame_get_params(OakCodecFrame frame,
									 oakrender_video_params *out);

/**
 * @brief Allocate the pixel buffer per the frame's video params
 * (Frame::allocate()).
 *
 * @return OAKRENDER_OK, OAKRENDER_E_INVALID (empty frame), or
 *         OAKRENDER_E_FAILED (invalid params / allocation failed).
 */
int oakrender_codec_frame_allocate(OakCodecFrame frame);

/** @brief Borrowed pixel data pointer (valid until the final release). */
void *oakrender_codec_frame_data(OakCodecFrame frame);

/** @brief Borrowed const pixel data pointer. */
const void *oakrender_codec_frame_const_data(OakCodecFrame frame);

/** @brief Line stride in bytes. */
int oakrender_codec_frame_linesize_bytes(OakCodecFrame frame);

/** @brief 1 when the pixel buffer is allocated, 0 otherwise / empty. */
int oakrender_codec_frame_is_allocated(OakCodecFrame frame);

/* ---- Color-managed blit -------------------------------------------------- */

/**
 * @brief Blit a color-managed image through the OCIO pipeline
 * (Renderer::blit_color_managed()).
 *
 * @param dst_texture Destination texture handle, or an empty handle for
 *        the current output target.
 * @param params Destination video params, or NULL to use dst_texture's.
 */
int oakrender_display_renderer_blit_color_managed(
	OakRenderRenderer renderer, const oakrender_color_transform_job *job,
	OakRenderTexture dst_texture, const oakrender_video_params *params);

/* ---- Cross-backend texture download -------------------------------------- */

int oakrender_display_renderer_download_from_texture(
	OakRenderRenderer renderer, int texture_id,
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
