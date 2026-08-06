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

#ifndef OAK_EDITOR_CODEC_TASK_H
#define OAK_EDITOR_CODEC_TASK_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Background task submission hook for oakcodec (interim state).
 *
 * The codec module occasionally needs background work (audio conforms,
 * proxy transcodes). The task system itself is split out at milestone M8;
 * until then oakcodec exposes a single global submit callback. A host
 * (M8: oaktask) registers a callback with oakcodec_set_task_submit_cb();
 * the conform/proxy managers call it whenever they need a task.
 *
 * While no callback is registered, managers report the work as
 * unavailable (they never crash and never block).
 */

/**
 * @brief Kinds of background tasks oakcodec can request.
 */
enum OakCodecTaskKind {
	OAKCODEC_TASK_CONFORM = 0, /**< Audio conform to pcm cache files. */
	OAKCODEC_TASK_PROXY = 1 /**< Video proxy transcode. */
};

/**
 * @brief Description of one background task request.
 *
 * All strings are borrowed and only valid for the duration of the
 * submit call; the callback must copy anything it retains.
 *
 * Field usage by kind:
 * - OAKCODEC_TASK_CONFORM: input_filename (source media), stream_index
 *   (audio stream), output_filename (final path of the FIRST channel's
 *   pcm file; the task derives the sibling per-channel paths and the
 *   ".working" temporary names from the deterministic naming rule),
 *   sample_rate / channel_layout / sample_format (target audio params,
 *   sample_format is olive::core::SampleFormat::Format as int).
 * - OAKCODEC_TASK_PROXY: input_filename (source media), stream_index
 *   (video stream), output_filename (final proxy path; the task owns
 *   the ".working.mp4" temporary name and the rename on success),
 *   proxy_width / proxy_height (absolute target size, both 0 when the
 *   request is divider-based).
 */
typedef struct OakCodecTaskRequest {
	int kind; /**< OakCodecTaskKind. */
	const char *input_filename; /**< Source media filename. */
	const char *output_filename; /**< Final destination path (see above). */
	int stream_index; /**< Stream inside the source media. */
	int sample_rate; /**< conform: target sample rate. */
	uint64_t channel_layout; /**< conform: target channel layout mask. */
	int sample_format; /**< conform: target sample format (enum as int). */
	int proxy_width; /**< proxy: target width, 0 = unspecified/divider. */
	int proxy_height; /**< proxy: target height, 0 = unspecified/divider. */
} OakCodecTaskRequest;

/**
 * @brief Task submit callback.
 *
 * @return 0 (OAKCODEC_OK) if the task was accepted - either completed
 * synchronously or queued; a negative OAKCODEC_E_* code if the request
 * was rejected.
 */
typedef int (*oakcodec_task_submit_fn)(const OakCodecTaskRequest *req,
					   void *userdata);

/**
 * @brief Registers (or replaces) the global task submit callback.
 *
 * Thread-safe. Pass cb == NULL to unregister. Interim state (pre-M8):
 * nobody registers and all task-dependent work reports unavailable.
 */
OAKCODEC_API void oakcodec_set_task_submit_cb(oakcodec_task_submit_fn cb, void *userdata);

/**
 * @brief Returns 1 if a submit callback is currently registered, else 0.
 *
 * Thread-safe.
 */
OAKCODEC_API int oakcodec_task_submit_is_registered(void);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_CODEC_TASK_H
