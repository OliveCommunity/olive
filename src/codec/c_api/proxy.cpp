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

#include "codec/proxy.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "proxymanager.h"

namespace
{

int string_out(const std::string &s, char *buf, int buf_size)
{
	int need = static_cast<int>(s.size()) + 1;
	if (buf && buf_size > 0) {
		int n = std::min(static_cast<int>(s.size()), buf_size - 1);
		memcpy(buf, s.data(), n);
		buf[n] = '\0';
	}
	return need;
}

olive::ProxyManager::ProxyParams to_native(const oakcodec_proxy_params *p)
{
	olive::ProxyManager::ProxyParams n;
	if (p) {
		n.width = p->width;
		n.height = p->height;
		n.divider = p->divider;
		n.version = p->version;
		n.crf = p->crf;
		n.include_audio = p->include_audio != 0;
		n.extension = p->extension;
		n.preset = p->preset;
	}
	return n;
}

} // namespace

int oakcodec_proxy_create_instance(void)
{
	olive::ProxyManager::create_instance();
	return OAKCODEC_OK;
}

int oakcodec_proxy_destroy_instance(void)
{
	olive::ProxyManager::destroy_instance();
	return OAKCODEC_OK;
}

int oakcodec_proxy_params_default(oakcodec_proxy_params *out)
{
	if (!out)
		return OAKCODEC_E_INVALID;
	olive::ProxyManager::ProxyParams n =
		olive::ProxyManager::proxy_params_from_config();
	*out = {};
	out->width = n.width;
	out->height = n.height;
	out->divider = n.divider;
	out->version = n.version;
	out->crf = n.crf;
	out->include_audio = n.include_audio ? 1 : 0;
	snprintf(out->extension, sizeof(out->extension), "%s",
		 n.extension.c_str());
	snprintf(out->preset, sizeof(out->preset), "%s", n.preset.c_str());
	return OAKCODEC_OK;
}

int oakcodec_proxy_get_state(const char *proxy_filename)
{
	if (!proxy_filename || !*proxy_filename)
		return OAKCODEC_PROXY_STATE_MISSING;
	return static_cast<int>(
		olive::ProxyManager::get_proxy_state(proxy_filename));
}

int oakcodec_proxy_state_to_string(int state, char *buf, int buf_size)
{
	if (state < OAKCODEC_PROXY_STATE_MISSING ||
		state > OAKCODEC_PROXY_STATE_FAILED)
		return OAKCODEC_E_INVALID;
	return string_out(olive::ProxyManager::proxy_state_to_string(
					  static_cast<olive::ProxyManager::ProxyState>(state)),
				  buf, buf_size);
}

int oakcodec_proxy_get_proxy_directory(const char *cache_path, char *buf,
								   int buf_size)
{
	if (!cache_path)
		return OAKCODEC_E_INVALID;
	return string_out(olive::ProxyManager::get_proxy_directory(cache_path),
				  buf, buf_size);
}

int oakcodec_proxy_get_proxy_filename(const char *cache_path,
								  const char *source_filename,
								  int stream_index,
								  const oakcodec_proxy_params *params,
								  char *buf, int buf_size)
{
	if (!cache_path || !source_filename)
		return OAKCODEC_E_INVALID;
	return string_out(
		olive::ProxyManager::get_proxy_filename(
			cache_path, source_filename, stream_index, to_native(params)),
		buf, buf_size);
}

int oakcodec_proxy_get_working_filename(const char *proxy_filename,
									char *buf, int buf_size)
{
	if (!proxy_filename)
		return OAKCODEC_E_INVALID;
	return string_out(
		olive::ProxyManager::get_working_proxy_filename(proxy_filename),
		buf, buf_size);
}

int oakcodec_proxy_get_or_start(const char *cache_path,
							const char *source_filename, int stream_index,
							const oakcodec_proxy_params *params,
							oakcodec_proxy_result *out)
{
	if (!cache_path || !source_filename || !out)
		return OAKCODEC_E_INVALID;
	if (!olive::ProxyManager::instance())
		return OAKCODEC_E_STATE;

	olive::ProxyManager::Proxy p =
		olive::ProxyManager::instance()->get_or_start_proxy(
			cache_path, source_filename, stream_index, to_native(params));

	out->state = static_cast<int>(p.state);
	snprintf(out->filename, sizeof(out->filename), "%s",
		 p.filename.c_str());
	return OAKCODEC_OK;
}

int oakcodec_proxy_find_ffmpeg(const char *configured_path, char *buf,
							   int buf_size)
{
	return string_out(olive::ProxyManager::find_f_fmpeg_executable(
					  configured_path ? configured_path : ""),
				  buf, buf_size);
}
