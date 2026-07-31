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

#include "projectproperties.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

#include "oakutil/filefunctions.h"
#include "oakutil/qtutils.h"
#include "common/projecttypes.h"
#include "oakengine/color.h"
#include "oakengine/disk.h"
#include "oakengine/project.h"
#include "widget/manageddisplay/colorprocessorhandle.h"

namespace olive
{

#define super QDialog

ProjectPropertiesDialog::ProjectPropertiesDialog(OakEngineProject *p,
												 QWidget *parent)
	: super(parent)
	, working_project_(p)
	, ocio_config_is_valid_(true)
{
	QVBoxLayout *layout = new QVBoxLayout(this);

	char name_buf[256];
	oakengine_project_name(
		working_project_,
		name_buf, sizeof(name_buf));
	setWindowTitle(
		tr("Project Properties for '%1'").arg(name_buf));

	QTabWidget *tabs = new QTabWidget;
	layout->addWidget(tabs);

	{
		// Color management group
		QWidget *color_group = new QWidget();

		QVBoxLayout *color_outer_layout = new QVBoxLayout(color_group);

		QGridLayout *color_layout = new QGridLayout();
		color_outer_layout->addLayout(color_layout);

		int row = 0;

		color_layout->addWidget(new QLabel(tr("OpenColorIO Configuration:")),
								row, 0);

		ocio_filename_ = new QLineEdit();
		ocio_filename_->setPlaceholderText(tr("(default)"));
		color_layout->addWidget(ocio_filename_, row, 1);

		row++;

		color_layout->addWidget(new QLabel(tr("Default Input Color Space:")),
								row, 0);

		default_input_colorspace_ = new QComboBox();
		color_layout->addWidget(default_input_colorspace_, row, 1, 1, 2);

		row++;

		color_layout->addWidget(new QLabel(tr("Reference Space:")), row, 0);

		reference_space_ = new QComboBox(this);
		// OpenColorIO role names (ocio::ROLE_SCENE_LINEAR /
		// ocio::ROLE_COMPOSITING_LOG); mirrored as literals because the OCIO
		// headers only reached this file transitively through the engine.
		reference_space_->addItem(tr("Scene Linear"),
								  QStringLiteral("scene_linear"));
		reference_space_->addItem(tr("Compositing Log"),
								  QStringLiteral("compositing_log"));
		QtUtils::set_combo_box_data(reference_space_,
									[p]() -> QString {
										char buf[256];
										oakengine_project_get_color_reference_space(
											p, buf, sizeof(buf));
										return QString::fromUtf8(buf);
									}());
		color_layout->addWidget(reference_space_, row, 1, 1, 2);

		row++;

		QPushButton *browse_btn = new QPushButton(tr("Browse"));
		color_layout->addWidget(browse_btn, 0, 2);
		connect(browse_btn, &QPushButton::clicked, this,
				&ProjectPropertiesDialog::browse_for_ocio_config);

		OakEngineColorManager *cm = oakengine_color_manager_from_project(
			working_project_);
		ocio_filename_->setText(oak_query_string([cm](char *buf, int size) {
			return oakengine_color_manager_get_config_filename(cm, buf, size);
		}));

		connect(ocio_filename_, &QLineEdit::textChanged, this,
				&ProjectPropertiesDialog::ocio_filename_updated);
		ocio_filename_updated();

		tabs->addTab(color_group, tr("Color Management"));

		color_outer_layout->addStretch();
	}

	{
		// Cache group
		QWidget *cache_group = new QWidget();

		QVBoxLayout *cache_layout = new QVBoxLayout(cache_group);

		QButtonGroup *disk_cache_btn_group = new QButtonGroup();

		// Create radio buttons and add to widget and button group
		disk_cache_radios_[Project::k_cache_use_default_location] =
			new QRadioButton(tr("Use Default Location"));
		disk_cache_radios_[Project::k_cache_store_alongside_project] =
			new QRadioButton(tr("Store Alongside Project"));
		disk_cache_radios_[Project::k_cache_custom_path] =
			new QRadioButton(tr("Use Custom Location:"));
		for (int i = 0; i < k_disk_cache_radio_count; i++) {
			disk_cache_btn_group->addButton(disk_cache_radios_[i]);
			cache_layout->addWidget(disk_cache_radios_[i]);
		}

		// Create custom cache path widget
		custom_cache_path_ =
			new PathWidget(
				[this]() -> QString {
					char buf[4096];
					oakengine_project_get_custom_cache_path(
						working_project_,
						buf, sizeof(buf));
					return QString::fromUtf8(buf);
				}(), this);
		custom_cache_path_->setEnabled(false);
		cache_layout->addWidget(custom_cache_path_);

		// Ensure custom cache path "enabled" is tied to the radio button being checked
		connect(disk_cache_radios_[Project::k_cache_custom_path],
				&QRadioButton::toggled, custom_cache_path_,
				&PathWidget::setEnabled);

		// Check the radio button that should currently be active
		disk_cache_radios_[oakengine_project_get_cache_location_setting(
			working_project_)]
			->setChecked(true);

		// Add disk cache settings button
		QPushButton *disk_cache_settings_btn =
			new QPushButton(tr("Disk Cache Settings"));
		connect(disk_cache_settings_btn, &QPushButton::clicked, this,
				&ProjectPropertiesDialog::open_disk_cache_settings);
		cache_layout->addWidget(disk_cache_settings_btn);

		tabs->addTab(cache_group, tr("Disk Cache"));
	}

	QDialogButtonBox *dialog_btns = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal);
	layout->addWidget(dialog_btns);
	connect(dialog_btns, &QDialogButtonBox::accepted, this,
			&ProjectPropertiesDialog::accept);
	connect(dialog_btns, &QDialogButtonBox::rejected, this,
			&ProjectPropertiesDialog::reject);
}

