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

#include "preferencesluttab.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <vector>

#include "oakengine/lut.h"

namespace olive
{

PreferencesLutTab::PreferencesLutTab()
{
	QVBoxLayout *outer_layout = new QVBoxLayout(this);

	QGroupBox *library_group = new QGroupBox(tr("LUT Library"));
	outer_layout->addWidget(library_group);

	QVBoxLayout *library_layout = new QVBoxLayout(library_group);

	library_layout->addWidget(new QLabel(
		tr("Directories scanned for .cube and .3dl LUT files. LUT nodes offer "
		   "these locations when picking a LUT file.")));

	library_dirs_list_ = new QListWidget();
	{
		int dir_count = oakengine_lut_directory_count();
		for (int i = 0; i < dir_count; i++) {
			char buf[4096];
			int len = oakengine_lut_directory_at(i, buf, sizeof(buf));
			if (len > 0) {
				library_dirs_list_->addItem(QString::fromUtf8(buf, len));
			}
		}
	}
	library_layout->addWidget(library_dirs_list_);

	QHBoxLayout *button_layout = new QHBoxLayout();
	button_layout->addStretch();

	QPushButton *add_btn = new QPushButton(tr("Add..."));
	connect(add_btn, &QPushButton::clicked, this, [this]() {
		const QString dir = QFileDialog::getExistingDirectory(
			this, tr("Add LUT Library Directory"));
		if (!dir.isEmpty() &&
			library_dirs_list_->findItems(dir, Qt::MatchExactly).isEmpty()) {
			library_dirs_list_->addItem(dir);
		}
	});
	button_layout->addWidget(add_btn);

	QPushButton *remove_btn = new QPushButton(tr("Remove"));
	connect(remove_btn, &QPushButton::clicked, this, [this]() {
		qDeleteAll(library_dirs_list_->selectedItems());
	});
	button_layout->addWidget(remove_btn);

	library_layout->addLayout(button_layout);

	outer_layout->addStretch();
}

void PreferencesLutTab::accept(void *command)
{
	Q_UNUSED(command)

	QStringList dirs;
	for (int i = 0; i < library_dirs_list_->count(); i++) {
		dirs.append(library_dirs_list_->item(i)->text());
	}

	std::vector<QByteArray> utf8_dirs;
	std::vector<const char*> cstr_dirs;
	for (int i = 0; i < dirs.size(); i++) {
		utf8_dirs.push_back(dirs[i].toUtf8());
		cstr_dirs.push_back(utf8_dirs.back().constData());
	}
	oakengine_lut_set_directories(cstr_dirs.data(),
								   static_cast<int>(cstr_dirs.size()));
}

}
