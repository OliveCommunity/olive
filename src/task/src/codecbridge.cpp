/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "codecbridge.h"

#include <memory>

#include "codec/task.h"
#include "common/config.h"
#include "conform/conform.h"
#include "proxy/proxy.h"

namespace olive
{

namespace
{

oakcodec_proxy_params proxy_params_from_config()
{
	oakcodec_proxy_params params = {};
	oakcodec_proxy_params_default(&params);

	params.width = oakcommon_config_get_int(nullptr, "ProxyWidth",
											params.width);
	params.height = oakcommon_config_get_int(nullptr, "ProxyHeight",
											 params.height);
	params.divider = oakcommon_config_get_int(nullptr, "ProxyDivider",
											  params.divider);
	params.crf = oakcommon_config_get_int(nullptr, "ProxyCRF", params.crf);
	params.include_audio =
		oakcommon_config_get_bool(nullptr, "ProxyIncludeAudio",
								  params.include_audio)
			? 1
			: 0;
	oakcommon_config_get(nullptr, "ProxyPreset", params.preset,
						 int(sizeof(params.preset)));
	oakcommon_config_get(nullptr, "ProxyExtension", params.extension,
						 int(sizeof(params.extension)));
	return params;
}

int submit_codec_task(const OakCodecTaskRequest *req, void *userdata)
{
	(void)userdata;

	if (!req) {
		return OAKCODEC_E_INVALID;
	}

	std::unique_ptr<Task> task;

	switch (req->kind) {
	case OAKCODEC_TASK_CONFORM:
		task = std::make_unique<ConformTask>(*req);
		break;
	case OAKCODEC_TASK_PROXY:
		task = std::make_unique<ProxyTask>(*req, proxy_params_from_config());
		break;
	default:
		return OAKCODEC_E_INVALID;
	}

	// Interim contract: submission is synchronous.
	return task->start() ? OAKCODEC_OK : OAKCODEC_E_FAILED;
}

} // namespace

void register_codec_task_submitter()
{
	oakcodec_set_task_submit_cb(submit_codec_task, nullptr);
}

void unregister_codec_task_submitter()
{
	oakcodec_set_task_submit_cb(nullptr, nullptr);
}

}
