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

#include "diskcachedialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>

#include "oakengine/disk.h"

namespace olive
{

DiskCacheDialog::DiskCacheDialog(DiskCacheFolder *folder, QWidget *parent)
	: QDialog(parent)
	, folder_(folder)
{
	QGridLayout *layout = new QGridLayout(this);

	int row = 0;

	layout->addWidget(new QLabel(tr("Disk Cache: %1").arg(folder->get_path())),
					  row, 0, 1, 2);
	setWindowTitle(tr("Disk Cache Settings"));

	row++;

	layout->addWidget(new QLabel(tr("Maximum Disk Cache:")), row, 0);

	maximum_cache_slider_ = new FloatSlider();
	maximum_cache_slider_->set_format(tr("%1 GB"));
	maximum_cache_slider_->set_minimum(1.0);
	maximum_cache_slider_->set_value(static_cast<double>(folder->get_limit()) /
									static_cast<double>(k_bytes_in_gigabyte));
	layout->addWidget(maximum_cache_slider_, row, 1);

	row++;

	clear_cache_btn_ = new QPushButton(tr("Clear Disk Cache"));
	connect(clear_cache_btn_, &QPushButton::clicked, this,
			static_cast<void (DiskCacheDialog::*)()>(
				&DiskCacheDialog::clear_disk_cache));
	layout->addWidget(clear_cache_btn_, row, 1);

	row++;

	clear_disk_cache_ =
		new QCheckBox(tr("Automatically clear disk cache on close"));
	clear_disk_cache_->setChecked(folder->get_clear_on_close());
	layout->addWidget(clear_disk_cache_, row, 1);

	row++;

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this,
			&DiskCacheDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this,
			&DiskCacheDialog::reject);
	layout->addWidget(buttons, row, 0, 1, 2);
}

void DiskCacheDialog::accept()
{
	qint64 new_disk_cache_limit =
		qRound64(maximum_cache_slider_->get_value() * k_bytes_in_gigabyte);
	if (new_disk_cache_limit != folder_->get_limit()) {
		folder_->set_limit(new_disk_cache_limit);
	}

	if (folder_->get_clear_on_close() != clear_disk_cache_->isChecked()) {
		folder_->set_clear_on_close(clear_disk_cache_->isChecked());
	}

	QDialog::accept();
}

void DiskCacheDialog::clear_disk_cache()
{
	clear_disk_cache(folder_->get_path(), this, clear_cache_btn_);
}

void DiskCacheDialog::clear_disk_cache(const QString &path, QWidget *parent,
									 QPushButton *clear_btn)
{
	if (QMessageBox::question(
			parent, tr("Clear Disk Cache"),
			tr("Are you sure you want to clear the disk cache in '%1'?")
				.arg(path),
			QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
		if (clear_btn)
			clear_btn->setEnabled(false);

		if (oakengine_disk_clear_cache(path.toUtf8().constData())) {
			if (clear_btn)
				clear_btn->setText(tr("Disk Cache Cleared"));
		} else {
			QMessageBox::information(
				parent, tr("Clear Disk Cache"),
				tr("Disk cache failed to fully clear. You may have to delete the cache files manually."),
				QMessageBox::Ok);
			if (clear_btn)
				clear_btn->setText(tr("Disk Cache Partially Cleared"));
		}
	}
}

}