void ProjectPropertiesDialog::accept()
{
	if (!ocio_config_is_valid_) {
		QMessageBox mb(this);
		mb.setWindowModality(Qt::WindowModal);
		mb.setIcon(QMessageBox::Critical);
		mb.setWindowTitle(tr("OpenColorIO Config Error"));
		mb.setText(tr("Failed to set OpenColorIO configuration: %1")
					   .arg(ocio_config_error_));
		mb.addButton(QMessageBox::Ok);
		mb.exec();
		return;
	}

	if (disk_cache_radios_[Project::k_cache_use_default_location]->isChecked()) {
		// Keep new cache path empty, which means default
	} else if (disk_cache_radios_[Project::k_cache_store_alongside_project]
				   ->isChecked()) {
		// Ensure alongside project path is valid
		if (!verify_path_and_warn_if_bad(
				[this]() -> QString {
					char buf[4096];
					oakengine_project_cache_alongside_path(
						working_project_,
						buf, sizeof(buf));
					return QString::fromUtf8(buf);
				}())) {
			return;
		}
	} else {
		// Ensure custom path is valid
		if (!verify_path_and_warn_if_bad(custom_cache_path_->text())) {
			return;
		}
	}

	if (custom_cache_path_->text() !=
		[this]() -> QString {
			char buf[4096];
			oakengine_project_get_custom_cache_path(
				working_project_,
				buf, sizeof(buf));
			return QString::fromUtf8(buf);
		}()) {
		// Check if the user is okay with invalidating the current cache
		if (!oakengine_disk_show_change_confirmation_dialog(this)) {
			return;
		}

		oakengine_project_set_custom_cache_path(
			working_project_,
			custom_cache_path_->text().toUtf8().constData());

		oakengine_disk_invalidate_project(
			working_project_);
	}

	// This should ripple changes throughout the graph/cache that the color config has changed, and
	// therefore should be done after the cache path is changed
	OakEngineColorManager *cm = oakengine_color_manager_from_project(
		working_project_);
	QString old_config = oak_query_string([cm](char *buf, int size) {
		return oakengine_color_manager_get_config_filename(cm, buf, size);
	});
	QString old_input_cs = oak_query_string([cm](char *buf, int size) {
		return oakengine_color_manager_default_input_color_space(cm, buf, size);
	});
	if (old_config != ocio_filename_->text()) {
		oakengine_color_manager_set_config_filename(
			cm, ocio_filename_->text().toUtf8().constData());
	}
	if (old_input_cs != default_input_colorspace_->currentText()) {
		oakengine_color_manager_set_default_input_color_space(
			cm, default_input_colorspace_->currentText().toUtf8().constData());
	}
	if ([this]() -> QString {
			char buf[256];
			oakengine_project_get_color_reference_space(
				working_project_,
				buf, sizeof(buf));
			return QString::fromUtf8(buf);
		}() != reference_space_->currentData().toString()) {
		oakengine_project_set_color_reference_space(
			working_project_,
			reference_space_->currentData().toString().toUtf8().constData());
	}

	super::accept();
}

