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

#ifndef OAK_DISKCACHEDIALOG_H
#define OAK_DISKCACHEDIALOG_H

#include <QCheckBox>
#include <QDialog>
#include <QPushButton>

#include "widget/slider/floatslider.h"

namespace olive
{

class DiskCacheDialog : public QDialog {
	Q_OBJECT
public:
	/**
	 * @param folder Opaque engine DiskCacheFolder handle (from
	 * oakengine_disk_get_open_folder()), accessed through the
	 * oakengine_disk_folder_* C ABI.
	 */
	DiskCacheDialog(void *folder, QWidget *parent = nullptr);

	static void clear_disk_cache(const QString &path, QWidget *parent,
							   QPushButton *clear_btn = nullptr);

public slots:
	virtual void accept() override;

private:
	void *folder_;

	FloatSlider *maximum_cache_slider_;

	QCheckBox *clear_disk_cache_;

	QPushButton *clear_cache_btn_;

private slots:
	void clear_disk_cache();
};

}

#endif // OAK_DISKCACHEDIALOG_H
