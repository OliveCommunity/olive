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

#include "render/ticket.h"

#include <new>

#include "../src/colorprocessor.h"
#include "../src/framehashcache.h"
#include "../src/rendermanager.h"
#include "../src/renderticket.h"
#include "internalhandles.h"

namespace
{

struct TicketHandle {
	olive::RenderTicketWatcher *watcher;
	olive::RenderManager::TicketType type;
	olive::core::Rational time;
	olive::core::TimeRange range;
};

TicketHandle *impl(OakRenderTicket *t)
{
	return reinterpret_cast<TicketHandle *>(t);
}

OakRenderTicket *wrap(olive::RenderTicketWatcher *w,
					  olive::RenderManager::TicketType type)
{
	if (!w) {
		return NULL;
	}
	TicketHandle *h = new (std::nothrow) TicketHandle{
		w, type, olive::core::Rational(), olive::core::TimeRange()
	};
	if (!h) {
		delete w;
		return NULL;
	}
	return reinterpret_cast<OakRenderTicket *>(h);
}

olive::Node *to_node(OakNodeNode *n)
{
	return reinterpret_cast<olive::Node *>(n);
}

} // namespace

OakRenderTicket *oakrender_ticket_render_frame(
	const oakrender_video_ticket_params *params,
	oakrender_ticket_finished_fn cb, void *userdata)
{
	if (!params || !params->output_node) {
		return NULL;
	}

	olive::RenderManager *manager = olive::RenderManager::instance();
	if (!manager) {
		return NULL;
	}

	try {
		const olive::VideoParams *vp =
			params->video_params.ctx
				? oakcommon_videoparams_get_native(params->video_params)
				: nullptr;
		if (!vp) {
			return NULL;
		}

		olive::RenderManager::RenderVideoParams rvp(
			to_node(params->output_node), *vp,
			params->audio_params
				? olive::AudioParams::from_handle(
					oakcore_audioparams_copy(params->audio_params))
				: olive::AudioParams(),
			olive::core::Rational(int(params->time_num),
								  int(params->time_den)),
			reinterpret_cast<olive::ColorManager *>(params->color_manager),
			static_cast<olive::RenderMode::Mode>(params->mode));

		rvp.force_size =
			olive::FrameSize(params->force_width, params->force_height);
		if (params->has_force_matrix) {
			olive::Matrix4x4 matrix;
			for (int row = 0; row < 4; row++) {
				for (int col = 0; col < 4; col++) {
					matrix(row, col) =
						float(params->force_matrix[row * 4 + col]);
				}
			}
			rvp.force_matrix = matrix;
		}
		rvp.force_format = static_cast<olive::core::PixelFormat::Format>(
			params->force_format);
		rvp.force_channel_count = params->force_channel_count;
		rvp.force_color_output =
			params->force_color_output ? params->force_color_output->ptr
									   : nullptr;
		if (params->force_color_transform.ctx) {
			const olive::ColorTransform *ct =
				oakcommon_colortransform_get_native(
					params->force_color_transform);
			if (ct) {
				rvp.force_color_transform = *ct;
			}
		}

		if (params->cache) {
			rvp.add_cache(reinterpret_cast<olive::FrameHashCache *>(
				params->cache));
		}

		auto *watcher = new olive::RenderTicketWatcher();
		watcher->set_property("type", olive::Variant::from_value(
										  int(olive::RenderManager::k_type_video)));
		watcher->set_property(
			"time", olive::Variant::from_value(olive::core::Rational(
						int(params->time_num), int(params->time_den))));

		OakRenderTicket *handle =
			wrap(watcher, olive::RenderManager::k_type_video);
		if (!handle) {
			return NULL;
		}
		impl(handle)->time = olive::core::Rational(
			int(params->time_num), int(params->time_den));

		if (cb) {
			watcher->set_finished_callback(
				[handle, cb, userdata](olive::RenderTicketWatcher *) {
					cb(handle, userdata);
				});
		}

		watcher->set_ticket(manager->render_frame(rvp));
		return handle;
	} catch (...) {
		return NULL;
	}
}

OakRenderTicket *oakrender_ticket_render_audio(
	OakNodeNode *output_node, int64_t in_num, int64_t in_den,
	int64_t out_num, int64_t out_den, const OakAudioParams *params,
	int mode, oakrender_ticket_finished_fn cb, void *userdata)
{
	if (!output_node || !params) {
		return NULL;
	}

	olive::RenderManager *manager = olive::RenderManager::instance();
	if (!manager) {
		return NULL;
	}

	try {
		olive::core::Rational range_in((int)in_num, (int)in_den);
		olive::core::Rational range_out((int)out_num, (int)out_den);
		olive::core::TimeRange range(range_in, range_out);

		olive::RenderManager::RenderAudioParams rap(
			to_node(output_node), range,
			olive::AudioParams::from_handle(
				oakcore_audioparams_copy(params)),
			static_cast<olive::RenderMode::Mode>(mode));

		auto *watcher = new olive::RenderTicketWatcher();
		watcher->set_property("type", olive::Variant::from_value(
										  int(olive::RenderManager::k_type_audio)));
		watcher->set_property("range", olive::Variant::from_value(range));

		OakRenderTicket *handle =
			wrap(watcher, olive::RenderManager::k_type_audio);
		if (!handle) {
			return NULL;
		}
		impl(handle)->range = range;

		if (cb) {
			watcher->set_finished_callback(
				[handle, cb, userdata](olive::RenderTicketWatcher *) {
					cb(handle, userdata);
				});
		}

		watcher->set_ticket(manager->render_audio(rap));
		return handle;
	} catch (...) {
		return NULL;
	}
}

