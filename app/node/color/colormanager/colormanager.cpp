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

#include "colormanager.h"

#include <QDir>
#include <QStandardPaths>

#include "common/define.h"
#include "common/filefunctions.h"
#include "config/config.h"
#include "core.h"

namespace olive
{

#define super Node

ocio::ConstConfigRcPtr ColorManager::default_config = nullptr;

ColorManager::ColorManager(Project *project)
	: QObject(project)
	, config_(nullptr)
{
}

void ColorManager::init()
{
	// Set config to our built-in default
	config_ = get_default_config();
	set_default_input_color_space(config_->getCanonicalName(ocio::ROLE_DEFAULT));
	project()->set_color_reference_space(ocio::ROLE_SCENE_LINEAR);
}

ocio::ConstConfigRcPtr ColorManager::get_config() const
{
	return config_;
}

ocio::ConstConfigRcPtr
ColorManager::create_config_from_file(const QString &filename)
{
	return ocio::Config::CreateFromFile(filename.toUtf8());
}

QString ColorManager::get_config_filename() const
{
	return project()->get_color_config_filename();
}

ocio::ConstConfigRcPtr ColorManager::get_default_config()
{
	// Set up on first use: Project construction calls ColorManager::Init()
	// unconditionally, so without this any Project created before
	// SetUpDefaultConfig() crashed dereferencing a null config.
	if (!default_config) {
		set_up_default_config();
	}

	return default_config;
}

void ColorManager::set_up_default_config()
{
	if (!qEnvironmentVariableIsEmpty("OCIO")) {
		// Attempt to set config from "OCIO" environment variable
		try {
			default_config = ocio::Config::CreateFromEnv();

			return;
		} catch (ocio::Exception &e) {
			qWarning()
				<< "Failed to load config from OCIO environment variable config:"
				<< e.what();
		}
	}

	// Extract OCIO config - kind of hacky, but it'll work
	QString dir =
		QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
			.filePath(QStringLiteral("ocioconf"));

	FileFunctions::copy_directory(QStringLiteral(":/ocioconf"), dir, true);

	qDebug() << "Extracting default OCIO config to" << dir;

	default_config =
		create_config_from_file(QDir(dir).filePath(QStringLiteral("config.ocio")));
}

void ColorManager::set_config_filename(const QString &filename)
{
	project()->set_color_config_filename(filename);
}

QStringList ColorManager::list_available_displays()
{
	QStringList displays;

	int number_of_displays = config_->getNumDisplays();

	for (int i = 0; i < number_of_displays; i++) {
		displays.append(config_->getDisplay(i));
	}

	return displays;
}

QString ColorManager::get_default_display()
{
	return config_->getDefaultDisplay();
}

QStringList ColorManager::list_available_views(QString display)
{
	QStringList views;

	int number_of_views = config_->getNumViews(display.toUtf8());

	for (int i = 0; i < number_of_views; i++) {
		views.append(config_->getView(display.toUtf8(), i));
	}

	return views;
}

QString ColorManager::get_default_view(const QString &display)
{
	return config_->getDefaultView(display.toUtf8());
}

QStringList ColorManager::list_available_looks()
{
	QStringList looks;

	int number_of_looks = config_->getNumLooks();

	for (int i = 0; i < number_of_looks; i++) {
		looks.append(config_->getLookNameByIndex(i));
	}

	return looks;
}

QStringList ColorManager::list_available_colorspaces() const
{
	return list_available_colorspaces(config_);
}

QString ColorManager::get_default_input_color_space() const
{
	return project()->get_default_input_color_space();
}

void ColorManager::set_default_input_color_space(const QString &s)
{
	project()->set_default_input_color_space(s);
}

QString ColorManager::get_reference_color_space() const
{
	return project()->get_color_reference_space();
}

QString ColorManager::get_compliant_color_space(const QString &s)
{
	if (list_available_colorspaces().contains(s)) {
		return s;
	} else {
		return get_default_input_color_space();
	}
}

ColorTransform
ColorManager::get_compliant_color_space(const ColorTransform &transform,
									 bool force_display)
{
	if (transform.is_display() || force_display) {
		// Get display information
		QString display = transform.display();
		QString view = transform.view();
		QString look = transform.look();

		// Check if display still exists in config
		if (!list_available_displays().contains(display)) {
			display = get_default_display();
		}

		// Check if view still exists in display
		if (!list_available_views(display).contains(view)) {
			view = get_default_view(display);
		}

		// Check if looks still exists
		if (!list_available_looks().contains(look)) {
			look.clear();
		}

		return ColorTransform(display, view, look);

	} else {
		QString output = transform.output();

		if (!list_available_colorspaces().contains(output)) {
			output = get_default_input_color_space();
		}

		return ColorTransform(output);
	}
}

QStringList
ColorManager::list_available_colorspaces(ocio::ConstConfigRcPtr config)
{
	QStringList spaces;

	if (config) {
		int number_of_colorspaces = config->getNumColorSpaces();

		for (int i = 0; i < number_of_colorspaces; i++) {
			spaces.append(config->getColorSpaceNameByIndex(i));
		}
	}

	return spaces;
}

void ColorManager::get_default_luma_coefs(double *rgb) const
{
	config_->getDefaultLumaCoefs(rgb);
}

Project *ColorManager::project() const
{
	return static_cast<Project *>(parent());
}

void ColorManager::update_config_from_filename()
{
	try {
		QString config_filename = get_config_filename();
		QString old_default_cs = get_default_input_color_space();

		config_ = ocio::Config::CreateFromFile(config_filename.toUtf8());

		// Set new default colorspace appropriately
		QString new_default = old_default_cs;
		QStringList available_cs = list_available_colorspaces();
		for (int i = 0; i < available_cs.size(); i++) {
			const QString &c = available_cs.at(i);
			if (c.compare(old_default_cs, Qt::CaseInsensitive)) {
				new_default = c;
				break;
			}
		}
		set_default_input_color_space(new_default);

		emit config_changed(config_filename);
	} catch (ocio::Exception &) {
	}
}

}
