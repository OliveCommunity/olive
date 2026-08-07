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

#include "plugin/host.h"

#include <cstring>
#include <string>
#include <vector>

#include "../src/olivehost.h"

namespace
{

olive::plugin::HostMessageHandler forwarder_fn;
oakplugin_message_fn user_fn;
void *user_userdata;

int copy_string(const std::string &value, char *buf, int buf_size)
{
	int needed = int(value.size()) + 1;
	if (buf && buf_size >= needed) {
		memcpy(buf, value.c_str(), needed);
	}
	return needed;
}

std::vector<std::string> plugin_ids()
{
	std::vector<std::string> ids;
	OFX::Host::PluginCache *cache =
		OFX::Host::PluginCache::getPluginCache();
	if (!cache) {
		return ids;
	}
	for (const auto &plugin : cache->getPlugins()) {
		if (plugin) {
			ids.push_back(plugin->getIdentifier());
		}
	}
	return ids;
}

} // namespace

int oakplugin_host_init(void)
{
	try {
		olive::plugin::load_plugins();
		return OAKPLUGIN_OK;
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

void oakplugin_host_shutdown(void)
{
	// The OFX plugin cache is process-global; nothing to tear down here
	// (the Current slots release their references on their own).
}

int oakplugin_host_scan(const char *const *bundle_dirs, int dir_count)
{
	if (!bundle_dirs || dir_count < 0) {
		return OAKPLUGIN_E_INVALID;
	}

	try {
		for (int i = 0; i < dir_count; i++) {
			if (bundle_dirs[i]) {
				olive::plugin::load_plugins(bundle_dirs[i]);
			}
		}
		return OAKPLUGIN_OK;
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

int oakplugin_host_plugin_count(void)
{
	try {
		return int(plugin_ids().size());
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

int oakplugin_host_plugin_id_at(int index, char *buf, int buf_size)
{
	try {
		std::vector<std::string> ids = plugin_ids();
		if (index < 0 || index >= int(ids.size())) {
			return OAKPLUGIN_E_NOT_FOUND;
		}
		return copy_string(ids[size_t(index)], buf, buf_size);
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

int oakplugin_host_plugin_label(const char *plugin_id, char *buf,
								int buf_size)
{
	if (!plugin_id) {
		return OAKPLUGIN_E_INVALID;
	}

	try {
		for (const std::string &id : plugin_ids()) {
			if (id == plugin_id) {
				return copy_string(id, buf, buf_size);
			}
		}
		return OAKPLUGIN_E_NOT_FOUND;
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

void oakplugin_host_set_message_handler(oakplugin_message_fn fn,
										void *userdata)
{
	user_fn = fn;
	user_userdata = userdata;

	if (!fn) {
		olive::plugin::set_host_message_handler(nullptr);
		return;
	}

	olive::plugin::set_host_message_handler(
		[](const char *type, const std::string &message) -> OfxStatus {
			int answer = user_fn(type, message.c_str(), user_userdata);
			return answer == OAKPLUGIN_MESSAGE_ANSWER_YES ? kOfxStatReplyYes
														  : kOfxStatReplyNo;
		});
}
