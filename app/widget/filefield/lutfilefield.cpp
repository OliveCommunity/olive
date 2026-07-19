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

#include "lutfilefield.h"

#include <QDir>
#include <QHBoxLayout>

#include "render/lutlibrary.h"

namespace olive
{

LutFileField::LutFileField(QWidget *parent) : FileField(parent)
{
	library_combo_ = new QComboBox();
	library_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	library_combo_->setMinimumContentsLength(12);
	static_cast<QHBoxLayout *>(layout())->insertWidget(0, library_combo_, 1);

	refresh_library_entries();

	connect(library_combo_,
			static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this,
			[this](int index) {
				const QString path =
					library_combo_->itemData(index).toString();
				if (!path.isEmpty()) {
					set_filename(path);
					emit filename_changed(path);
				}
			});

	// Keep the combo in sync when the path is edited directly
	connect(this, &FileField::filename_changed, this, [this](const QString &) {
		refresh_library_entries();
	});
}

void LutFileField::set_filename(const QString &s)
{
	FileField::set_filename(s);
	refresh_library_entries();
}

void LutFileField::refresh_library_entries()
{
	const QString current = get_filename();

	const QSignalBlocker blocker(library_combo_);
	library_combo_->clear();
	library_combo_->addItem(tr("Other (Custom File)..."), QString());

	const QStringList library_dirs = LUTLibrary::get_directories();
	const QStringList luts = LUTLibrary::get_lut_files();
	for (const QString &lut : luts) {
		// Show the path relative to the library directory that contains it
		QString display = lut;
		for (const QString &dir : library_dirs) {
			if (lut.startsWith(dir)) {
				display = QDir(dir).relativeFilePath(lut);
				break;
			}
		}
		library_combo_->addItem(display, lut);
	}

	const int index = library_combo_->findData(current);
	library_combo_->setCurrentIndex(index >= 0 ? index : 0);
}

}
