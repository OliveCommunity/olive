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

#ifndef OAK_COLORSERVICE_H
#define OAK_COLORSERVICE_H

#include <memory>
#include <string>

#include "codec/frame.h"
#include "colortransform.h"
#include "node.h"
#include "render/colorprocessor.h"

namespace olive
{

class ColorManager {
public:
	ColorManager(Project *project);

	void init();

	ocio::ConstConfigRcPtr get_config() const;

	static ocio::ConstConfigRcPtr create_config_from_file(const std::string &filename);

	std::string get_config_filename() const;

	static ocio::ConstConfigRcPtr get_default_config();

	static void set_up_default_config();

	void set_config_filename(const std::string &filename);

	StringList list_available_displays();

	std::string get_default_display();

	StringList list_available_views(std::string display);

	std::string get_default_view(const std::string &display);

	StringList list_available_looks();

	StringList list_available_colorspaces() const;

	std::string get_default_input_color_space() const;

	/**
	 * @brief Auto-detects an input colorspace from media color tags
	 *
	 * Maps raw FFmpeg color primaries/transfer values (as exposed on
	 * VideoParams) to a colorspace of the active OCIO config. Returns an
	 * empty string when the tags are unknown or the config has no matching
	 * colorspace, in which case the default input colorspace applies.
	 */
	std::string get_colorspace_for_ffmpeg_tags(int primaries, int trc) const;

	void set_default_input_color_space(const std::string &s);

	std::string get_reference_color_space() const;

	std::string get_compliant_color_space(const std::string &s);

	ColorTransform get_compliant_color_space(const ColorTransform &transform,
										  bool force_display = false);

	static StringList list_available_colorspaces(ocio::ConstConfigRcPtr config);

	void get_default_luma_coefs(double *rgb) const;

	Project *project() const;

	void update_config_from_filename();

private:
	Project *project_;

	ocio::ConstConfigRcPtr config_;

	static ocio::ConstConfigRcPtr default_config;
};

}

#endif // OAK_COLORSERVICE_H
