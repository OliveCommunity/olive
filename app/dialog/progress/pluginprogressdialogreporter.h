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

#ifndef OAK_PLUGINPROGRESSDIALOGREPORTER_H
#define OAK_PLUGINPROGRESSDIALOGREPORTER_H

#include <QPointer>

#include "pluginSupport/pluginprogressreporter.h"

namespace olive
{

class ProgressDialog;

/**
 * @brief PluginProgressReporter implementation wrapping a ProgressDialog
 *
 * UI-side reporter created through the plugin progress reporter factory.
 * Forwards progress calls to the dialog and the dialog's cancelled() signal
 * back to the engine. The dialog is deleted on close; the reporter itself
 * is destroyed by the engine with deleteLater().
 */
class PluginProgressDialogReporter : public plugin::PluginProgressReporter {
	Q_OBJECT
public:
	PluginProgressDialogReporter(const QString &message, const QString &title);

	virtual ~PluginProgressDialogReporter() override;

	virtual void set_progress(double value) override;

	virtual void show() override;

	virtual void close() override;

private:
	QPointer<ProgressDialog> dialog_;
};

}

#endif // OAK_PLUGINPROGRESSDIALOGREPORTER_H
