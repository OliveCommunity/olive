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

#ifndef OAK_EDITOR_RENDER_TICKET_H
#define OAK_EDITOR_RENDER_TICKET_H

#include <stdint.h>

#include "common/colortransform.h"
#include "common/videoparams.h"
#include "node/colormanager.h"
#include "node/node.h"
#include "olive/core/oakcore/audioparams.h"
#include "olive/core/oakcore/samplebuffer.h"
#include "render/error.h"
#include "render/cache.h"
#include "render/color.h"
#include "render/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reference-counted handle to a render ticket
 *        (olive::RenderTicketWatcher).
 *
 * By-value handle (shared_ptr semantics, see oakcommon's
 * common/handle.h). Created by oakrender_ticket_render_frame() /
 * oakrender_ticket_render_audio() with reference count 1; release with
 * oakrender_ticket_free().
 */
typedef struct OakRenderTicket {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKRENDER_ABI_VERSION. */
} OakRenderTicket;

/**
 * @brief Finished callback (async command return channel, 01 §4
 *        exception). Fires on the ticket's finishing thread, exactly
 *        once (cancelled tickets fire with a NULL result). The ticket
 *        handle is a borrowed copy of the submitter's handle; the
 *        submitter keeps ownership and releases it.
 */
typedef void (*oakrender_ticket_finished_fn)(OakRenderTicket ticket,
											 void *userdata);

/** @brief Ticket types (RenderManager::TicketType). */
enum OakRenderTicketType {
	OAKRENDER_TICKET_VIDEO = 0,
	OAKRENDER_TICKET_AUDIO = 1
};

/**
 * @brief Parameters for a video frame ticket
 *        (RenderManager::RenderVideoParams).
 */
typedef struct oakrender_video_ticket_params {
	OakNodeNode output_node; /**< Connected texture output node (borrowed). */
	OakVideoParams video_params; /**< By value (oakcommon handle). */
	OakAudioParams *audio_params; /**< Borrowed oakcore handle, may be NULL. */
	int64_t time_num; /**< Frame timestamp as rational. */
	int64_t time_den;
	OakNodeColorManager color_manager; /**< Borrowed, empty ctx = NULL. */
	int mode; /**< olive::RenderMode::Mode as int. */
	int force_width; /**< 0/0 = off. */
	int force_height;
	double force_matrix[16]; /**< Used when has_force_matrix != 0. */
	int has_force_matrix;
	int force_format; /**< PixelFormat as int, -1 = off. */
	int force_channel_count; /**< 0 = off. */
	OakColorProcessor force_color_output; /**< Borrowed; empty ctx = none. */
	OakColorTransform force_color_transform; /**< By value; empty ctx = default. */
	OakRenderCache cache; /**< Borrowed frame cache; empty ctx = none. */
} oakrender_video_ticket_params;

/**
 * @brief Submit a video frame render ticket.
 *
 * @return Ticket handle with reference count 1 (caller releases); ctx is
 *         NULL on failure. The finished callback fires exactly once;
 *         NULL `cb` is allowed (poll with
 *         oakrender_ticket_wait()/oakrender_ticket_is_finished()).
 */
OakRenderTicket oakrender_ticket_render_frame(
	const oakrender_video_ticket_params *params,
	oakrender_ticket_finished_fn cb, void *userdata);

/**
 * @brief Submit an audio render ticket (RenderManager::render_audio()).
 *
 * @param output_node Connected sample output node.
 * @param params Audio params (borrowed oakcore handle).
 */
OakRenderTicket oakrender_ticket_render_audio(
	OakNodeNode output_node, int64_t in_num, int64_t in_den,
	int64_t out_num, int64_t out_den, const OakAudioParams *params,
	int mode, oakrender_ticket_finished_fn cb, void *userdata);

int oakrender_ticket_is_finished(OakRenderTicket ticket);

/** @brief Block until the ticket finishes. */
int oakrender_ticket_wait(OakRenderTicket ticket);

int oakrender_ticket_cancel(OakRenderTicket ticket);

/** @brief OAKRENDER_TICKET_* or negative error. */
int oakrender_ticket_get_type(OakRenderTicket ticket);

/** @brief Ticket timestamp (video tickets). */
int oakrender_ticket_get_time(OakRenderTicket ticket, int64_t *out_num,
							  int64_t *out_den);

/** @brief Ticket time range (audio tickets). */
int oakrender_ticket_get_range(OakRenderTicket ticket, int64_t *in_num,
							   int64_t *in_den, int64_t *out_num,
							   int64_t *out_den);

/**
 * @brief The resulting frame (video tickets). *out receives an owned
 *        OakCodecFrame (release with oakrender_codec_frame_free()).
 *        OAKRENDER_E_STATE when unfinished, OAKRENDER_E_FAILED when the
 *        ticket has no frame result.
 */
int oakrender_ticket_get_frame(OakRenderTicket ticket, OakCodecFrame *out);

/**
 * @brief The resulting samples (audio tickets). *out receives a copy
 *        (release with oakcore_samplebuffer_free()).
 */
int oakrender_ticket_get_samples(OakRenderTicket ticket,
								 OakSampleBuffer **out);

/**
 * @brief Release one reference to a ticket (the final release is safe on
 * finished tickets; cancels and waits on running ones). Convenience
 * wrapper around ticket->release(ticket->ctx). NULL / empty-handle
 * no-op; clears ticket->ctx after releasing.
 */
void oakrender_ticket_free(OakRenderTicket *ticket);

/**
 * @brief Toggle aggressive garbage collection on the render manager
 *        (RenderManager::set_aggressive_garbage_collection()).
 */
int oakrender_manager_set_aggressive_gc(int enabled);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_RENDER_TICKET_H