bool ProjectPropertiesDialog::verify_path_and_warn_if_bad(const QString &path)
{
	if (!FileFunctions::directory_is_valid(path)) {
		QMessageBox mb(this);
		mb.setWindowModality(Qt::WindowModal);
		mb.setIcon(QMessageBox::Critical);
		mb.setWindowTitle(tr("Invalid path"));
		mb.setText(tr(
			"The custom cache path is invalid. Please check it and try again."));
		mb.addButton(QMessageBox::Ok);
		mb.exec();
		return false;
	}

	return true;
}

void ProjectPropertiesDialog::browse_for_ocio_config()
{
	QString fn = QFileDialog::getOpenFileName(
		this, tr("Browse for OpenColorIO configuration"));
	if (!fn.isEmpty()) {
		ocio_filename_->setText(fn);
	}
}

void ProjectPropertiesDialog::ocio_filename_updated()
{
	default_input_colorspace_->clear();

	OakEngineColorConfig *config = nullptr;

	if (ocio_filename_->text().isEmpty()) {
		config = oakengine_color_config_load_default();
	} else {
		config = oakengine_color_config_load_file(
			ocio_filename_->text().toUtf8().constData());
	}

	if (config) {
		ocio_filename_->setStyleSheet(QString());
		ocio_config_is_valid_ = true;

		// List input color spaces
		int cs_count = oakengine_color_config_colorspace_count(config);
		OakEngineColorManager *cm = oakengine_color_manager_from_project(
			working_project_);
		QString default_cs = oak_query_string([cm](char *buf, int size) {
			return oakengine_color_manager_default_input_color_space(cm, buf,
																	 size);
		});

		for (int i = 0; i < cs_count; i++) {
			QString cs = oak_query_string([config, i](char *buf, int size) {
				return oakengine_color_config_colorspace_at(config, i, buf,
															size);
			});
			default_input_colorspace_->addItem(cs);

			if (cs == default_cs) {
				default_input_colorspace_->setCurrentIndex(
					default_input_colorspace_->count() - 1);
			}
		}

		oakengine_color_config_free(config);
	} else {
		char err_buf[1024];
		oakengine_color_last_error(err_buf, sizeof(err_buf));
		ocio_config_is_valid_ = false;
		ocio_filename_->setStyleSheet(
			QStringLiteral("QLineEdit {color: red;}"));
		ocio_config_error_ = QString::fromUtf8(err_buf);
	}
}

void ProjectPropertiesDialog::open_disk_cache_settings()
{
	if (disk_cache_radios_[Project::k_cache_use_default_location]->isChecked()) {
		oakengine_disk_show_settings_dialog(nullptr, this);
	} else if (disk_cache_radios_[Project::k_cache_store_alongside_project]
				   ->isChecked()) {
		oakengine_disk_show_settings_dialog(
			[this]() -> QString {
				char buf[4096];
				oakengine_project_cache_alongside_path(
					working_project_,
					buf, sizeof(buf));
				return QString::fromUtf8(buf);
			}().toUtf8().constData(), this);
	} else {
		oakengine_disk_show_settings_dialog(
			custom_cache_path_->text().toUtf8().constData(), this);
	}
}

}
