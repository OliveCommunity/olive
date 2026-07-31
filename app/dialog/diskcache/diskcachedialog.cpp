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
#include "oakutil/define.h"

namespace olive
{

namespace
{

/// DiskCacheFolder::get_path() through the C ABI (buf/size convention)
QString disk_folder_path(const void *folder)
{
	char buf[1024];
	buf[0] = '\0';
	oakengine_disk_folder_get_path(folder, buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

} // namespace

DiskCacheDialog::DiskCacheDialog(void *folder, QWidget *parent)
	: QDialog(parent)
	, folder_(folder)
{
	QGridLayout *layout = new QGridLayout(this);

	int row = 0;

	layout->addWidget(
		new QLabel(tr("Disk Cache: %1").arg(disk_folder_path(folder))), row,
		0, 1, 2);
	setWindowTitle(tr("Disk Cache Settings"));

	row++;

	layout->addWidget(new QLabel(tr("Maximum Disk Cache:")), row, 0);

	maximum_cache_slider_ = new FloatSlider();
	maximum_cache_slider_->set_format(tr("%1 GB"));
	maximum_cache_slider_->set_minimum(1.0);
	// The folder limit is a byte count; the slider works in GB
	maximum_cache_slider_->set_value(oakengine_disk_folder_get_limit(folder) /
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
	clear_disk_cache_->setChecked(
		oakengine_disk_folder_get_clear_on_close(folder) != 0);
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
	if (static_cast<double>(new_disk_cache_limit) !=
		oakengine_disk_folder_get_limit(folder_)) {
		oakengine_disk_folder_set_limit(
			folder_, static_cast<double>(new_disk_cache_limit));
	}

	const bool clear_on_close = clear_disk_cache_->isChecked();
	if ((oakengine_disk_folder_get_clear_on_close(folder_) != 0) !=
		clear_on_close) {
		oakengine_disk_folder_set_clear_on_close(folder_,
												 clear_on_close ? 1 : 0);
	}

	QDialog::accept();
}

void DiskCacheDialog::clear_disk_cache()
{
	clear_disk_cache(disk_folder_path(folder_), this, clear_cache_btn_);
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
