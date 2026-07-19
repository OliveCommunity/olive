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

#ifndef OAK_MSGBOX_H
#define OAK_MSGBOX_H

#include <QMessageBox>
#include <QWidget>

namespace olive
{

/**
 * @brief Shows a simple window-modal message box
 *
 * Lives in the UI layer: the engine (common/qtutils) must not reference
 * QMessageBox. Previously QtUtils::msg_box.
 */
inline int msg_box(QWidget *parent, QMessageBox::Icon icon,
				   const QString &title, const QString &message,
				   QMessageBox::StandardButtons buttons = QMessageBox::Ok)
{
	QMessageBox b(parent);
	b.setIcon(icon);
	b.setWindowModality(Qt::WindowModal);
	b.setWindowTitle(title);
	b.setText(message);

	uint mask = QMessageBox::FirstButton;
	while (mask <= QMessageBox::LastButton) {
		uint sb = buttons & mask;
		if (sb) {
			b.addButton(static_cast<QMessageBox::StandardButton>(sb));
		}
		mask <<= 1;
	}

	return b.exec();
}

}

#endif // OAK_MSGBOX_H
