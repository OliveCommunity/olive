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

#include "exportsavepresetdialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

namespace olive
{

ExportSavePresetDialog::ExportSavePresetDialog(const OakEngineEncodingParams *p,
											   QWidget *parent)
	: QDialog(parent)
	, params_(p)
{
	auto layout = new QVBoxLayout(this);

	name_edit_ = new QLineEdit();

	// Populate existing list
	QStringList l;
	{
		const int n = oakengine_encoding_preset_count();
		for (int i = 0; i < n; i++) {
			char name_buf[256];
			if (oakengine_encoding_preset_name(
					i, name_buf, static_cast<int>(sizeof(name_buf))) > 0) {
				l.append(QString::fromUtf8(name_buf));
			}
		}
	}
	if (!l.empty()) {
		auto list_widget = new QListWidget();
		for (const QString &f : l) {
			list_widget->addItem(f);
		}
		connect(list_widget, &QListWidget::currentTextChanged, name_edit_,
				&QLineEdit::setText);
		layout->addWidget(list_widget);
	}

	auto name_layout = new QHBoxLayout();
	layout->addLayout(name_layout);

	name_layout->addWidget(new QLabel(tr("Name:")));

	name_edit_->setFocus();
	name_layout->addWidget(name_edit_);

	auto btns =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(btns, &QDialogButtonBox::accepted, this,
			&ExportSavePresetDialog::accept);
	connect(btns, &QDialogButtonBox::rejected, this,
			&ExportSavePresetDialog::reject);
	layout->addWidget(btns);

	setWindowTitle(tr("Save Export Preset"));
}

void ExportSavePresetDialog::accept()
{
	if (name_edit_->text().isEmpty()) {
		QMessageBox::critical(
			this, tr("Invalid Name"),
			tr("You must enter a name to save an export preset."));
		return;
	}

	char preset_path_buf[1024];
	preset_path_buf[0] = '\0';
	oakengine_encoding_preset_path(
		preset_path_buf, static_cast<int>(sizeof(preset_path_buf)));
	QDir d(QString::fromUtf8(preset_path_buf));

	if (!d.exists()) {
		d.mkpath(QStringLiteral("."));
	}

	if (d.exists(name_edit_->text())) {
		if (QMessageBox::question(
				this, tr("Overwrite Preset"),
				tr("A preset with the name \"%1\" already exists. Do you wish to overwrite it?")
					.arg(name_edit_->text()),
				QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
			return;
		}
	}

	const QByteArray full_path =
		d.filePath(name_edit_->text()).toUtf8();
	const int rc = oakengine_encoding_params_save_file(
		params_, full_path.constData());
	if (rc != OAKENGINE_OK) {
		QMessageBox::critical(
			this, tr("Write Error"),
			tr("Failed to save preset to \"%1\".").arg(
				QString::fromUtf8(full_path)));
		return;
	}

	QDialog::accept();
}

}
