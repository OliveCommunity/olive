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

#ifndef OAK_DEBUGAPP_H
#define OAK_DEBUGAPP_H

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QMutex>
#include <QTextStream>
#include <iostream>

namespace olive {

/**
 * @brief App-side debug handler (moved from engine/common/debug.cpp)
 *
 * Replaces engine's olive::debug_handler so oak-editor doesn't import
 * that symbol. Only used in main.cpp's qInstallMessageHandler.
 */
[[maybe_unused]] [[maybe_unused]] static void debug_handler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
	// Suppress noisy warnings from Qt's QXcbIntegration
	if (type == QtWarningMsg && msg.contains("QXcbIntegration")) {
		return;
	}

	// Suppress all Qt warnings during automated testing
	static const bool is_testing = qEnvironmentVariableIsSet("OAK_TESTING");
	if (is_testing && type == QtWarningMsg) {
		return;
	}

	QString log_line;

	switch (type) {
	case QtDebugMsg:
		log_line = QStringLiteral("Debug: %1 (%2:%3, %4)\n");
		break;
	case QtInfoMsg:
		log_line = QStringLiteral("Info: %1 (%2:%3, %4)\n");
		break;
	case QtWarningMsg:
		log_line = QStringLiteral("Warning: %1 (%2:%3, %4)\n");
		break;
	case QtCriticalMsg:
		log_line = QStringLiteral("Critical: %1 (%2:%3, %4)\n");
		break;
	case QtFatalMsg:
		log_line = QStringLiteral("Fatal: %1 (%2:%3, %4)\n");
		break;
	}

	log_line = log_line.arg(msg, context.file != nullptr ? context.file : "<null>",
							QString::number(context.line), context.function != nullptr ?
														   context.function : "<null>");

	std::cerr << log_line.toUtf8().constData();

	if (type == QtFatalMsg) {
		abort();
	}
}

} // namespace olive

#endif // OAK_DEBUGAPP_H
