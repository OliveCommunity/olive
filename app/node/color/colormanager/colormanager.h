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
#include <QMutex>

#include "codec/frame.h"
#include "node/node.h"
#include "render/colorprocessor.h"

namespace olive
{

class ColorManager : public QObject {
	Q_OBJECT
public:
	ColorManager(Project *project);

	void init();

	ocio::ConstConfigRcPtr get_config() const;

	static ocio::ConstConfigRcPtr create_config_from_file(const QString &filename);

	QString get_config_filename() const;

	static ocio::ConstConfigRcPtr get_default_config();

	static void set_up_default_config();

	void set_config_filename(const QString &filename);

	QStringList list_available_displays();

	QString get_default_display();

	QStringList list_available_views(QString display);

	QString get_default_view(const QString &display);

	QStringList list_available_looks();

	QStringList list_available_colorspaces() const;

	QString get_default_input_color_space() const;

	/**
	 * @brief Auto-detects an input colorspace from media color tags
	 *
	 * Maps raw FFmpeg color primaries/transfer values (as exposed on
	 * VideoParams) to a colorspace of the active OCIO config. Returns an
	 * empty string when the tags are unknown or the config has no matching
	 * colorspace, in which case the default input colorspace applies.
	 */
	QString get_colorspace_for_ffmpeg_tags(int primaries, int trc) const;

	void set_default_input_color_space(const QString &s);

	QString get_reference_color_space() const;

	QString get_compliant_color_space(const QString &s);

	ColorTransform get_compliant_color_space(const ColorTransform &transform,
										  bool force_display = false);

	static QStringList list_available_colorspaces(ocio::ConstConfigRcPtr config);

	void get_default_luma_coefs(double *rgb) const;

	Project *project() const;

	void update_config_from_filename();

signals:
	void config_changed(const QString &s);

	void reference_space_changed(const QString &s);

	void default_input_changed(const QString &s);

private:
	ocio::ConstConfigRcPtr config_;

	static ocio::ConstConfigRcPtr default_config;
};

}

#endif // OAK_COLORSERVICE_H
