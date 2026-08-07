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

#include "../../../include/render/manager.h"

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>

#include "alivecount.h"
#include "internalhandles.h"

#include "../../node/c_api/nodehandle.h"

#include "diskmanager.h"
#include "output/viewer/viewer.h"
#include "previewautocacher.h"
#include "rendermanager.h"
#include "renderticket.h"

namespace
{

int write_string(const std::string &s, char *buf, int n)
{
	const int required = int(s.size()) + 1;
	if (buf && n >= required) {
		std::memcpy(buf, s.c_str(), size_t(required));
	}
	return required;
}

std::mutex g_requests_mutex;
std::map<int64_t, olive::RenderTicketPtr> g_requests;
std::atomic<int64_t> g_next_request_id(1);

} // namespace

int oakrender_manager_init(void)
{
	if (olive::RenderManager::instance()) {
		return OAKRENDER_E_STATE;
	}
	try {
		olive::RenderManager::create_instance();
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

void oakrender_manager_shutdown(void)
{
	try {
		olive::RenderManager::destroy_instance();
	} catch (...) {
	}
}

int64_t oakrender_request_frame(OakNodeNode viewer, int64_t ts,
								oakrender_frame_ready_fn cb, void *userdata)
{
	if (!viewer.ctx || !cb) {
		return OAKRENDER_E_INVALID;
	}
	olive::RenderManager *manager = olive::RenderManager::instance();
	if (!manager) {
		return OAKRENDER_E_STATE;
	}
	auto *v = dynamic_cast<olive::ViewerOutput *>(
		oaknode_c_api::to_native<olive::Node>(viewer));
	if (!v) {
		return OAKRENDER_E_INVALID;
	}
	try {
		// ts is a frame number in the viewer's video timebase; fall back
		// to whole seconds when the viewer carries no valid timebase.
		olive::Rational tb = v->get_video_params().time_base();
		const olive::Rational time = tb.isNull() ?
										 olive::Rational::from_double(double(ts)) :
										 olive::core::Timecode::timestamp_to_time(ts, tb);

		olive::RenderTicketPtr ticket =
			manager->get_cacher()->get_single_frame(v, time);
		if (!ticket) {
			return OAKRENDER_E_FAILED;
		}

		const int64_t id =
			g_next_request_id.fetch_add(1, std::memory_order_relaxed);

		olive::RenderTicket *raw_ticket = ticket.get();
		ticket->set_finished_callback([ticket, cb, ts, userdata, id]() {
			OakCodecFrame handle = {};
			if (ticket->has_result()) {
				olive::FramePtr f = ticket->get().value<olive::FramePtr>();
				if (f) {
					auto *impl = new (std::nothrow) OakCodecFrameImpl;
					if (impl) {
						impl->ptr = std::move(f);
						handle = oakrender_c_api::make_handle<OakCodecFrame>(
							impl, true,
							&oakrender_c_api::delete_as<OakCodecFrameImpl>);
					}
				}
			}
			{
				std::lock_guard<std::mutex> locker(g_requests_mutex);
				g_requests.erase(id);
			}
			cb(handle, ts, userdata);
		});

		{
			std::lock_guard<std::mutex> locker(g_requests_mutex);
			g_requests[id] = ticket;
		}
		(void) raw_ticket;
		return id;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_cancel_request(int64_t request_id)
{
	olive::RenderTicketPtr ticket;
	{
		std::lock_guard<std::mutex> locker(g_requests_mutex);
		auto it = g_requests.find(request_id);
		if (it == g_requests.end()) {
			return OAKRENDER_E_NOT_FOUND;
		}
		ticket = it->second;
		g_requests.erase(it);
	}
	ticket->cancel();
	if (olive::RenderManager::instance()) {
		olive::RenderManager::instance()->remove_ticket(ticket);
	}
	return OAKRENDER_OK;
}

int oakrender_set_cacher_multicam(OakNodeNode multicam_or_NULL)
{
	olive::RenderManager *manager = olive::RenderManager::instance();
	if (!manager) {
		return OAKRENDER_E_STATE;
	}
	manager->get_cacher()->set_multicam_node(
		multicam_or_NULL.ctx ?
			oaknode_c_api::to_native<olive::MultiCamNode>(multicam_or_NULL) :
			nullptr);
	return OAKRENDER_OK;
}

int oakrender_set_display_color_processor(OakColorProcessor p_or_NULL)
{
	olive::RenderManager *manager = olive::RenderManager::instance();
	if (!manager) {
		return OAKRENDER_E_STATE;
	}
	OakColorProcessorImpl *p =
		oakrender_c_api::to_native<OakColorProcessorImpl>(p_or_NULL);
	manager->get_cacher()->set_display_color_processor(p ? p->ptr : nullptr);
	return OAKRENDER_OK;
}

/* ---- Disk cache ----------------------------------------------------------- */

int oakrender_disk_cache_path(char *buf, int n)
{
	try {
		return write_string(olive::DiskManager::get_default_disk_cache_path(),
							buf, n);
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int64_t oakrender_disk_cache_size(void)
{
	try {
		olive::DiskManager *dm = olive::DiskManager::instance();
		if (!dm || !dm->get_default_cache_folder()) {
			return OAKRENDER_E_FAILED;
		}
		return dm->get_default_cache_folder()->get_consumption();
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_disk_cache_clear(void)
{
	try {
		olive::DiskManager *dm = olive::DiskManager::instance();
		if (!dm) {
			return OAKRENDER_E_FAILED;
		}
		return dm->clear_disk_cache(
				   olive::DiskManager::get_default_disk_cache_path()) ?
				   OAKRENDER_OK :
				   OAKRENDER_E_FAILED;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}
