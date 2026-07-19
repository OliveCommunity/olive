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

#include "lutlibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "config/config.h"

namespace olive
{

const QStringList &LUTLibrary::supported_extensions()
{
	// LUT formats OCIO FileTransform can load
	static const QStringList extensions = {
		QStringLiteral("cube"),	 QStringLiteral("3dl"),  QStringLiteral("spi1d"),
		QStringLiteral("spi3d"), QStringLiteral("spimtx"), QStringLiteral("csp"),
		QStringLiteral("clf"),	 QStringLiteral("ctf"),  QStringLiteral("cub"),
	};
	return extensions;
}

bool LUTLibrary::is_supported_extension(const QString &suffix)
{
	QString s = suffix;
	if (s.startsWith(QLatin1Char('.'))) {
		s.remove(0, 1);
	}

	return supported_extensions().contains(s.toLower());
}

QStringList LUTLibrary::get_directories()
{
	const QString serialized = OAK_CONFIG("LUTLibraryPaths").toString();

	QStringList dirs = serialized.split(QLatin1Char(';'), Qt::SkipEmptyParts);
	for (QString &dir : dirs) {
		dir = QDir::fromNativeSeparators(dir.trimmed());
	}
	return dirs;
}

void LUTLibrary::set_directories(const QStringList &dirs)
{
	QStringList cleaned;
	for (const QString &dir : dirs) {
		const QString trimmed = dir.trimmed();
		if (!trimmed.isEmpty() && !cleaned.contains(trimmed)) {
			cleaned.append(trimmed);
		}
	}

	Config::current()[QStringLiteral("LUTLibraryPaths")] =
		cleaned.join(QLatin1Char(';'));
}

QStringList LUTLibrary::get_lut_files()
{
	QStringList files;

	static const QStringList k_filters = { QStringLiteral("*.cube"),
										  QStringLiteral("*.3dl") };

	for (const QString &dir : get_directories()) {
		QDirIterator it(dir, k_filters, QDir::Files,
						QDirIterator::Subdirectories);
		while (it.hasNext()) {
			files.append(it.next());
		}
	}

	return files;
}

}
