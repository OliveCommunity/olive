/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2020 Olive Team
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

#ifndef OAK_MEDIAPROPERTIESDIALOG_H
#define OAK_MEDIAPROPERTIESDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>

#include "node/project/footage/footage.h"

namespace olive
{

/**
 * @brief The MediaPropertiesDialog class
 *
 * A dialog for setting properties on Media. This can be loaded from any part of the application provided it's given
 * a valid Media object.
 */
class FootagePropertiesDialog : public QDialog {
	Q_OBJECT
public:
	/**
   * @brief MediaPropertiesDialog Constructor
   *
   * @param parent
   *
   * QWidget parent. Usually MainWindow or Project panel.
   *
   * @param i
   *
   * Media object to set properties for.
   */
	FootagePropertiesDialog(QWidget *parent, Footage *footage);

private:
	/**
   * @brief Stack of widgets that changes based on whether the stream is a video or audio stream
   */
	QStackedWidget *stacked_widget_;

	/**
   * @brief Media name text field
   */
	QLineEdit *footage_name_field_;

	/**
   * @brief Whether a manual source start time should be used
   */
	QCheckBox *source_start_time_enable_;

	/**
   * @brief Source start time in seconds
   */
	QDoubleSpinBox *source_start_time_spin_;

	/**
   * @brief Internal pointer to Media object (set in constructor)
   */
	Footage *footage_;

	/**
   * @brief A list widget for listing the tracks in Media
   */
	QListWidget *track_list_;

	/**
   * @brief Frame rate to conform to
   */
	QDoubleSpinBox *conform_fr_;

private slots:
	/**
   * @brief Overridden accept function for saving the properties back to the Media class
   */
	void accept();
};

}

#endif // OAK_MEDIAPROPERTIESDIALOG_H
