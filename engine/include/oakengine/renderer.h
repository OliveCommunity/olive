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

#ifndef OAKENGINE_RENDERER_H
#define OAKENGINE_RENDERER_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file renderer.h
 * @brief C ABI for synchronous frame/audio rendering of a sequence
 *
 * An OakEngineRenderer pulls finished CPU frames and audio buffers out of an
 * OakEngineSequence. It is a thin synchronous facade over the engine's
 * asynchronous render pipeline (RenderManager::render_frame()/
 * render_audio() returning RenderTicket objects, engine/render/
 * rendermanager.h): each render call submits a ticket and blocks until it
 * finishes, a timeout elapses, or it is cancelled.
 *
 * Rendering requires the engine to be initialized with OAKENGINE_INIT_RENDER
 * (oakengine/init.h); oakengine_renderer_create() itself only validates its
 * arguments, but oakengine_renderer_render_frame()/_render_audio() fail with
 * NULL and set the per-renderer error string (query with
 * oakengine_renderer_last_error()) when the render services are not up.
 *
 * Video frames are produced by the engine's render worker pool
 * (oak-render-worker child processes, which is where a GL context may be
 * needed); audio is rendered in-process on the audio render thread.
 *
 * Conventions (matching the other facade families):
 *   - Returned handles are owned by the caller and must be released with the
 *     matching _free(). NULL is accepted by every function and yields a
 *     no-op / zero result.
 *   - Data pointers (oakengine_frame_data(), oakengine_audio_data()) are
 *     borrowed: they stay valid until the owning frame/buffer is freed.
 *   - The `pixel_format` argument and oakengine_frame_format() carry
 *     olive::core::PixelFormat::Format values: u8 = 0, u10 = 1, u16 = 2,
 *     f16 = 3, f32 = 4.
 *   - Timestamps are frame numbers in the timebase of the frame rate passed
 *     to oakengine_renderer_create() (e.g. timestamp 30 at 30000/1001 means
 *     frame 30, 1001/1000 seconds). This is the same timestamp/timebase
 *     convention as the timeline family (oakengine/timeline.h).
 *   - `mode` follows olive::RenderMode: 0 = offline (preview quality),
 *     1 = online (export/master quality).
 */

/**
 * @brief Opaque renderer handle, bound to one sequence and one output
 * geometry. Owned by the caller; release with oakengine_renderer_free().
 */
typedef struct OakEngineRenderer OakEngineRenderer;

/**
 * @brief Opaque CPU video frame. Owned by the caller; release with
 * oakengine_frame_free().
 */
typedef struct OakEngineFrame OakEngineFrame;

/**
 * @brief Opaque planar float audio buffer. Owned by the caller; release with
 * oakengine_audio_free().
 */
typedef struct OakEngineAudioBuffer OakEngineAudioBuffer;

/**
 * @brief Create a renderer for `seq` producing `width`x`height` frames of
 * `pixel_format` at the given frame rate.
 *
 * `frame_rate_num`/`frame_rate_den` is the frame rate as a rational (e.g.
 * 30000/1001); it defines both the video time base and the meaning of all
 * timestamps passed to this renderer. `output_colorspace` names the OCIO
 * color space the frames are converted into after rendering in the
 * project's reference space (a ColorTransform to that space, applied as
 * RenderVideoParams::force_color_output); NULL renders straight into the
 * reference space without an output transform. If the named color space
 * cannot be resolved, the renderer falls back to no transform and records
 * the reason in the error string.
 *
 * Returns NULL on invalid arguments (NULL `seq`, non-positive size, frame
 * rate or pixel format).
 */
OAKENGINE_API OakEngineRenderer *oakengine_renderer_create(
	OakEngineSequence *seq, int width, int height, int pixel_format,
	int frame_rate_num, int frame_rate_den, const char *output_colorspace);

OAKENGINE_API void oakengine_renderer_free(OakEngineRenderer *self);

/**
 * @brief Set the render mode: 0 = offline/preview, 1 = online/export
 * (olive::RenderMode). Defaults to 0. Returns OAKENGINE_E_INVALID for other
 * values.
 */
OAKENGINE_API int oakengine_renderer_set_mode(OakEngineRenderer *self,
											  int mode);

/**
 * @brief Human-readable reason for the last failed render call on this
 * renderer (buf/size convention). Empty when the last call succeeded.
 */
OAKENGINE_API int oakengine_renderer_last_error(
	const OakEngineRenderer *self, char *buf, int buf_size);

/**
 * @brief Synchronously render the frame at `timestamp` (a frame number in
 * the renderer's timebase).
 *
 * Submits a RenderVideoParams ticket (RenderManager::render_frame(), return
 * type k_frame = CPU frame) and blocks up to 60 seconds for it to finish.
 * Returns NULL on failure, timeout or cancellation; the reason is available
 * through oakengine_renderer_last_error().
 */
OAKENGINE_API OakEngineFrame *
oakengine_renderer_render_frame(OakEngineRenderer *self, int64_t timestamp);

/**
 * @brief Synchronously render audio starting at `start_timestamp` (frame
 * number) and spanning `length_timestamp` timebase units, in the
 * sequence's audio parameters (RenderManager::render_audio()).
 *
 * Same blocking/timeout/error semantics as
 * oakengine_renderer_render_frame().
 */
OAKENGINE_API OakEngineAudioBuffer *oakengine_renderer_render_audio(
	OakEngineRenderer *self, int64_t start_timestamp,
	int64_t length_timestamp);

/**
 * @brief Cancel the in-flight render call, if any (RenderTicket::cancel()
 * plus RenderManager::remove_ticket()).
 *
 * Intended to be called from another thread while a render call blocks;
 * safe to call anytime, a no-op when nothing is in flight.
 */
OAKENGINE_API void oakengine_renderer_cancel(OakEngineRenderer *self);

/* ---- OakEngineFrame ----------------------------------------------------- */

OAKENGINE_API int oakengine_frame_width(const OakEngineFrame *self);
OAKENGINE_API int oakengine_frame_height(const OakEngineFrame *self);

/**
 * @brief Pixel format as an olive::core::PixelFormat::Format value.
 */
OAKENGINE_API int oakengine_frame_format(const OakEngineFrame *self);
OAKENGINE_API int oakengine_frame_channel_count(const OakEngineFrame *self);

/**
 * @brief Bytes per scanline (stride).
 */
OAKENGINE_API int oakengine_frame_linesize_bytes(const OakEngineFrame *self);

/**
 * @brief Borrowed pointer to the pixel data (linesize_bytes * height bytes).
 * Valid until the frame is freed.
 */
OAKENGINE_API const void *oakengine_frame_data(const OakEngineFrame *self);

OAKENGINE_API void oakengine_frame_free(OakEngineFrame *self);

/* ---- OakEngineAudioBuffer ------------------------------------------------ */

OAKENGINE_API int oakengine_audio_sample_rate(const OakEngineAudioBuffer *self);
OAKENGINE_API int
oakengine_audio_channel_count(const OakEngineAudioBuffer *self);

/**
 * @brief Samples per channel.
 */
OAKENGINE_API int64_t
oakengine_audio_sample_count(const OakEngineAudioBuffer *self);

/**
 * @brief Borrowed pointer to one channel's planar float samples
 * (sample_count floats). Valid until the buffer is freed. Returns NULL for
 * an out-of-range channel.
 */
OAKENGINE_API const float *
oakengine_audio_data(const OakEngineAudioBuffer *self, int channel);

OAKENGINE_API void oakengine_audio_free(OakEngineAudioBuffer *self);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_RENDERER_H */