int oakrender_ticket_is_finished(OakRenderTicket *ticket)
{
	if (!ticket) {
		return OAKRENDER_E_INVALID;
	}
	return impl(ticket)->watcher->is_running() ? 0 : 1;
}

int oakrender_ticket_wait(OakRenderTicket *ticket)
{
	if (!ticket) {
		return OAKRENDER_E_INVALID;
	}
	impl(ticket)->watcher->wait_for_finished();
	return OAKRENDER_OK;
}

int oakrender_ticket_cancel(OakRenderTicket *ticket)
{
	if (!ticket) {
		return OAKRENDER_E_INVALID;
	}
	impl(ticket)->watcher->cancel();
	return OAKRENDER_OK;
}

int oakrender_ticket_get_type(OakRenderTicket *ticket)
{
	if (!ticket) {
		return OAKRENDER_E_INVALID;
	}
	return int(impl(ticket)->type);
}

int oakrender_ticket_get_time(OakRenderTicket *ticket, int64_t *out_num,
							  int64_t *out_den)
{
	if (!ticket || !out_num || !out_den) {
		return OAKRENDER_E_INVALID;
	}
	*out_num = impl(ticket)->time.numerator();
	*out_den = impl(ticket)->time.denominator();
	return OAKRENDER_OK;
}

int oakrender_ticket_get_range(OakRenderTicket *ticket, int64_t *in_num,
							   int64_t *in_den, int64_t *out_num,
							   int64_t *out_den)
{
	if (!ticket || !in_num || !in_den || !out_num || !out_den) {
		return OAKRENDER_E_INVALID;
	}
	*in_num = impl(ticket)->range.in().numerator();
	*in_den = impl(ticket)->range.in().denominator();
	*out_num = impl(ticket)->range.out().numerator();
	*out_den = impl(ticket)->range.out().denominator();
	return OAKRENDER_OK;
}

int oakrender_ticket_get_frame(OakRenderTicket *ticket, OakCodecFrame **out)
{
	if (!ticket || !out) {
		return OAKRENDER_E_INVALID;
	}
	*out = NULL;

	TicketHandle *h = impl(ticket);
	if (h->watcher->is_running()) {
		return OAKRENDER_E_STATE;
	}

	olive::Variant result = h->watcher->get();
	if (!result.can_convert<olive::FramePtr>()) {
		return OAKRENDER_E_FAILED;
	}

	olive::FramePtr frame = result.value<olive::FramePtr>();
	if (!frame) {
		return OAKRENDER_E_FAILED;
	}

	OakCodecFrame *handle = new (std::nothrow) OakCodecFrame;
	if (!handle) {
		return OAKRENDER_E_NOMEM;
	}
	handle->ptr = frame;
	*out = handle;
	return OAKRENDER_OK;
}

int oakrender_ticket_get_samples(OakRenderTicket *ticket,
								 OakSampleBuffer **out)
{
	if (!ticket || !out) {
		return OAKRENDER_E_INVALID;
	}
	*out = NULL;

	TicketHandle *h = impl(ticket);
	if (h->watcher->is_running()) {
		return OAKRENDER_E_STATE;
	}

	olive::Variant result = h->watcher->get();
	if (!result.can_convert<olive::SampleBuffer>()) {
		return OAKRENDER_E_FAILED;
	}

	olive::SampleBuffer samples = result.value<olive::SampleBuffer>();
	*out = oakcore_samplebuffer_copy(samples.handle());
	return *out ? OAKRENDER_OK : OAKRENDER_E_NOMEM;
}

void oakrender_ticket_free(OakRenderTicket *ticket)
{
	if (!ticket) {
		return;
	}

	TicketHandle *h = impl(ticket);
	if (h->watcher->is_running()) {
		h->watcher->cancel();
		h->watcher->wait_for_finished();
	}
	delete h->watcher;
	delete h;
}

int oakrender_manager_set_aggressive_gc(int enabled)
{
	olive::RenderManager *manager = olive::RenderManager::instance();
	if (!manager) {
		return OAKRENDER_E_STATE;
	}
	manager->set_aggressive_garbage_collection(enabled != 0);
	return OAKRENDER_OK;
}
