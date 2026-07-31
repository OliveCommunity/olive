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

#include "pluginprogressdialogreporter.h"

#include "dialog/progress/progress.h"

namespace olive
{

PluginProgressDialogReporter::PluginProgressDialogReporter(
	const QString &message, const QString &title)
	: dialog_(new ProgressDialog(message, title, nullptr))
{
	dialog_->setAttribute(Qt::WA_DeleteOnClose);
	QObject::connect(dialog_, &ProgressDialog::cancelled, dialog_, [this]() {
		cancelled_ = true;
	});
	// The engine's C adapter treats show() as a no-op, so the dialog is
	// shown here at create time.
	dialog_->show();
}

PluginProgressDialogReporter::~PluginProgressDialogReporter()
{
	delete dialog_;
}

void PluginProgressDialogReporter::set_progress(double value)
{
	if (dialog_) {
		dialog_->set_progress(value);
	}
}

}
