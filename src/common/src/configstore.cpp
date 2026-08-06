/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "configstore.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "filefunctions.h"

namespace fs = std::filesystem;

ConfigStore::ErrorHandler ConfigStore::error_handler_ = nullptr;

ConfigStore &ConfigStore::current()
{
	static ConfigStore store;
	return store;
}

ConfigStore::ConfigStore()
{
	set_defaults();
}

void ConfigStore::set_error_handler(ErrorHandler handler)
{
	error_handler_ = std::move(handler);
}

void ConfigStore::report_error(const std::string &title,
							   const std::string &message)
{
	if (error_handler_) {
		error_handler_(title, message);
	} else {
		fprintf(stderr, "%s: %s\n", title.c_str(), message.c_str());
	}
}

std::string ConfigStore::get_config_file_path()
{
	return (fs::path(FileFunctions::get_configuration_location()) /
			"config.ini")
		.string();
}

std::string ConfigStore::join_key(const char *group, const char *key)
{
	if (group != nullptr && group[0] != '\0') {
		return std::string(group) + "/" + key;
	}
	return key;
}

void ConfigStore::set_defaults()
{
	std::lock_guard<std::mutex> lock(mutex_);
	config_map_.clear();

	auto set_string = [this](const char *key, const char *value) {
		Entry e;
		e.type = Type::k_string;
		e.string_value = value;
		config_map_[key] = e;
	};
	auto set_int = [this](const char *key, int64_t value) {
		Entry e;
		e.type = Type::k_int;
		e.int_value = value;
		config_map_[key] = e;
	};
	auto set_bool = [this](const char *key, bool value) {
		Entry e;
		e.type = Type::k_bool;
		e.bool_value = value;
		config_map_[key] = e;
	};

	// Only the keys the de-Qt engine modules (oaknode/oakrender/oakcodec)
	// actually read are registered here; the app-layer keys of the old Qt
	// config arrive with the app/config wave. Enum-valued ints hardcode
	// the numeric values of their (still Qt-based) defining headers:
	//
	//  - Timeline::k_thumbnail_in_out / k_waveforms_enabled = 1
	//    (engine/timeline/timelinecommon.h)
	//  - PixelFormat::f32 = 4 (core/include/olive/core/render/pixelformat.h)
	//  - VideoParams::k_interlace_none = 0 (src/common/src/videoparams.h)
	//  - k_channel_layout_stereo = 3
	//    (core/include/olive/core/render/channellayout.h)
	//  - ColorCoding::k_red..k_navy = 0..11, k_lime = 6
	//    (engine/ui/colorcoding.h)

	set_int("TimelineThumbnailMode", 1);
	set_int("TimelineWaveformMode", 1);

	set_int("DefaultSequenceWidth", 1920);
	set_int("DefaultSequenceHeight", 1080);
	// Rational settings are stored as strings in oakcore_rational
	// "num/den" form; this mirrors the old default Rational(1001, 30000).
	set_string("DefaultSequenceFrameRate", "1001/30000");
	set_string("DefaultSequencePixelAspect", "1/1");
	set_int("DefaultSequenceInterlacing", 0);
	set_int("DefaultSequenceAudioFrequency", 48000);
	set_int("DefaultSequenceAudioLayout", 3);
	set_int("OfflinePixelFormat", 4);

	set_bool("SplitClipsCopyNodes", true);
	set_bool("UseProxyMedia", true);
	set_bool("UseGLFinish", false);
	set_bool("ReassocLinToNonLin", false);

	set_string("GraphicsBackend", "opengl");
	set_string("LUTLibraryPaths", "");

	set_int("DiskCacheSaveInterval", 10000);
	set_int("AutoCacheDelay", 1000);
	set_string("DiskCacheBehind", "0/1");
	set_string("DiskCacheAhead", "60/1");

	set_int("ProxyWidth", 1280);
	set_int("ProxyHeight", 720);
	set_int("ProxyDivider", 1);
	set_int("ProxyCRF", 23);
	set_string("ProxyPreset", "veryfast");
	set_bool("ProxyIncludeAudio", true);

	set_int("MarkerColor", 6);
	for (int i = 0; i <= 11; i++) {
		set_int(("CatColor" + std::to_string(i)).c_str(), i);
	}
}

std::string ConfigStore::value_to_string(const Entry &entry)
{
	switch (entry.type) {
	case Type::k_string:
		return entry.string_value;
	case Type::k_int:
		return std::to_string(entry.int_value);
	case Type::k_double: {
		char buf[64];
		snprintf(buf, sizeof(buf), "%g", entry.double_value);
		return buf;
	}
	case Type::k_bool:
		return entry.bool_value ? "true" : "false";
	default:
		return std::string();
	}
}

