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

#include "oakengine/config.h"

#include <cstring>

#include <QByteArray>
#include <QString>

#include "config/config.h"

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

oakengine_config_error_fn g_error_fn = nullptr;
void *g_error_userdata = nullptr;

void error_handler(const QString &title, const QString &message)
{
	if (g_error_fn) {
		const QByteArray t = title.toUtf8();
		const QByteArray m = message.toUtf8();
		g_error_fn(t.constData(), m.constData(), g_error_userdata);
	}
}

} // namespace

extern "C" int oakengine_config_load(void)
{
	olive::Config::load();
	return OAKENGINE_OK;
}

extern "C" int oakengine_config_save(void)
{
	olive::Config::save();
	return OAKENGINE_OK;
}

extern "C" int oakengine_config_get_string(const char *key, char *buf,
										   int buf_size)
{
	if (!key) {
		return OAKENGINE_E_INVALID;
	}
	const QVariant v = olive::Config::current()[QString::fromUtf8(key)];
	const QString s = v.toString();
	return write_string(s, buf, buf_size);
}

extern "C" int oakengine_config_set_string(const char *key,
										   const char *value)
{
	if (!key) {
		return OAKENGINE_E_INVALID;
	}
	olive::Config::current()[QString::fromUtf8(key)] =
			QString::fromUtf8(value ? value : "");
	return OAKENGINE_OK;
}

extern "C" int64_t oakengine_config_get_int(const char *key,
											int64_t default_value)
{
	if (!key) {
		return default_value;
	}
	const QVariant v = olive::Config::current()[QString::fromUtf8(key)];
	bool ok = false;
	const qlonglong val = v.toLongLong(&ok);
	return ok ? static_cast<int64_t>(val) : default_value;
}

extern "C" int oakengine_config_set_int(const char *key, int64_t value)
{
	if (!key) {
		return OAKENGINE_E_INVALID;
	}
	olive::Config::current()[QString::fromUtf8(key)] =
			static_cast<qlonglong>(value);
	return OAKENGINE_OK;
}

extern "C" int oakengine_config_set_error_handler(
		oakengine_config_error_fn fn, void *userdata)
{
	g_error_fn = fn;
	g_error_userdata = userdata;
	olive::Config::set_error_handler(fn ? error_handler : nullptr);
	return OAKENGINE_OK;
}

extern "C" int oakengine_config_report_error(const char *title,
											 const char *message)
{
	olive::Config::report_error(QString::fromUtf8(title ? title : ""),
							QString::fromUtf8(message ? message : ""));
	return OAKENGINE_OK;
}
