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

#include "oakengine/lut.h"

#include <cstring>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "render/lutlibrary.h"

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

} // namespace

extern "C" int oakengine_lut_directory_count(void)
{
    return olive::LUTLibrary::get_directories().size();
}

extern "C" int oakengine_lut_directory_at(int index, char *buf, int buf_size)
{
    const QStringList dirs = olive::LUTLibrary::get_directories();
    if (index < 0 || index >= dirs.size()) {
        return OAKENGINE_E_NOT_FOUND;
    }
    return write_string(dirs.at(index), buf, buf_size);
}

extern "C" int oakengine_lut_file_count(void)
{
    return olive::LUTLibrary::get_lut_files().size();
}

extern "C" int oakengine_lut_file_at(int index, char *buf, int buf_size)
{
    const QStringList files = olive::LUTLibrary::get_lut_files();
    if (index < 0 || index >= files.size()) {
        return OAKENGINE_E_NOT_FOUND;
    }
    return write_string(files.at(index), buf, buf_size);
}

extern "C" int oakengine_lut_set_directories(const char *const *dirs,
                                             int count)
{
    QStringList list;
    if (dirs && count > 0) {
        list.reserve(count);
        for (int i = 0; i < count; i++) {
            if (dirs[i]) {
                list.append(QString::fromUtf8(dirs[i]));
            }
        }
    }
    olive::LUTLibrary::set_directories(list);
    return OAKENGINE_OK;
}
