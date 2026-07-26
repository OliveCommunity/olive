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

// App-side implementations of FileFunctions methods that would otherwise
// be imported from liboakengine. The declarations live in the engine header
// (common/filefunctions.h) which is on the public include path; these
// definitions resolve the symbols locally in the app binary.

#include "common/filefunctions.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace olive
{

bool FileFunctions::directory_is_valid(const QDir &d,
									 bool try_to_create_if_not_exists)
{
	return d.exists() ||
		   (try_to_create_if_not_exists && d.mkpath(QStringLiteral(".")));
}

QString FileFunctions::read_file_as_string(const QString &filename)
{
	QFile f(filename);
	QString file_data;
	if (f.open(QFile::ReadOnly | QFile::Text)) {
		QTextStream text_stream(&f);
		file_data = text_stream.readAll();
		f.close();
	}
	return file_data;
}

QString FileFunctions::get_auto_recovery_root()
{
	return QDir(QStandardPaths::writableLocation(
					QStandardPaths::AppLocalDataLocation))
		.filePath(QStringLiteral("autorecovery"));
}

QString FileFunctions::ensure_filename_extension(QString fn,
											   const QString &extension)
{
	if (!fn.isEmpty() && !extension.isEmpty()) {
		QString extension_with_dot;
		extension_with_dot.append('.');
		extension_with_dot.append(extension);
		if (!fn.endsWith(extension_with_dot, Qt::CaseInsensitive)) {
			fn.append(extension_with_dot);
		}
	}
	return fn;
}

QString FileFunctions::get_configuration_location()
{
	if (is_portable()) {
		return get_application_path();
	} else {
		QString s = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		QDir(s).mkpath(".");
		return s;
	}
}

bool FileFunctions::is_portable()
{
	return QFileInfo::exists(QDir(get_application_path()).filePath("portable"));
}

QString FileFunctions::get_application_path()
{
	return QCoreApplication::applicationDirPath();
}

}