bool ConfigStore::string_to_value(const std::string &text, Type type,
								  Entry *out)
{
	Entry e;
	e.type = type;

	switch (type) {
	case Type::k_string:
		e.string_value = text;
		break;
	case Type::k_int: {
		try {
			size_t pos = 0;
			e.int_value = std::stoll(text, &pos);
			if (pos != text.size()) {
				return false;
			}
		} catch (...) {
			return false;
		}
		break;
	}
	case Type::k_double: {
		try {
			size_t pos = 0;
			e.double_value = std::stod(text, &pos);
			if (pos != text.size()) {
				return false;
			}
		} catch (...) {
			return false;
		}
		break;
	}
	case Type::k_bool:
		if (text == "true" || text == "1") {
			e.bool_value = true;
		} else if (text == "false" || text == "0") {
			e.bool_value = false;
		} else {
			return false;
		}
		break;
	default:
		return false;
	}

	*out = e;
	return true;
}

namespace
{

std::string trim(const std::string &s)
{
	const size_t first = s.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return std::string();
	}
	const size_t last = s.find_last_not_of(" \t\r\n");
	return s.substr(first, last - first + 1);
}

} // namespace

bool ConfigStore::load()
{
	set_defaults();

	const std::string path = get_config_file_path();

	std::error_code ec;
	if (!fs::exists(path, ec)) {
		// No saved settings yet: defaults are fine, not an error.
		return true;
	}

	// exists() also covers directories, which ifstream would happily
	// "open" on POSIX; only a regular file is a readable config.
	if (!fs::is_regular_file(path, ec)) {
		report_error("Error loading settings",
					 "Failed to load application settings. This session will "
					 "use defaults.");
		return false;
	}

	std::ifstream in(path);
	if (!in.is_open()) {
		report_error("Error loading settings",
					 "Failed to load application settings. This session will "
					 "use defaults.");
		return false;
	}

	std::string group;
	std::string line;
	while (std::getline(in, line)) {
		line = trim(line);
		if (line.empty() || line.front() == ';' || line.front() == '#') {
			continue;
		}

		if (line.front() == '[' && line.back() == ']') {
			group = trim(line.substr(1, line.size() - 2));
			continue;
		}

		const size_t eq = line.find('=');
		if (eq == std::string::npos) {
			// Malformed line: skip, keep going (matches QSettings' lax
			// INI parsing).
			continue;
		}

		std::string key = trim(line.substr(0, eq));
		const std::string value = trim(line.substr(eq + 1));
		if (key.empty()) {
			continue;
		}
		if (!group.empty()) {
			key = group + "/" + key;
		}

		Entry parsed;
		const Entry *existing = get(key);
		if (existing != nullptr) {
			// Known key: honor its declared type. An unparseable value
			// keeps the default.
			if (string_to_value(value, existing->type, &parsed)) {
				set(key, parsed);
			}
		} else {
			// Unknown key: stored as a string.
			parsed.type = Type::k_string;
			parsed.string_value = value;
			set(key, parsed);
		}
	}

	return true;
}

bool ConfigStore::save()
{
	const std::string real_filename = get_config_file_path();
	const std::string temp_filename = real_filename + ".tmp";

	// Flat keys are written at the top level; keys containing '/' become
	// [group] sections (group = everything before the last '/'), keeping
	// the QSettings INI key shape.
	std::map<std::string, std::map<std::string, std::string>> sections;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const auto &pair : config_map_) {
			const std::string &key = pair.first;
			const size_t slash = key.rfind('/');
			std::string group = slash == std::string::npos
									? std::string()
									: key.substr(0, slash);
			std::string sub = slash == std::string::npos
								  ? key
								  : key.substr(slash + 1);
			sections[group][sub] = value_to_string(pair.second);
		}
	}

	{
		std::ofstream out(temp_filename, std::ios::trunc);
		if (!out.is_open()) {
			report_error("Error saving settings",
						 "Failed to save application settings. The "
						 "application may lack write permissions for this "
						 "location.");
			return false;
		}

		const auto flat = sections.find(std::string());
		if (flat != sections.end()) {
			for (const auto &pair : flat->second) {
				out << pair.first << '=' << pair.second << '\n';
			}
		}

		for (const auto &section : sections) {
			if (section.first.empty()) {
				continue;
			}
			out << '\n'
				<< '[' << section.first << ']' << '\n';
			for (const auto &pair : section.second) {
				out << pair.first << '=' << pair.second << '\n';
			}
		}

		out.flush();
		if (!out.good()) {
			report_error("Error saving settings",
						 "Failed to save application settings. The "
						 "application may lack write permissions for this "
						 "location.");
			return false;
		}
	}

	std::error_code ec;
	fs::rename(temp_filename, real_filename, ec);
	if (ec) {
		fs::remove(real_filename, ec);
		ec.clear();
		fs::rename(temp_filename, real_filename, ec);
		if (ec) {
			report_error("Error saving settings",
						 "Failed to overwrite the application settings "
						 "file.");
			return false;
		}
	}

	return true;
}

const ConfigStore::Entry *ConfigStore::get(const std::string &key) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = config_map_.find(key);
	return it == config_map_.end() ? nullptr : &it->second;
}

void ConfigStore::set(const std::string &key, const Entry &entry)
{
	std::lock_guard<std::mutex> lock(mutex_);
	config_map_[key] = entry;
}
