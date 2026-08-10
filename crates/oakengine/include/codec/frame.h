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

#ifndef OAK_EDITOR_CODEC_FRAME_H
#define OAK_EDITOR_CODEC_FRAME_H

#include <stdint.h>

#include "common/videoparams.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file frame.h
 * @brief C ABI for the oakcodec frame object (olive::Frame), a CPU pixel
 *        buffer plus an OakVideoParams parameter set.
 *
 * Handle convention (all oakcodec families): neutral by-value handles with
 * the same four fields as oakcommon (see oakcommon's common/handle.h):
 *
 *     typedef struct OakFrame {
 *         void *ctx;                       // opaque, points to the impl
 *         void (*addref)(void *ctx);       // atomic +1, owner-DLL code
 *         void (*release)(void *ctx);      // atomic -1, destroys at 0
 *         uint32_t abi_version;            // OAKCODEC_ABI_VERSION
 *     } OakFrame;
 *
 * oakcodec_frame_init*() returns a handle whose underlying object has
 * reference count 1. Copying the struct copies the pointer, not the
 * count: call handle.addref(handle.ctx) for every additional long-lived
 * copy and handle.release(handle.ctx) (or oakcodec_frame_free()) when
 * done with each copy. Functions that only use a handle take it BY
 * VALUE; an empty handle (ctx == NULL) is reported as
 * OAKCODEC_E_INVALID. oakcodec_frame_free() takes a pointer so it can
 * null out the caller's ctx; NULL and ctx == NULL are no-ops.
 */
typedef struct OakFrame {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCODEC_ABI_VERSION. */
} OakFrame;

/**
 * @brief Create an empty frame with default (invalid) video parameters.
 *
 * @return Handle with reference count 1; ctx is NULL on allocation
 *         failure.
 */
OAKCODEC_API OakFrame oakcodec_frame_init(void);

/**
 * @brief Create a frame with a copy of the given parameter set.
 *
 * The params handle is addref'd internally; the caller keeps its own
 * reference. The frame is not allocated; call oakcodec_frame_allocate().
 *
 * @return Handle with reference count 1; ctx is NULL on failure.
 */
OAKCODEC_API OakFrame oakcodec_frame_init_with_params(OakVideoParams params);

/**
 * @brief Release one reference to a frame.
 *
 * Convenience wrapper around handle.release(handle.ctx); nulls ctx
 * afterwards. No-op when frame is NULL or frame->ctx is NULL.
 */
OAKCODEC_API void oakcodec_frame_free(OakFrame *frame);

/**
 * @brief Get a copy of the frame's parameter set.
 *
 * @param out Receives an addref'd OakVideoParams; the caller must release
 *        it with oakcommon_videoparams_free().
 * @return OAKCODEC_OK, or OAKCODEC_E_INVALID for bad arguments.
 */
OAKCODEC_API int oakcodec_frame_get_params(OakFrame frame, OakVideoParams *out);

/**
 * @brief Replace the frame's parameter set (the handle is addref'd
 *        internally). Recomputes the line sizes; does not reallocate the
 *        buffer.
 */
OAKCODEC_API int oakcodec_frame_set_params(OakFrame frame, OakVideoParams params);

/**
 * @brief Allocate the pixel buffer from the current parameters.
 *
 * @return OAKCODEC_OK on success (including already-allocated),
 *         OAKCODEC_E_STATE when the parameters are invalid,
 *         OAKCODEC_E_INVALID for an empty handle.
 */
OAKCODEC_API int oakcodec_frame_allocate(OakFrame frame);

/** @brief 1 when the pixel buffer is allocated, 0 otherwise. */
OAKCODEC_API int oakcodec_frame_is_allocated(OakFrame frame);

/** @brief Writable pixel buffer, or NULL when unallocated/empty. */
OAKCODEC_API void *oakcodec_frame_data(OakFrame frame);

/** @brief Const variant of oakcodec_frame_data(). */
OAKCODEC_API const void *oakcodec_frame_const_data(OakFrame frame);

/** @brief Size of the pixel buffer in bytes (0 when unallocated). */
OAKCODEC_API int oakcodec_frame_allocated_size(OakFrame frame);

/** @brief Distance between two rows in bytes (0 when params are unset). */
OAKCODEC_API int oakcodec_frame_linesize_bytes(OakFrame frame);

/** @brief Distance between two rows in pixels. */
OAKCODEC_API int oakcodec_frame_linesize_pixels(OakFrame frame);

/* Query helpers; all return 0 / OAKCOMMON_PIXEL_FORMAT_INVALID on an
 * empty handle. */
OAKCODEC_API int oakcodec_frame_width(OakFrame frame);
OAKCODEC_API int oakcodec_frame_height(OakFrame frame);
OAKCODEC_API int oakcodec_frame_format(OakFrame frame); /**< OakPixelFormat value. */
OAKCODEC_API int oakcodec_frame_channel_count(OakFrame frame);

/**
 * @brief Frame timestamp as a rational number of seconds.
 *
 * @return OAKCODEC_OK, or OAKCODEC_E_INVALID for bad arguments.
 */
OAKCODEC_API int oakcodec_frame_get_timestamp(OakFrame frame, int *numerator,
								 int *denominator);
OAKCODEC_API int oakcodec_frame_set_timestamp(OakFrame frame, int numerator,
								 int denominator);

/**
 * @brief Number of live oakcodec handle objects (debug/leak checking).
 *
 * Counts every boxed object created by oakcodec_*_init*() that has not
 * been released yet, across all families (frame/decoder/encoder/...).
 */
OAKCODEC_API int oakcodec_debug_alive_count(void);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_CODEC_FRAME_H
