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

#include "oakengine/proxy.h"

#include <cstring>

#include <QByteArray>
#include <QString>

#include "codec/proxymanager.h"

namespace
{

int write_string(const QString &s, char *buf, int buf_size)
{
    const QByteArray utf8 = s.toUtf8();
    const int len = int(utf8.size());
    if (buf && buf_size > 0) {
        const int n = qMin(len, buf_size - 1);
        std::memcpy(buf, utf8.constData(), size_t(n));
        buf[n] = '\0';
    }
    return len;
}

olive::ProxyManager::ProxyParams params_from_c(const oak_proxy_params *params)
{
    olive::ProxyManager::ProxyParams out;
    if (!params) {
        return out;
    }
    out.width = params->width;
    out.height = params->height;
    out.divider = params->divider;
    out.version = params->version;
    out.crf = params->crf;
    out.include_audio = params->include_audio != 0;
    out.extension = QString::fromUtf8(params->extension);
    out.preset = QString::fromUtf8(params->preset);
    return out;
}

void params_to_c(const olive::ProxyManager::ProxyParams &params,
                 oak_proxy_params *out)
{
    if (!out) {
        return;
    }
    out->width = params.width;
    out->height = params.height;
    out->divider = params.divider;
    out->version = params.version;
    out->crf = params.crf;
    out->include_audio = params.include_audio ? 1 : 0;
    const QByteArray ext = params.extension.toUtf8();
    const int ext_n = qMin(int(ext.size()), int(sizeof(out->extension) - 1));
    std::memcpy(out->extension, ext.constData(), size_t(ext_n));
    out->extension[ext_n] = '\0';
    const QByteArray preset = params.preset.toUtf8();
    const int preset_n = qMin(int(preset.size()), int(sizeof(out->preset) - 1));
    std::memcpy(out->preset, preset.constData(), size_t(preset_n));
    out->preset[preset_n] = '\0';
}

} // namespace

extern "C" int oakengine_proxy_create_instance(void)
{
    olive::ProxyManager::create_instance();
    return olive::ProxyManager::instance() ? OAKENGINE_OK : OAKENGINE_E_FAILED;
}

extern "C" int oakengine_proxy_destroy_instance(void)
{
    olive::ProxyManager::destroy_instance();
    return OAKENGINE_OK;
}

extern "C" int oakengine_proxy_params_from_config(oak_proxy_params *out)
{
    if (!out) {
        return OAKENGINE_E_INVALID;
    }
    params_to_c(olive::ProxyManager::proxy_params_from_config(), out);
    return OAKENGINE_OK;
}

extern "C" int oakengine_proxy_get_state(const char *proxy_filename)
{
    if (!proxy_filename || std::strlen(proxy_filename) == 0) {
        return OAKENGINE_PROXY_STATE_MISSING;
    }
    return static_cast<int>(olive::ProxyManager::get_proxy_state(
        QString::fromUtf8(proxy_filename)));
}

extern "C" int oakengine_proxy_state_to_string(int state, char *buf,
                                               int buf_size)
{
    if (state != OAKENGINE_PROXY_STATE_MISSING &&
        state != OAKENGINE_PROXY_STATE_GENERATING &&
        state != OAKENGINE_PROXY_STATE_READY &&
        state != OAKENGINE_PROXY_STATE_FAILED) {
        return OAKENGINE_E_INVALID;
    }
    const QString s = olive::ProxyManager::proxy_state_to_string(
        static_cast<olive::ProxyManager::ProxyState>(state));
    return write_string(s, buf, buf_size);
}

extern "C" int oakengine_proxy_get_or_start(const char *cache_path,
                                            const char *source_filename,
                                            int stream_index,
                                            const oak_proxy_params *params,
                                            oak_proxy_result *out)
{
    if (!out) {
        return OAKENGINE_E_INVALID;
    }
    olive::ProxyManager *mgr = olive::ProxyManager::instance();
    if (!mgr) {
        return OAKENGINE_E_STATE;
    }
    if (!cache_path || !source_filename || !params) {
        return OAKENGINE_E_INVALID;
    }

    olive::ProxyManager::Proxy proxy = mgr->get_or_start_proxy(
        QString::fromUtf8(cache_path), QString::fromUtf8(source_filename),
        stream_index, params_from_c(params));

    out->state = static_cast<int>(proxy.state);
    const QByteArray fn = proxy.filename.toUtf8();
    const int n = qMin(int(fn.size()), int(sizeof(out->filename) - 1));
    std::memcpy(out->filename, fn.constData(), size_t(n));
    out->filename[n] = '\0';
    out->task = reinterpret_cast<int64_t>(proxy.task);
    return OAKENGINE_OK;
}

extern "C" int oakengine_proxy_get_working_filename(const char *proxy_filename,
                                                    char *buf, int buf_size)
{
    if (!proxy_filename) {
        return OAKENGINE_E_INVALID;
    }
    const QString s = olive::ProxyManager::get_working_proxy_filename(
        QString::fromUtf8(proxy_filename));
    return write_string(s, buf, buf_size);
}
