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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <strings.h>

#include "define.h"
#include "filefunctions.h"
#include "config/config.h"
#include "project.h"

namespace olive
{

ocio::ConstConfigRcPtr ColorManager::default_config = nullptr;

ColorManager::ColorManager(Project *project)
	: project_(project)
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
ColorManager::create_config_from_file(const std::string &filename)
{
	return ocio::Config::CreateFromFile(filename.c_str());
}

std::string ColorManager::get_config_filename() const
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
	const char *ocio_env = std::getenv("OCIO");
	if (ocio_env != nullptr && ocio_env[0] != '\0') {
		// Attempt to set config from "OCIO" environment variable
		try {
			default_config = ocio::Config::CreateFromEnv();

			return;
		} catch (ocio::Exception &e) {
			fprintf(stderr,
					"Failed to load config from OCIO environment variable config: %s\n",
					e.what());
		}
	}

	// Extract OCIO config - kind of hacky, but it'll work
	// NOTE: was QStandardPaths::CacheLocation; oakcommon's configuration
	// location is the closest Qt-free persistent directory available here.
	std::string dir = FileFunctions::get_configuration_location() + "/ocioconf";

	FileFunctions::copy_directory(":/ocioconf", dir, true);

	fprintf(stderr, "Extracting default OCIO config to %s\n", dir.c_str());

	default_config = create_config_from_file(dir + "/config.ocio");
}

void ColorManager::set_config_filename(const std::string &filename)
{
	project()->set_color_config_filename(filename);
}

StringList ColorManager::list_available_displays()
{
	StringList displays;

	int number_of_displays = config_->getNumDisplays();

	for (int i = 0; i < number_of_displays; i++) {
		displays.push_back(config_->getDisplay(i));
	}

	return displays;
}

std::string ColorManager::get_default_display()
{
	return config_->getDefaultDisplay();
}

StringList ColorManager::list_available_views(std::string display)
{
	StringList views;

	int number_of_views = config_->getNumViews(display.c_str());

	for (int i = 0; i < number_of_views; i++) {
		views.push_back(config_->getView(display.c_str(), i));
	}

	return views;
}

std::string ColorManager::get_default_view(const std::string &display)
{
	return config_->getDefaultView(display.c_str());
}

StringList ColorManager::list_available_looks()
{
	StringList looks;

	int number_of_looks = config_->getNumLooks();

	for (int i = 0; i < number_of_looks; i++) {
		looks.push_back(config_->getLookNameByIndex(i));
	}

	return looks;
}

StringList ColorManager::list_available_colorspaces() const
{
	return list_available_colorspaces(config_);
}

std::string ColorManager::get_default_input_color_space() const
{
	return project()->get_default_input_color_space();
}

void ColorManager::set_default_input_color_space(const std::string &s)
{
	project()->set_default_input_color_space(s);
}

std::string ColorManager::get_colorspace_for_ffmpeg_tags(int primaries,
													 int trc) const
{
	// FFmpeg AVColorPrimaries/AVColorTransferCharacteristic values mapped to
	// candidate colorspace names, in order of preference
	struct TagMapping {
		int primaries;
		int trc;
		const char *candidates[3];
	};

	static const TagMapping k_tag_mappings[] = {
		{ 1, 1, { "Rec.709 OETF", "Rec.709", "BT.709" } },
		{ 1, 13, { "sRGB OETF", "sRGB", nullptr } },
		{ 6, 6, { "Rec.601 OETF (NTSC)", "Rec.601 NTSC", nullptr } },
		{ 5, 5, { "Rec.601 OETF (PAL)", "Rec.601 PAL", nullptr } },
		{ 5, 6, { "Rec.601 OETF (PAL)", "Rec.601 PAL", nullptr } },
		{ 9, 16, { "Rec.2020 PQ", "BT.2020 PQ", "ST 2084 PQ" } },
		{ 9, 18, { "Rec.2020 HLG", "BT.2020 HLG", "HLG" } },
		{ 9, 1, { "Rec.2020", "BT.2020", nullptr } },
		{ 9, 14, { "Rec.2020", "BT.2020", nullptr } },
		{ 9, 15, { "Rec.2020", "BT.2020", nullptr } },
	};

	// 0 = unset, 2 = AVCOL_PRI/TRC_UNSPECIFIED
	if (primaries == 0 || primaries == 2 || trc == 0 || trc == 2) {
		return std::string();
	}

	const StringList available = list_available_colorspaces();

	for (const TagMapping &mapping : k_tag_mappings) {
		if (mapping.primaries == primaries && mapping.trc == trc) {
			for (const char *candidate : mapping.candidates) {
				if (candidate &&
					std::find(available.begin(), available.end(),
							  std::string(candidate)) != available.end()) {
					return candidate;
				}
			}
			// Known tag pair, but the config has no matching colorspace
			return std::string();
		}
	}

	return std::string();
}

std::string ColorManager::get_reference_color_space() const
{
	return project()->get_color_reference_space();
}

std::string ColorManager::get_compliant_color_space(const std::string &s)
{
	const StringList available = list_available_colorspaces();
	if (std::find(available.begin(), available.end(), s) != available.end()) {
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
		std::string display = transform.display();
		std::string view = transform.view();
		std::string look = transform.look();

		const StringList displays = list_available_displays();

		// Check if display still exists in config
		if (std::find(displays.begin(), displays.end(), display) ==
			displays.end()) {
			display = get_default_display();
		}

		const StringList views = list_available_views(display);

		// Check if view still exists in display
		if (std::find(views.begin(), views.end(), view) == views.end()) {
			view = get_default_view(display);
		}

		const StringList looks = list_available_looks();

		// Check if looks still exists
		if (std::find(looks.begin(), looks.end(), look) == looks.end()) {
			look.clear();
		}

		return ColorTransform(display, view, look);

	} else {
		std::string output = transform.output();

		const StringList colorspaces = list_available_colorspaces();

		if (std::find(colorspaces.begin(), colorspaces.end(), output) ==
			colorspaces.end()) {
			output = get_default_input_color_space();
		}

		return ColorTransform(output);
	}
}

StringList
ColorManager::list_available_colorspaces(ocio::ConstConfigRcPtr config)
{
	StringList spaces;

	if (config) {
		int number_of_colorspaces = config->getNumColorSpaces();

		for (int i = 0; i < number_of_colorspaces; i++) {
			spaces.push_back(config->getColorSpaceNameByIndex(i));
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
	return project_;
}

void ColorManager::update_config_from_filename()
{
	try {
		std::string config_filename = get_config_filename();
		std::string old_default_cs = get_default_input_color_space();

		config_ = ocio::Config::CreateFromFile(config_filename.c_str());

		// Set new default colorspace appropriately
		std::string new_default = old_default_cs;
		StringList available_cs = list_available_colorspaces();
		for (const std::string &c : available_cs) {
			// NOTE: preserves the original Qt code's truthiness semantics
			// (QString::compare(...) != 0, i.e. case-insensitively different)
			if (strcasecmp(c.c_str(), old_default_cs.c_str())) {
				new_default = c;
				break;
			}
		}
		set_default_input_color_space(new_default);

		// The former config_changed signal is gone with Qt; the facade/event
		// layer is responsible for notifying subscribers.
	} catch (ocio::Exception &) {
	}
}

}
