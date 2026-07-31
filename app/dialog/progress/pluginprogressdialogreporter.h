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

namespace olive
{

class ProgressDialog;

/**
 * @brief ProgressDialog wrapper created through the plugin progress
 * reporter factory
 *
 * Plain UI-side class; the engine only ever talks to it through the C
 * callbacks registered with oakengine_plugin_set_progress_reporter_factory()
 * (create/destroy/is_cancelled/set_progress, see engine/src/capi/plugin.cpp).
 * The dialog is shown on construction and deleted with the reporter.
 */
class PluginProgressDialogReporter {
public:
	PluginProgressDialogReporter(const QString &message, const QString &title);

	~PluginProgressDialogReporter();

	void set_progress(double value);

	bool was_cancelled() const { return cancelled_; }

private:
	QPointer<ProgressDialog> dialog_;
	bool cancelled_ = false;
};

}

#endif // OAK_PLUGINPROGRESSDIALOGREPORTER_H
